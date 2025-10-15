//
// Created by Charles Green on 7/1/25.
//

#ifndef MYLOCKFREESTACK2_H
#define MYLOCKFREESTACK2_H
#include <assert.h>

// aidding class
class HazardPool {
    static constexpr int POOL_SIZE = 1024;
    struct HazardPointerSlot {
        std::atomic<void*> ptr_;
        std::atomic<bool> occupied_;
        HazardPointerSlot(): ptr_(nullptr), occupied_(false) {}
        HazardPointerSlot(const HazardPointerSlot&) = delete;
        HazardPointerSlot& operator=(const HazardPointerSlot&) = delete;
    };
    HazardPointerSlot pool_[POOL_SIZE];
public:
    struct HazardPointerWrapper {
    private:
        friend class HazardPool;
        HazardPointerSlot* hptr_;
    public:
        HazardPointerWrapper(): hptr_(nullptr) {}
        // the only reason we need a wrapper is to use its desturctor
        ~HazardPointerWrapper() {
            // restore the state of the slot allocated
            if(hptr_) {
                hptr_->ptr_.store(nullptr);
                hptr_->occupied_.store(false);
            }
        }
        std::atomic<void*>& retreive_hazard_pointer() const {
            return hptr_->ptr_;
        }
        bool initialized() const {
            return hptr_ != nullptr;
        }
    };
    HazardPool() {
        for(int i=0;i<POOL_SIZE;++i) {
            pool_[i].ptr_.store(nullptr);
        }
    }
    void allocate_into_wrapper(HazardPointerWrapper& wrapper) {
        for(int i=0;i<POOL_SIZE;++i) {
            bool expect = false;
            if(pool_[i].occupied_.compare_exchange_strong(expect, true)) {
                wrapper.hptr_ = &pool_[i];
                return;
            }
        }
        throw std::runtime_error("hazard pool is out of space");
    }
    bool safe_to_clean(void* ptr) const {
        for(int i=0;i<POOL_SIZE;++i) {
            if(pool_[i].ptr_.load() == ptr) {
                return false;
            }
        }
        return true;
    }
};

// this is mysterious
template <typename T>
void do_delete(void* ptr) {
    delete static_cast<T*>(ptr);
}

class HazardDustbin {
    struct BinNode {
        void* ptr_;
        std::function<void(void*)> deletor_;
        BinNode* next;
        template <typename T>
        BinNode(T* ptr): ptr_(ptr), deletor_(&do_delete<T>) {}
        BinNode(): ptr_(nullptr), deletor_(nullptr), next(nullptr) {}
        ~BinNode() {
            if(ptr_) {
                deletor_(ptr_);
            }
        }
    };
    std::atomic<BinNode*> dustbin_head_;
    const HazardPool& reference_pool_;
public:
    HazardDustbin(const HazardPool& reference_pool): dustbin_head_(nullptr), reference_pool_(reference_pool) {}
    ~HazardDustbin() {
        // a correct state should be already empty
        assert(dustbin_head_.load() == nullptr);
    }
    // thread safe
    template <typename T>
    void add_to_dustbin(T* ptr) {
        BinNode* new_node = new BinNode(ptr);
        new_node->next = dustbin_head_.load();
        while(!dustbin_head_.compare_exchange_weak(new_node->next, new_node));
    }
    // thread safe
    void insert_back(BinNode* head, BinNode* tail) {
        assert(tail != nullptr);
        tail->next = dustbin_head_.load();
        while (!dustbin_head_.compare_exchange_weak(tail->next, head));
    }
    // thread safe
    void try_to_clean() {
        BinNode* head = dustbin_head_.exchange(nullptr);
        BinNode cannot_clean;
        BinNode* cannot_clean_head = &cannot_clean;
        BinNode* cannot_clean_tail = cannot_clean_head;

        BinNode* curr = head;
        while (curr) {
            if(reference_pool_.safe_to_clean(curr)) {
                BinNode* next = curr->next;
                delete curr;
                curr = next;
            } else {
                cannot_clean_tail->next = curr;
                cannot_clean_tail = curr;
                curr = curr->next;
            }
        }
        if(cannot_clean_head->next) {
            insert_back(cannot_clean_head->next, cannot_clean_tail);
        }
    }
};



template <typename T>
class MyLockFreeStack2 {
    struct StackNode {
        // we must use (smart) pointers here so that when we pop a node, we can directly return a pointer, otherwise
        // we need to move create it on heap, which may fail!
        std::shared_ptr<T> data_;
        StackNode* next_;
        template <typename U>
        StackNode(U&& data): data_(std::make_shared<T>(std::forward<U>(data))) {}
    };
    StackNode* pop_head() noexcept;
    std::atomic<void*>& get_hazard_pointer();

    std::atomic<StackNode*> head_;
    HazardPool hzd_pool_;
    HazardDustbin dustbin_;
public:
    MyLockFreeStack2(): head_(nullptr), dustbin_(hzd_pool_) {}
    ~MyLockFreeStack2() = default;
    template <typename U>
    void push(U&& data);
    std::shared_ptr<T> pop();
    bool pop(T& placeholder);
};

