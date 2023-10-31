//
// Created by utt on 3/22/23.
//

#ifndef WEBSERVER_TW_TIMER_H
#define WEBSERVER_TW_TIMER_H

#include <netinet/in.h>

const int BUFFER_SIZE = 64;

class tw_timer;


struct client_data {
    sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    tw_timer* timer;
};

class tw_timer {
public:
    tw_timer(int rot, int ts) : next(nullptr), prev(nullptr), rotation(rot), time_slot(ts) {}
public:
    int rotation; // 定时器在多少圈之后生效
    int time_slot; // 定时器属于哪个槽
    void (*cb_func)(client_data*, int epollfd);
    client_data* user_data;
    tw_timer* prev;
    tw_timer* next;
};

class time_wheel {
public:
    int epollfd;
    ~time_wheel();
    time_wheel();
    time_wheel(int epollfd);

    tw_timer* add_timer(int timeout);
    void del_timer(tw_timer* timer);
    void adjust_timer(tw_timer* timer, int timeout);
    void tick();
    void time_handler();
private:
    static const int N = 60;
    static const int SI = 1;
    static const int TIMESLOT = 5;
    tw_timer* slots[N];
    int cur_slot;
};

#endif //WEBSERVER_TW_TIMER_H
