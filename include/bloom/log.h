#ifndef __BLOOM_H_LOG__
#define __BLOOM_H_LOG__

#define log(...) \
    printf("[%s:%d] ", __FILE__, __LINE__); \
    printf(__VA_ARGS__);

#define logf(...) \
    printf("[%s:%d] ", __FILE__, __LINE__); \
    printf(__VA_ARGS__);

#endif // __BLOOM_H_LOG__
