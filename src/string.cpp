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

char char_at(String *str, size_t index) {
    assert(index < str->length && "String index out of bounds");
    return str->data[index];
}

bool str_contains(String *str, char c) {
    for (size_t i = 0; i < str->length; i++) {
        if (str->data[i] == c) {
            return true;
        }
    }
    return false;
}

void print_value(FILE *file, String const &value){
    fprintf(_bloom_test_get_file(file), "%.*s", static_cast<int>(value.length), value.data);
}