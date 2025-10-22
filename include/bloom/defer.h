#ifndef __BLOOM_H_DEFER__
#define __BLOOM_H_DEFER__
#include <functional>

class Defer {
    std::function<void()> func;
public:
    Defer(std::function<void()> &&func) : func(std::move(func)) {}
    ~Defer() { func(); }
};

#define _BLOOM_DEFER_CONCAT_INNER(a, b) a##b
#define _BLOOM_DEFER_CONCAT(a, b) _BLOOM_DEFER_CONCAT_INNER(a, b)

/**
 * Defers the execution of a statement until the end of the current scope.
 */
#define defer(statement) \
    Defer _BLOOM_DEFER_CONCAT(_defer_, __COUNTER__)([&](){ statement; })

#endif // __BLOOM_H_DEFER__
