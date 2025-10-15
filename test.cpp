//
// Created by Charles Green on 4/24/25.
//

#include "test.h"

#include <algorithm>
#include <bitset>
#include <future>
#include <iostream>
#include <list>
#include <locale>
#include <map>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <vector>
#include <stack>
#include <thread>
#include <latch>
#include <barrier>
#include <unordered_map>
#include <unordered_set>
#include <future>
#include <ranges>
#include "sync_container_with_lock/my_channel/my_channel_advanced.h"
#include "sync_container_with_lock/my_select/my_select.h"
#include "nice_printer.h"
#include "my_utility/my_defer.h"
#include "sync_container_lock_free/my_lock_free_queue/my_lock_free_queue.h"
#include "multi_thread_algorithms/simple_algorithm.h"
#include "my_thread_pool/thread_pool_demo01.h"
#include <semaphore>
#include "my_utility/my_interruptible_thread.h"
using namespace std;

template<typename T>
void quickSort3Way(vector<T>& nums, int leftBound, int rightBound) {
    if(leftBound >= rightBound) {
        return;
    }
    T pivot = nums[leftBound];
    int endOfLessThan = leftBound;
    int preStartOfGreatThan = rightBound;
    int idx = leftBound + 1;
    while (idx <= preStartOfGreatThan) {
        if(nums[idx] < pivot) {
            swap(nums[endOfLessThan], nums[idx]);
            ++endOfLessThan;
            ++idx;
        } else if(nums[idx] > pivot) {
            swap(nums[preStartOfGreatThan], nums[idx]);
            --preStartOfGreatThan;
        } else {
            ++idx;
        }
    }
    quickSort3Way(nums, leftBound, endOfLessThan-1);
    quickSort3Way(nums, preStartOfGreatThan + 1, rightBound);
}

template<typename T>
void quickSort(vector<T>& nums) {
    quickSort3Way(nums, 0, nums.size()-1);
}

void printStackSize() {
    pthread_attr_t attr;
    pthread_getattr_np(pthread_self(), &attr);  // get current thread's attributes

    size_t stack_size;
    pthread_attr_getstacksize(&attr, &stack_size);

    std::cout << "Stack size: " << stack_size / 1024 << " KB\n";
}

void test1() {
    MyBufferedChannel<int> mbc1(1);
    MyBufferedChannel<double> mbc2(4);
    MyBufferedChannel<string> mbc3(5);

    MySelect ms;
    mbc1.blocking_push(0);
    ms.addSendCase(mbc1, 100, []() {
        cout << "the first select arm is triggered\n";
    });
    std::unique_ptr<double> mbc2_val;
    ms.addReceiveCase(mbc2, &mbc2_val, [&]() {
        cout << "the second select arm is triggered\n";
        cout << "the double value received is " << *mbc2_val << endl;
    });
    std::unique_ptr<string> mbc3_val;
    ms.addReceiveCase(mbc3, &mbc3_val, [&]() {
        cout << "the third select arm is triggered\n";
        cout << "the string value received is " << *mbc3_val << endl;
    });
    std::thread t1([&]() {
        sleep(5);
        mbc2.blocking_push(3.14);
    });
    std::thread t2([&]() {
        sleep(3);
        mbc3.blocking_push("hello");
    });
    cout << "before main thread waits\n";
    ms.wait();
    cout << "after main thread wakes up\n";
    t1.join();
    t2.join();
}

void test2() {
    MyBufferedChannel<int> mbc1(1);
    MyBufferedChannel<double> mbc2(4);
    MyBufferedChannel<string> mbc3(5);
    mbc1.blocking_push(0);
    std::thread t1([&]() {
        std::unique_ptr<double> mbc2_val = mbc2.blocking_pop();
        cout << "thread t1: pop "<< *mbc2_val << " from channel2\n";
    });
    std::thread t2([&]() {
        std::unique_ptr<string> mbc3_val = mbc3.blocking_pop();
        cout << "thread t2: pop "<< *mbc3_val << " from channel3\n";
    });
    sleep(2);
    MySelect ms;
    ms.addSendCase(mbc1, 100, []() {
        cout << "main thread: the first select arm is triggered\n";
    });
    std::unique_ptr<double> mbc2_val;
    ms.addSendCase(mbc2, 6.66, [&]() {
        cout << "main thread: the second select arm is triggered\n\t send 6.66 to channel 2\n";
    });
    std::unique_ptr<string> mbc3_val;
    ms.addSendCase(mbc3, "sb", [&]() {
        cout << "main thread: the third select arm is triggered\n\t send sb to channel 3\n";
    });
    std::thread t3([&]() {
        MySelect ms;
        ms.addSendCase(mbc1, 100, []() {
            cout << "t3: the first select arm is triggered\n";
        });
        // std::unique_ptr<double> mbc2_val;
        // ms.addSendCase(mbc2, 7.77, [&]() {
        //     cout << "main thread: the second select arm is triggered\n";
        //     cout << "\t send 6.66 to channel 2\n";
        // });
        std::unique_ptr<string> mbc3_val;
        ms.addSendCase(mbc3, "sb2", [&]() {
            cout << "thread t3: the third select arm is triggered\n\t send sb2 to channel 3\n";
        });
        cout << "before t3's waiting\n";
        ms.wait();
        cout << "after t3's waiting\n";
    });
    ms.wait();
    t1.join();
    t2.join();
    t3.join();
}

int main() {
    test2();
    return 0;
}