//
// Created by utt on 3/18/23.
//

#ifndef WEBSERVER_LOCK_H
#define WEBSERVER_LOCK_H

#include <semaphore.h>
#include <exception>
#include <thread>

class Sem {
public:
    Sem() {
        if (sem_init(&sem, 0, 0) != 0) {
            throw std::exception();
        }
    }

    Sem(unsigned int sem_num) {
        if (sem_init(&sem, 0, sem_num) != 0) {
            throw std::exception();
        }
    }

    ~Sem() {
        sem_destroy(&sem);
    }

    bool wait() {
        return sem_wait(&sem) == 0;
    }

    bool post() {
        return sem_post(&sem) == 0;
    }

private:
    sem_t sem;
};

class Mutex {
public:
    Mutex() {
        if (pthread_mutex_init(&mutex, nullptr) != 0) {
            throw  std::exception();
        }
    }

    ~Mutex() { pthread_mutex_destroy(&mutex); }

    bool lock() {
        return pthread_mutex_lock(&mutex) == 0;
    }

    bool unlock() {
        return pthread_mutex_unlock(&mutex) == 0;
    }

    pthread_mutex_t *get() { return &mutex; }
private:
    pthread_mutex_t mutex;
};

class Cond {
public:
    Cond() {
        if (pthread_cond_init(&cond, nullptr) != 0) throw std::exception();
    }

    ~Cond() {
        pthread_cond_destroy(&cond);
    }

    // 等待条件变量
    bool wait(pthread_mutex_t* mutex) {
        return pthread_cond_wait(&cond, mutex) == 0;
    }

    bool time_wait(pthread_mutex_t* mutex, struct timespec time) {
        return pthread_cond_timedwait(&cond, mutex, &time) == 0;
    }

    // 唤醒一个线程，按照线程优先级和调度策略
    bool signal() {
        return pthread_cond_signal(&cond) == 0;
    }
    // 唤醒所有等待线程
    bool broadcast() {
        return pthread_cond_broadcast(&cond) == 0;
    }
private:
    pthread_cond_t cond;
};
#endif //WEBSERVER_LOCK_H
