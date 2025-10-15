//
// Created by Charles Green on 6/30/25.
//

#ifndef MYLOCKFREESTACK_H
#define MYLOCKFREESTACK_H
#include <assert.h>

template <typename T>
class MyLockFreeStack {
    struct StackNode {
        // we must use (smart) pointers here so that when we pop a node, we can directly return a pointer, otherwise
        // we need to move create it on heap, which may fail!
        std::shared_ptr<T> data_;
        StackNode* next_;
        template <typename U>
        StackNode(U&& data): data_(std::make_shared<T>(std::forward<U>(data))) {}
    };
    std::atomic<unsigned> pop_rf_cnt_;
    std::atomic<StackNode*> head_;
    StackNode* pop_head() noexcept;
    // for gc
    std::atomic<StackNode*> dustbin_;
    void try_to_recycle(StackNode*);
    void add_to_dustbin_(StackNode*, StackNode*);
    StackNode* find_tail_node(StackNode*);
public:
    MyLockFreeStack(): pop_rf_cnt_(0), head_(nullptr), dustbin_(nullptr) {}
    ~MyLockFreeStack() {
        // clear the stack and
        // if the dustbin is not empty, it means that at least one thread is still in pop()
        assert(dustbin_.load() == nullptr);
        StackNode* sn = head_.load();
        while(sn) {
            StackNode* next = sn->next_;
            delete sn;
            sn = next;
        }
    }
    template <typename U>
    void push(U&& data);
    std::shared_ptr<T> pop();
    bool pop(T& placeholder);

};

template<typename T>
template<typename U>
void MyLockFreeStack<T>::push(U &&data) {
    StackNode* new_node = new StackNode(std::forward<U>(data));
    new_node->next_ = head_.load(); // maybe this step is unnecessary
    while(!head_.compare_exchange_weak(new_node->next_, new_node));
}

template<typename T>
typename MyLockFreeStack<T>::StackNode* MyLockFreeStack<T>::pop_head() noexcept {
    ++pop_rf_cnt_;
    StackNode* current_head = head_.load(std::memory_order::acquire); // this step is needed
    // CRTICIAL: ABA problem:
    /**
     *  In thread 1: assuming at some iteration, current_head is not equal to head_, so a compare_exchange_weak set current_head
     *  to head_, and the thread is preempted.
     *
     *  At the same time, another thread, thread 2 managed to pop the element, causing it to be deleted on heap. Then it push a
     *  new element, but it happens to be allocated at the same address. So head_ now still holds the same address,
     *  but actually it points to a new element.
     *
     *  Thread 1 is running again, the compare_exchange_weak passes because the address is the same. But current_head has
     *  in fact been deleted, deferencing it is undefined behavior.
     *
     *  Notes:
     *  Once an object’s lifetime ends, any pointer that referred to it becomes a dangling pointer.
     *  Even if that memory is reallocated for a new object, dereferencing the old pointer is undefined behavior.
     */
    while(current_head && !head_.compare_exchange_weak(current_head, current_head->next_));
    return current_head;
}

// thread safe
template<typename T>
std::shared_ptr<T> MyLockFreeStack<T>::pop() {
    StackNode* current_head = pop_head();
    if(current_head) {
        // smart pointer assignment can never fail
        std::shared_ptr<T> ret = std::move(current_head->data_);
        // delete would cause trouble
        try_to_recycle(current_head);
        return ret;
    }
    return nullptr;
}

template <typename T>
MyLockFreeStack<T>::StackNode* MyLockFreeStack<T>::find_tail_node(StackNode* front) {
    StackNode* current = front;
    while (current->next_) {
        current = current->next_;
    }
    return current;
}

template<typename T>
void MyLockFreeStack<T>::try_to_recycle(StackNode* node) {
    if(pop_rf_cnt_ == 1) {
        // time lapse A
        StackNode* dustbin_head = dustbin_.exchange(nullptr);
        // time lapse B
        if(pop_rf_cnt_.fetch_sub(1) == 1) {
            /*
                CAUTION:
                The first `if` in this function checks if `pop_rf_cnt_` is 1. If so, at that exact moment,
                we know this thread is the only one calling `pop()`, meaning all elements in `dustbin_`,
                as well as the input `node`, can be safely recycled.

                However, between this check and the call to `dustbin_.exchange(nullptr)`, a period of time
                (time lapse A) has passed. During this window, other threads may have added nodes to the dustbin,
                making parts of the retrieved `dustbin_` potentially unsafe to reclaim.

                Therefore, we must double-check the state by calling `fetch_sub(1)` — this introduces another time window (time lapse B),
                during which even more nodes might be inserted into `dustbin_`. But we don’t care about those newly added nodes.
                We only care whether the nodes we *already* retrieved (via `dustbin_.exchange(nullptr)`) are safe to free.

                If `fetch_sub(1)` returns 1, it means:
                    - No other threads were in `pop()` during time lapse A or B, OR
                    - Any threads that entered `pop()` during these lapses have already exited.
                  Therefore, `dustbin_head` and the input `node` are safe to delete.

                Otherwise, other threads are still running `pop()`, and the retrieved `dustbin_head` may include
                nodes that are still visible or reachable by those threads. Even though it’s possible that all nodes
                in `dustbin_head` are safe (e.g. if new `pop()` calls occurred only during time lapse B),
                we can’t be sure. To ensure correctness, we conservatively avoid deletion and put the list back.
            */

            delete node;
            node = dustbin_head;
            StackNode* next;
            while (node) {
                // we don't need to worry that node->next may points to an element inside the stack
                // in that when we add an element into dustbin, it's next is always be assigned to dustbin_head(initially nullptr)
                next = node->next_;
                delete node;
                node = next;
            }
        } else {
            delete node;
            StackNode* dustbin_tail = find_tail_node(dustbin_head);
            add_to_dustbin_(dustbin_head, dustbin_tail);
        }
    } else {
        add_to_dustbin_(node, node);
        --pop_rf_cnt_;
    }
}

template<typename T>
void MyLockFreeStack<T>::add_to_dustbin_(StackNode* front, StackNode* rear) {
    rear->next_ = dustbin_.load();
    while(!dustbin_.compare_exchange_weak(rear->next, front));
}



template<typename T>
bool MyLockFreeStack<T>::pop(T &placeholder) {
    StackNode* current_head = pop_head();
    if(current_head) {
        // not thread safe: if the construcion is failed, the data is lost forever
        placeholder = std::move(*current_head->data_);
        try_to_recycle(current_head);
        return true;
    }
    return false;
}

#endif //MYLOCKFREESTACK_H
