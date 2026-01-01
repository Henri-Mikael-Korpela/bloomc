#include <cassert>
#include <cstring>
#include <bloom/print.h>
#include <bloom/string.h>

String String::from_data_and_length(char const *data, size_t length) {
    return String {
        .data = data,
        .length = length,
    };
}
String String::from_null_terminated_str(char const* value) {
    String str;
    str.length = strlen(value);
    str.data = value;
    return str;
}

auto String::operator==(char const *value) -> bool {
    size_t b_length = strlen(value);
    if (this->length != b_length) {
        return false;
    }
    return strncmp(this->data, value, this->length) == 0;
}

auto char_at(String *str, size_t index) -> char {
    assert(index < str->length && "String index out of bounds");
    return str->data[index];
}

auto contains_str(String *str, char c) -> bool {
    for (size_t i = 0; i < str->length; i++) {
        if (str->data[i] == c) {
            return true;
        }
    }
    return false;
}

auto push_str(DynamicString *str, char value) -> size_t {
    assert (str->length + 1 < str->max_length &&
        "Not enough space in DynamicString to push new value");
    str->data[str->length] = value;
    str->length += 1;
    return 1;
}
auto push_str(DynamicString *str, String *value) -> size_t {
    size_t const value_len = value->length;
    assert (str->length + value_len < str->max_length &&
        "Not enough space in DynamicString to push new value");
    memcpy(str->data + str->length, value->data, value_len * sizeof(char));
    str->length += value_len;
    return value_len;
}

auto push_str(DynamicString *str, String &&value) -> size_t {
    return push_str(str, &value);
}

auto print_value(FILE *file, String const &value) -> void {
    fprintf(_bloom_test_get_file(file), "%.*s", static_cast<int>(value.length), value.data);
}
