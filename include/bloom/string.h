#ifndef __BLOOM_H_STRING__
#define __BLOOM_H_STRING__
#include <cstddef>
#include <cstdio>
#include <bloom/allocation.h>

struct Str;

/**
 * Represents a dynamically resizable string.
 */
struct DynamicStr {
    char *data;
    size_t length;
    size_t max_length;
};

/**
 * Represents a string, whose length is determined
 * by a length field instead of null-termination.
 */
struct Str {
    char const *data;
    size_t length;

    auto operator==(char const *value) -> bool;
};
static_assert(sizeof(Str) == 16, "String size is not 16 bytes");

extern auto str_char_at(Str *str, size_t index) -> char;
extern auto str_contains(Str *str, char c) -> bool;
extern auto str_from_data_and_length(char const *data, size_t length) -> Str;
extern auto str_from_cstr(char const *value) -> Str;

/**
 * Pushes a character value to the end of a dynamic string.
 * @return Length increase after pushing the value (always 1 for a single char).
 */
extern auto str_push(DynamicStr *str, char value) -> size_t;
/**
 * Pushes a string value to the end of a dynamic string.
 * @return Length increase after pushing the value.
 */
extern auto str_push(DynamicStr *str, Str *value) -> size_t;
/**
 * Pushes a string value to the end of a dynamic string.
 * @return Length increase after pushing the value.
 */
extern auto str_push(DynamicStr *str, Str &&value) -> size_t;
/**
 * Pushes a null-terminated C-string value to the end of a dynamic string.
 * @return Length increase after pushing the value.
 */
inline auto str_push(DynamicStr *str, char const *value) -> size_t {
    return str_push(str, str_from_cstr(value));
}

// Add support for printing String values
extern void print_value(FILE *file, Str const &value);

#endif // __BLOOM_H_STRING__
