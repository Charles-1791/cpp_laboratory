//
// Created by Charles Green on 10/14/25.
//

#ifndef MY_CHANNEL_ADVANCED_H
#define MY_CHANNEL_ADVANCED_H
//
// Created by Charles Green on 9/29/25.
//

#include <thread>
#include <list>
#include <atomic>
#include <future>
template <typename T>
class MyBufferedChannel;

inline std::atomic<uint64_t> global_counter{1};

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


};

class MySelect;
template <typename T>
struct InSelectHelper {
    // only the thread manages to compare and swap atomic int can set promise
    std::shared_ptr<std::promise<void>> waker_;
    // -1 means not resolved, >=0 mean resolved
    std::shared_ptr<std::atomic<int>> resolved_case_idx_;
    // identify the select thread, used to dequeue no longer needed operations from waiting queue
    std::thread::id tid_;
    // for a send operation, it is set by select, otherwise it is set by the channel
    std::unique_ptr<T> value_holder_;
    int case_id_;
};

template<typename T>
class CAABImplementation;

template <typename T>
class MyBufferedChannel {
    friend class MySelect;
    friend class CAABImplementation<T>;
    struct SleepHelper {
        // if it is a in-select operation
        std::shared_ptr<InSelectHelper<T>> select_info_;
        // if not in select, use the following two fields
        std::promise<void> waker_;
        std::unique_ptr<T> value_holder_;

    };
    std::mutex mtx_;
    CircularArray<T> buffer_;
    std::list<std::shared_ptr<SleepHelper>> consumers_;
    std::list<std::shared_ptr<SleepHelper>> producers_;
    bool closed_;
    std::runtime_error CLOSED_ERROR = std::runtime_error("trying to push to a closed channel");
    const uint64_t CHANNEL_ID;
public:
    MyBufferedChannel(int capacity): buffer_(capacity), closed_(false), CHANNEL_ID(global_counter++) {}
    ~MyBufferedChannel() {
        close();
    }

    std::unique_ptr<T> blocking_pop() {
        std::unique_lock<std::mutex> uni_lck(mtx_);
        if(buffer_.empty()) {
            if(closed_) {
                return nullptr;
            }
            std::shared_ptr<SleepHelper> sptr = std::make_shared<SleepHelper>();
            std::future<void> wait_for = sptr->waker_.get_future();
            consumers_.push_back(sptr);
            uni_lck.unlock();
            wait_for.get();
            return std::move(sptr->value_holder_);
        }
        // pop the first element from the circular queue
        std::unique_ptr<T> ret(std::make_unique<T>(buffer_.pop()));
        // check if producer_ is empty
        if(producers_.empty()) {
            // if so, early return
            return std::move(ret);
        }
        // wakeup a producer ======================
        while(!producers_.empty()) {
            std::shared_ptr<SleepHelper> sptr = producers_.front();
            producers_.pop_front();
            if(sptr->select_info_ == nullptr) {
                // push the element into buffer
                buffer_.push(std::move(*sptr->value_holder_));
                // free the lock asap
                uni_lck.unlock();
                // unblock that producer
                sptr->waker_.set_value();
                return std::move(ret);
            }
            // in select
            int tmp = -1;
            if(sptr->select_info_->resolved_case_idx_->compare_exchange_strong(tmp, sptr->select_info_->case_id_)) {
                // resposible for waking the blocking select up
                buffer_.push(std::move(*sptr->select_info_->value_holder_));
                // free the lock asap
                uni_lck.unlock();
                // unblock that producer
                sptr->select_info_->waker_->set_value();
                return std::move(ret);
            }
            // some other thread must have woken up the blocking select thread, directly delete it(this is different from golang)
        }
        return std::move(ret);
    }

    template <typename U>
    void blocking_push(U&& ele) {
        std::unique_lock<std::mutex> uni_lck(mtx_);
        if(closed_) {
            throw CLOSED_ERROR;
        }
        if(buffer_.full()) {
            // consumers must be empty
            std::shared_ptr<SleepHelper> sptr = std::make_shared<SleepHelper>();
            sptr->value_holder_ = std::make_unique<T>(std::forward<U>(ele));
            std::future<void> wait_for = sptr->waker_.get_future();
            producers_.push_back(sptr);
            uni_lck.unlock();
            wait_for.get();
            return;
        }
        // check if there is consumer waiting
        if(consumers_.empty()) {
            // push in buffer
            buffer_.push(std::forward<U>(ele));
            return;
        }
        // send the value to a consumer(if possible)
        while (!consumers_.empty()) {
            std::shared_ptr<SleepHelper> sptr = consumers_.front();
            consumers_.pop_front();
            if(sptr->select_info_ == nullptr) {
                uni_lck.unlock();
                // a not-in-select goroutine
                sptr->value_holder_ = std::make_unique<T>(std::forward<U>(ele));
                sptr->waker_.set_value();
                return;
            }
            // an in-select-thread
            int tmp = -1;
            if(sptr->select_info_->resolved_case_idx_->compare_exchange_strong(tmp, sptr->select_info_->case_id_)) {
                sptr->select_info_->value_holder_ = std::make_unique<T>(std::forward<U>(ele));
                uni_lck.unlock();
                // unlock the consumer
                sptr->select_info_->waker_->set_value();
                return;
            }
        }
        // there are indeed consumers, but they are in-select thread and they have been triggered
        // have no choice but to add to the buffer
        buffer_.push(std::forward<U>(ele));
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
            std::shared_ptr<SleepHelper> shptr = producers_.front();
            producers_.pop_front();
            int tmp = -1;
            if(shptr->select_info_ == nullptr) {
                shptr->waker_.set_exception(std::make_exception_ptr(CLOSED_ERROR));
            } else if(shptr->select_info_->resolved_case_idx_->compare_exchange_strong(tmp, shptr->select_info_->case_id_)) {
                shptr->select_info_->waker_->set_exception(std::make_exception_ptr(CLOSED_ERROR));
            }
        }
        // next, we free all consumers, if any
        while(!consumers_.empty()) {
            std::shared_ptr<SleepHelper> shptr = consumers_.front();
            consumers_.pop_front();
            int tmp = -1;
            if(shptr->select_info_ == nullptr) {
                shptr->waker_.set_value();
            } else if(shptr->select_info_->resolved_case_idx_->compare_exchange_strong(tmp, shptr->select_info_->case_id_)) {
                shptr->select_info_->waker_->set_value();
            }
        }
    }

    bool closed() {
        std::lock_guard<std::mutex> guard(mtx_);
        return closed_;
    }

    uint64_t get_channel_id() const {
        return CHANNEL_ID;
    }

    int size() {
        std::lock_guard<std::mutex> guard(mtx_);
        return buffer_.size_;
    }
