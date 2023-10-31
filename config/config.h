//
// Created by utt on 3/31/23.
//

#ifndef WEBSERVER_CONFIG_H
#define WEBSERVER_CONFIG_H


class Config {
private:
    const char *args_format = "p:l:m:o:s:t:c:a:";
public:
    Config();
    ~Config() {}

    void parse_args(int argc, char* argv[]);

     // 端口号 -p
     int PORT;

     // 触发组合模式 -m
     int mix_trigger_mode;

     // listenfd触发模式
     int listen_trigger_mode;

     // connfd触发模式
     int connfd_trigger_mode;

     // 如何关闭连接 -o
    int opt_linger;

    // 数据库连接池数量 -s
    int sql_nums;

    // 线程池内的线程数量 -t
    int thread_nums;

    // 并发模型选择 -a
    int actor_model;

    // 是否开启日志 -c
    bool open_log;

    // 日志写入方式 -l
    int log_write; // 0: 同步写入， 1: 异步写入
};

#endif //WEBSERVER_CONFIG_H
