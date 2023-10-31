//
// Created by utt on 3/18/23.
//

#ifndef WEBSERVER_HTTP_CONN_H
#define WEBSERVER_HTTP_CONN_H

#include <netinet/in.h>
#include <string>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/mman.h>

#include "sqlpool/sqlpool.h"
#include "utils/utils.h"

static const int FILENAME_LEN = 200;
static const int READ_BUFFER_SIZE = 2048;
static const int WRITE_BUFFER_SIZE = 1024;

enum METHOD {
    GET = 0,
    POST,
    HEAD
};

enum CHECK_STATE {
    CHECK_STATE_REQUESTLINE = 0,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT
};

enum LINE_STATUS {
    LINE_OK = 0,
    LINE_OPEN,
    LINE_BAD
};

enum HTTP_CODE {
    NO_REUEST = 0,
    GET_REQUEST,
    BAD_REQUEST,
    NO_RESOURCE,
    FORBIDDEN_REQUEST,
    FILE_REQUEST,
    INTERNAL_ERROR,
    CLOSED_CONNECTION
};

enum OPERATOR {
    READ = 0,
    WRITE
};

class HttpCon {
public:
    HttpCon() {}
    ~HttpCon() {}

    void init(int sockfd, sockaddr_in& addr, char* root, trigger_mode mode,
              std::string db_user, std::string db_pwd, std::string db_name, bool open_log);
    void initmysql_result(SqlPool* pool);

    void close_connection();

public:
    LINE_STATUS parse_line();
    HTTP_CODE parse_request_line(char* request_line);
    HTTP_CODE parse_headers(char*  headers);
    HTTP_CODE parse_content(char* content);
    HTTP_CODE do_request();

    bool read_once();
    bool write();


    HTTP_CODE process_read();
    bool process_write(HTTP_CODE read_ret);
    void process();

    sockaddr_in *get_address() {
        return &client_address;
    }

public:
    static int connection_count;
    static int epollfd;
    static Utils *utils;

    MYSQL* sql_connection;
    sockaddr_in client_address;

    // 读写类型
    int operator_type;

    // 时钟是否到期，到期删除
    bool timer_flag;



private:
    int check_state;

    int start_index; // 上一次报文检查到的位置
    int read_index; // 报文长度
    int checked_index; // 报文检查到的位置

    int write_index;

    char write_buf[WRITE_BUFFER_SIZE];
    char read_buf[READ_BUFFER_SIZE];

    bool open_log;

    int sockfd;
    trigger_mode t_mode;

    /* 数据库信息 */
    std::string db_username;
    std::string db_pwd;
    std::string db_name;

    /* 请求信息 */
    char* request_url; // 请求路径：http://127.0.0.1/test 指向‘/’
    METHOD request_method;
    char* protocol_version;
    long request_content_length;
    bool request_keep_alive;
    char* request_host;
    char* request_content;


    /* 响应信息 */
    char* response_url;
    struct iovec response_content[2];
    int response_iv_count;

    int bytes_to_send;
    int bytes_have_sent;

    char* site_root_path; // 网站根目录
    char request_file_path[FILENAME_LEN]; // 请求html在本机的路径
    struct stat request_file_stat;
    char* request_file_address; // 文件映射到进程空间的地址




    void init();
    char* getline() { return read_buf + start_index; }
    void free_char(char* pointer) { free(pointer); pointer = nullptr; }
    void unmap() { if (request_file_address) { munmap(request_file_address, request_file_stat.st_size); request_file_address = nullptr; } }

    bool add_response(const char* format, ...);
    bool add_status_line(int status, const char* title);
    bool add_headers(int conten_len);
    bool add_content_length(int content_len);
    bool add_content_type();
    bool add_linger();
    bool add_blank_line();
    bool add_content(const char* content);
};
#endif //WEBSERVER_HTTP_CONN_H
