#ifndef __BLOOM_H_ASSERT__
#define __BLOOM_H_ASSERT__

#if !defined(NDEBUG)
#   define ASSERTIONS_ENABLED 1
#else
#   define ASSERTIONS_ENABLED 0
#endif

/**
 * Asserts and prints a formatted message before aborting on failure.
 * @note Uses custom print function for formatting instead of C standard library printf.
 */
#define assertf(cond, fmt, ...) \
    do { \
        if (!(cond)) { \
            print("Assertion failed: " fmt "\n", ##__VA_ARGS__); \
            std::abort(); \
        } \
    } while (0)

#endif // __BLOOM_H_ASSERT__
