#pragma once
#include <bloom/array.h>
#include <bloom/string.h>
#include <bloom/tokenization.h>


enum class ASTNodeType : uint8_t {
    UNKNOWN = 0,
    ADDRESS_OF,
    ARRAY_ACCESS,
    ARRAY_ELEMENT_ASSIGN,
    ARRAY_ELEMENT_MEMBER_ACCESS,
    ARRAY_INIT,
    ARRAY_RANGE_ASSIGN,
    ARRAY_SLICE,
    ARRAY_STRUCT_INIT,
    BINARY_ADD,
    BINARY_DIV,
    BINARY_MUL,
    BINARY_SUB,
    BOOLEAN_LITERAL,
    BREAK,
    COMPARISON,
    BUILTIN_LENGTH,
    CONSTANT_DEFINITION,
    BUILTIN_LENGTH_IN_BYTES,
    DEFER,
    DEREF,
    ENUM_DEF,
    FOR_COND_LOOP,
    INTBE_CAST,
    INTLE_CAST,
    FOR_IN_LOOP,
    FOR_LOOP,
    FOR_RANGE_LOOP,
    IF_ELSE,
    MAKE_SLICE,
    MAKE_DYNAMIC_ARRAY,
    SCOPE,
    IDENTIFIER,
    INTEGER_LITERAL,
    MEMBER_ACCESS,
    MEMBER_ASSIGN,
    ADD_ASSIGN,
    PACKAGE_DEF,
    PASS,
    PROC_CALL,
    PROC_DEF,
    RETURN,
    STRING_LITERAL,
    INTERPOLATED_STRING_LITERAL,
    SLICE_CAST,
    STRUCT_DEF,
    STRUCT_INIT,
    TYPED_VAR_DECL,
    TYPE_INFO_ENUM_MEMBER_KEY,
    TYPE_INFO_NAME,
    TYPE_INFO_SIZE,
    TYPE_INFO_STORE,
    VARIABLE_DEFINITION,
};

enum class BinaryOperatorType : uint8_t {
    ADD = '+',
    SUB = '-',
    DIV = '/',
    MUL = '*',
};

struct IntegerLiteralASTNode {
    union {
        int64_t value;
        uint64_t uvalue;
    };
};

struct ConditionOperand {
    bool is_identifier;
    bool is_enum_shorthand;
    bool is_proc_call;
    bool is_array_access;
    bool is_string_literal;
    bool is_nil;
    bool is_member_access;
    union {
        Str identifier;
        IntegerLiteralASTNode integer_literal;
        struct {
            Str enum_type_name;
            Str member_name;
        } enum_shorthand;
        struct {
            Str caller;
            Str arg_identifier;
        } proc_call;
        struct {
            Str variable_name;
            int64_t index;
        } array_access;
        Str string_literal;
        struct {
            Str object_name;
            Str field_name;
        } member_access;
    };
};

struct ASTNode;

enum class BinaryOperandType : uint8_t {
    IDENTIFIER,
    INTEGER_LITERAL,
    PROC_CALL,
    ARRAY_ACCESS,
    ARRAY_SLICE,
    MEMBER_ACCESS,
    STRING_LITERAL,
    DEREF,
    EXPR_NODE,
    ADDRESS_OF,
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
            Str index_identifier;
        } array_access;
        struct {
            Str object_name;
            Str field_name;
            Str field2_name;
        } member_access;
        struct {
            Str variable_name;
            int64_t start_index;
            int64_t end_index;
            Str count_identifier;
        } array_slice;
        Str string_literal;
        ASTNode *expr_node;
    };
};

struct ProcParameterASTNode {
    Str name;
    Str type_name;
    bool is_pointer;
    bool is_slice;
    bool is_array;
    bool is_dynamic_array;
    bool has_default_context_allocator;  // param := context.allocator
    int64_t array_length;
};

struct TypeASTNode {
    Str name;
    bool is_pointer;
    bool is_array;
    int64_t array_length;
    bool is_slice;  // []Type return type
};

