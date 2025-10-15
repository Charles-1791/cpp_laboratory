//
// Created by Charles Green on 10/14/25.
//

#ifndef MY_SELECT_H
#define MY_SELECT_H
#include "../my_channel/my_channel_advanced.h"
#include <assert.h>
#include <vector>
#include <thread>
#include <set>

enum ChannelOperation {SEND, RECEIVE};
struct CAABInterface {
    virtual  ~CAABInterface() = default;
    virtual bool tryChannelOp() = 0;
    virtual std::unique_lock<std::mutex> lockChannel() = 0;
    virtual void registerIntoChannel() = 0;
    virtual void cleanUp() = 0;
    virtual void takeAction() = 0;
    virtual int64_t channel_id() = 0;
};


template <typename T>
class CAABImplementation: public CAABInterface {
public:
    // case arm and branch
    bool is_default = false;
    std::function<void()> action_;
    MyBufferedChannel<T>& channel_;
    ChannelOperation op_;
    std::shared_ptr<InSelectHelper<T>> ish_;
    std::unique_ptr<T>* receiver_place_holder_;
    // synchronization
    // std::shared_ptr<std::promise<void>> waker_;
    // std::shared_ptr<std::atomic<int>> resolved_case_idx_;
    // int case_idx_;
    CAABImplementation() = default;
    // for receiver
    CAABImplementation(std::function<void()> action, MyBufferedChannel<T>& channel, ChannelOperation op,
        std::unique_ptr<T>* receiver_place_holder,
        std::shared_ptr<std::promise<void>> waker,
        std::shared_ptr<std::atomic<int>> resolved_case_idx, int case_idx):
    action_(action), channel_(channel), op_(op), ish_(std::make_shared<InSelectHelper<T>>()),
    receiver_place_holder_(receiver_place_holder) {
        ish_->waker_ = waker;
        ish_->resolved_case_idx_ = resolved_case_idx;
        ish_->tid_ = std::this_thread::get_id();
        ish_->case_id_ = case_idx;
    }
    // for sender
    template<typename U>
    CAABImplementation(std::function<void()> action, MyBufferedChannel<T>& channel, ChannelOperation op,
        U&& sender_val,
        std::shared_ptr<std::promise<void>> waker,
        std::shared_ptr<std::atomic<int>> resolved_case_idx, int case_idx):
    action_(action), channel_(channel), op_(op), ish_(std::make_shared<InSelectHelper<T>>()),
    receiver_place_holder_(nullptr) {
        ish_->waker_ = waker;
        ish_->resolved_case_idx_ = resolved_case_idx;
        ish_->tid_ = std::this_thread::get_id();
        ish_->case_id_ = case_idx;
        ish_->value_holder_ = std::make_unique<T>(std::forward<U>(sender_val));
    }

    CAABImplementation(CAABImplementation&& another) noexcept
      : action_(std::move(another.action_)),
        channel_(another.channel_),
        op_(another.op_),
        ish_(std::move(another.ish_)),
        receiver_place_holder_(another.receiver_place_holder_) {}

    bool tryChannelOp() override {
        switch (op_) {
            case SEND: {
                assert(ish_->value_holder_ != nullptr);
                T obj(std::move(*ish_->value_holder_));
                bool success = channel_.tryPush(std::move(obj));
                if(!success) {
                    ish_->value_holder_ = std::make_unique<T>(std::move(obj));
                    return false;
                }
                return true;
            }
            case RECEIVE: {
                return channel_.tryPop(receiver_place_holder_);
            }
        }
        return false;
    }
    std::unique_lock<std::mutex> lockChannel() override {
        return channel_.unique_lock();
    }
    void registerIntoChannel() override {
        switch (op_) {
            case SEND: {
                // channel must be full
                channel_.registerInProducer(ish_);
                break;
            }
            case RECEIVE: {
                // channel must be empty
                channel_.registerInConsumer(ish_);
                break;
            }
        }
    }
    void cleanUp() override {
        channel_.clean_queue_with_tid(ish_->tid_);
    }

    void takeAction() override {
        if(op_ == RECEIVE) {
            assert(receiver_place_holder_ != nullptr);
            if(ish_->value_holder_ != nullptr) {
                *receiver_place_holder_ = std::move(ish_->value_holder_);
            } else {
                // receive from a closed channel, set to default value(golang's behavior)
                *receiver_place_holder_ = std::make_unique<T>();
            }
        }
        action_();
    }

    int64_t channel_id() override {
        return channel_.get_channel_id();
    }
};

// type erasure type
struct CaseArmAndBranch {
    std::unique_ptr<CAABInterface> content_;
    template <typename T>
    CaseArmAndBranch(std::function<void()> action, MyBufferedChannel<T>& channel,
            std::unique_ptr<T>* receiver_place_holder,
            std::shared_ptr<std::promise<void>> waker,
            std::shared_ptr<std::atomic<int>> resolved_case_idx, int case_idx): content_(std::make_unique<CAABImplementation<T>>(
                action, channel, RECEIVE, receiver_place_holder, waker, resolved_case_idx, case_idx)) {}

