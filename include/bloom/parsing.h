#ifndef __BLOOM_H_PARSING__
#define __BLOOM_H_PARSING__
#include <bloom/array.h>
#include <bloom/string.h>
#include <bloom/tokenization.h>

enum class ASTNodeType : uint8_t {
    UNKNOWN = 0,
    PROC_DEF,
};

struct ProcParameterASTNode {
    String name;
};

struct TypeASTNode {
    String name;
};

struct ASTNode {
    ASTNodeType type;
    struct {
        String name;
        Array<ProcParameterASTNode> parameters;
        TypeASTNode *return_type;
    } proc_def;
};

extern auto parse(Array<Token> *tokens, ArenaAllocator *allocator) -> Array<ASTNode>;

constexpr auto to_string(ASTNodeType type) -> String {
    #define STR(x) String::from_null_terminated_str(x)
    switch (type) {
        case ASTNodeType::PROC_DEF: return STR("procedure definition");
        default:                    return STR("undefined");
    }
    #undef STR
}

#endif // __BLOOM_H_PARSING__
