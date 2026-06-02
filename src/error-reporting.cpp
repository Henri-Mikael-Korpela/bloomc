#include <cstdio>
#include <bloom/error-reporting.h>

static constexpr char const *ANSI_RESET  = "\033[0m";
static constexpr char const *ANSI_RED    = "\033[91m";
static constexpr char const *ANSI_CYAN   = "\033[96m";
static constexpr char const *ANSI_ORANGE = "\033[38;5;208m";
static constexpr char const *ANSI_BLUE   = "\033[94m";

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

auto report_parse_errors(Array<ParseError> errors, bool *had_errors, Str source_content, Str filename) -> void {
    if (errors.length == 0) {
        return;
    }
    if (had_errors != nullptr) {
        *had_errors = true;
    }
    fprintf(stderr, "Compilation failed:\n");
    for (auto &error : errors) {
        if (error.code == ParseErrorCode::ARRAY_LENGTH_MISMATCH) {
            fprintf(stderr, "\n%sError: Array length mismatch:%s\n", ANSI_RED, ANSI_RESET);
            fprintf(stderr,
                "%s    An explicit length of %lld was given for an array initialization"
                " but there's a total of %zu elements in the initialization.%s\n",
                ANSI_RED, (long long)error.explicit_length, error.actual_count, ANSI_RESET);
            fprintf(stderr, "\n%sLocation:%s\n", ANSI_CYAN, ANSI_RESET);
            fprintf(stderr, "%s    In file %.*s, line %llu, column %llu:%s\n",
                ANSI_CYAN, (int)filename.length, filename.data,
                (unsigned long long)error.position.line,
                (unsigned long long)(error.position.col - 1), ANSI_RESET);
            Str const source_line = get_source_line(source_content, error.position.line);
            uint64_t const line_num = error.position.line;
            int gutter_digits = 0;
            for (uint64_t n = line_num; n > 0; n /= 10) { gutter_digits++; }
            int const gutter_width = gutter_digits + 1;
            fprintf(stderr, "%s%*s|%s\n", ANSI_CYAN, gutter_width, "", ANSI_RESET);
            fprintf(stderr, "%s%llu |%s%.*s\n",
                ANSI_CYAN, (unsigned long long)line_num, ANSI_RESET,
                (int)source_line.length, source_line.data);
            fprintf(stderr, "%s%*s|%s", ANSI_CYAN, gutter_width, "", ANSI_RESET);
            for (uint64_t i = 0; i < error.position.col - 1; i++) {
                fputc(' ', stderr);
            }
            fprintf(stderr, "%s", ANSI_ORANGE);
            for (size_t i = 0; i < error.size_token_width; i++) {
                fputc('^', stderr);
            }
            fprintf(stderr, "%s", ANSI_RESET);
            if (error.brace_open_pos.line == error.position.line &&
                error.brace_close_pos.line == error.position.line)
            {
                uint64_t const gap = error.brace_open_pos.col - error.position.col - error.size_token_width;
                for (uint64_t i = 0; i < gap; i++) {
                    fputc(' ', stderr);
                }
                fprintf(stderr, "%s", ANSI_ORANGE);
                uint64_t const brace_span = error.brace_close_pos.col - error.brace_open_pos.col + 1;
                for (uint64_t i = 0; i < brace_span; i++) {
                    fputc('^', stderr);
                }
                fprintf(stderr, "%s", ANSI_RESET);
            }
            fprintf(stderr, "\n\n%sHow to fix:%s\n", ANSI_BLUE, ANSI_RESET);
            fprintf(stderr,
                "%s    - If the list of elements is correct, change the array length of %lld"
                " to %zu to match the number of elements.%s\n",
                ANSI_BLUE, (long long)error.explicit_length, error.actual_count, ANSI_RESET);
            fprintf(stderr,
                "%s    - If the array length of %lld is correct, change the number of"
                " values to match the length of %lld.%s\n",
                ANSI_BLUE, (long long)error.explicit_length, (long long)error.explicit_length, ANSI_RESET);
        }
        else if (error.code == ParseErrorCode::UNEXPECTED_TOKEN) {
            Str const token_type_str = to_string(error.token_type);
            fprintf(stderr, "\n%sError: Unexpected token:%s\n", ANSI_RED, ANSI_RESET);
            fprintf(stderr, "%s    Encountered an unexpected \"%.*s\" at this location.%s\n",
                ANSI_RED, (int)token_type_str.length, token_type_str.data, ANSI_RESET);
            fprintf(stderr, "\n%sLocation:%s\n", ANSI_CYAN, ANSI_RESET);
            fprintf(stderr, "%s    In file %.*s, line %llu, column %llu:%s\n",
                ANSI_CYAN, (int)filename.length, filename.data,
                (unsigned long long)error.position.line,
                (unsigned long long)(error.position.col - 1), ANSI_RESET);
            Str const source_line = get_source_line(source_content, error.position.line);
            uint64_t const line_num = error.position.line;
            int gutter_digits = 0;
            for (uint64_t n = line_num; n > 0; n /= 10) { gutter_digits++; }
            int const gutter_width = gutter_digits + 1;
            fprintf(stderr, "%s%*s|%s\n", ANSI_CYAN, gutter_width, "", ANSI_RESET);
            fprintf(stderr, "%s%llu |%s%.*s\n",
                ANSI_CYAN, (unsigned long long)line_num, ANSI_RESET,
                (int)source_line.length, source_line.data);
            fprintf(stderr, "%s%*s|%s", ANSI_CYAN, gutter_width, "", ANSI_RESET);
            for (uint64_t i = 0; i < error.position.col - 1; i++) {
                fputc(' ', stderr);
            }
            fprintf(stderr, "%s^%s\n", ANSI_ORANGE, ANSI_RESET);
        }
    }
}
