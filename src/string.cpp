#include <cassert>
#include <cstring>
#include <bloom/print.h>
#include <bloom/string.h>

auto Str::operator==(char const *value) -> bool {
    size_t b_length = strlen(value);
    if (this->length != b_length) {
        return false;
    }
    return strncmp(this->data, value, this->length) == 0;
}

auto str_char_at(Str *str, size_t index) -> char {
    assert(index < str->length && "String index out of bounds");
    return str->data[index];
}

auto str_contains(Str *str, char c) -> bool {
    for (size_t i = 0; i < str->length; i++) {
        if (str->data[i] == c) {
            return true;
        }
    }
    return false;
}

auto str_from_data_and_length(char const *data, size_t length) -> Str {
    return Str {
        .data = data,
        .length = length,
    };
}

auto str_from_cstr(char const* value) -> Str {
    Str str;
    str.length = strlen(value);
    str.data = value;
    return str;
}

auto str_slice(Str *str, size_t begin, size_t length) -> Str {
    assert(begin + length <= str->length && "Slice out of bounds");
    return str_from_data_and_length(str->data + begin, length);
}

auto str_push(DynamicStr *str, char value) -> size_t {
    assert (str->length + 1 < str->max_length &&
        "Not enough space in DynamicString to push new value");
    str->data[str->length] = value;
    str->length += 1;
    return 1;
}
auto str_push(DynamicStr *str, Str *value) -> size_t {
    size_t const value_len = value->length;
    assert (str->length + value_len < str->max_length &&
        "Not enough space in DynamicString to push new value");
    memcpy(str->data + str->length, value->data, value_len * sizeof(char));
    str->length += value_len;
    return value_len;
}

auto str_push(DynamicStr *str, Str &&value) -> size_t {
    return str_push(str, &value);
}

auto str_push_int(DynamicStr *str, intmax_t value) -> size_t {
    char buf[32] = {0};
    int written = snprintf(buf, sizeof(buf), "%jd", value);
    assert(written > 0 && "Failed to convert integer to string");
    return str_push(str, str_from_data_and_length(buf, static_cast<size_t>(written)));
}

auto str_push_bool(DynamicStr *str, bool value) -> size_t {
    if (value) {
        return str_push(str, "true");
    }
    return str_push(str, "false");
}

auto print_value(FILE *file, Str const &value) -> void {
    fprintf(_bloom_test_get_file(file), "%.*s", static_cast<int>(value.length), value.data);
}
