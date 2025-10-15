//
// Created by Charles Green on 6/25/25.
//

#ifndef MY_DEFER_H
#define MY_DEFER_H
#include <utility>

template <typename Callable>
class Defer {
public:
    explicit Defer(Callable&& cbl): func_(std::forward<Callable>(cbl)) {}
    ~Defer() {
        func_();
    }
    Defer() = delete;
    Defer(const Defer&) = delete;
    Defer& operator=(const Defer&) = delete;
    Defer(const Defer&&) = delete;
    Defer& operator=(const Defer&&) = delete;
private:
    Callable func_;
};

// Macro utilities
#define CONCATENATE_DETAIL(x, y) x##y
#define CONCATENATE(x, y) CONCATENATE_DETAIL(x, y)
#define DEFER(code) \
auto CONCATENATE(_defer_, __COUNTER__) = Defer([&](){code;})

#endif //MY_DEFER_H
