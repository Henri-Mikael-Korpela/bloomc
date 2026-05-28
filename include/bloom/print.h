#ifndef __BLOOM_H_PRINT__
#define __BLOOM_H_PRINT__
#include <cassert>
#include <cstdio>
#include <utility>

static FILE *_bloom_test_output = nullptr;

inline auto _bloom_test_get_file(FILE *file) -> FILE* {
#ifdef BLOOM_MODE_DEV
    return _bloom_test_output;
#else
    return file;
#endif // BLOOM_MODE_DEV
}

auto print_value(FILE *file, char const *value) -> void;
auto print_value(FILE *file, unsigned long value) -> void;

/**
 * Prints a string to given file. The file can be stdout, stderr or some other file.
 * 
 * This function is a type safe alternative to C's printf.
 * You can use the '%' character as a placeholder
 * without typing the type of the argument.
 */
auto print(FILE *file, char const *format) -> void;
/**
 * Prints a formatted string to given file. The file can be stdout, stderr or some other file.
 * 
 * This function is a type safe alternative to C's printf.
 * You can use the '%' character as a placeholder
 * without typing the type of the argument.
 */
template <typename PointerT, typename... Args>
auto print(FILE *file, char const *format, PointerT &&value, Args &&...args) -> void {
    FILE *current_file = _bloom_test_get_file(file);
    // This involves recursion
    for (; *format; ++format) {
        if (*format == '%') {
            print_value(current_file, std::forward<PointerT>(value));
            print(current_file, format + 1, std::forward<Args>(args)...);
            return;
        }
        else {
            fputc(*format, current_file);
        }
    }
    assert(false && "Too few arguments for format string");
}
/**
 * Prints a formatted string to a standard output.
 * 
 * This function is a type safe alternative to C's printf.
 * You can use the '%' character as a placeholder
 * without typing the type of the argument.
 */
template <typename PointerT, typename... Args>
constexpr auto print(char const *format, PointerT &&value, Args &&...args) -> void {
    print(_bloom_test_get_file(stdout), format, std::forward<PointerT>(value), std::forward<Args>(args)...);
}

/**
 * Prints a string to standard error output.
 */
auto eprint(char const *format) -> void;
/**
 * Prints a formatted string to standard error output.
 * 
 * This function is a type safe alternative to C's fprintf with stderr.
 * You can use the '%' character as a placeholder
 * without typing the type of the argument.
 */
template <typename PointerT, typename... Args>
constexpr auto eprint(char const *format, PointerT &&value, Args &&...args) -> void {
    print(stderr, format, std::forward<PointerT>(value), std::forward<Args>(args)...);
}

#endif // __BLOOM_H_PRINT__