template<typename T>
template<typename U>
void MyLockFreeStack2<T>::push(U &&data) {
    StackNode* new_node = new StackNode(std::forward<U>(data));
    new_node->next_ = head_.load(); // maybe this step is unnecessary
    while(!head_.compare_exchange_weak(new_node->next_, new_node));
}

template<typename T>
typename MyLockFreeStack2<T>::StackNode* MyLockFreeStack2<T>::pop_head() noexcept {
    StackNode* current_head;
    std::atomic<void*>& hp = get_hazard_pointer();
    do {
        StackNode* new_head = head_.load();
        do {
            current_head = new_head;
            hp.store(current_head);
            new_head = head_.load();
        } while(current_head != new_head);
    } while(current_head && !head_.compare_exchange_strong(current_head, current_head->next_));
    hp.store(nullptr);
    return current_head;
}

// thread safe
template<typename T>
std::shared_ptr<T> MyLockFreeStack2<T>::pop() {
    StackNode* current_head = pop_head();
    if(current_head) {
        // smart pointer assignment can never fail
        std::shared_ptr<T> ret = std::move(current_head->data_);
        if(free_to_clean(current_head)) {
            delete current_head;
        } else {
            dustbin_.add_to_dustbin(current_head);
        }
        dustbin_.try_to_clean();
        return ret;
    }
    return nullptr;
}

// CAUTION: each thread should call this function only once
template <typename T>
std::atomic<void*>& MyLockFreeStack2<T>::get_hazard_pointer() {
    thread_local static HazardPool::HazardPointerWrapper wrp;
    if(!wrp.initialized()) {
        // if this is the first time this thread call get_hazard_pointer, allocate a new one for it
        hzd_pool_.allocate_into_wrapper(wrp);
    }
    return wrp.retreive_hazard_pointer();
}

template<typename T>
bool MyLockFreeStack2<T>::pop(T &placeholder) {
    StackNode* current_head = pop_head();
    if(current_head) {
        // not thread safe: if the construcion is failed, the data is lost forever
        placeholder = std::move(*current_head->data_);
        if(hzd_pool_.safe_to_clean(current_head)) {
            delete current_head;
        } else {
            dustbin_.add_to_dustbin(current_head);
        }
        dustbin_.try_to_clean();
        return true;
    }
    return false;
}



/**
This is the implementation offered by textbook:
```
// textbook
std::shared_ptr<T> pop()
{
    std::atomic<void*>& hp=get_hazard_pointer_for_current_thread();
    node* old_head=head.load();
    do {
         node* temp;
        do {
           temp=old_head;
           hp.store(old_head);
           old_head=head.load();
         } while(old_head!=temp);
    } while(old_head && !head.compare_exchange_strong(old_head,old_head->next));
    // ...
}
```
In the first instance, I was annoyed by the verbosity of the design and I rewrote it into:
// my intuition
std::shared_ptr<T> pop()
{
    std::atomic<void*>& hp=get_hazard_pointer_for_current_thread();
    node* old_head=head.load();
    // time lapse A
    do {
        // time lapse B
        hp.store(old_head);
    } while(old_head && !head.compare_exchange_weak(old_head,old_head->next));
    // ...
}

But it is incorrect:
// Thread X assign head.load to old_head and is pre-empted,
imagine before X wakes up again, within time lapse A, some other thread Y finished the whole pop process,
and since old_head has yet been stored in a hazaard pointer, thread Y delete this pointer.
Now Thread X wakes up and store the old_head into hp, but it is too late. The following compare_exchange_weak
is undefined because of deferencing the deleted old_head.

Similarly, if a compare exchange weak fails, before hp get reset in the next round, within time lapse B, some other thread
can delete old_head.

There is no way to promise that there is no other thread deleting old_head within time lapse A or B, so instead, we
optimistically believe that hp.store() stores the real head and double check if it is indeed the case, if no, we retry it.

// my second edition, more concise that the one in textbook
std::shared_ptr<T> pop()
{
    std::atomic<void*>& hp=get_hazard_pointer_for_current_thread();
    node* old_head;
    do {
        do {
            old_head = head.load();
            hp.store(old_head);
         } while(old_head != head.load());
    } while(old_head && !head.compare_exchange_strong(old_head,old_head->next));
    // ...
}
This edition is correct and clear, but it use two load() in each iteration, slightly more expensive than the one in
textbook, which uses only one. So my final version should be:
std::shared_ptr<T> pop()
{
    std::atomic<void*>& hp=get_hazard_pointer_for_current_thread();
    node* old_head;
    do {
        node* new_head = head.load();
        do {
            old_head = new_head;
            hp.store(old_head);
            new_head = head.load();
         } while(old_head != new_head));
    } while(old_head && !head.compare_exchange_strong(old_head,old_head->next));
    // ...
}

*/


#endif //MYLOCKFREESTACK2_H
