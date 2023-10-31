//
// Created by utt on 4/4/23.
// 阻塞队列，线程安全
//

#ifndef WEBSERVER_BLOCK_QUEUE_H
#define WEBSERVER_BLOCK_QUEUE_H

#include <queue>
#include <sys/time.h>
#include "../lock/lock.h"

template<typename T>
class block_queue {
public:
    block_queue(int max_size = 1000);
    ~block_queue();
    bool full();
    bool empty();
    void clear();
    bool front(T &value);
    bool back(T &value);
    int size();
    int capacity();

    bool push(T &item);

    bool pop(T &item);
    bool pop(T &item, int ms_timeout);


private:
    Mutex lock;
    Cond cond;

   int max_size;
   std::deque<T> _block_queue;
};

// 超时pop
template<typename T>
bool block_queue<T>::pop(T &item, int ms_timeout) {
    lock.lock();
    struct timespec wait_time = {0 , 0}; // 绝对时间：系统时间 + 等待时间
    struct timeval now_time = {0, 0}; // 系统时间

    gettimeofday(&now_time, nullptr);

    if (empty()) {
        wait_time.tv_sec = now_time.tv_sec + ms_timeout / 1000;
        wait_time.tv_nsec = (ms_timeout % 1000) * 1000;

        if (cond.time_wait(lock.get(), wait_time)) { // 获得了锁
            lock.unlock();
            return false;
        }
    }

    if(empty()) { // 超时等待 RETURNVAL == ETIMEOUT
        lock.unlock();
        return false;
    }

    item = _block_queue.front();
    _block_queue.pop_front();
    lock.unlock();
    return true;

}

//pop时,如果队列没有元素，将会等待条件变量
template<typename T>
bool block_queue<T>::pop(T &item) {
    lock.lock();

    while (empty()) {
        if (!cond.wait(lock.get())) {
            lock.unlock();
            return false;
        }
    }

    item = _block_queue.front();
    _block_queue.pop_front();
    lock.unlock();

    return true;
}

template<typename T>
bool block_queue<T>::push(T &item) {
    lock.lock();

    if (_block_queue.size() >= max_size) {
        cond.broadcast();
        lock.unlock();
        return false;
    }

    _block_queue.push_back(item);

    cond.broadcast();
    lock.unlock();
    return true;
}

template<typename T>
int block_queue<T>::capacity() {
    lock.lock();

    int mz = max_size;

    lock.unlock();

    return mz;
}

template<typename T>
int block_queue<T>::size() {
    lock.lock();
    int sz = _block_queue.size();
    lock.unlock();
    return sz;
}

template<typename T>
bool block_queue<T>::back(T &value) {
    lock.lock();

    if (_block_queue.empty()) {
        lock.unlock();
        return false;
    }

    value = _block_queue.back();
    lock.unlock();
    return true;
}

template<typename T>
bool block_queue<T>::front(T &value) {
    lock.lock();

    if (_block_queue.empty()) {
        lock.unlock();
        return false;
    }

    value = _block_queue.front();
    lock.unlock();
    return true;
}

template<typename T>
void block_queue<T>::clear() {
    lock.lock();

    _block_queue.clear();
    max_size = 0;

    lock.unlock();
}

template<typename T>
block_queue<T>::block_queue(int max_size) {
    if (max_size <= 0) exit(-1);

    this->max_size = max_size;
}

template<typename T>
block_queue<T>::~block_queue() {
    lock.lock();
    _block_queue.clear();
    max_size = 0;
    lock.unlock();
}

template<typename T>
bool block_queue<T>::full() {
    lock.lock();
    if (_block_queue.size() == max_size) {
        lock.unlock();
        return true;
    }
    lock.unlock();
    return false;
}

template<typename T>
bool block_queue<T>::empty() {
    lock.lock();

    if (_block_queue.empty()) {
        lock.unlock();
        return true;
    }

    lock.unlock();
    return false;
}





#endif //WEBSERVER_BLOCK_QUEUE_H
