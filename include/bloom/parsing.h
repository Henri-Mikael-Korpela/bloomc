#pragma once
#include <bloom/array.h>
#include <bloom/string.h>
#include <bloom/tokenization.h>

enum class DeducedType : uint8_t {
    UNKNOWN = 0,
    ARRAY_INT,
    BOOLEAN,
    INTEGER,
};

constexpr auto to_string(DeducedType type) -> Str {
    #define STR(x) cstr_to_str(x)
    switch (type) {
        case DeducedType::ARRAY_INT: return STR("array_int");
        case DeducedType::BOOLEAN:   return STR("boolean");
        case DeducedType::INTEGER:   return STR("integer");
        default:                   return STR("unknown");
    }
    #undef STR
}

enum class ASTNodeType : uint8_t {
    UNKNOWN = 0,
    ARRAY_ACCESS,
    ARRAY_INIT,
    BINARY_ADD,
    BOOLEAN_LITERAL,
    IF_ELSE,
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

struct ConditionOperand {
    bool is_identifier;
    union {
        Str identifier;
        IntegerLiteralASTNode integer_literal;
    };
};

struct ASTNode;

enum class BinaryOperandType : uint8_t {
    IDENTIFIER,
    INTEGER_LITERAL,
    PROC_CALL,
};

struct BinaryOperand {
    BinaryOperandType type;
    union {
        Str identifier;
        IntegerLiteralASTNode integer_literal;
        struct {
            Str caller_identifier;
            Array<ASTNode> arguments;
        } proc_call;
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
            Str variable_name;
            int64_t index;
        } array_access;
        struct {
            Str element_type;
            Array<int64_t> elements;
        } array_init;
        struct {
            BinaryOperatorType oprt;
            Array<BinaryOperand> operands;
        } binary_operation;
        Str identifier;
        struct {
            bool value;
        } boolean_literal;
        struct {
            IntegerLiteralASTNode value;
        } integer_literal;
        struct {
            ConditionOperand condition_left;
            ConditionOperand condition_right;
            Array<ASTNode> then_body;
            Array<ASTNode> else_body;
        } if_else;
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
                Array<BinaryOperand> add_expr;
                struct {
                    Str caller_identifier;
                    Array<ASTNode> arguments;
                } proc_call_expr;
                struct {
                    Str element_type;
                    Array<int64_t> elements;
                } array_init_expr;
            };
        } variable_definition;
    };
};

auto parse(Array<Token> *tokens, ArenaAllocator *allocator) -> Array<ASTNode>;

constexpr auto to_string(ASTNodeType type) -> Str {
    #define STR(x) cstr_to_str(x)
    switch (type) {
        case ASTNodeType::ARRAY_ACCESS:        return STR("array_access");
        case ASTNodeType::ARRAY_INIT:          return STR("array_init");
        case ASTNodeType::BINARY_ADD:          return STR("binary_add");
        case ASTNodeType::BOOLEAN_LITERAL:     return STR("boolean_literal");
        case ASTNodeType::IF_ELSE:             return STR("if_else");
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
