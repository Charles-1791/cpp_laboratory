//
// Created by Charles Green on 6/28/25.
//

#ifndef MY_SYNC_QUEU_H
#define MY_SYNC_QUEU_H

#include <mutex>
#include <condition_variable>

template <typename T>
class MySyncQueue {
    struct QueueNode {
        std::shared_ptr<T> data_;
        std::unique_ptr<QueueNode> next_;
        QueueNode() = default;
        template <typename U>
        QueueNode(U&& data) : data_(std::make_shared<T>(std::forward<U>(data))), next_(nullptr) {}
    };
public:
    MySyncQueue(): front_(new QueueNode()), rear_(front_.get()) {}
    template <typename U>
    void push(U&& data);
    bool tryPop(std::shared_ptr<T>& sptr);
    std::shared_ptr<T> pop();
    bool pop(T&);
    void close();
private:
    std::unique_ptr<QueueNode> front_;
    QueueNode* rear_;
    std::mutex front_lock_;
    std::mutex rear_lock_;
    std::condition_variable cv_;
    std::atomic<bool> closed_;
};

template<typename T>
template<typename U>
void MySyncQueue<T>::push(U &&data) {
    // suppose you use raw pointer, it's possible that the first allocation succeeds while the second fails and throws,
    // you will left the pointer undeleted!
    std::shared_ptr<T> sptr = std::make_shared<T>(std::forward<U>(data));
    std::unique_ptr<QueueNode> uptr = std::make_unique<QueueNode>();
    // minimize the critical region
    {
        std::lock_guard<std::mutex> grd(rear_lock_);
        rear_->data_ = sptr;
        rear_->next_ = std::move(uptr);
        rear_ = rear_->next_.get();
    }
    cv_.notify_one();
}

template<typename T>
bool MySyncQueue<T>::tryPop(std::shared_ptr<T>& sptr) {
    std::unique_ptr<QueueNode> defer;
    {
        std::lock_guard<std::mutex> front_uni_lock(front_lock_);
        {
            std::lock_guard<std::mutex> rear_guard(rear_lock_);
            if(front_.get() == rear_) {
                return false;
            }
        }
        sptr = front_->data_;
        defer = std::move(front_);
        front_ = std::move(defer->next_);
    }
    return true;
}

template<typename T>
std::shared_ptr<T> MySyncQueue<T>::pop() {
    std::unique_ptr<QueueNode> defer;
    std::shared_ptr<T> data;
    {
        std::unique_lock<std::mutex> lock(front_lock_);
        cv_.wait(lock, [this]() {
            if(closed_) {
                return true;
            }
            std::lock_guard<std::mutex> rlock(rear_lock_);
            return front_.get() != rear_;
        });
        if(closed_) {
            return nullptr;
        }
        data = front_->data_;
        defer = std::move(front_);
        front_ = std::move(defer->next_);
    }
    return data;
}

template<typename T>
bool MySyncQueue<T>::pop(T& placeholder) {
    std::unique_ptr<QueueNode> defer;
    {
        std::unique_lock<std::mutex> lock(front_lock_);
        cv_.wait(lock, [this]() {
            if(closed_) {
                return true;
            }
            std::lock_guard<std::mutex> rlock(rear_lock_);
            return front_.get() != rear_;
        });
        if(closed_) {
            return false;
        }
        // if this move throws, front_->data_ is damaged
        placeholder = std::move(*(front_->data_));
        // noexcept
        defer = std::move(front_);
        // noexcept
        front_ = std::move(defer->next_);
    }
    return true;
}

template<typename T>
void MySyncQueue<T>::close() {
    closed_ = true;
    cv_.notify_all();
}


#endif //MY_SYNC_QUEU_H
