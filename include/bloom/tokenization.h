#ifndef __BLOOM_H_TOKENIZATION__
#define __BLOOM_H_TOKENIZATION__
#include <cstddef>
#include <cstdint>
#include <bloom/array.h>
#include <bloom/allocation.h>
#include <bloom/string.h>

enum class TokenType : uint8_t {
    UNKNOWN = 0,
    ARROW,
    CONST_DEF,
    END,
    IDENTIFIER,
    INDENT,
    INTEGER_LITERAL,
    KEYWORD_PASS,
    KEYWORD_PROC,
    VAR_DEF,
    ADD = '+',
    BRACE_CLOSE = '}',
    BRACE_OPEN = '{',
    COMMA = ',',
    NEWLINE = '\n',
    PARENTHESIS_CLOSE = ')',
    PARENTHESIS_OPEN = '(',
    TYPE_SEPARATOR = ':',
};

const auto TOKEN_KEYWORD_PASS = "pass";
const auto TOKEN_KEYWORD_PROC = "proc";

struct Token {
    TokenType type;
    union {
        struct {
            String content;
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
    };
};
static_assert(sizeof(Token) == 24, "Token size is not 24 bytes");

constexpr auto to_string(TokenType type) -> String {
    #define STR(x) String::from_null_terminated_str(x)
    switch (type) {
        case TokenType::ADD:               return STR("+");
        case TokenType::ARROW:             return STR("->");
        case TokenType::BRACE_CLOSE:       return STR("}");
        case TokenType::BRACE_OPEN:        return STR("{");
        case TokenType::COMMA:             return STR(",");
        case TokenType::CONST_DEF:         return STR("const_def");
        case TokenType::END:               return STR("end");
        case TokenType::IDENTIFIER:        return STR("identifier");
        case TokenType::INDENT:            return STR("indent");
        case TokenType::INTEGER_LITERAL:   return STR("integer_literal");
        case TokenType::KEYWORD_PASS:      return STR(TOKEN_KEYWORD_PASS);
        case TokenType::KEYWORD_PROC:      return STR(TOKEN_KEYWORD_PROC);
        case TokenType::NEWLINE:           return STR("newline");
        case TokenType::PARENTHESIS_CLOSE: return STR(")");
        case TokenType::PARENTHESIS_OPEN:  return STR("(");
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
auto tokenize(String *input, ArenaAllocator *allocator) -> Array<Token>;

#endif // __BLOOM_H_TOKENIZATION__
