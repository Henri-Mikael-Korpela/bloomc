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

extern void print_value(FILE *file, char const *value);
extern void print_value(FILE *file, unsigned long value);

/**
 * Prints a string to given file. The file can be stdout, stderr or some other file.
 * 
 * This function is a type safe alternative to C's printf.
 * You can use the '%' character as a placeholder
 * without typing the type of the argument.
 */
extern void print(FILE *file, char const *format);
/**
 * Prints a formatted string to given file. The file can be stdout, stderr or some other file.
 * 
 * This function is a type safe alternative to C's printf.
 * You can use the '%' character as a placeholder
 * without typing the type of the argument.
 */
template <typename T, typename... Args>
void print(FILE *file, char const *format, T &&value, Args &&...args) {
    FILE *current_file = _bloom_test_get_file(file);
    // This involves recursion
    for (; *format; ++format) {
        if (*format == '%') {
            print_value(current_file, std::forward<T>(value));
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
template <typename T, typename... Args>
constexpr void print(char const *format, T &&value, Args &&...args) {
    print(_bloom_test_get_file(stdout), format, std::forward<T>(value), std::forward<Args>(args)...);
}

/**
 * Prints a string to standard error output.
 */
extern void eprint(char const *format);
/**
 * Prints a formatted string to standard error output.
 * 
 * This function is a type safe alternative to C's fprintf with stderr.
 * You can use the '%' character as a placeholder
 * without typing the type of the argument.
 */
template <typename T, typename... Args>
constexpr void eprint(char const *format, T &&value, Args &&...args) {
    print(stderr, format, std::forward<T>(value), std::forward<Args>(args)...);
}

#endif // __BLOOM_H_PRINT__
