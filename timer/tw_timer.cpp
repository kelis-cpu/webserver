//
// Created by utt on 3/22/23.
//

#include <unistd.h>
#include <iostream>
#include "tw_timer.h"

time_wheel::time_wheel() : cur_slot(0) {
    for (int i=0; i<N; ++i) {
        slots[i] = nullptr;
    }
}

time_wheel::time_wheel(int epollfd) : epollfd(epollfd), cur_slot(0) {
    for (int i = 0; i < N; ++i) {
        slots[i] = nullptr;
    }
}

time_wheel::~time_wheel() {
    for (int i=0; i<N; ++i) {
        tw_timer* tmp = slots[i];

        while (tmp) {
            slots[i] = tmp->next;
            delete tmp;
            tmp = slots[i];
        }
    }
}

tw_timer* time_wheel::add_timer(int timeout) {
    if (timeout < 0) return nullptr;

    int ticks = 0; // 多少次tick之后触发

    if (timeout < SI) {
        ticks = 1;
    } else {
        ticks = timeout / SI;
    }

    int rot = ticks / N;

    int ts = (cur_slot + ticks % N) % N;

    tw_timer* timer = new tw_timer(rot, ts);

    if (slots[ts] == nullptr) {
        slots[ts] = timer;
    } else {
        timer->next = slots[ts];
        slots[ts]->prev = timer;
        slots[ts] = timer;
    }
    return timer;
}

void time_wheel::del_timer(tw_timer *timer) {
    if (timer == nullptr) return;

    int slot = timer->time_slot;

    if (slots[slot] == timer) { // timer位于头节点
        slots[slot] = slots[slot]->next;
        if (slots[slot]) {
            slots[slot]->prev = nullptr;
        }
    } else {
        timer->prev->next = timer->next;
        if (timer->next) {
            timer->next->prev = timer->prev;
        }
    }

    delete timer;
}

// SI时间到，调用该函数，时间轮向前滚动一个槽
void time_wheel::tick() {
    tw_timer* cs = slots[cur_slot];

    //std::cout << "cur slot is " << cur_slot << std::endl;

    while (cs) {
        if (cs->rotation > 0) {
            --(cs->rotation);
            cs = cs->next;
        } else {
            cs->cb_func(cs->user_data,  epollfd);

            tw_timer* temp = cs->next;
            del_timer(cs);
            cs = temp;
        }
    }
    cur_slot = (cur_slot + 1) % N;
}

void time_wheel::adjust_timer(tw_timer *timer, int timeout) {
    if (timer == nullptr) return;

    int ticks = timeout > SI ? timeout / SI : 1;

    int rot = ticks / N;

    timer->rotation += rot;
}

void time_wheel::time_handler() {
    tick();
    alarm(TIMESLOT);
}