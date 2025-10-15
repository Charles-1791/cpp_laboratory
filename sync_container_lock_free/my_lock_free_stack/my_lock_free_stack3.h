//
// Created by Charles Green on 7/2/25.
//

#ifndef MYLOCKFREESTACK3_H
#define MYLOCKFREESTACK3_H

// Warining: only works for architecture supporting canonical addressing
template <typename T>
class MyLockFreeStack3 {
    struct StackNode;
    // This following CountedPointer would make atomic<CountedPointer> NOT lock-free.
    // But fortunately, on x64 machine, beacuse of canonical address, a pointer in fact uses only the lower 48 bits,
    // so we can use the extra 16 bits to store the acquire_cnt_. This magic is called pointer tagging.
    // struct CountedPointer {
    //     int16_t acquire_cnt_;
    //     StackNode* node_;
    // };
    // The following CountedPointer is simply a wrapper of uint64_t, thus, atomic<CountedPointer> is lock free
    class CountedPointer {
        uint64_t internal_representation_;
        constexpr static uint64_t ADDR_MASK = (1ULL << 48) - 1;
        constexpr static uint64_t packIntoCountedPointer(StackNode* addr, int16_t rf) {
            uint64_t ret = 0;
            assert((reinterpret_cast<uint64_t>(addr) & ~ADDR_MASK) == 0);
            ret |= ADDR_MASK & reinterpret_cast<uint64_t>(addr);
            ret |= static_cast<uint64_t>(rf) << 48;
            return ret;
        }
    public:
        CountedPointer(StackNode* ptr, uint16_t rf): internal_representation_(packIntoCountedPointer(ptr, rf)) {}
        CountedPointer(): internal_representation_(0) {}
        bool operator==(const CountedPointer& another) const {
            return internal_representation_ == another.internal_representation_;
        }
        bool operator!=(const CountedPointer& another) const {
            return internal_representation_ != another.internal_representation_;
        }
        StackNode* pointer() const {
            return reinterpret_cast<StackNode*>(internal_representation_ & ADDR_MASK);
        }

        int16_t counter() const {
            return static_cast<int16_t>(internal_representation_ >> 48);
        }
    };

    struct StackNode {
        std::atomic<int16_t> release_cnt_;
        std::shared_ptr<T> data_;
        CountedPointer next_;
        StackNode(): release_cnt_(0) {}
    };
    // when the stack is emtpty, there is still a dummy node at head_. The acquire_cnt_ of the dummy node may be any
    // positive interger.
    std::atomic<CountedPointer> head_;
public:
    MyLockFreeStack3() = default;
    ~MyLockFreeStack3() {
        while (pop());
    }
    template <typename U>
    void push(U&& input) {
        StackNode* sn = new StackNode();
        sn->data_ = std::make_shared<T>(std::forward<U>(input));
        sn->next_ = head_.load(std::memory_order_relaxed);
        CountedPointer ncp(sn, 0);
        /*
        * Using release on success ensures that:
        * Prior writes to sn->data_ and sn->next_ are visible to another thread that later acquires head_.
        */
        while(!head_.compare_exchange_weak(sn->next_, ncp, std::memory_order_release, std::memory_order_relaxed));
    }

    CountedPointer get_and_increase() {
        CountedPointer old_cp = head_.load(std::memory_order_relaxed);
        CountedPointer new_cp;
        int16_t counter = 0;
        StackNode* ptr;
        do {
            new_cp = old_cp;
            ptr = old_cp.pointer();
            counter = old_cp.counter();
            new_cp = CountedPointer(ptr, counter + 1);
        } while(!head_.compare_exchange_strong(old_cp, new_cp, std::memory_order_acquire, std::memory_order_relaxed));
        return new_cp;
    }

    std::shared_ptr<T> pop() {
        while(true) {
            CountedPointer head_cp = get_and_increase();
            StackNode* ptr = head_cp.pointer();
            int16_t ac_cnt = head_cp.counter();
            if(!ptr) {
                // we don't care about the dummy node's acquire_cnt_
                return nullptr;
            }
            if(head_.compare_exchange_strong(head_cp, ptr->next_, std::memory_order_relaxed, std::memory_order_relaxed)) {
                // For a certain node, only the thread owning the largest acquire_cnt_ can pass the above compare exchange strong.
                // The reason is simple: suppose by contradition that there is another thread, X, holding
                // this node but with a larger acquired_cnt_. To obtain this node, thread X must have finished calling get_and_increase().
                // And that means the `head_` must have been updated to X's version - a CountedPointer with larger acquire_cnt_.
                // But that would make the current compare_exchange_strong fail - a contradiction.

                // another situation is that some thread tries to push while you are doing pop. If that thread succesfully
                // acquires head_ by changing it to anther node, you cannot possibly reach here.
                std::shared_ptr<T> ret = std::move(ptr->data_);
                // there are head_cp.acquire_cnt_ threads used to hold this node, so there should be head_cp.acquire_cnt_ fetch_add(1)
                int addon = - ac_cnt + 1;
                if(ptr->release_cnt_.fetch_add(addon, std::memory_order_release) == -addon) {
                    delete ptr;
                }
                return ret;
            }
            if(ptr->release_cnt_.fetch_add(1, std::memory_order_relaxed) == -1) {
                // only one thread would do delete, so this thread must delete the ptr after ptr->data_ is retreived
                // so only the thread passes the if condition need to achieve the happens-after semanstics
                ptr->release_cnt_.load(std::memory_order_acquire);
                delete ptr;
            }
        }
    }
};


#endif //MYLOCKFREESTACK3_H