struct ASTNode {
    ASTNodeType type;
    ASTNode *parent;
    union {
        struct {
            Str variable_name;
            int64_t index;
            Str index_identifier;
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
            Str name;
        } package_def;
        struct {
            bool value;
        } boolean_literal;
        struct {
            IntegerLiteralASTNode value;
        } integer_literal;
        struct {
            ConditionOperand condition_left;
            ConditionOperand condition_right;
            Str comparison_op;
            ConditionOperand and_condition_lefts[4];
            ConditionOperand and_condition_rights[4];
            Str and_comparison_ops[4];
            size_t and_count;
            ConditionOperand or_condition_lefts[4];
            ConditionOperand or_condition_rights[4];
            Str or_comparison_ops[4];
            size_t or_count;
            Array<ASTNode> then_body;
            Array<ASTNode> else_body;
            Token::Position if_pos;
            Token::Position else_pos;
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
            bool is_foreign;
        } proc_def;
        ASTNode *return_value;
        ASTNode *unary_operand;
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
            Str inline_element_type;
            Array<int64_t> inline_elements;
            Str enum_members_type_name;
            Array<ASTNode> body;
        } for_in_loop;
        struct {
            ConditionOperand condition_left;
            ConditionOperand condition_right;
            Array<ASTNode> body;
        } for_cond_loop;
        struct {
            Array<ASTNode> body;
            Token::Position for_pos;
        } for_loop;
        struct {
            Str element_name;
            int64_t range_start;
            int64_t range_end;
            Str range_start_identifier;
            Str range_end_identifier;
            bool range_end_inclusive;
            Str range_count_identifier;
            Str range_end_proc_call_name;
            Str range_end_proc_call_arg;
            int64_t range_end_offset;
            Array<ASTNode> body;
        } for_range_loop;
        struct {
            Array<ASTNode> arguments;
            Str caller_identifier;
        } defer_stmt;
        struct {
            Array<ASTNode> body;
        } scope;
        struct {
            Str variable_name;
            BinaryOperand operand;
        } add_assign;
        struct {
            Str object_name;
            Str field_name;
            Str field2_name;
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
            Str variable_name;
            int64_t start_index;
            bool is_full_slice;  // slice[..] = src: dest is a slice, copy its full extent
            ASTNode *value_expr;
        } array_range_assign;
        struct {
            Str variable_name;
            int64_t start_index;     // -1 = from beginning (0)
            int64_t end_index;       // -1 = to end (sizeof); ignored when count_identifier is set
            Str count_identifier;    // variable name for counted range [N..+VAR]; empty = use end_index
        } array_slice;
        struct {
            Str element_type;
            Array<ASTNode> elements;
        } array_struct_init;
        struct {
            Str array_name;
            int64_t element_index;
            Str field_name;
        } array_element_member_access;
        struct {
            Str element_type;
            ASTNode *expr;
        } slice_cast;
        ASTNode *intle_cast;
        struct {
            Str type_name;
        } type_info_name;
        struct {
            Str type_name;
        } type_info_size;
        struct {
            Str type_name;
        } type_info_store;
        struct {
            Str name;
            Array<ProcParameterASTNode> fields;
        } struct_def;
        struct {
            Str name;
            Array<ProcParameterASTNode> members;
            bool is_str_typed;
        } enum_def;
        struct {
            Str enum_type_name;
            Str index_var;
        } type_info_enum_member_key;
        struct {
            Str type_name;
            Array<ProcParameterASTNode> field_names;
            Array<BinaryOperand> field_values;
        } struct_init;
        struct {
            Str name;
            Str type_name;  // "Arena" etc.
        } typed_var_decl;
        struct {
            bool size_is_literal;
            int64_t size_literal;
            Str size_identifier;
            bool has_explicit_allocator;
            Str allocator_identifier;
        } make_slice;
        struct {
            Str element_type;
            bool has_explicit_allocator;
            Str allocator_identifier;
            bool allocator_is_context_temp;
            bool allocator_is_context;
        } make_dynamic_array;
    };
};

auto parse(Array<Token> *tokens, ArenaAllocator *allocator, Str source_content, Str filename, bool *had_errors) -> Array<ASTNode>;

