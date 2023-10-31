//
// Created by utt on 3/18/23.
//

#include <cerrno>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <map>
#include "webserver.h"
#include "log/log.h"

extern std::map<std::string, std::string> users_info;

void cb_func(client_data* data, int epollfd) {
    assert(data);
    epoll_ctl(epollfd, EPOLL_CTL_DEL, data->sockfd, nullptr);
    close(data->sockfd);
    --HttpCon::connection_count;
}

void webserver::adjust_timer(tw_timer *timer) {
    timer_wheel->adjust_timer(timer, 3 * TIMESLOT);

    LOG_INFO("%s", "adjust timer once");
}

webserver::webserver() {
    // 设置工作目录
    std::string server_path(getcwd(nullptr, 0));
    work_rootdirectory = (char*) malloc(server_path.length() + 6);
    strcpy(work_rootdirectory, server_path.c_str());
    strcat(work_rootdirectory, "/root");

    timer_wheel = new time_wheel();
    LOG_INFO("%s", "init webserver");
}

webserver::~webserver() {
    close(sev_epollfd);
    close(sev_listenfd);
    close(sev_signal[1]);
    close(sev_signal[0]);
    delete timer_wheel;
    delete threadpool;

    free_clients();
    free_clients_timers();
}


void webserver::free_clients() {
    for (auto it : clients) {
        delete it.second;
    }
}

void webserver::free_clients_timers() {
    for (auto it : client_timers) delete it.second;
}

void webserver::init(int port, std::string db_uname, std::string db_name, std::string db_pwd, int sql_num,
                     int thread_num, int mix_mode, int lmode, int amode, bool open_log, int log_write) {
    this->sev_listen_port = port;
    this->db_username = db_uname;
    this->db_name = db_name;
    this->db_pwd = db_pwd;
    this->max_sqlcon_num = sql_num;
    this->sev_threadnum = thread_num;

    this->open_log = open_log;
    this->log_write = log_write;

    assert(mix_mode <= 3 && mix_mode >= 0);
    this->mix_mode = static_cast<mix_triggermode>(mix_mode);

    assert(close_mode <= 1 && close_mode >= 0);
    this->close_mode = static_cast<linger_mode>(lmode);

    assert(mode <= 1 && amode >= 0);
    this->mode = static_cast<actor_mode>(mode);
}

void webserver::init_threadpool() {
    threadpool = new Threadpool<HttpCon>(mode, sqlPool, sev_threadnum);
}

