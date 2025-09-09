#ifndef __BLOOM_H_STRING__
#define __BLOOM_H_STRING__
#include <cassert>
#include <cstddef>

/**
 * Represents a string, whose length is determined
 * by a length field instead of null-termination.
 */
struct String {
    char *data;
    size_t length;

    static inline String from_data_and_length(char *data, size_t length) {
        return String {
            .data = data,
            .length = length,
        };
    }
    static String from_null_terminated_str(char *value);
};
static_assert(sizeof(String) == 16, "String size is not 16 bytes");

inline char char_at(String *str, size_t index) {
    assert(index < str->length && "String index out of bounds");
    return str->data[index];
}

#endif // __BLOOM_H_STRING__
