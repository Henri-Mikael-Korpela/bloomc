#ifndef __BLOOM_H_STRING__
#define __BLOOM_H_STRING__
#include <cstddef>
#include <cstdio>
#include <bloom/allocation.h>

struct String;

/**
 * Represents a dynamically resizable string.
 */
struct DynamicString {
    char *data;
    size_t length;
    size_t max_length;
};

/**
 * Represents a string, whose length is determined
 * by a length field instead of null-termination.
 */
struct String {
    char const *data;
    size_t length;

    static String from_data_and_length(char const *data, size_t length);
    static String from_null_terminated_str(char const *value);
};
static_assert(sizeof(String) == 16, "String size is not 16 bytes");

extern auto char_at(String *str, size_t index) -> char;
extern auto compare_str(String *a, char const *b) -> bool;
extern auto contains_str(String *str, char c) -> bool;

// Add support for printing String values
extern void print_value(FILE *file, String const &value);

/**
 * Pushes a character value to the end of a dynamic string.
 * @return Length increase after pushing the value (always 1 for a single char).
 */
extern auto push_str(DynamicString *str, char value) -> size_t;
/**
 * Pushes a string value to the end of a dynamic string.
 * @return Length increase after pushing the value.
 */
extern auto push_str(DynamicString *str, String *value) -> size_t;
/**
 * Pushes a string value to the end of a dynamic string.
 * @return Length increase after pushing the value.
 */
extern auto push_str(DynamicString *str, String &&value) -> size_t;
/**
 * Pushes a null-terminated C-string value to the end of a dynamic string.
 * @return Length increase after pushing the value.
 */
inline auto push_str(DynamicString *str, char const *value) -> size_t {
    return push_str(str, String::from_null_terminated_str(value));
}

#endif // __BLOOM_H_STRING__
