#pragma once
#include <cstddef>
#include <cstdint>
#include <bloom/array.h>
#include <bloom/string.h>
#include <bloom/tokenization.h>

enum class ParseErrorCode {
    UNEXPECTED_TOKEN,
    ARRAY_INDEX_OUT_OF_BOUNDS,
    ARRAY_LENGTH_MISMATCH,
    BOOL_IN_ADDITION,
    PROC_ARG_TYPE_MISMATCH,
    STRUCT_MISSING_FIELDS,
    STRUCT_DUPLICATE_FIELD,
};

constexpr auto to_string(ParseErrorCode code) -> char const * {
    switch (code) {
        case ParseErrorCode::UNEXPECTED_TOKEN:          return "unexpected token";
        case ParseErrorCode::ARRAY_INDEX_OUT_OF_BOUNDS: return "array index out of bounds";
        case ParseErrorCode::ARRAY_LENGTH_MISMATCH:     return "array length mismatch";
        case ParseErrorCode::BOOL_IN_ADDITION:          return "bool in addition";
        case ParseErrorCode::PROC_ARG_TYPE_MISMATCH:    return "proc arg type mismatch";
        case ParseErrorCode::STRUCT_MISSING_FIELDS:     return "struct missing fields";
        case ParseErrorCode::STRUCT_DUPLICATE_FIELD:    return "struct duplicate field";
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
    Str expected_type_name;
    Str actual_type_name;
    Str param_name;
    // For STRUCT_MISSING_FIELDS (supports up to 8 struct fields)
    Str struct_type_name;
    Str struct_field_names[8];
    Str struct_field_type_names[8];
    size_t struct_field_count;
    bool struct_field_is_missing[8];
    // For STRUCT_DUPLICATE_FIELD
    Str duplicate_field_name;
    // For ARRAY_INDEX_OUT_OF_BOUNDS
    Str array_name;
    int64_t array_index;
    int64_t known_array_size;
};

auto report_parse_errors(Array<ParseError> errors, bool *had_errors, Str source_content, Str filename) -> void;
