//
// Created by Charles Green on 7/16/25.
//

#ifndef MY_INTERRUPTIBLE_THREAD_H
#define MY_INTERRUPTIBLE_THREAD_H
#include <thread>


struct InterruptFlag {
    std::atomic<bool> state_;
    InterruptFlag(): state_(false) {}
    bool is_set() const {
        return state_.load();
    }
    void set() {
        state_.store(true);
    }
};
thread_local InterruptFlag per_thread_flag;

/**
 * Why is this function global?
 * Because we hope to offer an interface that can be called directly in a client's function.
 *
 */
void RegisterInterruptPoint() {
    if(per_thread_flag.is_set()) {
        throw std::runtime_error("the thread has been interrupted");
    }
}

class InterruptibleThread {
    InterruptFlag* intr_flg_;
    std::thread thd_;
    template <typename Callable>
    InterruptibleThread(Callable&& clb) {
        std::promise<void> synchronizer;
        thd_ = std::thread([&clb, &synchronizer, this]() {
            intr_flg_ = &per_thread_flag;
            synchronizer.set_value();
            clb();
        });
        synchronizer.get_future().wait();
    }

    InterruptibleThread(const InterruptibleThread&) = delete;
    InterruptibleThread& operator=(const InterruptibleThread&) = delete;
    InterruptibleThread(InterruptibleThread&& another): thd_(std::move(another.thd_)), intr_flg_(another.intr_flg_){
        another.intr_flg_ = nullptr;
    }
    InterruptibleThread& operator=(InterruptibleThread&& another) {
        if(this == &another) {
            return *this;
        }
        thd_ = std::move(another.thd_);
        intr_flg_ = another.intr_flg_;
        another.intr_flg_ = nullptr;
        return *this;
    }

    ~InterruptibleThread() {
        if(thd_.joinable()) {
            thd_.join();
        }
    }

    void join() {
        thd_.join();
    }

    void detach() {
        thd_.detach();
    }

    bool joinable() {
        return thd_.joinable();
    }

    void interrupt() {
        if(intr_flg_) {
            intr_flg_->set();
        }
    }
};


#endif //MY_INTERRUPTIBLE_THREAD_H
