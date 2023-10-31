//
// Created by utt on 4/4/23.
//

#include <cstring>
#include <cstdarg>
#include "log.h"

bool Log::open_log;

Log::Log() {
    async = false; // 默认同步写日志
    logs_lines = 0;
    open_log = false;
}

Log::~Log() {
    if (log_fp != nullptr) fclose(log_fp);
}


bool Log::init(const char* file_name, bool open_lg, int log_buf_size,
               int max_lines, int max_queue_size) {
    // 异步需要设置阻塞队列长度
    if (max_queue_size >= 1) {
        async = true;
        log_queue = new block_queue<std::string>;

        pthread_t tid;
        // 创建线程异步写日志
        pthread_create(&tid, nullptr, flush_log_thread, nullptr);
    }

    // this->open_log = open_log;
    open_log = open_lg;

    this->log_buf_size = log_buf_size;
    log_buf = new char[log_buf_size];
    memset(log_buf, '\0', log_buf_size);

    this->max_lines = max_lines;

    // 日志文件处理
    time_t now_time = time(nullptr);
    struct tm* sys_tm = localtime(&now_time);
    // localtime返回一个tm指针，空间由localtime控制，连续使用这个函数会覆盖之前的时间，一旦使用了该函数，应该马上取出tm结构中的内容
    struct tm my_tm = *sys_tm;

    const char* file_pos = strrchr(file_name, '/');

    char log_full_name[256];
    memset(log_full_name, '\0', 256);

    if (file_pos == nullptr) {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    } else {
        strcpy(log_filename, file_pos + 1);
        strncpy(dir_path, file_name, file_pos - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s",
                 dir_path, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_filename);
    }

    today = my_tm.tm_mday;

    log_fp = fopen(log_full_name, "a");

    return log_fp != nullptr;

}

void Log::write_log(int level, const char *format, ...) {
    struct timeval now_time = {0, 0};
    gettimeofday(&now_time, nullptr);

    time_t now_timesec = now_time.tv_sec;

    struct tm *sys_tm = localtime(&now_timesec);
    struct tm my_tm = *sys_tm;

    char log_level[16];
    memset(log_level, '\0', 16);

    switch (level) {
        case 0:
            strcpy(log_level, "[debug]:");
            break;
        case 1:
            strcpy(log_level, "[info]:");
            break;
        case 2:
            strcpy(log_level, "[warn]:");
            break;
        case 3:
            strcpy(log_level, "[errno]:");
            break;
        default:
            strcpy(log_level, "[info]:");
            break;
    }
    // 写入一个log， logs_lines++
    mutex.lock();
    ++logs_lines;


    if (today != my_tm.tm_mday || logs_lines % max_lines == 0) {
        char new_log_filename[256] = {0};

        fflush(log_fp);
        fclose(log_fp);

        char file_tail[16] = {0};

        snprintf(file_tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        if (today != my_tm.tm_mday) {
            // 该日志文件不属于今日
            snprintf(new_log_filename, 255, "%s%s%s", dir_path, file_tail, log_filename);
            today = my_tm.tm_mday;
            logs_lines = 0;
        } else {
            // 日志行数超过最大行数
            snprintf(new_log_filename, 255, "%s%s%s.%lld", dir_path, file_tail, log_filename, logs_lines / max_lines);
        }
        log_fp = fopen(new_log_filename, "a");
    }
    mutex.unlock();

    va_list valist;
    va_start(valist, format);

    std::string log_str;

    mutex.lock();

    // 写入时间的具体格式
    int time_len = snprintf(log_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                            my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                            my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now_time.tv_usec, log_level);
    // 写入日志
    int log_len = vsnprintf(log_buf, log_buf_size - time_len - 1, format, valist);

    log_buf[time_len + log_len] = '\n';
    log_buf[time_len + log_len + 1] = '\0';

    log_str = log_buf;

    mutex.unlock();

    if (async && !log_queue->full()) {
        log_queue->push(log_str);
    } else {
        mutex.lock();
        fputs(log_buf, log_fp);
        mutex.unlock();
    }

    va_end(valist);
}

void Log::flush() {
    mutex.lock();
    fflush(log_fp);
    mutex.unlock();
}

void* Log::async_write_log() {

    std::string singel_log;

    // 从阻塞队列取出一个日志string，写入文件
    while (log_queue->pop(singel_log)) {
        mutex.lock();
        fputs(singel_log.c_str(), log_fp);
        mutex.unlock();
    }
}