//
// Created by utt on 3/27/23.
//

#ifndef WEBSERVER_UTILS_H
#define WEBSERVER_UTILS_H

enum mix_triggermode {LL_MODE = 0, LE_MODE, EL_MODE, EE_MODE, };
enum trigger_mode {LT_MODE = 0, ET_MODE};
enum actor_mode {PROACTOR = 0, REACTOR};
enum linger_mode {NO_LINGER = 0, LINGER};

class Utils {
public:
    Utils() {}
    ~Utils() {}

    int setnonblocking(int fd);

    void register_read(int epollfd, int fd, bool one_shot, trigger_mode mode);

    static void send_signal(int sig);

    void del_fd(int epollfd, int fd);

    void reset_oneshot(int epollfd, int fd, int ev, int trigger_mode);

    /* 设置信号函数 */
    void sig_handler(int sig, void(handler)(int), bool restart = true);

    /* 定时处理任务，重新定时不断触发SIGALRM */
    // void timer_handler();

    void show_error(int connfd, const char* info);

public:
    static int* utils_signal_pipefd;
    static int utils_epollfd;

};
#endif //WEBSERVER_UTILS_H
