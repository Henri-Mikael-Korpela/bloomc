#include <bloom/string.h>

auto str_push_int(DynamicStr *str, intmax_t value) -> size_t {
    char buf[32] = {0};
    int written = snprintf(buf, sizeof(buf), "%jd", value);
    assert(written > 0 && "Failed to convert integer to string");
    return str_push(str, str_from_data_and_length(buf, static_cast<size_t>(written)));
}
