#ifndef __BLOOM_H_PARSING__
#define __BLOOM_H_PARSING__
#include <bloom/array.h>
#include <bloom/string.h>
#include <bloom/tokenization.h>

enum class ASTNodeType : uint8_t {
    UNKNOWN = 0,
    BINARY_ADD,
    PASS,
    PROC_CALL,
    PROC_DEF,
    RETURN,
    STRING_LITERAL,
};

enum class BinaryOperatorType : uint8_t {
    ADD = '+',
};

struct ProcParameterASTNode {
    String name;
};

struct TypeASTNode {
    String name;
};

struct ASTNode {
    ASTNodeType type;
    ASTNode *parent;
    union {
        struct {
            BinaryOperatorType oprt;
            String identifier_left;
            String identifier_right;
        } binary_operation;
        struct {
            Array<ASTNode> arguments;
            String caller_identifier;
        } proc_call;
        struct {
            String name;
            Array<ProcParameterASTNode> parameters;
            TypeASTNode *return_type;
            Array<ASTNode> body;
        } proc_def;
        ASTNode *return_value;
        struct {
            String value;
        } string_literal;
    };
};

extern auto parse(Array<Token> *tokens, ArenaAllocator *allocator) -> Array<ASTNode>;

constexpr auto to_string(ASTNodeType type) -> String {
    #define STR(x) String::from_null_terminated_str(x)
    switch (type) {
        case ASTNodeType::BINARY_ADD:     return STR("binary_add");
        case ASTNodeType::PASS:           return STR("pass");
        case ASTNodeType::PROC_CALL:      return STR("procedure call");
        case ASTNodeType::PROC_DEF:       return STR("procedure definition");
        case ASTNodeType::RETURN:         return STR("return");
        case ASTNodeType::STRING_LITERAL: return STR("string_literal");
        default:                          return STR("undefined");
    }
    #undef STR
}

#endif // __BLOOM_H_PARSING__