    template <typename T, typename U>
    CaseArmAndBranch(std::function<void()> action, MyBufferedChannel<T>& channel,
            U&& sender_value,
            std::shared_ptr<std::promise<void>> waker,
            std::shared_ptr<std::atomic<int>> resolved_case_idx, int case_idx):
            content_(std::make_unique<CAABImplementation<T>>(
                action, channel, SEND, std::forward<U>(sender_value), waker, resolved_case_idx, case_idx)) {}


    bool tryChannelOp() {
        return content_->tryChannelOp();
    }
    std::unique_lock<std::mutex> lockChannel() {
        return content_->lockChannel();
    }
    void registerIntoChannel() {
        content_->registerIntoChannel();
    }
    void cleanUp() {
        content_->cleanUp();
    }

    void takeAction() {
        content_->takeAction();
    }
};

struct UniqueLocksGuard {
    std::vector<std::unique_lock<std::mutex>> locks_;
    UniqueLocksGuard() {}
    ~UniqueLocksGuard() {
        for(int i=static_cast<int>(locks_.size())-1; i>=0; --i) {
            if(locks_[i].owns_lock()) {
                locks_[i].unlock();
            }
        }
    }
    void addUniqueLock(std::unique_lock<std::mutex>&& lck) {
        locks_.push_back(std::move(lck));
    }

    void lockAll() {
        for(int i=0;i<locks_.size();++i) {
            locks_[i].lock();
        }
    }

    void inverselyUnlockAll() {
        for(int i=static_cast<int>(locks_.size())-1; i>=0; --i) {
            assert(locks_[i].owns_lock());
            locks_[i].unlock();
        }
    }
};

class MySelect {
    // all cases
    std::vector<CaseArmAndBranch> cases_;
    // default operation
    std::function<void()> default_action_;
    bool register_finished_;
    std::shared_ptr<std::atomic<int>> action_idx_;
    std::shared_ptr<std::promise<void>> waker_;
    std::future<void> wait_for_;
public:
    MySelect(): register_finished_(false), action_idx_(std::make_shared<std::atomic<int>>(-1)),
    waker_(std::make_shared<std::promise<void>>()), wait_for_(waker_->get_future()) {

    }

    MySelect(const MySelect&) = delete;
    MySelect& operator=(const MySelect&) = delete;
    MySelect(MySelect&& another) noexcept:
        cases_(std::move(another.cases_)),
        register_finished_(another.register_finished_),
        action_idx_(std::move(another.action_idx_)),
        waker_(std::move(another.waker_)),
        wait_for_(std::move(another.wait_for_))
        {}
    MySelect& operator=(MySelect&& another) noexcept {
        if(this == &another) {
            return *this;
        }
        cases_ = std::move(another.cases_);
        register_finished_ = another.register_finished_;
        action_idx_ = std::move(another.action_idx_);
        waker_ = std::move(another.waker_);
        wait_for_ = std::move(another.wait_for_);
        return *this;
    }


    template <typename T>
    void addReceiveCase(MyBufferedChannel<T>& channel, std::unique_ptr<T>* value_holder, std::function<void()> action) {
        if(register_finished_) {
            throw std::runtime_error("attempting to register a case after wait");
        }
        cases_.emplace_back(action, channel, value_holder, waker_, action_idx_, cases_.size());
    }

    template <typename T, typename U>
    void addSendCase(MyBufferedChannel<T>& channel, U&& value, std::function<void()> action) {
        if(register_finished_) {
            throw std::runtime_error("attempting to register a case after wait");
        }
        cases_.emplace_back(action, channel, std::forward<U>(value), waker_, action_idx_, cases_.size());
    }

    template <typename T>
    void addDefaultCase(std::function<void()> action) {
       // you can only add one default operation
        if(default_action_) {
            throw std::runtime_error("multiple default operation detected in select clause");
        }
        default_action_ = action;
    }


    void wait() {
        register_finished_ = true;
        std::map<uint64_t, int> channel_id_2_case_idx;
        // step 1: lock all channels in certain order, be care of duplications
        for(int i=0;i<cases_.size();++i) {
            channel_id_2_case_idx[cases_[i].content_->channel_id()] = i;
        }
        UniqueLocksGuard guard;
        for(auto it=channel_id_2_case_idx.begin(); it!=channel_id_2_case_idx.end();++it) {
            guard.addUniqueLock(cases_[it->second].lockChannel());
        }
        // step 2: try each channel operations
        for(int i = 0; i<cases_.size();++i) {
            if(cases_[i].tryChannelOp()) {
                cases_[i].takeAction();
                return;
            }
        }
        // check if there is a default clause
        if(default_action_) {
            default_action_();
            return;
        }
        // step 3: add the current thread into all channels
        for(int i = 0; i<cases_.size();++i) {
            cases_[i].registerIntoChannel();
        }
        // unlocks locks
        guard.inverselyUnlockAll();
        // sent itself to sleep
        wait_for_.get();
        // action idx must have been set up
        int triggered_idx = action_idx_->load();
        assert(triggered_idx >= 0);
        // regain all locks
        guard.lockAll();
        // degister itself from failed contenders
        for(int i=0;i<cases_.size();++i) {
            // we do not skip i == triggered_idx because maybe the current thread is duplicated in waiting queues
            cases_[i].cleanUp();
        }
        // execute action
        cases_[triggered_idx].takeAction();
    }
};

#endif //MY_SELECT_H
