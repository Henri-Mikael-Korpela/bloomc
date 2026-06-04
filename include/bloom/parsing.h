#pragma once
#include <bloom/array.h>
#include <bloom/string.h>
#include <bloom/tokenization.h>


enum class ASTNodeType : uint8_t {
    UNKNOWN = 0,
    ARRAY_ACCESS,
    ARRAY_ELEMENT_ASSIGN,
    ARRAY_INIT,
    BINARY_ADD,
    BOOLEAN_LITERAL,
    BREAK,
    BUILTIN_LENGTH,
    CONSTANT_DEFINITION,
    BUILTIN_LENGTH_IN_BYTES,
    FOR_COND_LOOP,
    FOR_IN_LOOP,
    FOR_LOOP,
    FOR_RANGE_LOOP,
    IF_ELSE,
    IDENTIFIER,
    INTEGER_LITERAL,
    MEMBER_ACCESS,
    MEMBER_ASSIGN,
    ADD_ASSIGN,
    PASS,
    PROC_CALL,
    PROC_DEF,
    RETURN,
    STRING_LITERAL,
    STRUCT_DEF,
    STRUCT_INIT,
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
    ARRAY_ACCESS,
    MEMBER_ACCESS,
    STRING_LITERAL,
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
        struct {
            Str variable_name;
            int64_t index;
        } array_access;
        struct {
            Str object_name;
            Str field_name;
        } member_access;
        Str string_literal;
    };
};

struct ProcParameterASTNode {
    Str name;
    Str type_name;
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
            ASTNode *expr;
        } variable_definition;
        struct {
            Str name;
            int64_t value;
        } constant_def;
        struct {
            Str element_name;
            Str index_name;
            Str collection_name;
            Array<ASTNode> body;
        } for_in_loop;
        struct {
            ConditionOperand condition_left;
            ConditionOperand condition_right;
            Array<ASTNode> body;
        } for_cond_loop;
        struct {
            Array<ASTNode> body;
        } for_loop;
        struct {
            Str element_name;
            int64_t range_start;
            int64_t range_end;
            Array<ASTNode> body;
        } for_range_loop;
        struct {
            Str variable_name;
            BinaryOperand operand;
        } add_assign;
        struct {
            Str object_name;
            Str field_name;
        } member_access;
        struct {
            Str object_name;
            Str field_name;
            ASTNode *expr;
        } member_assign;
        struct {
            Str variable_name;
            int64_t index;
            ASTNode *expr;
        } array_element_assign;
        struct {
            Str name;
            Array<ProcParameterASTNode> fields;
        } struct_def;
        struct {
            Str type_name;
            Array<ProcParameterASTNode> field_names;
            Array<BinaryOperand> field_values;
        } struct_init;
    };
};

auto parse(Array<Token> *tokens, ArenaAllocator *allocator, Str source_content, Str filename, bool *had_errors) -> Array<ASTNode>;

constexpr auto to_string(ASTNodeType type) -> Str {
    #define STR(x) cstr_to_str(x)
    switch (type) {
        case ASTNodeType::ADD_ASSIGN:          return STR("add_assign");
        case ASTNodeType::ARRAY_ACCESS:          return STR("array_access");
        case ASTNodeType::ARRAY_ELEMENT_ASSIGN: return STR("array_element_assign");
        case ASTNodeType::ARRAY_INIT:           return STR("array_init");
        case ASTNodeType::BINARY_ADD:          return STR("binary_add");
        case ASTNodeType::BOOLEAN_LITERAL:     return STR("boolean_literal");
        case ASTNodeType::BREAK:               return STR("break");
        case ASTNodeType::BUILTIN_LENGTH:          return STR("builtin_length");
        case ASTNodeType::CONSTANT_DEFINITION: return STR("constant_definition");
        case ASTNodeType::BUILTIN_LENGTH_IN_BYTES: return STR("builtin_length_in_bytes");
        case ASTNodeType::FOR_COND_LOOP:       return STR("for_cond_loop");
        case ASTNodeType::FOR_IN_LOOP:         return STR("for_in_loop");
        case ASTNodeType::FOR_LOOP:            return STR("for_loop");
        case ASTNodeType::FOR_RANGE_LOOP:      return STR("for_range_loop");
        case ASTNodeType::IF_ELSE:             return STR("if_else");
        case ASTNodeType::IDENTIFIER:          return STR("identifier");
        case ASTNodeType::INTEGER_LITERAL:     return STR("integer_literal");
        case ASTNodeType::MEMBER_ACCESS:       return STR("member_access");
        case ASTNodeType::MEMBER_ASSIGN:       return STR("member_assign");
        case ASTNodeType::PASS:                return STR("pass");
        case ASTNodeType::PROC_CALL:           return STR("procedure call");
        case ASTNodeType::PROC_DEF:            return STR("procedure definition");
        case ASTNodeType::RETURN:              return STR("return");
        case ASTNodeType::STRING_LITERAL:      return STR("string_literal");
        case ASTNodeType::STRUCT_DEF:          return STR("struct_def");
        case ASTNodeType::STRUCT_INIT:         return STR("struct_init");
        case ASTNodeType::VARIABLE_DEFINITION: return STR("variable_definition");
        default:                               return STR("undefined");
    }
    #undef STR
}
