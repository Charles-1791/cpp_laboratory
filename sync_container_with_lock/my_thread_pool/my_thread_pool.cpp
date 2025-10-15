//
// Created by Charles Green on 6/18/25.
//

#include "my_thread_pool.h"

#include <assert.h>
#include <functional>
#include <mutex>
#include <thread>

MyThreadPool::MyThreadPool(int pool_size): POOL_SIZE_(pool_size) {
    workers_.reserve(pool_size);
    for(int i=0;i<pool_size; ++i) {
        workers_.emplace_back(std::make_unique<MyWorker>(i, this));
        std::thread t([](int id, MyThreadPool* parent){parent->workers_[id]->work();}, i, this);
        t.detach();
        available_workers_.push(i);
    }
}

MyThreadPool::~MyThreadPool() {
    stopAll();
}

void MyThreadPool::submit(std::function<void()> task) {
    // check if these is an available worker
    int worker_id = -1;
    {
        std::unique_lock<std::mutex> lock(mtx_);
        // wait until we have one
        cv_.wait(lock, [this]() {
            return stopped_ || !available_workers_.empty();
        });
        if(stopped_) {
            throw std::runtime_error("thead pool has been closed");
        }
        worker_id = available_workers_.front();
        available_workers_.pop();
    }
    workers_[worker_id]->acceptTask(task);
}

void MyThreadPool::syncStop(int id) {
    workers_[id]->syncStop();
}

void MyThreadPool::stopAll() {
    {
        // stop the service and wake up all working thread
        std::lock_guard<std::mutex> lk(mtx_);
        if(stopped_)
            // cannot stop twice
            return;
        stopped_ = true;
        cv_.notify_all();
    }
    for(int i=0;i<POOL_SIZE_;++i) {
        // stop all
        std::thread stop_i_thd([](int id, MyThreadPool* pool) {
            pool->workers_[id]->syncStop();
        }, i, this);
        stop_i_thd.detach();
    }
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this]() {
        return available_workers_.size() == POOL_SIZE_;
    });
    while(!available_workers_.empty()) {
        // clear the workers
        available_workers_.pop();
    }
}

void MyThreadPool::report_finish(int id) {
    {
       std::lock_guard<std::mutex> lock(mtx_);
        available_workers_.push(id);
    }
    cv_.notify_one();
}

size_t MyThreadPool::size() const {
    return POOL_SIZE_;
}

void MyThreadPool::MyWorker::work() {
    while(true) {
        {
            std::unique_lock<std::mutex> lock(worker_mtx_);
            cv_.wait(lock, [this]() {return task_ != nullptr || stop_flag;});
            // should i stop?
            if(stop_flag) {
                break;
            }
        }
        // execute the task after free lock
        task_();
        {
            std::lock_guard<std::mutex> lock(worker_mtx_);
            // notify the thread pool
            belongs_to_->report_finish(id_);
            // reset the task
            task_ = nullptr;
        }
    }
    // can only reach here when stop_flag is true, and now, syncStop must be waiting for task_==nullptr
    cv_.notify_one();
}

void MyThreadPool::MyWorker::acceptTask(std::function<void()> task) {
    {
        // Do i really need to acquired the lock here? I think this lock is already obtainable
        std::lock_guard<std::mutex> lock(worker_mtx_);
        assert(task_ == nullptr);
        task_ = task;
    }
    cv_.notify_one();
}

void MyThreadPool::MyWorker::syncStop() {
    {
        std::lock_guard<std::mutex> lock(worker_mtx_);
        stop_flag = true;
    }
    cv_.notify_one();
    {
        std::unique_lock<std::mutex> lock(worker_mtx_);
        cv_.wait(lock, [this]() {
            return task_ == nullptr;
        });
    }
}

