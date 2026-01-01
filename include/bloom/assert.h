#ifndef __BLOOM_H_ASSERT__
#define __BLOOM_H_ASSERT__

/**
 * Asserts and prints a formatted message before aborting on failure.
 */
#define assertf(cond, fmt, ...) \
    do { \
        if (!(cond)) { \
            printf("Assertion failed: " fmt "\n", ##__VA_ARGS__); \
            std::abort(); \
        } \
    } while (0)

#endif // __BLOOM_H_ASSERT__
