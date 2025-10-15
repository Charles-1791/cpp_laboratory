//
// Created by Charles Green on 7/12/25.
//

#ifndef SIMPLE_ALGORITHM_H
#define SIMPLE_ALGORITHM_H
#include <vector>
#include <future>
#include "../my_utility/my_defer.h"

template <typename Iterator, typename TargetType>
void single_thread_find(Iterator begin, Iterator end, const TargetType& target, std::atomic<bool>& stop,
    std::promise<Iterator>& ret) {
    try {
        for(;begin != end && !stop.load();++begin) {
            if(*begin == target) {
                ret.set_value(begin);
                stop.store(true);
                break;
            }
        }
    } catch (...) {
        try {
            ret.set_exception(std::current_exception());
            stop.store(true);
        } catch (...) {} // in case other thread has already set the exception
    }
}

template <typename Iterator, typename TargetType>
Iterator parallel_find(Iterator begin, Iterator end, const TargetType& target) {
    size_t element_count = std::distance(begin, end);
    constexpr size_t SMALLEST_WORKLOAD = 25;
    const size_t MAX_THREAD_COUNT = std::thread::hardware_concurrency();
    size_t workload = SMALLEST_WORKLOAD;
    size_t thread_count = 0;
    if(MAX_THREAD_COUNT == 1 || MAX_THREAD_COUNT == 0) {
        thread_count = 1;
        workload = element_count;
    } else if(MAX_THREAD_COUNT * SMALLEST_WORKLOAD >= element_count) {
        thread_count = (element_count + SMALLEST_WORKLOAD - 1) / SMALLEST_WORKLOAD;
        workload = SMALLEST_WORKLOAD;
    } else {
        thread_count = MAX_THREAD_COUNT;
        workload = (element_count + MAX_THREAD_COUNT - 1) / MAX_THREAD_COUNT;
    }
    Iterator offset = begin;
    std::promise<Iterator> ret;
    std::atomic<bool> stop;
    std::vector<std::thread> threads(thread_count-1);

    {
        DEFER(
            for(int i=0;i<threads.size();++i) {
                if(threads[i].joinable()) {
                    threads[i].join();
                }
            }
            std::cout<<"all threads have been joined\n";
        );
        for(int i=0;i<thread_count-1;++i) {
            Iterator next_offset = offset;
            std::advance(next_offset, workload);
            threads[i] = std::thread(single_thread_find<Iterator, TargetType> ,offset, next_offset, target, std::ref(stop), std::ref(ret));
            offset = next_offset;
        }
        Iterator next_offset = offset;
        std::advance(next_offset, workload);
        single_thread_find<Iterator, TargetType>(offset, next_offset, target, std::ref(stop), std::ref(ret));
    }
    // the defer is triggered, threads have been joined
    if(stop.load()) {
        // std::cout << "here\n";
        // may throw exception here
        return ret.get_future().get();
    }
    return end;
}
template <typename Iterator, typename ElementType>
void single_thread_partial_sum(Iterator begin, Iterator end, std::promise<ElementType>* wait_for,
    std::promise<ElementType>* write_to) {
    try {
        Iterator prev = begin;
        Iterator it = begin;
        ++it;
        for(; it != end; ++it) {
            *it += *prev;
            prev = it;
        }

        if(wait_for) {
            ElementType addon = wait_for->get_future().get();
            Iterator tail = prev;
            *tail += addon;
            if(write_to) {
                 write_to->set_value(*tail);
            }
            for(it = begin; it != tail; ++it) {
                *it += addon;
            }
        } else if (write_to) {
            write_to->set_value(*prev);
        }
    } catch (...) {
        if(write_to) {
            // not main thread
            write_to->set_exception(std::current_exception());
        } else {
            // main thread
            throw;
        }
    }

}


template <typename Iterator, typename ElementType>
void parallel_partial_sum(Iterator begin, Iterator end) {
    size_t element_count = std::distance(begin, end);
    constexpr size_t SMALLEST_WORKLOAD = 25;
    const size_t MAX_THREAD_COUNT = std::thread::hardware_concurrency();
    size_t workload = SMALLEST_WORKLOAD;
    size_t thread_count = 0;
    if(MAX_THREAD_COUNT == 1 || MAX_THREAD_COUNT == 0) {
        thread_count = 1;
        workload = element_count;
    } else if(MAX_THREAD_COUNT * SMALLEST_WORKLOAD >= element_count) {
        thread_count = (element_count + SMALLEST_WORKLOAD - 1) / SMALLEST_WORKLOAD;
        workload = SMALLEST_WORKLOAD;
    } else {
        thread_count = MAX_THREAD_COUNT;
        workload = (element_count + MAX_THREAD_COUNT - 1) / MAX_THREAD_COUNT;
    }
    Iterator offset = begin;
    std::vector<std::thread> threads(thread_count-1);
    std::vector<std::promise<ElementType>> promises(thread_count-1);
    {
        DEFER(
            for(int i=0;i<threads.size();++i) {
                if(threads[i].joinable()) {
                    threads[i].join();
                }
            }
            std::cout<<"all threads have been joined\n";
        );
        Iterator offset = begin;
        for(int i=0; i < thread_count - 1;++i) {
            Iterator next_offset = offset;
            std::advance(next_offset, workload);
            std::promise<ElementType>* wait_for = nullptr;
            if(i>0) {
                wait_for = &promises[i-1];
            }
            std::promise<ElementType>* write_to = &promises[i];
            threads[i] = std::thread(single_thread_partial_sum<Iterator, ElementType>, offset, next_offset, wait_for, write_to);
            offset = next_offset;
        }
        // handle first batch
        if(thread_count > 1) {
            single_thread_partial_sum<Iterator, ElementType>(offset, end, &promises[thread_count-2], nullptr);
        } else {
            single_thread_partial_sum<Iterator, ElementType>(offset, end, nullptr, nullptr);
        }
    }

}

template <typename Func>
std::future<typename std::result_of<Func()>::type> wrap_function(Func f) {
    using ret_type = typename std::result_of<Func()>::type;
    std::promise<ret_type> pms;
    ret_type tmp = f();
    pms.set_value(tmp);
    return pms.get_future();
}

#endif //SIMPLE_ALGORITHM_H
