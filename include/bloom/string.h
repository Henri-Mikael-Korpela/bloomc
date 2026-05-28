#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

    inline auto operator==(char const *value) -> bool {
        size_t b_length = strlen(value);
        if (this->length != b_length) {
            return false;
        }
        return strncmp(this->data, value, this->length) == 0;
    }
};
static_assert(sizeof(Str) == 16, "String size is not 16 bytes");

inline auto cstr_to_str(char const *value) -> Str {
    return Str {
        .data = value,
        .length = strlen(value),
    };
}

inline auto str_char_at(Str *str, size_t index) -> char {
    assert(index < str->length && "String index out of bounds");
    return str->data[index];
}

constexpr auto str_contains(Str *str, char c) -> bool {
    for (size_t i = 0; i < str->length; i++) {
        if (str->data[i] == c) {
            return true;
        }
    }
    return false;
}

constexpr auto str_from_data_and_length(char const *data, size_t length) -> Str {
    return Str {
        .data = data,
        .length = length,
    };
}

inline auto str_slice(Str *str, size_t begin, size_t length) -> Str {
    assert(begin + length <= str->length && "Slice out of bounds");
    return str_from_data_and_length(str->data + begin, length);
}

inline auto str_push(DynamicStr *str, char value) -> size_t {
    assert(str->length + 1 < str->max_length &&
        "Not enough space in DynamicString to push new value");
    str->data[str->length] = value;
    str->length += 1;
    return 1;
}

inline auto str_push(DynamicStr *str, Str value) -> size_t {
    assert(str->length + value.length < str->max_length &&
        "Not enough space in DynamicString to push new value");
    memcpy(str->data + str->length, value.data, value.length * sizeof(char));
    str->length += value.length;
    return value.length;
}

/**
 * Pushes a null-terminated C-string value to the end of a dynamic string.
 * @return Length increase after pushing the value.
 */
inline auto str_push(DynamicStr *str, char const *value) -> size_t {
    return str_push(str, cstr_to_str(value));
}

/**
 * Converts an integer to its decimal string representation and pushes it.
 * @return Length increase after pushing the value.
 */
auto str_push_int(DynamicStr *str, intmax_t value) -> size_t;

inline auto str_push_bool(DynamicStr *str, bool value) -> size_t {
    if (value) {
        return str_push(str, "true");
    }
    return str_push(str, "false");
}
