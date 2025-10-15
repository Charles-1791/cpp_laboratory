//
// Created by Charles Green on 9/29/25.
//

#ifndef MY_CHANNEL2_H
#define MY_CHANNEL2_H
#include <assert.h>

template <typename T>
class MyBufferedChannel;

template <typename T>
class CircularArray {
    T* arr_ptr_;
    size_t capacity_;
    size_t size_;
    size_t push_idx_;
    size_t pop_idx_;
    friend class MyBufferedChannel<T>;
public:
    CircularArray(int size): arr_ptr_(new T[size]), capacity_(size), size_(0), push_idx_(0), pop_idx_(0) {}
    CircularArray(const CircularArray& ca) = delete;
    CircularArray& operator=(const CircularArray& ca) = delete;
    CircularArray(CircularArray&& ca): arr_ptr_(ca.arr_ptr_), capacity_(ca.capacity_), size_(ca.size_),
    push_idx_(ca.push_idx_), pop_idx_(ca.pop_idx_) {
        ca.arr_ptr_ = nullptr;
    }
    CircularArray& operator=(CircularArray&& ca) {
        if(this == &ca) {
            return *this;
        }
        // free the old memory
        if(arr_ptr_) {
            delete[] arr_ptr_;
        }
        arr_ptr_ = ca.arr_ptr_;
        ca.arr_ptr_ = nullptr;
        capacity_ = ca.capacity_;
        size_ = ca.size_;
        push_idx_ = ca.push_idx_;
        pop_idx_ = ca.pop_idx_;
        return *this;
    }

    ~CircularArray() {
        if(arr_ptr_) {
            delete[] arr_ptr_;
        }
    }

    bool empty() const {
        return size_ == 0;
    }

    bool full() const {
        return size_ == capacity_;
    }

    template <typename U>
    void push(U&& ele) {
        if(size_ == capacity_) {
            throw std::runtime_error("trying to push into a full ciruclar array");
        }
        arr_ptr_[push_idx_] = std::forward<U>(ele);
        push_idx_ = (push_idx_ + 1) % capacity_;
        ++size_;
    }


    T pop() {
        if(size_ == 0) {
            throw std::runtime_error("trying to pop an empty circular queue");
        }
        T ret(std::move(arr_ptr_[pop_idx_]));
        pop_idx_ = (pop_idx_ + 1) % capacity_;
        --size_;
        return ret;
    }
private:
    template <typename U>
    void revertPop(U&& ele) {
        if(size_ == capacity_) {
            throw std::runtime_error("trying to push into a full ciruclar array");
        }
        pop_idx_ = (pop_idx_ + capacity_ - 1) & capacity_;
        arr_ptr_[pop_idx_] = std::forward<U>(ele);
        ++size_;
    }


};

class MySelect;

template <typename T>
class MyBufferedChannel {
    friend class MySelect;
    struct SleepHelper {
        std::promise<void> waker_;
        std::unique_ptr<T> value_holder_;
    };
    std::mutex mtx_;
    CircularArray<T> buffer_;
    std::queue<std::shared_ptr<SleepHelper>> consumers_;
    std::queue<std::shared_ptr<SleepHelper>> producers_;
    bool closed_;
    std::runtime_error CLOSED_ERROR = std::runtime_error("trying to push to a closed channel");
public:
    MyBufferedChannel(int capacity): buffer_(capacity), closed_(false) {}
    ~MyBufferedChannel() {
        close();
    }

    std::unique_ptr<T> blocking_pop() {
        std::unique_lock<std::mutex> uni_lck(mtx_);
        if(!buffer_.empty()) {
            // pop the first element from the circular queue
            std::unique_ptr<T> ret(std::make_unique<T>(buffer_.pop()));
            // check if producer_ is empty
            if(producers_.empty()) {
                return std::move(ret);
            }
            // wakeup a producer
            std::shared_ptr<SleepHelper> sptr = producers_.front();
            producers_.pop();
            // push the element into buffer
            buffer_.push(std::move(*sptr->value_holder_));
            // free the lock asap
            uni_lck.unlock();
            // unblock that producer
            sptr->waker_.set_value();
            return std::move(ret);
        }
        // if closed, return nil
        if(closed_) {
            return nullptr;
        }
        // otherwise, add itself to consumer queue
        std::shared_ptr<SleepHelper> sptr = std::make_shared<SleepHelper>();
        std::future<void> wait_for = sptr->waker_.get_future();
        consumers_.push(sptr);
        uni_lck.unlock();
        wait_for.wait();
        return std::move(sptr->value_holder_);
    }

    template <typename U>
    void blocking_push(U&& ele) {
        std::unique_lock<std::mutex> uni_lck(mtx_);
        if(closed_) {
            throw CLOSED_ERROR;
        }
        if(!buffer_.full()) {
            // check if consumers_ is empty
            if(consumers_.empty()) {
                buffer_.push(std::forward<U>(ele));
                return;
            }
            // the buffer must be empty
            assert(buffer_.empty());
            // wake up one consumer and send this value to him
            std::shared_ptr<SleepHelper> shptr = consumers_.front();
            consumers_.pop();
            // free the lock asap
            uni_lck.unlock();
            shptr->value_holder_ = std::make_unique<T>(std::forward<U>(ele));
            // wake up the consumer thread
            shptr->waker_.set_value();
            return;
        }
        // consumers_ must be empty
        std::shared_ptr<SleepHelper> shptr(std::make_shared<SleepHelper>());
        shptr->value_holder_ = std::make_unique<T>(std::forward<U>(ele));
        std::future<void> blocker = shptr->waker_.get_future();
        producers_.push(shptr);
        uni_lck.unlock();
        // wait for wake up
        blocker.wait();
    }

    void close() {
        std::lock_guard<std::mutex> guard(mtx_);
        if(closed_) {
            return;
        }
        // first, set the flag
        closed_ = true;
        // then we set exception to all producers in queue
        while (!producers_.empty()) {
            std::shared_ptr<SleepHelper> shptr(producers_.front());
            producers_.pop();
            shptr->waker_.set_exception(std::make_exception_ptr(CLOSED_ERROR));
        }
        // next, we free all consumers, if any
        while(!consumers_.empty()) {
            std::shared_ptr<SleepHelper> shptr(consumers_.front());
            consumers_.pop();
            shptr->waker_.set_value();
        }
    }

    bool closed() {
        std::lock_guard<std::mutex> guard(mtx_);
        return closed_;
    }
};

#endif //MY_CHANNEL2_H
