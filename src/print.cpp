#include <cassert>
#include <bloom/string.h>
#include <bloom/print.h>

auto print_value(FILE *file, char const* value) -> void {
    fprintf(_bloom_test_get_file(file), "%s", value);
}
auto print_value(FILE *file, unsigned long value) -> void {
    fprintf(_bloom_test_get_file(file), "%zu", value);
}

auto print(FILE *file, char const* format) -> void {
    for (; *format; ++format) {
        assert(*format != '%' && "Too few arguments for format string");
        fputc(*format, _bloom_test_get_file(file));
    }
}

auto eprint(char const *format) -> void {
    for (; *format; ++format) {
        assert(*format != '%' && "Too few arguments for format string");
        fputc(*format, stderr);
    }
}