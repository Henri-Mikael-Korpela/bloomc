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
    ADDRESS_OF        = '%',
    PARENTHESIS_OPEN  = '(',
    PARENTHESIS_CLOSE = ')',
    MULTIPLY          = '*',
    ADD               = '+',
    COMMA             = ',',
    SUBTRACT          = '-',
    DIVIDE            = '/',
    BRACKET_OPEN      = '[',
    BRACKET_CLOSE     = ']',
    CARET             = '^',
    BRACE_OPEN        = '{',
    BRACE_CLOSE       = '}',

    ADD_ASSIGN,
    ARROW,
    CONST_DEF,
    KEYWORD_DEFER,
    RANGE,
    RANGE_COUNTED,
    RANGE_EXCLUSIVE,
    RANGE_INCLUSIVE,
    DOT,
    END,
    EQUAL_EQUAL,
    EQUALS,
    IDENTIFIER,
    INDENT,
    INTEGER_LITERAL,
    KEYWORD_AND,
    KEYWORD_BREAK,
    KEYWORD_CONST,
    KEYWORD_ELSE,
    KEYWORD_ENUM,
    KEYWORD_FALSE,
    KEYWORD_FOR,
    KEYWORD_FOREIGN,
    KEYWORD_IF,
    KEYWORD_IN,
    KEYWORD_PASS,
    KEYWORD_PROC,
    KEYWORD_RETURN,
    KEYWORD_STRUCT,
    KEYWORD_TRUE,
    LESS_THAN,
    STRING_LITERAL,
    TYPE_SEPARATOR,
    VAR_DEF,
};

constexpr auto TOKEN_KEYWORD_AND     = "and";
constexpr auto TOKEN_KEYWORD_BREAK   = "break";
constexpr auto TOKEN_KEYWORD_DEFER   = "defer";
constexpr auto TOKEN_KEYWORD_CONST   = "const";
constexpr auto TOKEN_KEYWORD_ELSE    = "else";
constexpr auto TOKEN_KEYWORD_ENUM    = "enum";
constexpr auto TOKEN_KEYWORD_FALSE   = "false";
constexpr auto TOKEN_KEYWORD_FOR     = "for";
constexpr auto TOKEN_KEYWORD_FOREIGN = "foreign";
constexpr auto TOKEN_KEYWORD_IF      = "if";
constexpr auto TOKEN_KEYWORD_IN      = "in";
constexpr auto TOKEN_KEYWORD_PASS    = "pass";
constexpr auto TOKEN_KEYWORD_PROC    = "proc";
constexpr auto TOKEN_KEYWORD_RETURN  = "return";
constexpr auto TOKEN_KEYWORD_STRUCT  = "struct";
constexpr auto TOKEN_KEYWORD_TRUE    = "true";

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
        case TokenType::SUBTRACT:          return STR("-");
        case TokenType::DIVIDE:            return STR("/");
        case TokenType::MULTIPLY:          return STR("*");
        case TokenType::ADDRESS_OF:        return STR("%");
        case TokenType::ADD_ASSIGN:        return STR("+=");
        case TokenType::ARROW:             return STR("->");
        case TokenType::BRACE_CLOSE:       return STR("}");
        case TokenType::BRACE_OPEN:        return STR("{");
        case TokenType::BRACKET_CLOSE:     return STR("]");
        case TokenType::BRACKET_OPEN:      return STR("[");
        case TokenType::CARET:             return STR("^");
        case TokenType::COMMA:             return STR(",");
        case TokenType::CONST_DEF:         return STR("const_def");
        case TokenType::KEYWORD_DEFER:     return STR(TOKEN_KEYWORD_DEFER);
        case TokenType::DOT:               return STR(".");
        case TokenType::END:               return STR("end");
        case TokenType::EQUAL_EQUAL:       return STR("==");
        case TokenType::EQUALS:            return STR("=");
        case TokenType::IDENTIFIER:        return STR("identifier");
        case TokenType::INDENT:            return STR("indent");
        case TokenType::INTEGER_LITERAL:   return STR("integer_literal");
        case TokenType::KEYWORD_AND:       return STR(TOKEN_KEYWORD_AND);
        case TokenType::KEYWORD_BREAK:     return STR(TOKEN_KEYWORD_BREAK);
        case TokenType::KEYWORD_CONST:     return STR(TOKEN_KEYWORD_CONST);
        case TokenType::KEYWORD_ELSE:      return STR(TOKEN_KEYWORD_ELSE);
        case TokenType::KEYWORD_ENUM:      return STR(TOKEN_KEYWORD_ENUM);
        case TokenType::KEYWORD_FALSE:     return STR(TOKEN_KEYWORD_FALSE);
        case TokenType::KEYWORD_FOR:       return STR(TOKEN_KEYWORD_FOR);
        case TokenType::KEYWORD_FOREIGN:   return STR(TOKEN_KEYWORD_FOREIGN);
        case TokenType::KEYWORD_IF:        return STR(TOKEN_KEYWORD_IF);
        case TokenType::KEYWORD_IN:        return STR(TOKEN_KEYWORD_IN);
        case TokenType::KEYWORD_PASS:      return STR(TOKEN_KEYWORD_PASS);
        case TokenType::KEYWORD_PROC:      return STR(TOKEN_KEYWORD_PROC);
        case TokenType::KEYWORD_RETURN:    return STR(TOKEN_KEYWORD_RETURN);
        case TokenType::KEYWORD_STRUCT:    return STR(TOKEN_KEYWORD_STRUCT);
        case TokenType::KEYWORD_TRUE:      return STR(TOKEN_KEYWORD_TRUE);
        case TokenType::LESS_THAN:         return STR("<");
        case TokenType::NEWLINE:           return STR("newline");
        case TokenType::PARENTHESIS_CLOSE: return STR(")");
        case TokenType::PARENTHESIS_OPEN:  return STR("(");
        case TokenType::RANGE:             return STR("..");
        case TokenType::RANGE_COUNTED:     return STR("..+");
        case TokenType::RANGE_EXCLUSIVE:   return STR("..<");
        case TokenType::RANGE_INCLUSIVE:   return STR("..=");
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
