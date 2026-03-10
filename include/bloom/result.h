/**
 * Contains Rust-like Result type and related functions.
 */
#ifndef __BLOOM_H_RESULT__
#define __BLOOM_H_RESULT__

template<typename T, typename E>
struct Result {
    bool is_ok;
    union {
        T ok;
        E err;
    };
};

template<typename T, typename E>
inline auto err(E error) -> Result<T, E> {
    return Result<T, E> {
        .is_ok = false,
        .err = error,
    };
}

template<typename T, typename E>
inline auto ok(T value) -> Result<T, E> {
    return Result<T, E> {
        .is_ok = true,
        .ok = value,
    };
}

template<typename T, typename E>
inline auto is_ok(Result<T, E> const &result) -> bool {
    return result.is_ok;
}

#endif // __BLOOM_H_RESULT__