void initmysql_result(SqlPool *pool) {
    // 从池中取一个连接
    MYSQL* mysql = nullptr;
    connectionRAII connection(&mysql, pool);

    // 在user表中检索username，passwd数据
    if (mysql_query(mysql, "select username, passwd from user")) {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    // 从表中检索完整的结果集
    MYSQL_RES* mysql_result = mysql_store_result(mysql);

    // 从结果集中获取下一行存入到map中
    while (MYSQL_ROW row = mysql_fetch_row(mysql_result)) {
        users_info[std::string(row[0])] = std::string(row[1]);
    }
}

void webserver::init_sqlpool() {
    sqlPool = SqlPool::GetInstance();
    sqlPool->init("localhost", db_username, db_pwd, db_name, 3306, max_sqlcon_num, open_log);

    // 初始化数据库，读取表
    initmysql_result(sqlPool);

}

void webserver::init_triggermode() {
    connect_mode = trigger_mode(mix_mode & 1);
    listen_mode = trigger_mode((mix_mode >> 1) & 1);
}

void webserver::init_log() {
    if (open_log) {
        if (log_write == 0) // 同步写日志
            Log::get_instance()->init("./ServerLog", open_log, 2000, 800000, 0);
        else
            Log::get_instance()->init("./ServerLog", open_log, 2000, 800000, 800);
    }
}

void webserver::run() {
    bool stop_server = false;
    bool timeout = false; // 定时器是否到期

    while (!stop_server) {

        int number = epoll_wait(sev_epollfd, sev_events, MAX_EVENT_NUMBER, -1);

        if (number < 0 && errno != EINTR) {
            /* 如果在系统调用进行时出现信号，许多系统调用将报告EINTR错误代码。
             * 实际上没有发生错误，只是这样报告，因为系统不能自动恢复系统调用。 */
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i=0; i<number; ++i) {
            int sockfd = sev_events[i].data.fd;

            if (sockfd == sev_listenfd) {

                deal_connection();
            } else if (sev_events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                /*
                 * EPOLLHUP 表示 socket 连接被挂断或对端关闭连接，该事件通常与 EPOLLIN 一起使用，用于判断是否可以从该 socket 中读取数据，如果 EPOLLHUP 事件被触发，则一般认为该 socket 可读。
                 * EPOLLRDHUP 表示对端关闭连接，该事件只有在使用 EPOLLIN 监听该 socket 时才会触发，而且只会在所有数据都被读取完毕后才会触发，所以可以认为这是一个更加准确的关闭连接事件标志
                 * */
                tw_timer* timer = client_timers[sockfd]->timer;
                del_timer(timer);

            } else if (sockfd == sev_signal[0] && (sev_events[i].events & EPOLLIN)) {

                deal_signal(timeout, stop_server);
            } else if (sev_events[i].events & EPOLLIN) {
                deal_read(sockfd);
            } else if (sev_events[i].events & EPOLLOUT) {
                deal_write(sockfd);
            }
        }

        if (timeout) {
            LOG_INFO("%s", "timer tick");
            timer_wheel->time_handler(); // 定时器清理非活动连接
            timeout = false;
        }
    }
}

void webserver::deal_connection() {
    sockaddr_in client_address;
    socklen_t client_addlength = sizeof client_address;

    if (listen_mode == LT_MODE) {
        int connfd = accept(sev_listenfd, (sockaddr*)&client_address, &client_addlength);
        if (connfd < 0) {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return;
        }
        if (HttpCon::connection_count >= MAX_CONNECTION_NUMBER) {

            LOG_ERROR("%s", "Internal server busy");
            utils.show_error(connfd, "Internal server busy");
            return;
        }
        init_connection(connfd, client_address);
    }
    else {
        while (true) {
            int connfd = accept(sev_listenfd, (sockaddr*)&client_address, &client_addlength);
            if (connfd < 0) {
                // 无连接请求了，退出循环
                LOG_ERROR("%s:errno is:%d", "accept error", errno); // errno为EAGIN/EWOULDBLOCK
                return;
            }
            if (HttpCon::connection_count >= MAX_CONNECTION_NUMBER) {

                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                return;
            }
            init_connection(connfd, client_address);
        }
    }
}

void webserver::init_connection(int connfd, sockaddr_in client_address) {

    clients.insert({connfd, new HttpCon});
    clients[connfd]->init(connfd, client_address, work_rootdirectory, connect_mode,db_username, db_pwd, db_name, open_log);


    client_timers.insert({connfd, new client_data});
    client_timers[connfd]->address = client_address;
    client_timers[connfd]->sockfd = connfd;


    tw_timer* timer = timer_wheel->add_timer(3 * TIMESLOT);
    timer->cb_func = cb_func;
    timer->user_data = client_timers[connfd];

    client_timers[connfd]->timer = timer;

}

void webserver::del_timer(tw_timer* timer) {

    // assert(timer);
    if (timer == nullptr) {
        std::cout << "delete timer is null" << std::endl;
        return;
    }
    timer->cb_func(timer->user_data, sev_epollfd);

    timer_wheel->del_timer(timer);

    LOG_INFO("close fd %d", client_timers[timer->user_data->sockfd]->sockfd);
}

void webserver::deal_signal(bool &timeout, bool &stop_server) {
    int ret = 0;
    char signals[1024];

    ret = recv(sev_signal[0], signals, sizeof signals, 0);

    if (ret == -1 || ret == 0) return;

    for (int i=0; i<ret; ++i) {
        switch (signals[i]) {
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            case SIGALRM:
            {
                timeout = true;
                break;
            }
        }
    }
}

void webserver::deal_read(int sockfd) {
    tw_timer* timer = client_timers[sockfd]->timer;

    if (mode == REACTOR) {
        adjust_timer(timer);

        // 检测到读事件，将该事件放入请求队列
        threadpool->reactor_append(clients[sockfd], READ);
        /* TODO */

        while (true) {
            if (clients[sockfd]->timer_flag) {
                del_timer(timer);
                clients[sockfd]->timer_flag = false;
                break;
            }
        }
    }
    else {
        if (clients[sockfd]->read_once()) {
            //LOG_INFO("deal with the client(%s)", inet_ntoa(clients[sockfd].get_address()->sin_addr));

            threadpool->proactor_append(clients[sockfd]);

            adjust_timer(timer);
        } else {
            del_timer(timer);
        }
    }
}

void webserver::deal_write(int sockfd) {
    tw_timer* timer = client_timers[sockfd]->timer;

    if (mode == REACTOR) {

        adjust_timer(timer);

        threadpool->reactor_append(clients[sockfd], WRITE);

        while (true) {

            if (clients[sockfd]->timer_flag) {
                del_timer(timer);
                clients[sockfd]->timer_flag = false;
                break;
            }
        }
    } else {
        if (clients[sockfd]->write()) {
            LOG_INFO("send data to the client(%s)", inet_ntoa(clients[sockfd]->get_address()->sin_addr));
            adjust_timer(timer);
        } else {
            del_timer(timer);
        }
    }
}
int Utils::utils_epollfd;
int* Utils::utils_signal_pipefd;

void webserver::server_listen() {
    sev_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(sev_listenfd >= 0);

    // 如何关闭连接
    /*
     * 当一个连接关闭时，有可能会有一些未发送的数据在发送缓冲区中等待发送。SO_LINGER选项可以用来控制close()函数的行为，以确保发送缓冲区中的所有数据都被发送或丢弃。
当SO_LINGER选项被设置为一个非零的linger结构体时，close()函数将会等待指定的时间（以秒为单位）来发送未发送的数据，如果在指定的时间内所有数据都被发送完毕，则close()函数会立即返回。如果在指定时间内未能发送所有数据，则close()函数会强制关闭连接并丢弃发送缓冲区中的所有数据。
当SO_LINGER选项被设置为一个零值的linger结构体时，close()函数将会立即返回，并丢弃发送缓冲区中的所有数据。
     * */
    linger lin = {close_mode, 1};
    setsockopt(sev_listenfd, SOL_SOCKET, SO_LINGER, &lin, sizeof lin);

    int ret = 0;

    sockaddr_in address;
    bzero(&address, sizeof address);
    /*
     * 监听所有本地可用IP地址
     * 如果服务端程序绑定到一个具体的IP地址上，那么只能接受来自该IP地址的客户端连接
     * */
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_family = AF_INET;
    address.sin_port = htons(sev_listen_port);

    int flag = 1;
    /*
     * 当SO_REUSEADDR选项被启用时，新的socket可以在TIME_WAIT状态期间绑定到同一端口，并且可以接受来自之前连接的数据。
     * 可能会导致一些安全问题，因为它允许来自之前连接的数据被传递给新的连接。
     * */
    setsockopt(sev_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof flag);
    ret = bind(sev_listenfd, (sockaddr*)&address, sizeof address);
    assert(ret >= 0);

    ret = listen(sev_listenfd, 5);
    assert(ret >= 0);

    // 创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    sev_epollfd = epoll_create(5);
    assert(sev_epollfd != -1);
    timer_wheel->epollfd = sev_epollfd; // 为时间轮设置epollfd

    utils.register_read(sev_epollfd, sev_listenfd, false, listen_mode);
    HttpCon::epollfd = sev_epollfd;

    //创建信号处理管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sev_signal);
    assert(ret != -1);
    utils.setnonblocking(sev_signal[1]);
    utils.register_read(sev_epollfd, sev_signal[0], false, LT_MODE);

    utils.sig_handler(SIGPIPE, SIG_IGN);
    utils.sig_handler(SIGALRM, utils.send_signal, false);
    utils.sig_handler(SIGTERM, utils.send_signal, false);

    alarm(TIMESLOT);

    Utils::utils_signal_pipefd = sev_signal;
    Utils::utils_epollfd = sev_epollfd;
}