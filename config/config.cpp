//
// Created by utt on 3/31/23.
//

#include <getopt.h>
#include <cstdlib>
#include "config.h"
#include "../utils/utils.h"

Config::Config() {
    PORT = 9006; // 默认端口

    // LT + LT，监听+连接端口混合epoll模式
    mix_trigger_mode = LL_MODE;

    // 监听端口epoll模式
    listen_trigger_mode = LT_MODE;

    // 连接端口epoll模式
    connfd_trigger_mode = LT_MODE;

    // close()关闭动作，直接丢弃未发送数据包，还是等待一段时间发送
    opt_linger = NO_LINGER;

    sql_nums = 8;

    thread_nums = 8;

    // 事件处理模型
    actor_model = PROACTOR;

    open_log = false; // 默认关闭日志

    log_write = 0; // 默认同步写入日志
}

void Config::parse_args(int argc, char **argv) {
    int opt;

    while ((opt = getopt(argc, argv, args_format)) != -1) {
        switch (opt) {
            case 'p':
            {
                PORT = atoi(optarg);
                break;
            }
            case 'm':
            {
                mix_trigger_mode = atoi(optarg);
                break;
            }
            case 'o':
            {
                opt_linger = atoi(optarg);
                break;
            }
            case 's':
            {
                sql_nums = atoi(optarg);
                break;
            }
            case 't':
            {
                thread_nums = atoi(optarg);
                break;
            }
            case 'a':
            {
                actor_model = atoi(optarg);
                break;
            }
            case 'c':
            {
                open_log = atoi(optarg) != 0;
                break;
            }
            case 'l':
            {
                log_write = atoi(optarg);
                break;
            }
            default:
                break;
        }
    }
}