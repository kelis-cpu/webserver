//
// Created by utt on 3/18/23.
//

#ifndef WEBSERVER_THREADPOOL_H
#define WEBSERVER_THREADPOOL_H

#include <csignal>
#include <list>
#include "../lock/lock.h"
#include "../sqlpool/sqlpool.h"
#include "../utils/utils.h"

enum THREAD_OPERATOR {
    TEMPLATE_READ = 0,
    TEMPLATE_WRITE
};

template<typename T>
class Threadpool {
public:
    Threadpool(actor_mode amode, SqlPool* sqlPool, int threads_num = 8, int max_requests = 10000);
    ~Threadpool();
    bool reactor_append(T* request, int operator_type);
    bool proactor_append(T* request);
private:
    static void* worker(void* arg) {
        Threadpool<T> *pool = (Threadpool<T>*) arg;
        pool->run();
        return pool;
    }
    void run();

private:
    pthread_t* pthreads;
    int threads_num;
    int max_requests;

    std::list<T*> work_queue; // 工作队列
    Sem queue_stat;
    Mutex queuelocker;

    SqlPool* sqlPool;
    actor_mode mode; // 事件处理模式：REACTOR PROACTOR
};


template<typename T>
Threadpool<T>::Threadpool(actor_mode amode, SqlPool* sqlPool, int threads_num, int max_requests) : mode(amode), sqlPool(sqlPool), threads_num(threads_num), max_requests(max_requests){
    if (threads_num <= 0 || max_requests <= 0) {
        throw std::exception();
    }

    pthreads = new pthread_t [threads_num];

    if (!pthreads) throw std::exception();

    for (int i=0; i<threads_num; ++i) {
        if (pthread_create(pthreads + i, nullptr, worker, this) != 0) {
            delete [] pthreads;
            throw std::exception();
        }
        /* 该线程结束后会自动释放所有资源 */
        if (pthread_detach(pthreads[i]) != 0) {
            delete [] pthreads;
            throw std::exception();
        }
    }
}

template<typename T>
Threadpool<T>::~Threadpool() {
    delete [] pthreads;
}

template<typename T>
bool Threadpool<T>::proactor_append(T *request) {
    /* 如果目标锁已经被加锁，该调用会阻塞，直到该锁的占有者将其解锁 */
    queuelocker.lock();

    if (work_queue.size() >= max_requests) {
        queuelocker.unlock();
        return false;
    }

    work_queue.push_back(request);
    queuelocker.unlock();
    /* 信号量初始值为0，加入请求任务之后增加信号量值 */
    queue_stat.post();
    return true;
}

template<typename T>
bool Threadpool<T>::reactor_append(T *request, int operator_type) {
    request->operator_type = operator_type;
    return proactor_append(request);
}

template<typename T>
void Threadpool<T>::run() {
    while (true) {
        queue_stat.wait();
        queuelocker.lock();

        if (work_queue.empty()) {
            queuelocker.unlock();
            continue;
        }
        T* request = work_queue.front();
        work_queue.pop_front();
        queuelocker.unlock();

        if (request == nullptr) continue;

        if (mode == REACTOR) {
            if (request->operator_type == TEMPLATE_READ) {
                if (request->read_once()) {
                    connectionRAII mysqlcon(&request->sql_connection, sqlPool);
                    request->process();
                } else {
                    request->timer_flag = true;
                }
            } else {
                if (!request->write()) {
                    request->timer_flag = true;
                }
            }
        }
        // PROACTOR
        else {
            connectionRAII mysqlconn(&request->sql_connection, sqlPool);
            request->process();
        }
    }
}


#endif //WEBSERVER_THREADPOOL_H
