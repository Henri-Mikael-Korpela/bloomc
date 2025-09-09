#ifndef __BLOOM_H_TOKENIZATION__
#define __BLOOM_H_TOKENIZATION__
#include <cstddef>
#include <cstdint>
#include "array.h"
#include "allocation.h"
#include "string.h"

enum class TokenType : uint8_t {
    UNKNOWN = 0,
    ARROW,
    CONST_DEF,
    END,
    IDENTIFIER,
    INDENT,
    INTEGER_LITERAL,
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

constexpr String to_string(TokenType type) {
    #define STR(x) String::from_null_terminated_str(const_cast<char*>(x))
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
Array<Token> tokenize(String *input, ArenaAllocator *allocator);

#endif // __BLOOM_H_TOKENIZATION__
