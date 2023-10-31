//
// Created by utt on 3/18/23.
//

#include <map>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>


#include "http_conn.h"
#include "log/log.h"

// 定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *ok_200_form = "<html><body></body></html>";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

std::map<std::string, std::string> users_info;
Mutex sql_lock;

/* 初始化静态变量 */
int HttpCon::epollfd = -1;
int HttpCon::connection_count = 0;
Utils* HttpCon::utils = new Utils;

void HttpCon::initmysql_result(SqlPool *pool) {
    // 从池中取一个连接
    MYSQL* mysql = nullptr;
    connectionRAII connection(&mysql, pool);

    // 在user表中检索username，passwd数据
    if (mysql_query(mysql, "select username, passwd from user")) {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    // 从表中检索完整的结果集
    MYSQL_RES* mysql_result = mysql_store_result(mysql);

    // 从结果集中获取下一行存入到map中
    while (MYSQL_ROW row = mysql_fetch_row(mysql_result)) {
        users_info[std::string(row[0])] = std::string(row[1]);
    }
}

void HttpCon::close_connection() {
    if (sockfd != -1) {
        utils->del_fd(epollfd, sockfd);
        sockfd = -1;
        --connection_count;
    }
}

bool HttpCon::add_response(const char *format, ...) {
    if (write_index >= WRITE_BUFFER_SIZE) return false;

    va_list arg_list;
    va_start(arg_list, format);

    int len = vsnprintf(write_buf + write_index, WRITE_BUFFER_SIZE- write_index - 1, format, arg_list);
    if (len < 0 || len > WRITE_BUFFER_SIZE) {
        va_end(arg_list);
        return false;
    }

    write_index += len;
    va_end(arg_list);

    LOG_INFO("request:%s", write_buf);
    return true;
}

bool HttpCon::add_status_line(int status, const char *title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool HttpCon::add_headers(int conten_len) {
    return add_content_length(conten_len) && add_content_type() && add_linger() && add_blank_line();
}
bool HttpCon::add_content_length(int content_len) {
    return add_response("Content-Length:%d\r\n", content_len);
}
bool HttpCon::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool HttpCon::add_linger() {
    return add_response("Connection:%s\r\n", request_keep_alive ? "keep-alive" : "close");
}
bool HttpCon::add_blank_line() {
    return add_response("%s", "\r\n");
}
bool HttpCon::add_content(const char *content) {
    return add_response("%s", content);
}

void HttpCon::init(int sockfd, sockaddr_in &addr, char *root, trigger_mode mode, std::string db_user, std::string db_pwd,
                   std::string db_name, bool open_log) {
    this->sockfd = sockfd;
    client_address = addr;
    site_root_path = root;

    utils->register_read(epollfd, sockfd, true, mode);

    ++connection_count;

    site_root_path = root;
    t_mode = mode;

    this->open_log = open_log;

    this->db_username = db_user;
    this->db_name = db_name;
    this->db_pwd = db_pwd;

    init();

}

// 初始化新接受的连接
void HttpCon::init() {

    sql_connection = nullptr;
    bytes_to_send = 0;
    bytes_have_sent = 0;

    check_state = CHECK_STATE_REQUESTLINE;

    request_keep_alive = false;
    request_method = GET;
    request_content_length = 0;
    start_index = 0;
    checked_index = 0;
    read_index = 0;
    write_index = 0;
    operator_type = READ;

    timer_flag = false;

    /*free_char(request_url);
    free_char(protocol_version);
    free_char(request_host);*/

    request_url = nullptr;
    protocol_version = nullptr;
    request_host = nullptr;

    memset(read_buf, '\0', READ_BUFFER_SIZE);
    memset(write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(request_file_path, '\0', FILENAME_LEN);

}
// 发送数据
bool HttpCon::write() {
    int bytes_sent = 0;

    if (bytes_to_send == 0) {
        utils->reset_oneshot(epollfd, sockfd, EPOLLIN, t_mode);
        init();
        return true;
    }

    while (true) {
        bytes_sent = writev(sockfd, response_content, response_iv_count);

        if  (bytes_sent < 0) {
            if (errno == EAGAIN) { // 非阻塞io，提示再进行一次读取
                utils->reset_oneshot(epollfd, sockfd, EPOLLOUT, t_mode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_sent += bytes_sent;
        bytes_to_send -= bytes_sent;

        if (bytes_have_sent >= response_content[0].iov_len) { // 发送完write_buf的内容
            response_content[0].iov_len = 0;
            response_content[1].iov_base = request_file_address + (bytes_have_sent - write_index);
            response_content[1].iov_len = bytes_to_send;
        } else {
            response_content[0].iov_len -= bytes_have_sent;
            response_content[0].iov_base = write_buf + bytes_have_sent;
        }

        // 发送完所有内容
        if (bytes_to_send <= 0) {
            unmap();

            utils->reset_oneshot(epollfd, sockfd, EPOLLIN, t_mode);

            if (request_keep_alive == true) {
                init();
                return true;
            } else return false;
        }
    }
}
// 读取数据
bool HttpCon::read_once() {
    if (read_index >= READ_BUFFER_SIZE) return false;

    int bytes_read = 0;

    // LT模式读取
    if (t_mode == LT_MODE) {
        bytes_read = recv(sockfd, read_index + read_buf, READ_BUFFER_SIZE - read_index, 0);

        if (bytes_read <= 0) return false;

        read_index += bytes_read;

        return true;
    } else { // ET模式读取
        while (true) {
            bytes_read = recv(sockfd, read_buf + read_index, READ_BUFFER_SIZE - read_index, 0);

            if (bytes_read == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                return false;
            } else if (bytes_read == 0) return false; // 客户端关闭了连接

            read_index += bytes_read;
        }
        return true;
    }
}

/* 从状态机，用于分析一行的内容 */
LINE_STATUS HttpCon::parse_line() {
    char temp;
    for (; checked_index<read_index; ++checked_index) {
        temp = read_buf[checked_index];

        if (temp == '\r') {
            if (checked_index + 1 == read_index) return LINE_OPEN;
            else if (read_buf[checked_index + 1] == '\n'){
                read_buf[checked_index++] = '\0';
                /* 怀疑有bug */
                read_buf[checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (checked_index > 1 && read_buf[checked_index - 1] == '\r') {
                read_buf[checked_index++] = '\0';
                read_buf[checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}
/* 解析请求行 */
HTTP_CODE HttpCon::parse_request_line(char *request_line) {
    // 检索字符串 str1 中第一个匹配字符串 str2 中字符的字符, 返回第一个匹配的字符指针
    request_url = strpbrk(request_line, " \t");

    if (request_url == nullptr) return BAD_REQUEST;

    *request_url++ = '\0'; // 截断请求方法-->url

    /* 分析请求方法 */
    char* method = request_line;
    if (strcasecmp(method, "GET") == 0) request_method = GET;
    else if (strcasecmp(method, "POST") == 0) request_method = POST;
    else return BAD_REQUEST;

    // 返回str1中第一个不在str2中出现的字符下标
    request_url += strspn(request_url, " \t"); // 清除多余" \t"-->url
    protocol_version = strpbrk(request_url, " \t");
    if (protocol_version == nullptr) return BAD_REQUEST;

    *protocol_version++ = '\0'; // 截断请求url-->version
    protocol_version += strspn(protocol_version, " \t");

    if (strcasecmp(protocol_version, "HTTP/1.1") != 0) return BAD_REQUEST;

    if (strncasecmp(request_url, "http://", 7) == 0) {
        request_url += 7;
        request_url = strchr(request_url, '/');
    }

    if (strncasecmp(request_url, "https://", 8) == 0) {
        request_url += 8;
        request_url = strchr(request_url, '/');
    }

    if (request_url == nullptr || request_url[0] != '/') return BAD_REQUEST;
    // 当url为/时，显示判断界面
    /*if (strlen(request_url) == 1) {
        strcat(request_url, "judge.html");
    }*/

    check_state = CHECK_STATE_HEADER;
    return NO_REUEST;
}
/* 解析请求头 */
HTTP_CODE HttpCon::parse_headers(char *headers) {
    if (headers[0] == '\0') {
        if (request_content_length != 0) {
            check_state = CHECK_STATE_CONTENT;
            return NO_REUEST;
        }
        return GET_REQUEST;
    } else if (strncasecmp(headers, "Connection:", 11) == 0) {
        headers += 11;
        headers += strspn(headers, " \t");
        if (strcasecmp(headers, "keep-alive") == 0) {
            request_keep_alive = true;
        }
    } else if (strncasecmp(headers, "Content-length:", 15) == 0) {
        headers += 15;
        headers += strspn(headers, " \t");

        request_content_length = atol(headers);
    }  else if (strncasecmp(headers, "Host:", 5) == 0) {
        headers += 5;
        headers += strspn(headers, " \t");
        request_host = headers;
    } else {
        //LOG_INFO("oop!unknow header: %s", headers);
    }
    return NO_REUEST;
}
/* 解析请求内容 */
HTTP_CODE HttpCon::parse_content(char *content) {
    // 判断请求是否完整读入
    if (read_index >= request_content_length + checked_index) {
        content[request_content_length] = '\0';
        // POST请求中的用户名和密码
        request_content = content;
        return GET_REQUEST;
    }
    return NO_REUEST;
}

HTTP_CODE HttpCon::do_request() {
    //
    memset(request_file_path, '\0', FILENAME_LEN);
    strcpy(request_file_path, site_root_path); // request_file_path = /home/utt/cpps/webserver/root
    int site_root_len = strlen(site_root_path); // 29

    const char* request_path = strrchr(request_url, '/'); // /judge.html


    char* request_file_name = (char*) malloc(sizeof(char) *  200);
    memset(request_file_name, '\0', sizeof (char) * 200);

    if (strlen(request_path) == 1 && request_path[0] == '/') {
        strcpy(request_file_name, "/judge.html");
    }
    else if (request_method == POST && (request_path[1] == '2' || request_path[1] == '3')) {

        // 根据标志判断是登录检测还是注册检测
        //strcpy(request_file_name, "/");
        //strcat(request_file_name, request_url + 2); // 请求格式：https：//127.0.0.1/2login.html
        //strncpy(request_file_path + site_root_len, request_file_name, FILENAME_LEN - site_root_len - 1);

        // 提取用户名和密码 user=123&password=123
        char name[100], password[100];
        int i;
        for (i=5; request_content[i] != '&'; ++i) {
            name[i - 5] = request_content[i];
        }
        name[i - 5] = '\0';

        int j=0;
        for (i=(i + 10); request_content[i] != '\0'; ++i)
            password[j++] = request_content[i];
        password[j] = '\0';

        // 注册
        if (request_path[1] == '3') {
            char* sql_insert = (char*) malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "','");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            // 未注册
            if (users_info.find(name) == users_info.end()) {
                sql_lock.lock();
                int sql_res = mysql_query(sql_connection, sql_insert);
                if (sql_res == 0) users_info.insert({name, password});
                sql_lock.unlock();
                free(sql_insert);

                if (sql_res == 0) {
                    // strcpy(response_url, "/log.html");
                    strcpy(request_file_name, "/log.html");
                } else {
                    // strcpy(response_url, "/registerError/html");
                    strcpy(request_file_name, "/registerError.html");
                }
            }
            else {
                // strcpy(response_url, "/registerError.html");
                strcpy(request_file_name, "/registerError.html");
            }
        }
        // 登录
        if (request_path[1] == '2') {
            if (users_info.find(name) != users_info.end() && users_info[name] == password) {
                strcpy(request_file_name, "/welcome.html");
            }
            else {
                strcpy(request_file_name, "/logError.html");
            }
        }
        //strncpy(request_file_path + site_root_len, request_file_name, strlen(request_file_name));
    }
    else {
        switch (request_path[1]) {
            case '0':
            {
                strcpy(request_file_name, "/register.html");
                //strncpy(request_file_path + site_root_len, request_file_name, strlen(request_file_name));
                break;
            }
            case '1':
            {
                strcpy(request_file_name, "/log.html");
                //strncpy(request_file_path + site_root_len, request_file_name, strlen(request_file_name));
                break;
            }
            case '5':
            {
                strcpy(request_file_name, "/picture.html");
                //strncpy(request_file_path + site_root_len, request_file_name, strlen(request_file_name));
                break;
            }
            case '6':
            {
                strcpy(request_file_name, "/video.html");
                //strncpy(request_file_path + site_root_len, request_file_name, strlen(request_file_name));
                break;
            }
            case '7':
            {
                strcpy(request_file_name, "/fans.html");
                //strncpy(request_file_path + site_root_len, request_file_name, strlen(request_file_name));
                break;
            }
            default:
                // /home/utt/cpps/webserver/root/judge.html
                //strncpy(request_file_path + site_root_len, request_path, FILENAME_LEN - site_root_len - 1);
                strcpy(request_file_name, request_path);
                break;
        }
    }
    strncpy(request_file_path + site_root_len, request_file_name, FILENAME_LEN - site_root_len - 1);
    free(request_file_name);

    if (stat(request_file_path, &request_file_stat) == -1) return NO_RESOURCE;

    if (!(request_file_stat.st_mode & S_IROTH)) return FORBIDDEN_REQUEST;

    if (S_ISDIR(request_file_stat.st_mode)) return BAD_REQUEST;

    int request_file_fd = open(request_file_path, O_RDONLY);

    request_file_address = (char*)mmap(nullptr, request_file_stat.st_size, PROT_READ, MAP_PRIVATE, request_file_fd, 0);
    close(request_file_fd);

    return FILE_REQUEST;

}

void HttpCon::process() {
    HTTP_CODE read_ret = process_read();

    if (read_ret == NO_REUEST) {
        utils->reset_oneshot(epollfd, sockfd, EPOLLIN, t_mode);
        return;
    }

    bool write_ret = process_write(read_ret);

    if (!write_ret) {
        close_connection();
    }
    utils->reset_oneshot(epollfd, sockfd, EPOLLOUT, t_mode);
}

HTTP_CODE HttpCon::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REUEST;

    char* text = nullptr;

    while ((line_status ==LINE_OK && check_state == CHECK_STATE_CONTENT) || (line_status = parse_line()) == LINE_OK) {
        text = getline();
        start_index = checked_index;

        //LOG_INFO("%s", text);

        switch (check_state) {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) return BAD_REQUEST;
                if (ret == GET_REQUEST) return do_request();
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if (ret == GET_REQUEST) return do_request();
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REUEST;
}

bool HttpCon::process_write(HTTP_CODE read_ret) {
    switch (read_ret) {
        case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (add_content(error_500_form) == false) return false;
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (add_content(error_404_form) == false) return false;
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (add_content(error_403_form) == false) return false;
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            if (request_file_stat.st_size != 0) {
                add_headers(request_file_stat.st_size);
                response_content[0].iov_base = write_buf;
                response_content[0].iov_len = write_index;

                response_content[1].iov_base = request_file_address;
                response_content[1].iov_len = request_file_stat.st_size;

                response_iv_count = 2;
                bytes_to_send = write_index + request_file_stat.st_size;
                return true;
            } else {
                add_headers(strlen(ok_200_form));
                if (add_content(ok_200_form) == false) return false;
            }
        }
        default:
            return false;
    }

    response_content[0].iov_base = write_buf;
    response_content[0].iov_len = write_index;

    response_iv_count = 1;
    bytes_to_send = write_index;

    return true;
}