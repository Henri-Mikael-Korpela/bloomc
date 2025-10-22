#ifndef __BLOOM_H_PARSING__
#define __BLOOM_H_PARSING__
#include <bloom/array.h>
#include <bloom/string.h>
#include <bloom/tokenization.h>

enum class ASTNodeType : uint8_t {
    UNKNOWN = 0,
    BINARY_ADD,
    PROC_DEF,
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
            String name;
            Array<ProcParameterASTNode> parameters;
            TypeASTNode *return_type;
            Array<ASTNode> body;
        } proc_def;
    };
};

extern auto parse(Array<Token> *tokens, ArenaAllocator *allocator) -> Array<ASTNode>;

constexpr auto to_string(ASTNodeType type) -> String {
    #define STR(x) String::from_null_terminated_str(x)
    switch (type) {
        case ASTNodeType::BINARY_ADD: return STR("binary_add");
        case ASTNodeType::PROC_DEF:   return STR("procedure definition");
        default:                      return STR("undefined");
    }
    #undef STR
}

#endif // __BLOOM_H_PARSING__
