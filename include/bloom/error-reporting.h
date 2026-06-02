#pragma once
#include <cstddef>
#include <cstdint>
#include <bloom/array.h>
#include <bloom/string.h>
#include <bloom/tokenization.h>

enum class ParseErrorCode {
    UNEXPECTED_TOKEN,
    ARRAY_LENGTH_MISMATCH,
    BOOL_IN_ADDITION,
};

constexpr auto to_string(ParseErrorCode code) -> char const * {
    switch (code) {
        case ParseErrorCode::UNEXPECTED_TOKEN:      return "unexpected token";
        case ParseErrorCode::ARRAY_LENGTH_MISMATCH: return "array length mismatch";
    }
    return "unknown error";
}

struct ParseError {
    ParseErrorCode code;
    Token::Position position;
    size_t src_code_line;
    TokenType token_type;
    int64_t explicit_length;
    size_t actual_count;
    size_t size_token_width;
    Token::Position brace_open_pos;
    Token::Position brace_close_pos;
};

auto report_parse_errors(Array<ParseError> errors, bool *had_errors, Str source_content, Str filename) -> void;
