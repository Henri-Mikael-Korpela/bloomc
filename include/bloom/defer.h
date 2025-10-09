#ifndef __BLOOM_H_DEFER__
#define __BLOOM_H_DEFER__
#include <functional>

class Defer {
    std::function<void()> func;
public:
    Defer(std::function<void()> &&func) : func(std::move(func)) {}
    ~Defer() { func(); }
};

/**
 * Defers the execution of a statement until the end of the current scope.
 */
// TODO Fix error when using multiple defer in the same scope.
// "note: ‘Defer _defer___LINE__’ previously declared here"
#define defer(statement) \
    Defer _defer_##__LINE__([&]() { statement; })

#endif // __BLOOM_H_DEFER__
