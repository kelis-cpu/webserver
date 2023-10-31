//
// Created by utt on 4/4/23.
//

#ifndef WEBSERVER_LOG_H
#define WEBSERVER_LOG_H

#include <string>
#include "block_queue.h"

class Log {
public:
    static Log* get_instance()
    {
        static Log instance;
        return &instance;
    }

    static void* flush_log_thread(void* args) { Log::get_instance()->async_write_log(); }
    bool init(const char* file_name, bool open_log, int log_buf_size = 8192, int max_lines = 5000000, int max_queue_size = 0);
    void write_log(int level, const char* format, ...);
    void flush(void);

    static bool open_log; // 是否打开日志
private:
    Log();
    virtual ~Log();
    void* async_write_log();
private:
    char dir_path[128]; // 日志文件路径
    char log_filename[128]; // 日志文件名
    char *log_buf; // 日志缓冲区
    int max_lines; // 一个日志文件中的日志最大行数
    int log_buf_size; // 日志缓冲区大小
    long long logs_lines; // 日志行数记录
    int today; // 日志按天分类

    FILE *log_fp; // 日志文件指针
    block_queue<std::string> *log_queue;
    bool async;
    Mutex mutex;

};

#define LOG_DEBUG(format, ...) if(Log::open_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(Log::open_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(Log::open_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(Log::open_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif //WEBSERVER_LOG_H
