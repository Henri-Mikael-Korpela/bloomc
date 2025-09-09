#include <cstring>
#include "string.h"

String String::from_null_terminated_str(char *value) {
    String str;
    str.length = strlen(value);
    str.data = value;
    return str;
}
