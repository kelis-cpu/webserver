//
// Created by utt on 3/27/23.
//

#include <fcntl.h>
#include <sys/epoll.h>
#include <cerrno>
#include <sys/socket.h>
#include <cstring>
#include <csignal>
#include <cassert>
#include <unistd.h>
#include "utils.h"

int Utils::setnonblocking(int fd) {
    int old_options = fcntl(fd, F_GETFL);
    int new_options = old_options | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_options);
    return old_options;
}

void Utils::register_read(int epollfd, int fd, bool one_shot, trigger_mode mode) {
    epoll_event event;
    event.data.fd = fd;

    event.events = EPOLLIN | EPOLLRDHUP;

    if (mode == ET_MODE) {
        event.events |= EPOLLET;
    }

    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void Utils::send_signal(int sig) {
    // 保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(utils_signal_pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void Utils::del_fd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

void Utils::reset_oneshot(int epollfd, int fd, int ev, int trigger_mode) {
    epoll_event event;

    event.data.fd = fd;

    event.events = EPOLLONESHOT | EPOLLRDHUP | ev;
    if (trigger_mode == ET_MODE) {
        event.events |= EPOLLET;
    }

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void Utils::sig_handler(int sig, void (*handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof sa);
    sa.sa_handler = handler;

    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

void Utils::show_error(int connfd, const char *info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

