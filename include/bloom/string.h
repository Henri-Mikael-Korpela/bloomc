#ifndef __BLOOM_H_STRING__
#define __BLOOM_H_STRING__
#include <cstddef>
#include <cstdio>

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

extern char char_at(String *str, size_t index);
extern bool str_contains(String *str, char c);

// Add support for printing String values
extern void print_value(FILE *file, String const &value);

#endif // __BLOOM_H_STRING__
