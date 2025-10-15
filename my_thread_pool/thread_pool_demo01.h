//
// Created by Charles Green on 7/13/25.
//

#ifndef THREAD_POOL_DEMO01_H
#define THREAD_POOL_DEMO01_H
#include "../sync_container_with_lock/my_sync_queue/my_sync_queue.h"
#include "../my_utility/my_defer.h"
#include <vector>
#include <deque>
#include <mutex>

class TaskWrapper {
    struct TaskWrapperInterface {
        virtual void operator()() = 0;
        virtual ~TaskWrapperInterface() = default;
    };
    template <typename Callable>
    struct TaskWrapperImpl: public TaskWrapperInterface {
    private:
        Callable func_;
    public:
        template<typename U>
        TaskWrapperImpl(U&& func): func_(std::forward<U>(func)) {}
        void operator()() override {
            func_();
        }
    };
    std::unique_ptr<TaskWrapperInterface> func_;
public:
    template <typename Callable>
    TaskWrapper(Callable&& func): func_(std::make_unique<TaskWrapperImpl<std::decay_t<Callable>>>(std::forward<Callable>(func))) {}
    void operator()() const {
        (*func_)();
    }
    TaskWrapper(const TaskWrapper&) = delete;
    TaskWrapper(TaskWrapper&& wrapper): func_(std::move(wrapper.func_)) {}
    TaskWrapper& operator=(const TaskWrapper&) = delete;
    TaskWrapper& operator=(TaskWrapper&& wrapper) {
        if(this == &wrapper) {
            return *this;
        }
        func_ = std::move(wrapper.func_);
        return *this;
    }
};

template <typename T>
class NaiveStealingQueue {
    std::deque<std::shared_ptr<T>> queue_;
    std::mutex mtx_;
public:
    NaiveStealingQueue() = default;
    ~NaiveStealingQueue() = default;
    NaiveStealingQueue(const NaiveStealingQueue&) = delete;
    NaiveStealingQueue& operator=(const NaiveStealingQueue&) = delete;
    NaiveStealingQueue(NaiveStealingQueue&&) = delete;
    NaiveStealingQueue& operator=(NaiveStealingQueue&&) = delete;

    template <typename U>
    void push(U&& ele) {
        std::lock_guard<std::mutex> guard(mtx_);
        queue_.emplace_front(std::make_shared<T>(std::forward<U>(ele)));
    }

    std::shared_ptr<T> tryPop() {
        std::lock_guard<std::mutex> guard(mtx_);
        if(queue_.empty()) {
            return nullptr;
        }
        std::shared_ptr<T> ret = queue_.front();
        queue_.pop_front();
        return ret;
    }

    std::shared_ptr<T> trySteal() {
        std::lock_guard<std::mutex> guard(mtx_);
        if(queue_.empty()) {
            return nullptr;
        }
        std::shared_ptr<T> ret = queue_.back();
        queue_.pop_back();
        return ret;
    }
};

class ThreadPoolDemo01 {
    struct ThreadGuardian {
    private:
        std::vector<std::thread>& threads_;
    public:
        ThreadGuardian(std::vector<std::thread>& threads): threads_(threads) {}
        ~ThreadGuardian() {
            for(int i=0;i<threads_.size(); ++i) {
                if(threads_[i].joinable()) {
                    threads_[i].join();
                }
            }

        }
    };
    // MySyncQueue<TaskWrapper> sync_queue_;
    std::atomic<bool> stop_;
    const int THREAD_NUM_;
    std::vector<std::thread> threads;
    // must declare after threads
    ThreadGuardian thd_guardian_;
    std::vector<std::unique_ptr<NaiveStealingQueue<TaskWrapper>>> all_queues_;

    static thread_local NaiveStealingQueue<TaskWrapper>* per_thread_queue_;
    static thread_local int thread_id;

    void worker_thread(int id) {
        thread_id = id;
        per_thread_queue_ = all_queues_[id].get();
        while(!stop_) {
            // TODO: add conditional variable to naive stealing queue to avoid busy waiting
            run_pending_task();
        }
    }

    // round robin
    std::atomic<size_t> responsible_thread_id_ = 0;

    size_t get_resposible_thread_id() {
        size_t current = responsible_thread_id_.load(std::memory_order_relaxed);
        while(!responsible_thread_id_.compare_exchange_weak(current, (current + 1) % THREAD_NUM_, std::memory_order_relaxed));
        return current;
    }
public:
    ThreadPoolDemo01(): THREAD_NUM_(std::thread::hardware_concurrency()), thd_guardian_(threads) {
        all_queues_.reserve(THREAD_NUM_);
        for(int i=0; i<THREAD_NUM_; ++i) {
            all_queues_.push_back(std::make_unique<NaiveStealingQueue<TaskWrapper>>());
        }
        threads.reserve(THREAD_NUM_);
        for(int i=0; i<THREAD_NUM_; ++i) {
            threads.emplace_back(&ThreadPoolDemo01::worker_thread, this, i);
        }
    }
    ~ThreadPoolDemo01() {
        // sync_queue_.close();
        stop_.store(true);
    }

    template <typename Callable>
    std::future<typename std::result_of<Callable()>::type> submit(Callable&& clb) {
        using ret_type = typename std::result_of<Callable()>::type;
        std::packaged_task<ret_type()> pkgd_tsk(std::forward<Callable>(clb));
        std::future<ret_type> ft = pkgd_tsk.get_future();
        // since a packaged_task stores exception internally, if we guarantee to call it only once, it's unlikely to throw
        // packaged_task is not copyable but moveable
        TaskWrapper task(std::move(pkgd_tsk));
        size_t id = get_resposible_thread_id();
        all_queues_[id]->push(std::move(task));
        return ft;
    }

    void run_pending_task() {
        std::shared_ptr<TaskWrapper> task;
        if(per_thread_queue_) {
            // try to get a task from its own queue
            if((task = per_thread_queue_->tryPop())) {
                (*task)();
                return;
            }
        }
        // steal one
        int begin_idx = (thread_id + 1) % THREAD_NUM_;
        int end_idx = thread_id;
        if(thread_id == -1) {
            // an outside thread
            begin_idx = 0;
            end_idx = THREAD_NUM_;
        }
        for(int offset = begin_idx; offset != end_idx; offset = (offset + 1) % THREAD_NUM_) {
            if((task = all_queues_[offset]->tryPop())) {
                (*task)();
                return;
            }
        }
        // nothing to do
        std::this_thread::yield();
    }
};

// Provide definitions for static thread_local members
thread_local NaiveStealingQueue<TaskWrapper>* ThreadPoolDemo01::per_thread_queue_ = nullptr;
thread_local int ThreadPoolDemo01::thread_id = -1;
#endif //THREAD_POOL_DEMO01_H
