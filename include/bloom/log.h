#pragma once

#define log(...) \
    printf("[%s:%d] ", __FILE__, __LINE__); \
    printf(__VA_ARGS__);

#define logf(...) \
    printf("[%s:%d] ", __FILE__, __LINE__); \
    printf(__VA_ARGS__);
