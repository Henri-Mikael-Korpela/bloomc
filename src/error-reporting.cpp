#include <cstdio>
#include <bloom/error-reporting.h>

static constexpr char const *ANSI_RESET  = "\033[0m";
static constexpr char const *ANSI_RED    = "\033[91m";
static constexpr char const *ANSI_CYAN   = "\033[96m";
static constexpr char const *ANSI_ORANGE = "\033[38;5;208m";
static constexpr char const *ANSI_BLUE   = "\033[94m";
static constexpr char const *ANSI_GRAY   = "\033[90m";

static auto get_source_line(Str content, uint64_t line_number) -> Str {
    uint64_t current_line = 1;
    size_t line_start = 0;
    for (size_t i = 0; i <= content.length; i++) {
        if (i == content.length || content.data[i] == '\n') {
            if (current_line == line_number) {
                return str_from_data_and_length(content.data + line_start, i - line_start);
            }
            current_line++;
            line_start = i + 1;
        }
    }
    return str_from_data_and_length(content.data, 0);
}

static auto count_source_lines(Str content) -> uint64_t {
    if (content.length == 0) { return 0; }
    uint64_t count = 1;
    for (size_t i = 0; i < content.length; i++) {
        if (content.data[i] == '\n') { count++; }
    }
    return count;
}

static auto emit_source_context(
    Str source_content,
    uint64_t error_line,
    uint64_t col,
    size_t underline_width,
    Token::Position brace_open_pos,
    Token::Position brace_close_pos
) -> void {
    uint64_t const total_lines = count_source_lines(source_content);
    uint64_t const first_line = error_line > 2 ? error_line - 2 : 1;
    uint64_t const last_line  = error_line + 2 <= total_lines ? error_line + 2 : total_lines;

    int gutter_digits = 0;
    for (uint64_t n = last_line; n > 0; n /= 10) { gutter_digits++; }
    if (gutter_digits == 0) { gutter_digits = 1; }
    int const gutter_width = gutter_digits + 1;

    fprintf(stderr, "%s%*s|%s\n", ANSI_CYAN, gutter_width, "", ANSI_RESET);

    for (uint64_t line = first_line; line <= last_line; line++) {
        Str const line_content = get_source_line(source_content, line);
        bool const is_error_line = (line == error_line);

        if (is_error_line) {
            fprintf(stderr, "%s%*llu |%s%.*s\n",
                ANSI_CYAN, gutter_digits, (unsigned long long)line, ANSI_RESET,
                (int)line_content.length, line_content.data);

            fprintf(stderr, "%s%*s|%s", ANSI_CYAN, gutter_width, "", ANSI_RESET);
            for (uint64_t i = 0; i < col - 1; i++) { fputc(' ', stderr); }
            fprintf(stderr, "%s", ANSI_ORANGE);
            size_t const underline = underline_width > 0 ? underline_width : 1;
            for (size_t i = 0; i < underline; i++) { fputc('^', stderr); }
            fprintf(stderr, "%s", ANSI_RESET);
            if (brace_open_pos.line == error_line && brace_close_pos.line == error_line) {
                uint64_t const gap = brace_open_pos.col - col - underline;
                for (uint64_t i = 0; i < gap; i++) { fputc(' ', stderr); }
                fprintf(stderr, "%s", ANSI_ORANGE);
                uint64_t const brace_span = brace_close_pos.col - brace_open_pos.col + 1;
                for (uint64_t i = 0; i < brace_span; i++) { fputc('^', stderr); }
                fprintf(stderr, "%s", ANSI_RESET);
            }
            fprintf(stderr, "\n");
        }
        else {
            fprintf(stderr, "%s%*llu |%.*s%s\n",
                ANSI_GRAY, gutter_digits, (unsigned long long)line,
                (int)line_content.length, line_content.data, ANSI_RESET);
        }
    }
}

static auto emit_error_location(Str filename, Str source_content, ParseError const *error) -> void {
    fprintf(stderr, "\n%sLocation:%s\n", ANSI_CYAN, ANSI_RESET);
    fprintf(stderr, "%s    In file %.*s, line %llu, column %llu:%s\n",
        ANSI_CYAN, (int)filename.length, filename.data,
        (unsigned long long)error->position.line,
        (unsigned long long)(error->position.col - 1), ANSI_RESET);
    emit_source_context(source_content, error->position.line, error->position.col,
        error->size_token_width, error->brace_open_pos, error->brace_close_pos);
}

