#pragma once
#include <cstddef>
#include <cstdint>
#include <bloom/array.h>
#include <bloom/allocation.h>
#include <bloom/string.h>

enum class TokenType : uint8_t {
    UNKNOWN = 0,

    // Printable characters, ASCII code in ascending order
    NEWLINE           = '\n',
    PARENTHESIS_OPEN  = '(',
    PARENTHESIS_CLOSE = ')',
    ADD               = '+',
    COMMA             = ',',
    BRACKET_OPEN      = '[',
    BRACKET_CLOSE     = ']',
    BRACE_OPEN        = '{',
    BRACE_CLOSE       = '}',

    ARROW,
    CONST_DEF,
    RANGE_EXCLUSIVE,
    END,
    EQUAL_EQUAL,
    EQUALS,
    IDENTIFIER,
    INDENT,
    INTEGER_LITERAL,
    KEYWORD_CONST,
    KEYWORD_ELSE,
    KEYWORD_FALSE,
    KEYWORD_IF,
    KEYWORD_PASS,
    KEYWORD_PROC,
    KEYWORD_TRUE,
    STRING_LITERAL,
    TYPE_SEPARATOR,
    VAR_DEF,
};

constexpr auto TOKEN_KEYWORD_CONST = "const";
constexpr auto TOKEN_KEYWORD_ELSE  = "else";
constexpr auto TOKEN_KEYWORD_FALSE = "false";
constexpr auto TOKEN_KEYWORD_IF    = "if";
constexpr auto TOKEN_KEYWORD_PASS  = "pass";
constexpr auto TOKEN_KEYWORD_PROC  = "proc";
constexpr auto TOKEN_KEYWORD_TRUE  = "true";

struct Token {
    TokenType type;
    struct Position {
        uint64_t col;
        uint64_t line;
    } position;
    union {
        struct {
            Str content;
        } identifier;
        struct {
            size_t level;
        } indent;
        struct {
            union {
                int64_t value;
                uint64_t uvalue;
            };
        } integer_literal;
        struct {
            Str content;
        } string_literal;
    };
};
static_assert(sizeof(Token) == 40, "Token size is not 40 bytes");

constexpr auto to_string(TokenType type) -> Str {
    #define STR(x) cstr_to_str(x)
    switch (type) {
        case TokenType::ADD:               return STR("+");
        case TokenType::ARROW:             return STR("->");
        case TokenType::BRACE_CLOSE:       return STR("}");
        case TokenType::BRACE_OPEN:        return STR("{");
        case TokenType::BRACKET_CLOSE:     return STR("]");
        case TokenType::BRACKET_OPEN:      return STR("[");
        case TokenType::COMMA:             return STR(",");
        case TokenType::CONST_DEF:         return STR("const_def");
        case TokenType::END:               return STR("end");
        case TokenType::EQUAL_EQUAL:       return STR("==");
        case TokenType::EQUALS:            return STR("=");
        case TokenType::IDENTIFIER:        return STR("identifier");
        case TokenType::INDENT:            return STR("indent");
        case TokenType::INTEGER_LITERAL:   return STR("integer_literal");
        case TokenType::KEYWORD_CONST:     return STR(TOKEN_KEYWORD_CONST);
        case TokenType::KEYWORD_ELSE:      return STR(TOKEN_KEYWORD_ELSE);
        case TokenType::KEYWORD_FALSE:     return STR(TOKEN_KEYWORD_FALSE);
        case TokenType::KEYWORD_IF:        return STR(TOKEN_KEYWORD_IF);
        case TokenType::KEYWORD_PASS:      return STR(TOKEN_KEYWORD_PASS);
        case TokenType::KEYWORD_PROC:      return STR(TOKEN_KEYWORD_PROC);
        case TokenType::KEYWORD_TRUE:      return STR(TOKEN_KEYWORD_TRUE);
        case TokenType::NEWLINE:           return STR("newline");
        case TokenType::PARENTHESIS_CLOSE: return STR(")");
        case TokenType::PARENTHESIS_OPEN:  return STR("(");
        case TokenType::RANGE_EXCLUSIVE:   return STR("..<");
        case TokenType::STRING_LITERAL:    return STR("string_literal");
        case TokenType::TYPE_SEPARATOR:    return STR(":");
        case TokenType::VAR_DEF:           return STR("var_def");
        default:                           return STR("undefined");
    }
    #undef STR
}

/**
 * Tokenizes the input string into an array of tokens.
 *
 * The tokens are stored in an ArenaAllocator for efficient memory management.
 */
auto tokenize(Str *input, ArenaAllocator *allocator) -> Array<Token>;
