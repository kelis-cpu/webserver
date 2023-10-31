//
// Created by utt on 3/18/23.
//

#include <string>
#include "config/config.h"
#include "webserver.h"

int main(int argc, char *argv[])
{
    std::string db_username = "root";
    std::string db_pwd = "root";
    std::string db_name = "webserver";

    // 命令行参数解析
    Config config;
    config.parse_args(argc, argv);

    webserver server;

    server.init(config.PORT, db_username, db_name, db_pwd, config.sql_nums, config.thread_nums, config.mix_trigger_mode,
                config.opt_linger, config.actor_model, config.open_log, config.log_write);

    // 初始化日志
    server.init_log();

    // 初始化数据库
    server.init_sqlpool();

    // 初始化线程池
    server.init_threadpool();

    // 设置触发模式
    server.init_triggermode();

    // 监听
    server.server_listen();

    // 运行
    server.run();

    return 0;
}