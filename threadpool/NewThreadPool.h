/*
 * @Author: kelise
 * @Date: 2023-10-28 22:55:09
 * @LastEditors: kelis-cpu
 * @LastEditTime: 2023-10-29 03:41:46
 * @Description: file content
 */
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

template <typename T>
class TaskQueue {
private:
    std::mutex mu;
    std::queue<T> tasks;

public:
    TaskQueue();
    ~TaskQueue();

    bool empty() {
        std::unique_lock<std::mutex> lock(mu);
        return tasks.empty();
    }
    int size() {
        std::unique_lock<std::mutex> lock(mu);
        return tasks.size();
    }
    void enqueue(T &t) {
        std::unique_lock<std::mutex> lock(mu);
        tasks.emplace(t);
    }
    bool dequeue(T &t) {
        std::unique_lock<std::mutex> lock(mu);
        if (tasks.empty()) return false;
        t = std::move(tasks.front());
        tasks.pop();
        return true;
    }
};

class NewThreadPool {
private:
    std::vector<std::thread> threads;            // 工作线程队列
    TaskQueue<std::function<void()>> taskQueue;  // 任务队列

    std::condition_variable cond;  // 条件变量，用于唤醒工作线程
    std::mutex cond_mutex;         // 线程休眠互斥锁

    bool isShutdown;  // 线程池是否停止工作
private:
    // 线程工作类
    class Worker {
    private:
        NewThreadPool *pool_;  // 所属线程池
        int theadId_;

    public:
        Worker(NewThreadPool *pool, int theadId)
            : pool_(pool), theadId_(theadId) {}
        // 重载()
        void operator()() {
            std::function<void()> func;  // 任务队列
            bool dequeue;                // 任务出队是否成功
            while (!pool_->isShutdown) {
                std::unique_lock<std::mutex> lock(pool_->cond_mutex);
                pool_->cond.wait(lock,
                                 [=]() { return !pool_->taskQueue.empty(); });

                dequeue = pool_->taskQueue.dequeue(func);
                if (dequeue) func();  // 出队成功，执行任务
            }
        }
    };

public:
    NewThreadPool(int threadNums = 4)
        : isShutdown(false), threads(std::vector<std::thread>(threadNums)) {}
    ~NewThreadPool();
    void init();
    void shutdown();
    // 提交任务函数
    template <typename F, typename... Args>
    auto submit(F &&f, Args &&...args) -> std::future<decltype(f(args...))>;
};
void NewThreadPool::init() {
    for (int i = 0; i < threads.size(); i++) {
        threads.at(i) = std::thread(Worker(this, i));
    }
}
void NewThreadPool::shutdown() {
    isShutdown = true;
    cond.notify_all();  // 唤醒所有等待线程
    for (auto &th : threads) {
        if (th.joinable()) th.join();
    }
}
template <typename F, typename... Args>
auto NewThreadPool::submit(F &&f, Args &&...args)
    -> std::future<decltype(f(args...))> {
    std::function<decltype(f(args...))> func =
        std::bind(std::forward<F>(f), std::forward<Args>(args)...);

    // 打包任务
    auto task =
        std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);

    // 封装task
    std::function<void()> t = [task]() { (*task)(); };
    // 任务入队
    taskQueue.enqueue(t);
    // 唤醒一个等待线程
    cond.notify_one();

    return task->get_future();
}