auto report_parse_errors(Array<ParseError> errors, bool *had_errors, Str source_content, Str filename) -> void {
    if (errors.length == 0) {
        return;
    }
    if (had_errors != nullptr) {
        *had_errors = true;
    }
    fprintf(stderr, "Compilation failed:\n");
    for (size_t i = 0; i < errors.length; i++) {
        auto const *error = &errors.data[i];
        if (error->code == ParseErrorCode::ARRAY_INDEX_OUT_OF_BOUNDS) {
            fprintf(stderr, "\n%sError: Array index out of bounds:%s\n", ANSI_RED, ANSI_RESET);
            fprintf(stderr,
                "%s    Index %lld is out of bounds for array '%.*s' of size %lld.%s\n",
                ANSI_RED,
                (long long)error->array_index,
                (int)error->array_name.length, error->array_name.data,
                (long long)error->known_array_size,
                ANSI_RESET);
            emit_error_location(filename, source_content, error);
            fprintf(stderr, "\n%sHow to fix:%s\n", ANSI_BLUE, ANSI_RESET);
            fprintf(stderr,
                "%s    - Use an index between 0 and %lld (inclusive).%s\n",
                ANSI_BLUE, (long long)(error->known_array_size - 1), ANSI_RESET);
        }
        else if (error->code == ParseErrorCode::ENUM_INVALID_KEY) {
            fprintf(stderr, "\n%sError: Invalid enum key:%s\n", ANSI_RED, ANSI_RESET);
            fprintf(stderr,
                "%s    '%.*s' is not a member of enum '%.*s'.%s\n",
                ANSI_RED,
                (int)error->enum_invalid_key_name.length, error->enum_invalid_key_name.data,
                (int)error->enum_invalid_key_type.length, error->enum_invalid_key_type.data,
                ANSI_RESET);
            emit_error_location(filename, source_content, error);
            fprintf(stderr, "\n%sHow to fix:%s\n", ANSI_BLUE, ANSI_RESET);
            fprintf(stderr,
                "%s    - Use one of the valid keys of '%.*s': ",
                ANSI_BLUE,
                (int)error->enum_invalid_key_type.length, error->enum_invalid_key_type.data);
            for (size_t j = 0; j < error->enum_member_count; j++) {
                if (j != 0) { fprintf(stderr, ", "); }
                fprintf(stderr, "%.*s",
                    (int)error->enum_member_names[j].length, error->enum_member_names[j].data);
            }
            fprintf(stderr, "%s\n", ANSI_RESET);
        }
        else if (error->code == ParseErrorCode::ARRAY_LENGTH_MISMATCH) {
            fprintf(stderr, "\n%sError: Array length mismatch:%s\n", ANSI_RED, ANSI_RESET);
            fprintf(stderr,
                "%s    An explicit length of %lld was given for an array initialization"
                " but there's a total of %zu elements in the initialization.%s\n",
                ANSI_RED, (long long)error->explicit_length, error->actual_count, ANSI_RESET);
            emit_error_location(filename, source_content, error);
            fprintf(stderr, "\n%sHow to fix:%s\n", ANSI_BLUE, ANSI_RESET);
            fprintf(stderr,
                "%s    - If the list of elements is correct, change the array length of %lld"
                " to %zu to match the number of elements.%s\n",
                ANSI_BLUE, (long long)error->explicit_length, error->actual_count, ANSI_RESET);
            fprintf(stderr,
                "%s    - If the array length of %lld is correct, change the number of"
                " values to match the length of %lld.%s\n",
                ANSI_BLUE, (long long)error->explicit_length, (long long)error->explicit_length, ANSI_RESET);
        }
        else if (error->code == ParseErrorCode::BOOL_IN_ADDITION) {
            fprintf(stderr, "\n%sError: Type error in addition:%s\n", ANSI_RED, ANSI_RESET);
            fprintf(stderr, "%s    Adding an integer to a boolean is forbidden.%s\n",
                ANSI_RED, ANSI_RESET);
            emit_error_location(filename, source_content, error);
            fprintf(stderr, "\n%sHow to fix:%s\n", ANSI_BLUE, ANSI_RESET);
            fprintf(stderr,
                "%s    - Ensure both values in an addition are of the same numeric type (integer).%s\n",
                ANSI_BLUE, ANSI_RESET);
            fprintf(stderr,
                "%s    - If this is intentional, convert the boolean value to an integer (Int) before adding.%s\n",
                ANSI_BLUE, ANSI_RESET);
        }
        else if (error->code == ParseErrorCode::PROC_ARG_TYPE_MISMATCH) {
            char const *ptr_prefix = error->expected_type_is_pointer ? "^" : "";
            fprintf(stderr, "\n%sError: Type mismatch in procedure call:%s\n", ANSI_RED, ANSI_RESET);
            fprintf(stderr,
                "%s    Cannot pass a value of type '%.*s' to parameter '%.*s' which expects type '%s%.*s'.%s\n",
                ANSI_RED,
                (int)error->actual_type_name.length, error->actual_type_name.data,
                (int)error->param_name.length, error->param_name.data,
                ptr_prefix,
                (int)error->expected_type_name.length, error->expected_type_name.data,
                ANSI_RESET);
            emit_error_location(filename, source_content, error);
            fprintf(stderr, "\n%sHow to fix:%s\n", ANSI_BLUE, ANSI_RESET);
            fprintf(stderr,
                "%s    - Change the argument to be of type '%s%.*s' to match the parameter '%.*s'.%s\n",
                ANSI_BLUE,
                ptr_prefix,
                (int)error->expected_type_name.length, error->expected_type_name.data,
                (int)error->param_name.length, error->param_name.data,
                ANSI_RESET);
            fprintf(stderr,
                "%s    - If this is intentional, convert the value to '%s%.*s' before passing it.%s\n",
                ANSI_BLUE,
                ptr_prefix,
                (int)error->expected_type_name.length, error->expected_type_name.data,
                ANSI_RESET);
        }
        else if (error->code == ParseErrorCode::STRUCT_DUPLICATE_FIELD) {
            fprintf(stderr, "\n%sError: Duplicate field in struct initialization:%s\n", ANSI_RED, ANSI_RESET);
            fprintf(stderr, "%s    Field '%.*s' is initialized more than once in %.*s.%s\n",
                ANSI_RED,
                (int)error->duplicate_field_name.length, error->duplicate_field_name.data,
                (int)error->struct_type_name.length, error->struct_type_name.data,
                ANSI_RESET);
            emit_error_location(filename, source_content, error);
            fprintf(stderr, "\n%sHow to fix:%s\n", ANSI_BLUE, ANSI_RESET);
            fprintf(stderr,
                "%s    - Remove the duplicate '%.*s' entry and keep only one initialization for this field.%s\n",
                ANSI_BLUE,
                (int)error->duplicate_field_name.length, error->duplicate_field_name.data,
                ANSI_RESET);
        }
        else if (error->code == ParseErrorCode::STRUCT_MISSING_FIELDS) {
            size_t const missing_count = [&]() -> size_t {
                size_t n = 0;
                for (size_t i = 0; i < error->struct_field_count; i++) {
                    if (error->struct_field_is_missing[i]) { n++; }
                }
                return n;
            }();
            fprintf(stderr, "\n%sError: Missing fields in struct initialization:%s\n", ANSI_RED, ANSI_RESET);
            fprintf(stderr, "%s    %.*s has %zu uninitialized field%s: ",
                ANSI_RED,
                (int)error->struct_type_name.length, error->struct_type_name.data,
                missing_count,
                missing_count == 1 ? "" : "s");
            bool first_missing = true;
            for (size_t i = 0; i < error->struct_field_count; i++) {
                if (!error->struct_field_is_missing[i]) { continue; }
                if (!first_missing) { fprintf(stderr, ", "); }
                fprintf(stderr, "%.*s",
                    (int)error->struct_field_names[i].length, error->struct_field_names[i].data);
                first_missing = false;
            }
            fprintf(stderr, "%s\n", ANSI_RESET);
            emit_error_location(filename, source_content, error);
            fprintf(stderr, "\n%sStructure:%s %.*s :: struct ->\n",
                ANSI_CYAN, ANSI_RESET,
                (int)error->struct_type_name.length, error->struct_type_name.data);
            for (size_t i = 0; i < error->struct_field_count; i++) {
                if (error->struct_field_is_missing[i]) {
                    fprintf(stderr, "%s    ^   %.*s: %.*s%s\n",
                        ANSI_ORANGE,
                        (int)error->struct_field_names[i].length, error->struct_field_names[i].data,
                        (int)error->struct_field_type_names[i].length, error->struct_field_type_names[i].data,
                        ANSI_RESET);
                }
                else {
                    fprintf(stderr, "        %.*s: %.*s\n",
                        (int)error->struct_field_names[i].length, error->struct_field_names[i].data,
                        (int)error->struct_field_type_names[i].length, error->struct_field_type_names[i].data);
                }
            }
            fprintf(stderr, "\n%sHow to fix:%s\n", ANSI_BLUE, ANSI_RESET);
            fprintf(stderr,
                "%s    - Provide a value for each missing field in the initialization.%s\n",
                ANSI_BLUE, ANSI_RESET);
            fprintf(stderr,
                "%s    - If default zero-initialization is desired, use %.*s {} instead.%s\n",
                ANSI_BLUE,
                (int)error->struct_type_name.length, error->struct_type_name.data,
                ANSI_RESET);
        }
        else if (error->code == ParseErrorCode::UNEXPECTED_TOKEN) {
            Str const token_type_str = to_string(error->token_type);
            fprintf(stderr, "\n%sError: Unexpected token:%s\n", ANSI_RED, ANSI_RESET);
            fprintf(stderr, "%s    Encountered an unexpected \"%.*s\" at this location.%s\n",
                ANSI_RED, (int)token_type_str.length, token_type_str.data, ANSI_RESET);
            emit_error_location(filename, source_content, error);
        }
    }
}
