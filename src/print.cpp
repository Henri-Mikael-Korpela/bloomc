#include <cassert>
#include <bloom/string.h>
#include <bloom/print.h>

void print_value(FILE *file, char const* value) {
    fprintf(_bloom_test_get_file(file), "%s", value);
}
void print_value(FILE *file, unsigned long value) {
    fprintf(_bloom_test_get_file(file), "%zu", value);
}

void print(FILE *file, char const* format) {
    for (; *format; ++format) {
        assert(*format != '%' && "Too few arguments for format string");
        fputc(*format, _bloom_test_get_file(file));
    }
}

void eprint(char const *format) {
    for (; *format; ++format) {
        assert(*format != '%' && "Too few arguments for format string");
        fputc(*format, stderr);
    }
}