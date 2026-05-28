#pragma once
#include <bloom/array.h>
#include <bloom/string.h>
#include <bloom/tokenization.h>

enum class DeducedType : uint8_t {
    UNKNOWN = 0,
    BOOLEAN,
    INTEGER,
};

constexpr auto to_string(DeducedType type) -> Str {
    #define STR(x) str_from_cstr(x)
    switch (type) {
        case DeducedType::BOOLEAN: return STR("boolean");
        case DeducedType::INTEGER: return STR("integer");
        default:                   return STR("unknown");
    }
    #undef STR
}

enum class ASTNodeType : uint8_t {
    UNKNOWN = 0,
    BINARY_ADD,
    BOOLEAN_LITERAL,
    IDENTIFIER,
    INTEGER_LITERAL,
    PASS,
    PROC_CALL,
    PROC_DEF,
    RETURN,
    STRING_LITERAL,
    VARIABLE_DEFINITION,
};

enum class BinaryOperatorType : uint8_t {
    ADD = '+',
};

struct IntegerLiteralASTNode {
    union {
        int64_t value;
        uint64_t uvalue;
    };
};

struct BinaryOperand {
    bool is_identifier;
    union {
        Str identifier;
        IntegerLiteralASTNode integer_literal;
    };
};

struct ProcParameterASTNode {
    Str name;
};

struct TypeASTNode {
    Str name;
};

struct ASTNode {
    ASTNodeType type;
    ASTNode *parent;
    union {
        struct {
            BinaryOperatorType oprt;
            BinaryOperand left;
            BinaryOperand right;
        } binary_operation;
        Str identifier;
        struct {
            bool value;
        } boolean_literal;
        struct {
            IntegerLiteralASTNode value;
        } integer_literal;
        struct {
            Array<ASTNode> arguments;
            Str caller_identifier;
        } proc_call;
        struct {
            Str name;
            Array<ProcParameterASTNode> parameters;
            TypeASTNode *return_type;
            Array<ASTNode> body;
        } proc_def;
        ASTNode *return_value;
        struct {
            Str value;
        } string_literal;
        struct {
            Str name;
            DeducedType deduced_type;
            ASTNodeType expr_type;
            union {
                IntegerLiteralASTNode integer_value;
                bool boolean_value;
                struct {
                    BinaryOperand left;
                    BinaryOperand right;
                } add_expr;
            };
        } variable_definition;
    };
};

auto parse(Array<Token> *tokens, ArenaAllocator *allocator) -> Array<ASTNode>;

constexpr auto to_string(ASTNodeType type) -> Str {
    #define STR(x) str_from_cstr(x)
    switch (type) {
        case ASTNodeType::BINARY_ADD:          return STR("binary_add");
        case ASTNodeType::BOOLEAN_LITERAL:     return STR("boolean_literal");
        case ASTNodeType::IDENTIFIER:          return STR("identifier");
        case ASTNodeType::INTEGER_LITERAL:     return STR("integer_literal");
        case ASTNodeType::PASS:                return STR("pass");
        case ASTNodeType::PROC_CALL:           return STR("procedure call");
        case ASTNodeType::PROC_DEF:            return STR("procedure definition");
        case ASTNodeType::RETURN:              return STR("return");
        case ASTNodeType::STRING_LITERAL:      return STR("string_literal");
        case ASTNodeType::VARIABLE_DEFINITION: return STR("variable_definition");
        default:                               return STR("undefined");
    }
    #undef STR
}
