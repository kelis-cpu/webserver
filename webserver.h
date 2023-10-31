//
// Created by utt on 3/18/23.
//

#ifndef WEBSERVER_WEBSERVER_H
#define WEBSERVER_WEBSERVER_H

#include <sys/epoll.h>
#include <unordered_map>
#include "threadpool/threadpool.h"
#include "http_conn.h"
#include "sqlpool/sqlpool.h"
#include "timer/tw_timer.h"
#include "utils/utils.h"



const int MAX_EVENT_NUMBER = 10000;
const int MAX_CONNECTION_NUMBER = 65536;
const int TIMESLOT = 5;



class webserver {
public:
    webserver();
    ~webserver();

    void init(int port, std::string db_uname, std::string db_name, std::string db_pwd,
              int sql_num, int thread_num, int mix_mode, int lmode, int amode, bool open_log, int log_write);

    void init_log();
    void init_threadpool();
    void init_sqlpool();
    void init_triggermode();
    void init_connection(int connfd, sockaddr_in client_address);


public:

    int sev_listen_port;
    int sev_epollfd;
    int sev_listenfd;
    int sev_signal[2];

    bool open_log; // 是否开启日志
    int log_write; // 日志写入方式

    char* work_rootdirectory;

    trigger_mode listen_mode;
    trigger_mode connect_mode;
    mix_triggermode mix_mode;

    actor_mode mode;
    linger_mode close_mode;

    epoll_event sev_events[MAX_EVENT_NUMBER];

    /* 线程池 */
    Threadpool<HttpCon>* threadpool;
    int sev_threadnum;

    /* 数据库连接池 */
    SqlPool* sqlPool;
    std::string db_username;
    std::string db_pwd;
    std::string db_name;
    int max_sqlcon_num;

    /* 所有客户端连接对象 */
    std::unordered_map<int, HttpCon*> clients;
    std::unordered_map<int, client_data*> client_timers;
    time_wheel* timer_wheel;

    Utils utils;


public:

    void deal_connection();
    void del_timer(tw_timer* timer);
    void deal_signal(bool& timeout, bool& stop_server);
    void deal_read(int sockfd);
    void deal_write(int sockfd);

    void adjust_timer(tw_timer* timer);


    void server_listen();
    void run();
private:
    void free_clients();
    void free_clients_timers();
};

void cb_func(client_data*, int epollfd);
#endif //WEBSERVER_WEBSERVER_H