constexpr auto to_string(ASTNodeType type) -> Str {
    #define STR(x) cstr_to_str(x)
    switch (type) {
        case ASTNodeType::ADDRESS_OF:           return STR("address_of");
        case ASTNodeType::ADD_ASSIGN:           return STR("add_assign");
        case ASTNodeType::ARRAY_ACCESS:         return STR("array_access");
        case ASTNodeType::ARRAY_ELEMENT_ASSIGN: return STR("array_element_assign");
        case ASTNodeType::ARRAY_INIT:           return STR("array_init");
        case ASTNodeType::ARRAY_RANGE_ASSIGN:   return STR("array_range_assign");
        case ASTNodeType::ARRAY_SLICE:                   return STR("array_slice");
        case ASTNodeType::ARRAY_STRUCT_INIT:             return STR("array_struct_init");
        case ASTNodeType::ARRAY_ELEMENT_MEMBER_ACCESS:   return STR("array_element_member_access");
        case ASTNodeType::BINARY_ADD:          return STR("binary_add");
        case ASTNodeType::BINARY_DIV:          return STR("binary_div");
        case ASTNodeType::BINARY_MUL:          return STR("binary_mul");
        case ASTNodeType::BINARY_SUB:          return STR("binary_sub");
        case ASTNodeType::BOOLEAN_LITERAL:     return STR("boolean_literal");
        case ASTNodeType::BREAK:               return STR("break");
        case ASTNodeType::COMPARISON:          return STR("comparison");
        case ASTNodeType::BUILTIN_LENGTH:          return STR("builtin_length");
        case ASTNodeType::CONSTANT_DEFINITION: return STR("constant_definition");
        case ASTNodeType::BUILTIN_LENGTH_IN_BYTES: return STR("builtin_length_in_bytes");
        case ASTNodeType::DEFER:               return STR("defer");
        case ASTNodeType::DEREF:               return STR("deref");
        case ASTNodeType::ENUM_DEF:            return STR("enum_def");
        case ASTNodeType::FOR_COND_LOOP:       return STR("for_cond_loop");
        case ASTNodeType::INTBE_CAST:          return STR("intbe_cast");
        case ASTNodeType::INTLE_CAST:          return STR("intle_cast");
        case ASTNodeType::FOR_IN_LOOP:         return STR("for_in_loop");
        case ASTNodeType::FOR_LOOP:            return STR("for_loop");
        case ASTNodeType::FOR_RANGE_LOOP:      return STR("for_range_loop");
        case ASTNodeType::MAKE_SLICE:          return STR("make_slice");
        case ASTNodeType::MAKE_DYNAMIC_ARRAY:  return STR("make_dynamic_array");
        case ASTNodeType::SCOPE:               return STR("scope");
        case ASTNodeType::IF_ELSE:             return STR("if_else");
        case ASTNodeType::IDENTIFIER:          return STR("identifier");
        case ASTNodeType::INTEGER_LITERAL:     return STR("integer_literal");
        case ASTNodeType::MEMBER_ACCESS:       return STR("member_access");
        case ASTNodeType::MEMBER_ASSIGN:       return STR("member_assign");
        case ASTNodeType::PACKAGE_DEF:         return STR("package_def");
        case ASTNodeType::PASS:                return STR("pass");
        case ASTNodeType::PROC_CALL:           return STR("procedure call");
        case ASTNodeType::PROC_DEF:            return STR("procedure definition");
        case ASTNodeType::RETURN:              return STR("return");
        case ASTNodeType::STRING_LITERAL:               return STR("string_literal");
        case ASTNodeType::INTERPOLATED_STRING_LITERAL:  return STR("interpolated_string_literal");
        case ASTNodeType::SLICE_CAST:          return STR("slice_cast");
        case ASTNodeType::STRUCT_DEF:          return STR("struct_def");
        case ASTNodeType::STRUCT_INIT:         return STR("struct_init");
        case ASTNodeType::TYPED_VAR_DECL:      return STR("typed_var_decl");
        case ASTNodeType::TYPE_INFO_ENUM_MEMBER_KEY: return STR("type_info_enum_member_key");
        case ASTNodeType::TYPE_INFO_NAME:      return STR("type_info_name");
        case ASTNodeType::TYPE_INFO_SIZE:      return STR("type_info_size");
        case ASTNodeType::TYPE_INFO_STORE:     return STR("type_info_store");
        case ASTNodeType::VARIABLE_DEFINITION: return STR("variable_definition");
        default:                               return STR("undefined");
    }
    #undef STR
}
