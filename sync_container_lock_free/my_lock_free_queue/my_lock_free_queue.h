//
// Created by Charles Green on 7/3/25.
//

#ifndef MYLOCKFREEQUEUE_H
#define MYLOCKFREEQUEUE_H
#include <cassert>

template <typename T>
class MyLockFreeQueue {
    /*
     * head                 tail
     *  [] --> [] --> [] --> []
     *
     */
    struct QueueNode;
    class CountedPointer {
        uint64_t internal_representation_;
        constexpr static uint64_t ADDR_MASK = (1ULL << 48) - 1;
        constexpr static uint64_t packIntoCountedPointer(QueueNode* addr, int16_t rf) {
            uint64_t ret = 0;
            assert((reinterpret_cast<uint64_t>(addr) & ~ADDR_MASK) == 0);
            ret |= ADDR_MASK & reinterpret_cast<uint64_t>(addr);
            ret |= static_cast<uint64_t>(rf) << 48;
            return ret;
        }
    public:
        CountedPointer(QueueNode* ptr, uint16_t rf): internal_representation_(packIntoCountedPointer(ptr, rf)) {}
        CountedPointer(): internal_representation_(0) {}
        QueueNode* pointer() const {
            return reinterpret_cast<QueueNode*>(internal_representation_ & ADDR_MASK);
        }

        int16_t counter() const {
            return static_cast<int16_t>(internal_representation_ >> 48);
        }

        void increment_counter(int16_t addon) {
            uint64_t current = counter();
            current += addon;
            current <<= 48;
            internal_representation_ = current | (internal_representation_ & ADDR_MASK);
        }
    };
    struct TwoPhaseCounter {
    private:
        struct cnt_and_phase {
            uint32_t cnt_;
            // the phases_ decreases by 1 when head or tail can no longer reaches it.
            int32_t phases_;
            cnt_and_phase(): cnt_(0), phases_(2) {}
            cnt_and_phase(int32_t cnt, int phases): cnt_(cnt), phases_(phases) {}
        };
        // make sure it is within a word to make atomic<TwoPhaseCounter> lock free
        std::atomic<cnt_and_phase> internal_representation_;
    public:
        TwoPhaseCounter(): internal_representation_(cnt_and_phase(0, 2)) {
            assert(internal_representation_.is_lock_free());
        }
        bool finish_one_phase_and_release(uint32_t acquire_cnt) {
            cnt_and_phase current = internal_representation_.load();
            cnt_and_phase modified(current);
            do {
                modified = current;
                modified.cnt_ -= acquire_cnt - 1;
                --modified.phases_;
            } while (!internal_representation_.compare_exchange_strong(current, modified, std::memory_order_acquire, std::memory_order_relaxed));
            return modified.phases_ == 0 && modified.cnt_ == 0;
        }
        bool release() {
            cnt_and_phase current = internal_representation_.load();
            cnt_and_phase modified;
            do {
                modified = current;
                ++modified.cnt_;
            } while (!internal_representation_.compare_exchange_strong(current, modified, std::memory_order_acquire, std::memory_order_relaxed));
            return modified.phases_ == 0 && modified.cnt_ == 0;
        }
    };
    struct QueueNode {
        std::atomic<T*> data_;
        // internal counter
        TwoPhaseCounter tp_cnt_;
        std::atomic<CountedPointer> next_;
        QueueNode(): data_(nullptr) {}
        template <typename U>
        QueueNode(U&& data):data_(std::make_shared<T>(std::forward<U>(data))) {}
        ~QueueNode() {
            T* data_ptr = data_.load();
            if(data_ptr) {
                delete data_ptr;
            }
        }
    };

    void set_new_tail(CountedPointer expected, CountedPointer setTo) {
        QueueNode* ptr = expected.pointer();
        while(expected.pointer() == ptr && !tail_.compare_exchange_weak(expected, setTo));
        bool recycle = false;
        if (expected.pointer() == ptr) {
            // you are the thread successfully updating tail_, and you currently must be holding the largest external counter
            recycle = ptr->tp_cnt_.finish_one_phase_and_release(expected.counter());
        } else {
            // some other thread has update the tail, your external counter is staled
            recycle = ptr->tp_cnt_.release();
        }
        if(recycle) {
            delete ptr;
        }
    }
    std::atomic<CountedPointer> head_;
    std::atomic<CountedPointer> tail_;

