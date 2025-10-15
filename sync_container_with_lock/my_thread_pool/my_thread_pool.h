//
// Created by Charles Green on 6/18/25.
//

#ifndef MY_THREAD_POOL_H
#define MY_THREAD_POOL_H
#include <condition_variable>
#include <functional>
#include <queue>

class MyThreadPool {
public:
    MyThreadPool(int pool_size);
    ~MyThreadPool();
    MyThreadPool(const MyThreadPool&) = delete;
    MyThreadPool& operator=(const MyThreadPool&) = delete;

    void submit(std::function<void()>);
    void stopAll();
    void syncStop(int);
    size_t size() const;
private:
    struct MyWorker {
        int id_;
        MyWorker(int id, MyThreadPool* owner): id_(id), belongs_to_(owner), task_(nullptr), stop_flag(false) {}
        // MyWorker(const MyWorker& mw): id_(mw.id_), belongs_to_(mw.belongs_to_), task_(mw.task_), stop_flag(mw.stop_flag){}
        MyWorker(const MyWorker&) = delete;
        MyWorker& operator=(const MyWorker&) = delete;

        // optionally delete move as well if you want to be strict
        MyWorker(MyWorker&&) = delete;
        MyWorker& operator=(MyWorker&&) = delete;
        void work();
        void acceptTask(std::function<void()>);
        void syncStop();
    private:
        MyThreadPool* belongs_to_;
        std::function<void()> task_;
        std::condition_variable cv_;
        std::mutex worker_mtx_;
        bool stop_flag;
    };
    void report_finish(int);
    const int POOL_SIZE_;
    std::condition_variable cv_;
    std::mutex mtx_;
    std::queue<int> available_workers_;
    std::vector<std::unique_ptr<MyWorker>> workers_;
    bool stopped_;
};





#endif //MY_THREAD_POOL_H