private:
    std::unique_lock<std::mutex> unique_lock() {
        return std::unique_lock<std::mutex>(mtx_);
    }

    void clean_queue_with_tid(std::thread::id tid) {
        auto it = consumers_.begin();
        while (it != consumers_.end()) {
            std::shared_ptr<SleepHelper> sptr = *it;
            auto copied = it++;
            if(sptr->select_info_ && sptr->select_info_->tid_ == tid) {
                std::cout << "clean up an entry with tid = " << tid << " from channel " << CHANNEL_ID << "'s consumers_\n";
                consumers_.erase(copied);
            }
        }
        it = producers_.begin();
        while (it != producers_.end()) {
            std::shared_ptr<SleepHelper> sptr = *it;
            auto copied = it++;
            if(sptr->select_info_ && sptr->select_info_->tid_ == tid) {
                std::cout << "clean up an entry with tid = " << tid << " from channel " << CHANNEL_ID << "'s producers\n";
                producers_.erase(copied);
            }
        }
    }

    // must be called while holding the lock
    // return true + not null place_holder -> get a real value
    // return true + null place_holder -> pop from a closed channel
    // return false + null place_holder -> pop nothing, try failed
    bool tryPop(std::unique_ptr<T>* place_holder) {
        if(buffer_.empty()) {
            if (closed_) {
                *place_holder = nullptr;
                return true; // closed + no value
            }
            return false;
        }
        // retreive one element from buffer
        // pop the first element from the circular queue
        *place_holder = std::make_unique<T>(buffer_.pop());
        // if producers is empty, we can early return
        if(producers_.empty()) {
            return true;
        }
        // wakeup a producer ======================
        while (!producers_.empty()) {
            std::shared_ptr<SleepHelper> sptr = producers_.front();
            producers_.pop_front();
            int tmp = -1;
            if(sptr->select_info_ == nullptr) {
                // not a select
                buffer_.push(std::move(*sptr->value_holder_));
                sptr->waker_.set_value();
                return true;
            }
            if(sptr->select_info_->resolved_case_idx_->compare_exchange_strong(tmp, sptr->select_info_->case_id_)) {
                // resposible for waking the blocking select up
                buffer_.push(std::move(*sptr->select_info_->value_holder_));
                // unblock that producer
                sptr->select_info_->waker_->set_value();
                return true;
            }
        }
        return true;
    }

    // reurn true -> sccessfully push into the channel
    // return false -> try failed
    template <typename U>
    bool tryPush(U&& element) {
        if(closed_) {
            throw CLOSED_ERROR;
        }
        if(buffer_.full()) {
            return false;
        }
        if(consumers_.empty()) {
            // push into buffer
            buffer_.push(std::forward<U>(element));
            return true;
        }
        // wake up a consumer
        while (!consumers_.empty()) {
            std::shared_ptr<SleepHelper> sptr = consumers_.front();
            consumers_.pop_front();
            if(sptr->select_info_ == nullptr) {
                sptr->value_holder_ = std::make_unique<T>(std::forward<U>(element));
                sptr->waker_.set_value();
                return true;
            }
            int tmp = -1;
            if(sptr->select_info_->resolved_case_idx_->compare_exchange_strong(tmp, sptr->select_info_->case_id_)) {
                // responsible for waking it up from haning select
                sptr->select_info_->value_holder_ = std::make_unique<T>(std::forward<U>(element));
                sptr->select_info_->waker_->set_value();
                return true;
            }
        }
        // there are indeed consumers, but they are in-select thread and they have been triggered
        // have no choice but to add to the buffer
        buffer_.push(std::forward<U>(element));
        return true;
    }

    void registerInConsumer(std::shared_ptr<InSelectHelper<T>> ish) {
        std::shared_ptr<SleepHelper> sptr = std::make_shared<SleepHelper>();
        sptr->select_info_ = ish;
        std::cout << "register " << ish->tid_ << " into channel " << CHANNEL_ID << "'s consumer queue\n";
        consumers_.push_back(sptr);
    }

    void registerInProducer(std::shared_ptr<InSelectHelper<T>> ish) {
        std::shared_ptr<SleepHelper> sptr = std::make_shared<SleepHelper>();
        sptr->select_info_ = ish;
        std::cout << "register " << ish->tid_ << " into channel " << CHANNEL_ID << "'s producer queue\n";
        producers_.push_back(sptr);
    }
};

#endif //MY_CHANNEL_ADVANCED_H
