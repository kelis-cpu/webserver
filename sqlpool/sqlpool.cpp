//
// Created by utt on 3/18/23.
//

#include <iostream>
#include "sqlpool.h"
#include "../log/log.h"

connectionRAII::connectionRAII(MYSQL **con, SqlPool *connPool) {
    *con = connPool->GetConnection();

    conRAII = *con;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII() {
    poolRAII->ReleaseConnection(conRAII);
}

SqlPool::SqlPool() :CurConn(0), FreeConn(0){}

SqlPool::~SqlPool() {
    DestoryPool();
}

int SqlPool::GetFreeConn() {
    return this->FreeConn;
}

void SqlPool::DestoryPool() {
    mutex.lock();

    for (auto & it : connList) {
        mysql_close(it);
    }
    CurConn = 0;
    FreeConn = 0;
    connList.clear();
    mutex.unlock();
}

MYSQL* SqlPool::GetConnection() {
    MYSQL* conn = nullptr;

    if (connList.empty()) return nullptr;

    sem.wait();

    mutex.lock();

    conn = connList.front();
    connList.pop_front();

    --FreeConn;
    ++CurConn;

    mutex.unlock();

    return conn;
}


bool SqlPool::ReleaseConnection(MYSQL *conn) {
    if (conn == nullptr) return false;

    mutex.lock();

    connList.push_back(conn);

    ++FreeConn;
    --CurConn;

    mutex.unlock();

    sem.post();

    return true;
}

SqlPool* SqlPool::GetInstance() {
    /*
     * best of all：函数内local static实现单例模式
     * */
    static SqlPool connPool;
    return &connPool;
}

void SqlPool::init(std::string url, std::string user, std::string pwd, std::string DataBaseName, int port,
                   int MaxConn, bool open_log) {
    this->url = url;
    this->user = user;
    this->DataBaseName = DataBaseName;
    this->pwd = pwd;
    this->port = port;

    this->open_log = open_log;

    for (int i = 0; i < MaxConn; ++i) {
        MYSQL* conn = nullptr;

        conn = mysql_init(conn);

        if (conn == nullptr) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }

        // conn = mysql_real_connect(conn, url.c_str(), user.c_str(), pwd.c_str(), DataBaseName.c_str(),
         //                         port, nullptr, 0);

        if ( mysql_real_connect(conn, url.c_str(), user.c_str(), pwd.c_str(), DataBaseName.c_str(),
                                port, nullptr, 0) == nullptr) {
            LOG_ERROR("MySQL Error");
            std::cout << mysql_error(conn);
            exit(1);
        }

        connList.push_back(conn);
        ++FreeConn;
    }
    sem = Sem(FreeConn);
    MaxConn = FreeConn;
}