    CountedPointer get_and_increase(std::atomic<CountedPointer>& cp) {
        CountedPointer cp_old = cp.load();
        CountedPointer cp_new;
        do {
            cp_new = cp_old;
            cp_new.increment_counter(1);
        } while (!cp.compare_exchange_strong(cp_old, cp_new, std::memory_order_acquire, std::memory_order_relaxed));
        return cp_new;
    }
public:
    MyLockFreeQueue(): head_(CountedPointer(new QueueNode,  0)), tail_(head_.load()) {}
    ~MyLockFreeQueue() {
        while (pop());
    }
    /*
    template <typename U>
    void push(U&& data) {
        // use smart pointer here in case some statement throw, leaving heap memory unreclyed
        std::unique_ptr<T> uptr = std::make_unique<T>(std::forward<U>(data));
        CountedPointer new_tail(new QueueNode, 0);
        while (true) {
            CountedPointer cp = get_and_increase(tail_);
            QueueNode* ptr = cp.pointer();
            T* data = nullptr;
            if(ptr->data_.compare_exchange_strong(data, uptr.get(), std::memory_order_release, std::memory_order_relaxed)) {
                // you now owns this node, others is spinning on the while loop
                // pop cannot see this node because we haven't update tail yet, pop now thinks tail == head
                ptr->next_ = new_tail;
                // this is samrt, using exchange to get the largest acquire count.
                // after this exchange, tail_ is changed to another CountedPointer and no other push thread can possibly
                // increase the acquire count.
                CountedPointer latest_cp = tail_.exchange(new_tail);
                assert(latest_cp.pointer() == cp.pointer());
                assert(latest_cp.counter() >= cp.counter());
                bool should_delete = ptr->tp_cnt_.finish_one_phase_and_release(latest_cp.counter());
                // as soon as tail_.exchange finishes, the pop() takes away the node, that may lead should_delete to be true
                if(should_delete) {
                    delete ptr;
                }
                // free unique pointer
                uptr.release();
                break;
            }
            if(ptr->tp_cnt_.release()) {
                delete ptr;
            }
        }
    }
    */

    template <typename U>
    void push(U&& data) {
        // use smart pointer here in case some statement throw, leaving heap memory unreclyed
        std::unique_ptr<T> uptr = std::make_unique<T>(std::forward<U>(data));
        CountedPointer new_tail(new QueueNode, 0);
        while (true) {
            CountedPointer cp = get_and_increase(tail_);
            QueueNode* ptr = cp.pointer();
            T* data = nullptr;
            if(ptr->data_.compare_exchange_strong(data, uptr.get(), std::memory_order_release, std::memory_order_relaxed)) {
                // try to set the next to its local newed next
                CountedPointer place_holder;
                if(ptr->next_.compare_exchange_strong(place_holder, new_tail)) {
                    set_new_tail(cp, new_tail);
                } else {
                    // some other thread has update the next for you
                    // allocated new_tail is useless now
                    delete new_tail.pointer();
                    set_new_tail(cp, place_holder);
                }
                uptr.release();
                break;
            } else {
                // help set the next
                CountedPointer place_holder;
                if(ptr->next_.compare_exchange_strong(place_holder, new_tail)) {
                    set_new_tail(cp, new_tail);
                    // the current has been used, must allocate a new one
                    new_tail = CountedPointer(new QueueNode, 0);
                } else {
                    set_new_tail(cp, place_holder);
                }
            }
        }
    }
    /*
    std::unique_ptr<T> pop() {
        std::unique_ptr<T> ret;
        while(true) {
            CountedPointer cp = get_and_increase(head_);
            QueueNode* ptr = cp.pointer();
            if(ptr == tail_.load().pointer()) {
                ptr->tp_cnt_.release();
                return ret;
            }
            if(head_.compare_exchange_strong(cp, ptr->next_)) {
                // cp must be holding the largest acquire cnt
                // use exchange to set data_ to nullptr, otherwise, destructor of QueueNode would delete such value
                ret = ptr->data_.exchange(nullptr);
                if(ptr->tp_cnt_.finish_one_phase_and_release(cp.counter())) {
                    delete ptr;
                }
                return ret;
            }
            if(ptr->tp_cnt_.release()) {
                delete ptr;
            }
        }
    }
    */
    std::unique_ptr<T> pop() {
        std::unique_ptr<T> ret;
        while(true) {
            // the first thread running pop() on a certain node must see its external count == 0
            CountedPointer cp = get_and_increase(head_);
            QueueNode* ptr = cp.pointer();
            if(ptr == tail_.load().pointer()) {
                ptr->tp_cnt_.release();
                return ret;
            }
            if(head_.compare_exchange_strong(cp, ptr->next_.load())) {
                // cp must be holding the largest acquire cnt
                // use exchange to set data_ to nullptr, otherwise, destructor of QueueNode would delete such value
                ret.reset(ptr->data_.exchange(nullptr));
                if(ptr->tp_cnt_.finish_one_phase_and_release(cp.counter())) {
                    delete ptr;
                }
                return ret;
            }
            if(ptr->tp_cnt_.release()) {
                delete ptr;
            }
        }
    }
};
#endif //MYLOCKFREEQUEUE_H
