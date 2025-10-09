#ifndef __BLOOM_H_PARSING__
#define __BLOOM_H_PARSING__
#include <bloom/array.h>
#include <bloom/string.h>
#include <bloom/tokenization.h>

enum class ASTNodeType : uint8_t {
    UNKNOWN = 0,
    PROC_DEF,
};

struct AstNodeProcParameter {
    String name;
};
static_assert(sizeof(AstNodeProcParameter) == 16, "AstNodeProcParameter size is not 16 bytes");

struct ASTNode {
    ASTNodeType type;
    struct {
        String name;
        Array<AstNodeProcParameter> parameters;
    } proc_def;
};
static_assert(sizeof(ASTNode) == 40, "ASTNode size is not 40 bytes");

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
