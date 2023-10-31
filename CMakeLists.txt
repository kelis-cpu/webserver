
project(webserver)

set(CMAKE_CXX_FLAGS "-pthread")

# 取消编译优化
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0")

# 生成gdb调试信息
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")




add_executable(webserver webserver.cpp webserver.h main.cpp threadpool/threadpool.h http_conn.cpp http_conn.h lock/lock.h sqlpool/sqlpool.cpp sqlpool/sqlpool.h timer/tw_timer.cpp timer/tw_timer.h utils/utils.cpp utils/utils.h config/config.cpp config/config.h log/block_queue.h log/log.cpp log/log.h)

set(EXECUTABLE_OUTPUT_PATH  ${PROJECT_SOURCE_DIR})
target_link_libraries(webserver mysqlclient)
