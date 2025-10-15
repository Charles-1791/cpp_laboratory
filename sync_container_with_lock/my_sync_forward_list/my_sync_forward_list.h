//
// Created by Charles Green on 6/29/25.
//

#ifndef MY_SYNC_FORWARD_LIST_H
#define MY_SYNC_FORWARD_LIST_H

template <typename T>
class MySyncForwardList {
    struct ListNode {
        // use pointer in case T does not provide default constructor, it can also save memory is T is large
        std::shared_ptr<T> data_;
        std::unique_ptr<ListNode> next_;
        std::mutex mtx_;
        template <typename U>
        ListNode(U&& ele): data_(std::make_shared<T>(std::forward<U>(ele))) {}
        ListNode() = default;
    };
    ListNode fooHead;

public:
    MySyncForwardList() = default;
    ~MySyncForwardList() = default;
    // exception safe
    template <typename U>
    void push_front(U&& data) {
        // use smart point here in case the following mtx lock throws, leaving newly allocated memory undeleted
        std::unique_ptr<ListNode> new_node = std::make_unique<ListNode>(std::forward<U>(data));
        std::lock_guard<std::mutex> mtx(fooHead.mtx_);
        new_node->next_ = std::move(fooHead.next_);
        fooHead.next_ = std::move(new_node);
    }

    // true if one element is removed
    template <typename Condition>
    bool remove_first_if(Condition cond) {
        ListNode* prev = &fooHead;
        std::unique_lock<std::mutex> prevLock(prev->mtx_);
        ListNode* curr = prev->next_.get();
        while(curr) {
            std::unique_lock<std::mutex> currLock(curr->mtx_);
            if(cond(*curr->data_)) {
                // CRITICAL: must use a unique pointer to steal the current->next,
                // directly assign prev->next with a new value causing rf of the original prev->next drop to 0,
                // triggering destructor call
                std::unique_ptr<ListNode> old_next = std::move(curr->next_);
                prev->next_ = std::move(old_next->next_);
                // explicitly free prev for higher concurrecy
                prevLock.unlock();
                // CRITICAL: must free currLock explicitly: according to pop order, old_next is recycled before currLock,
                // resulting in a Node recycled with a member a mutex locked - undefined behavior
                currLock.unlock();
                return true;
            }
            // unique lock automatically unlock the lock it holds when move operator is called
            // prevLock.unlock();
            prevLock = std::move(currLock);
            prev = curr;
            curr = curr->next_.get();
        }
        return false;
    }

    template <typename BoolFunction>
    void for_each_until(BoolFunction boolfunc) {
        std::unique_lock<std::mutex> prevLock(fooHead.mtx_);
        ListNode* current = fooHead.next_.get();
        while(current) {
            // CRITICAL: must get current->mtx_ with prevLock unreleased
            std::unique_lock<std::mutex> currLock(current->mtx_);
            prevLock.unlock();
            bool kontinue = boolfunc(*current->data_);
            if(!kontinue)
                return;
            prevLock = std::move(currLock);
            current = current->next_.get();
        }
    }

    template <typename VoidFunction>
    void for_each(VoidFunction voidfunc) {
        auto wrapper = [&voidfunc](const T& data)->bool {
            voidfunc(data);
            return true;
        };
        for_each_until(wrapper);
    }

    template <typename BoolFunction>
    std::shared_ptr<T> find_first_if(BoolFunction boolfunc) {
        std::unique_lock<std::mutex> prevLock(fooHead.mtx_);
        ListNode* current = fooHead.next_.get();
        while(current) {
            std::unique_lock<std::mutex> currLock(current->mtx_);
            prevLock.unlock();
            if(boolfunc(*current->data_)) {
                return current->data_;
            }
            prevLock = std::move(currLock);
            current = current->next_.get();
        }
        return nullptr;
    }


    // We must insert at the tail (not front) to avoid race conditions:
    // If two threads do not find the element, they may both try to insert.
    // Inserting at tail under lock of the last node ensures only one thread inserts;
    // the other will see the new element and update instead.
    // return true for insert and false for update
    template <typename BoolFunction, typename U>
    bool insert_or_update(BoolFunction boolfunc, U&& val) {
        std::unique_lock<std::mutex> prevLock(fooHead.mtx_);
        ListNode* prev = &fooHead;
        ListNode* current = fooHead.next_.get();
        while(current) {
            std::unique_lock<std::mutex> currLock(current->mtx_);
            prevLock.unlock();
            if(boolfunc(*current->data_)) {
                // defer destruction after unlock
                std::shared_ptr<T> defer = current->data_;
                current->data_ = std::make_shared<T>(std::forward<U>(val));
                // manually free lock here, otherwise according to stack pop order, defer gets destructed before prevLock
                currLock.unlock();
                return false;
            }
            prevLock = std::move(currLock);
            prev = current;
            current = current->next_.get();
        }
        // insert at back, since there no statement after this allocation, no unfreed memory
        prev->next_ = std::make_unique<ListNode>(std::forward<U>(val));
        return true;
    }
};
#endif //MY_SYNC_FORWARD_LIST_H
