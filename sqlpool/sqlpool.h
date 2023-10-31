//
// Created by utt on 3/18/23.
//

#ifndef WEBSERVER_SQLPOOL_H
#define WEBSERVER_SQLPOOL_H

#include <mysql/mysql.h>
#include <list>
#include "../lock/lock.h"

class SqlPool {
public:
    /* 获取数据库连接 */
    MYSQL* GetConnection();
    /* 释放连接 */
    bool ReleaseConnection(MYSQL* conn);
    /* 获取连接 */
    int GetFreeConn();
    /* 销毁所有连接 */
    void DestoryPool();

    /* 单例模式 */
    static SqlPool* GetInstance();

    void init(std::string url, std::string user, std::string pwd,
              std::string DataBaseName, int port, int MaxConn, bool open_log);

private:
    SqlPool();
    ~SqlPool();

    /* 最大连接数 */
    int Max_Conn;
    /* 当前已使用连接数 */
    int CurConn;
    /* 当前空闲的连接数 */
    int FreeConn;

    Mutex mutex;
    Sem sem;

    std::list<MYSQL*> connList;
public:
    /* 主机地址 */
    std::string url;
    /* 登录数据库用户名 */
    std::string user;
    /* 数据库名 */
    std::string DataBaseName;
    std::string pwd;
    int port;
    bool open_log; // 日志开关
};

class connectionRAII {
public:
    connectionRAII(MYSQL** con, SqlPool *connPool);
    ~connectionRAII();
private:
    MYSQL* conRAII;
    SqlPool* poolRAII;
};
#endif //WEBSERVER_SQLPOOL_H
