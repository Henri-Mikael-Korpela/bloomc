#include <functional>
#include <bloom/defer.h>
#include <bloom/log.h>
#include <bloom/print.h>
#include <bloom/transpilation.h>

static auto allocate_dynamic_str(ArenaAllocator *allocator) -> DynamicStr {
    return {
        .data = reinterpret_cast<char*>(allocator->data + allocator->offset),
        .length = 0,
        .max_length = allocator->length - allocator->offset,
    };
}

auto transpile_to_c(Array<ASTNode> *ast_nodes, ArenaAllocator *allocator) -> Str {
    auto str_buffer = allocate_dynamic_str(allocator);

    #define PUSH_STR(value)  allocator->offset += str_push(&str_buffer, value)
    #define PUSH_INT(value)  allocator->offset += str_push_int(&str_buffer, value)
    #define PUSH_BOOL(value) allocator->offset += str_push_bool(&str_buffer, value)

    struct ArrayVarEntry {
        Str name;
        size_t count;
        Str element_type;
    };
    ArrayVarEntry array_vars[64];
    size_t array_var_count = 0;

    struct SliceVarEntry {
        Str name;
        Str element_type;
    };
    SliceVarEntry slice_vars[32];
    size_t slice_var_count = 0;

    struct ArrayReturnTypeEntry {
        Str element_type;
        int64_t count;
    };
    ArrayReturnTypeEntry emitted_array_return_types[16];
    size_t emitted_array_return_type_count = 0;

    // Tracks which __bloom_SliceInt etc. typedefs have been emitted
    Str emitted_slice_return_types[16];
    size_t emitted_slice_return_type_count = 0;

    enum class VarKind : uint8_t { INT, BOOL, BLOOM_STR, BLOOM_CHAR, SIZE_T, STRUCT, PTR, PTR_STRUCT, SLICE_U8 };
    struct VarEntry {
        Str name;
        VarKind kind;
        Str struct_type_name;
    };
    VarEntry var_types[128] = {};
    size_t var_type_count = 0;

    auto register_var = [&](Str name, VarKind kind) {
        if (var_type_count < 128) {
            var_types[var_type_count++] = { .name = name, .kind = kind, .struct_type_name = {} };
        }
    };
    auto register_struct_var = [&](Str name, Str type_name) {
        if (var_type_count < 128) {
            var_types[var_type_count++] = { .name = name, .kind = VarKind::STRUCT, .struct_type_name = type_name };
        }
    };
    auto register_struct_var_ptr = [&](Str name, Str type_name) {
        if (var_type_count < 128) {
            var_types[var_type_count++] = { .name = name, .kind = VarKind::PTR_STRUCT, .struct_type_name = type_name };
        }
    };

    auto lookup_var_kind = [&](Str name) -> VarKind {
        for (size_t i = 0; i < var_type_count; i++) {
            if (var_types[i].name.length == name.length &&
                strncmp(var_types[i].name.data, name.data, name.length) == 0) {
                return var_types[i].kind;
            }
        }
        return VarKind::INT;
    };

    auto is_pointer_var = [&](Str name) -> bool {
        for (size_t i = 0; i < var_type_count; i++) {
            if (var_types[i].name.length == name.length &&
                strncmp(var_types[i].name.data, name.data, name.length) == 0) {
                return var_types[i].kind == VarKind::PTR ||
                       var_types[i].kind == VarKind::PTR_STRUCT;
            }
        }
        return false;
    };

    struct StructFieldDef { Str name; VarKind kind; };
    struct StructDefEntry { Str name; StructFieldDef fields[16]; size_t field_count; };
    StructDefEntry struct_defs[16] = {};
    size_t struct_def_count = 0;

    auto register_struct_def = [&](ASTNode const *node) {
        if (struct_def_count >= 16) { return; }
        auto *def = &struct_defs[struct_def_count++];
        def->name = node->struct_def.name;
        def->field_count = 0;
        for (size_t i = 0; i < node->struct_def.fields.length && i < 16; i++) {
            auto *field = &node->struct_def.fields.data[i];
            VarKind kind = VarKind::INT;
            if (field->type_name == "Bool") { kind = VarKind::BOOL; }
            else if (field->type_name == "Str") { kind = VarKind::BLOOM_STR; }
            else if (field->is_pointer) { kind = VarKind::PTR; }
            def->fields[def->field_count++] = { .name = field->name, .kind = kind };
        }
    };

    struct EnumDefEntry { Str name; Str member_names[32]; size_t member_count; };
    EnumDefEntry enum_defs[16] = {};
    size_t enum_def_count = 0;

    auto is_enum_type = [&](Str name) -> bool {
        for (size_t i = 0; i < enum_def_count; i++) {
            if (enum_defs[i].name.length == name.length &&
                strncmp(enum_defs[i].name.data, name.data, name.length) == 0)
            {
                return true;
            }
        }
        return false;
    };

    auto lookup_member_kind = [&](Str object_name, Str field_name) -> VarKind {
        Str struct_type = {};
        for (size_t i = 0; i < var_type_count; i++) {
            if (var_types[i].name.length == object_name.length &&
                strncmp(var_types[i].name.data, object_name.data, object_name.length) == 0)
            {
                if (var_types[i].kind == VarKind::STRUCT ||
                    var_types[i].kind == VarKind::PTR_STRUCT) {
                    struct_type = var_types[i].struct_type_name;
                }
                break;
            }
        }
        if (struct_type.length == 0) { return VarKind::INT; }
        for (size_t i = 0; i < struct_def_count; i++) {
            if (struct_defs[i].name.length != struct_type.length ||
                strncmp(struct_defs[i].name.data, struct_type.data, struct_type.length) != 0) { continue; }
            for (size_t j = 0; j < struct_defs[i].field_count; j++) {
                if (struct_defs[i].fields[j].name.length == field_name.length &&
                    strncmp(struct_defs[i].fields[j].name.data, field_name.data, field_name.length) == 0)
                {
                    return struct_defs[i].fields[j].kind;
                }
            }
        }
        return VarKind::INT;
    };

    auto find_array_size = [&](Str var_name) -> size_t {
        for (size_t i = 0; i < array_var_count; i++) {
            Str const *name = &array_vars[i].name;
            if (name->length == var_name.length &&
                strncmp(name->data, var_name.data, name->length) == 0)
            {
                return array_vars[i].count;
            }
        }
        assert(false && "Array variable not found for length() call");
        return 0;
    };

    // Returns the runtime byte length of a string literal content,
    // treating backslash-escape pairs as a single byte.
    auto str_literal_runtime_length = [](Str content) -> size_t {
        size_t len = 0;
        for (size_t k = 0; k < content.length; k++) {
            if (content.data[k] == '\\' && k + 1 < content.length) {
                k++;
            }
            len++;
        }
        return len;
    };

    struct ArrayStructVar { Str name; Str element_type; size_t count; };
    ArrayStructVar array_struct_vars[64] = {};
    size_t array_struct_var_count = 0;

    auto register_array_struct_var = [&](Str name, Str elem_type, size_t count) {
        if (array_struct_var_count < 64) {
            array_struct_vars[array_struct_var_count++] = { .name = name, .element_type = elem_type, .count = count };
        }
    };

    auto lookup_array_element_member_kind = [&](Str array_name, Str field_name) -> VarKind {
        Str elem_type = {};
        for (size_t i = 0; i < array_struct_var_count; i++) {
            if (array_struct_vars[i].name.length == array_name.length &&
                strncmp(array_struct_vars[i].name.data, array_name.data, array_name.length) == 0)
            {
                elem_type = array_struct_vars[i].element_type;
                break;
            }
        }
        if (elem_type.length == 0) { return VarKind::INT; }
        for (size_t i = 0; i < struct_def_count; i++) {
            if (struct_defs[i].name.length != elem_type.length ||
                strncmp(struct_defs[i].name.data, elem_type.data, elem_type.length) != 0) { continue; }
            for (size_t j = 0; j < struct_defs[i].field_count; j++) {
                if (struct_defs[i].fields[j].name.length == field_name.length &&
                    strncmp(struct_defs[i].fields[j].name.data, field_name.data, field_name.length) == 0)
                {
                    return struct_defs[i].fields[j].kind;
                }
            }
        }
        return VarKind::INT;
    };

    auto is_user_proc = [&](Str name) -> bool {
        for (size_t ni = 0; ni < ast_nodes->length; ni++) {
            auto *n = &ast_nodes->data[ni];
            if (n->type == ASTNodeType::PROC_DEF &&
                n->proc_def.name.length == name.length &&
                strncmp(n->proc_def.name.data, name.data, name.length) == 0)
            {
                return true;
            }
        }
        return false;
    };

    auto lookup_proc_return_type = [&](Str name) -> TypeASTNode * {
        for (size_t ni = 0; ni < ast_nodes->length; ni++) {
            auto *n = &ast_nodes->data[ni];
            if (n->type == ASTNodeType::PROC_DEF &&
                n->proc_def.name.length == name.length &&
                strncmp(n->proc_def.name.data, name.data, name.length) == 0)
            {
                return n->proc_def.return_type;
            }
        }
        return nullptr;
    };

    std::function<void(Array<ASTNode>*, Str)> emit_proc_call_args = [&](Array<ASTNode> *arguments, Str callee_name) {
        bool const wrap_strings = is_user_proc(callee_name) || callee_name == "clone_to_cstr";
        for (size_t i = 0; i < arguments->length; i++) {
            if (i != 0) { PUSH_STR(", "); }
            auto *arg = &(*arguments)[i];
            switch (arg->type) {
                case ASTNodeType::IDENTIFIER:
                    PUSH_STR(arg->identifier);
                    break;
                case ASTNodeType::ARRAY_ACCESS: {
                    bool const is_sv = [&]() {
                        for (size_t i = slice_var_count; i-- > 0;) {
                            if (slice_vars[i].name.length == arg->array_access.variable_name.length &&
                                strncmp(slice_vars[i].name.data, arg->array_access.variable_name.data,
                                        arg->array_access.variable_name.length) == 0) { return true; }
                        }
                        return false;
                    }();
                    if (is_sv) {
                        PUSH_STR("__bloom_");
                        PUSH_STR(arg->array_access.variable_name);
                        PUSH_STR("_data[");
                        PUSH_INT(arg->array_access.index);
                        PUSH_STR(']');
                    }
                    else {
                        PUSH_STR(arg->array_access.variable_name);
                        PUSH_STR('[');
                        PUSH_INT(arg->array_access.index);
                        PUSH_STR(']');
                    }
                    break;
                }
                case ASTNodeType::INTEGER_LITERAL:
                    PUSH_INT(arg->integer_literal.value.value);
                    break;
                case ASTNodeType::STRING_LITERAL:
                    if (wrap_strings) {
                        size_t const runtime_len = str_literal_runtime_length(arg->string_literal.value);
                        PUSH_STR("(BloomStr){.data = \"");
                        PUSH_STR(arg->string_literal.value);
                        PUSH_STR("\", .length = ");
                        PUSH_INT(static_cast<intmax_t>(runtime_len));
                        PUSH_STR("}");
                    }
                    else {
                        PUSH_STR('"');
                        PUSH_STR(arg->string_literal.value);
                        PUSH_STR('"');
                    }
                    break;
                case ASTNodeType::BUILTIN_LENGTH:
                    if (lookup_var_kind(arg->identifier) == VarKind::SLICE_U8) {
                        PUSH_STR(arg->identifier);
                        PUSH_STR(".length");
                    }
                    else {
                        PUSH_INT(static_cast<intmax_t>(find_array_size(arg->identifier)));
                    }
                    break;
                case ASTNodeType::BUILTIN_LENGTH_IN_BYTES:
                    PUSH_STR(arg->identifier);
                    PUSH_STR(".length");
                    break;
                case ASTNodeType::BOOLEAN_LITERAL:
                    PUSH_BOOL(arg->boolean_literal.value);
                    break;
                case ASTNodeType::PROC_CALL:
                    if (arg->proc_call.caller_identifier == "CStr" &&
                        arg->proc_call.arguments.length == 1 &&
                        arg->proc_call.arguments.data[0].type == ASTNodeType::IDENTIFIER &&
                        lookup_var_kind(arg->proc_call.arguments.data[0].identifier) == VarKind::SLICE_U8)
                    {
                        PUSH_STR("(char const *)");
                        PUSH_STR(arg->proc_call.arguments.data[0].identifier);
                        PUSH_STR(".data");
                    }
                    else {
                        PUSH_STR(arg->proc_call.caller_identifier);
                        PUSH_STR('(');
                        emit_proc_call_args(&arg->proc_call.arguments, arg->proc_call.caller_identifier);
                        PUSH_STR(')');
                    }
                    break;
                case ASTNodeType::ADDRESS_OF:
                    PUSH_STR("&");
                    PUSH_STR(arg->identifier);
                    break;
                case ASTNodeType::MEMBER_ACCESS:
                    if (arg->member_access.object_name == "context" &&
                        arg->member_access.field_name == "temp_allocator") {
                        PUSH_STR("&__bloom_context.temp_allocator");
                    }
                    else if (is_enum_type(arg->member_access.object_name)) {
                        PUSH_STR("__bloom_");
                        PUSH_STR(arg->member_access.object_name);
                        PUSH_STR("_");
                        PUSH_STR(arg->member_access.field_name);
                    }
                    else {
                        PUSH_STR(arg->member_access.object_name);
                        PUSH_STR(is_pointer_var(arg->member_access.object_name) ? "->" : ".");
                        PUSH_STR(arg->member_access.field_name);
                    }
                    break;
                case ASTNodeType::DEREF:
                    PUSH_STR("*");
                    PUSH_STR(arg->identifier);
                    break;
                case ASTNodeType::ARRAY_SLICE: {
                    Str const &arr_name = arg->array_slice.variable_name;
                    int64_t const offset = (arg->array_slice.start_index < 0) ? 0 : arg->array_slice.start_index;
                    int64_t const end = arg->array_slice.end_index;
                    Str const &count_id = arg->array_slice.count_identifier;
                    PUSH_STR("(BloomSliceU8){.data = (uint8_t*)");
                    PUSH_STR(arr_name);
                    if (offset > 0) {
                        PUSH_STR(" + ");
                        PUSH_INT(offset);
                    }
                    PUSH_STR(", .length = ");
                    if (count_id.data != nullptr) {
                        PUSH_STR(count_id);
                    }
                    else if (end >= 0) {
                        PUSH_INT(end - offset);
                    }
                    else {
                        PUSH_STR("sizeof(");
                        PUSH_STR(arr_name);
                        PUSH_STR(")");
                        if (offset > 0) {
                            PUSH_STR(" - ");
                            PUSH_INT(offset);
                        }
                    }
                    PUSH_STR("}");
                    break;
                }
                case ASTNodeType::ARRAY_ELEMENT_MEMBER_ACCESS:
                    PUSH_STR(arg->array_element_member_access.array_name);
                    PUSH_STR("[");
                    PUSH_INT(arg->array_element_member_access.element_index);
                    PUSH_STR("].");
                    PUSH_STR(arg->array_element_member_access.field_name);
                    break;
                default:
                    assert(false && "Unsupported argument type in emit_proc_call_args");
            }
        }
    };

    // Forward-declare as std::function so emit_expression and emit_binary_operand
    // can call each other (EXPR_NODE operands recursively call emit_expression).
    std::function<void(BinaryOperand const *)> emit_binary_operand;

    auto emit_expression = [&](ASTNode *expr) {
        switch (expr->type) {
            case ASTNodeType::INTEGER_LITERAL:
                PUSH_INT(expr->integer_literal.value.value);
                break;
            case ASTNodeType::BOOLEAN_LITERAL:
                PUSH_BOOL(expr->boolean_literal.value);
                break;
            case ASTNodeType::IDENTIFIER:
                PUSH_STR(expr->identifier);
                break;
            case ASTNodeType::ARRAY_ACCESS: {
                bool is_slice_acc = false;
                for (size_t i = slice_var_count; i-- > 0;) {
                    if (slice_vars[i].name.length == expr->array_access.variable_name.length &&
                        strncmp(slice_vars[i].name.data, expr->array_access.variable_name.data,
                                expr->array_access.variable_name.length) == 0)
                    {
                        is_slice_acc = true;
                        break;
                    }
                }
                if (is_slice_acc) {
                    PUSH_STR("__bloom_");
                    PUSH_STR(expr->array_access.variable_name);
                    PUSH_STR("_data[");
                    PUSH_INT(expr->array_access.index);
                    PUSH_STR(']');
                }
                else {
                    PUSH_STR(expr->array_access.variable_name);
                    PUSH_STR('[');
                    PUSH_INT(expr->array_access.index);
                    PUSH_STR(']');
                }
                break;
            }
            case ASTNodeType::ARRAY_SLICE: {
                Str const &arr_name = expr->array_slice.variable_name;
                int64_t const offset = (expr->array_slice.start_index < 0) ? 0 : expr->array_slice.start_index;
                int64_t const end = expr->array_slice.end_index;
                Str const &count_id = expr->array_slice.count_identifier;
                PUSH_STR("(BloomSliceU8){.data = (uint8_t*)");
                PUSH_STR(arr_name);
                if (offset > 0) {
                    PUSH_STR(" + ");
                    PUSH_INT(offset);
                }
                PUSH_STR(", .length = ");
                if (count_id.data != nullptr) {
                    PUSH_STR(count_id);
                }
                else if (end >= 0) {
                    PUSH_INT(end - offset);
                }
                else {
                    PUSH_STR("sizeof(");
                    PUSH_STR(arr_name);
                    PUSH_STR(")");
                    if (offset > 0) {
                        PUSH_STR(" - ");
                        PUSH_INT(offset);
                    }
                }
                PUSH_STR("}");
                break;
            }
            case ASTNodeType::SLICE_CAST: {
                ASTNode *inner = expr->slice_cast.expr;
                PUSH_STR("(BloomSliceU8){.data = (uint8_t*)");
                if (inner->type == ASTNodeType::MEMBER_ACCESS) {
                    char const *acc = is_pointer_var(inner->member_access.object_name) ? "->" : ".";
                    PUSH_STR(inner->member_access.object_name);
                    PUSH_STR(acc);
                    PUSH_STR(inner->member_access.field_name);
                    PUSH_STR(".data, .length = ");
                    PUSH_STR(inner->member_access.object_name);
                    PUSH_STR(acc);
                    PUSH_STR(inner->member_access.field_name);
                    PUSH_STR(".length}");
                }
                else {
                    PUSH_STR(inner->identifier);
                    PUSH_STR(".data, .length = ");
                    PUSH_STR(inner->identifier);
                    PUSH_STR(".length}");
                }
                break;
            }
            case ASTNodeType::PROC_CALL:
                if (expr->proc_call.caller_identifier == "CStr" &&
                    expr->proc_call.arguments.length == 1 &&
                    expr->proc_call.arguments.data[0].type == ASTNodeType::IDENTIFIER &&
                    lookup_var_kind(expr->proc_call.arguments.data[0].identifier) == VarKind::SLICE_U8)
                {
                    PUSH_STR("(char const *)");
                    PUSH_STR(expr->proc_call.arguments.data[0].identifier);
                    PUSH_STR(".data");
                }
                else if (expr->proc_call.caller_identifier == "Int") {
                    assert(expr->proc_call.arguments.length == 1 && "Int() cast requires exactly one argument");
                    PUSH_STR("(int)(");
                    emit_proc_call_args(&expr->proc_call.arguments, expr->proc_call.caller_identifier);
                    PUSH_STR(")");
                }
                else if (expr->proc_call.caller_identifier == "length") {
                    assert(expr->proc_call.arguments.length > 0 && "length() requires an argument");
                    if (lookup_var_kind(expr->proc_call.arguments[0].identifier) == VarKind::SLICE_U8) {
                        PUSH_STR(expr->proc_call.arguments[0].identifier);
                        PUSH_STR(".length");
                    }
                    else {
                        PUSH_INT(static_cast<intmax_t>(find_array_size(expr->proc_call.arguments[0].identifier)));
                    }
                }
                else if (expr->proc_call.caller_identifier == "length_in_bytes") {
                    assert(expr->proc_call.arguments.length == 1 && "length_in_bytes() requires exactly one argument");
                    auto *arg = &expr->proc_call.arguments.data[0];
                    if (arg->type == ASTNodeType::MEMBER_ACCESS) {
                        PUSH_STR(arg->member_access.object_name);
                        PUSH_STR(is_pointer_var(arg->member_access.object_name) ? "->" : ".");
                        PUSH_STR(arg->member_access.field_name);
                    }
                    else {
                        PUSH_STR(arg->identifier);
                    }
                    PUSH_STR(".length");
                }
                else {
                    PUSH_STR(expr->proc_call.caller_identifier);
                    PUSH_STR('(');
                    emit_proc_call_args(&expr->proc_call.arguments, expr->proc_call.caller_identifier);
                    PUSH_STR(')');
                }
                break;
            case ASTNodeType::BINARY_ADD: {
                auto *operands = &expr->binary_operation.operands;
                for (size_t i = 0; i < operands->length; i++) {
                    if (i != 0) { PUSH_STR(" + "); }
                    emit_binary_operand(&operands->data[i]);
                }
                break;
            }
            case ASTNodeType::BINARY_MUL: {
                auto *operands = &expr->binary_operation.operands;
                for (size_t i = 0; i < operands->length; i++) {
                    if (i != 0) { PUSH_STR(" * "); }
                    emit_binary_operand(&operands->data[i]);
                }
                break;
            }
            case ASTNodeType::BINARY_DIV: {
                auto *operands = &expr->binary_operation.operands;
                for (size_t i = 0; i < operands->length; i++) {
                    if (i != 0) { PUSH_STR(" / "); }
                    emit_binary_operand(&operands->data[i]);
                }
                break;
            }
            case ASTNodeType::BINARY_SUB: {
                auto *operands = &expr->binary_operation.operands;
                for (size_t i = 0; i < operands->length; i++) {
                    if (i != 0) { PUSH_STR(" - "); }
                    emit_binary_operand(&operands->data[i]);
                }
                break;
            }
            case ASTNodeType::COMPARISON: {
                auto *operands = &expr->binary_operation.operands;
                emit_binary_operand(&operands->data[0]);
                PUSH_STR(" == ");
                emit_binary_operand(&operands->data[1]);
                break;
            }
            case ASTNodeType::STRING_LITERAL: {
                Str const *content = &expr->string_literal.value;
                size_t const runtime_len = str_literal_runtime_length(*content);
                PUSH_STR("(BloomStr){.data = \"");
                PUSH_STR(*content);
                PUSH_STR("\", .length = ");
                PUSH_INT(static_cast<intmax_t>(runtime_len));
                PUSH_STR("}");
                break;
            }
            case ASTNodeType::MEMBER_ACCESS:
                if (is_enum_type(expr->member_access.object_name)) {
                    PUSH_STR("__bloom_");
                    PUSH_STR(expr->member_access.object_name);
                    PUSH_STR("_");
                    PUSH_STR(expr->member_access.field_name);
                }
                else {
                    PUSH_STR(expr->member_access.object_name);
                    PUSH_STR(is_pointer_var(expr->member_access.object_name) ? "->" : ".");
                    PUSH_STR(expr->member_access.field_name);
                }
                break;
            case ASTNodeType::ADDRESS_OF:
                PUSH_STR("&");
                PUSH_STR(expr->identifier);
                break;
            case ASTNodeType::DEREF:
                PUSH_STR("*");
                PUSH_STR(expr->identifier);
                break;
            case ASTNodeType::TYPE_INFO_NAME: {
                Str tn = expr->type_info_name.type_name;
                PUSH_STR("(BloomStr){.data = \"");
                PUSH_STR(tn);
                PUSH_STR("\", .length = ");
                PUSH_INT(static_cast<intmax_t>(tn.length));
                PUSH_STR("}");
                break;
            }
            case ASTNodeType::TYPE_INFO_SIZE: {
                Str tn = expr->type_info_size.type_name;
                PUSH_STR("sizeof(");
                if (tn == "Int")       { PUSH_STR("int"); }
                else if (tn == "U8")   { PUSH_STR("uint8_t"); }
                else if (tn == "Bool") { PUSH_STR("bool"); }
                else if (tn == "Str")  { PUSH_STR("BloomStr"); }
                else if (tn == "CStr") { PUSH_STR("char const *"); }
                else                   { PUSH_STR(tn); } // custom struct — same name in C
                PUSH_STR(")");
                break;
            }
            case ASTNodeType::TYPE_INFO_ENUM_MEMBER_KEY:
                PUSH_STR("__bloom_");
                PUSH_STR(expr->type_info_enum_member_key.enum_type_name);
                PUSH_STR("_members[");
                PUSH_STR(expr->type_info_enum_member_key.index_var);
                PUSH_STR("].name");
                break;
            default:
                assert(false && "Unsupported expression type in emit_expression");
        }
    };

    emit_binary_operand = [&](BinaryOperand const *op) {
        switch (op->type) {
            case BinaryOperandType::IDENTIFIER:
                PUSH_STR(op->identifier);
                break;
            case BinaryOperandType::ARRAY_ACCESS: {
                // Check if the variable is a slice var — access via __bloom_<name>_data
                bool is_slice_access = false;
                for (size_t i = slice_var_count; i-- > 0;) {
                    if (slice_vars[i].name.length == op->array_access.variable_name.length &&
                        strncmp(slice_vars[i].name.data, op->array_access.variable_name.data,
                                op->array_access.variable_name.length) == 0)
                    {
                        is_slice_access = true;
                        break;
                    }
                }
                if (is_slice_access) {
                    PUSH_STR("__bloom_");
                    PUSH_STR(op->array_access.variable_name);
                    PUSH_STR("_data[");
                    PUSH_INT(op->array_access.index);
                    PUSH_STR(']');
                }
                else {
                    PUSH_STR(op->array_access.variable_name);
                    PUSH_STR('[');
                    PUSH_INT(op->array_access.index);
                    PUSH_STR(']');
                }
                break;
            }
            case BinaryOperandType::PROC_CALL: {
                Str const *callee = &op->proc_call.caller_identifier;
                bool const is_length = callee->length == 6 &&
                    strncmp(callee->data, "length", 6) == 0;
                bool const is_length_in_bytes = callee->length == 15 &&
                    strncmp(callee->data, "length_in_bytes", 15) == 0;
                bool const is_int_cast = callee->length == 3 &&
                    strncmp(callee->data, "Int", 3) == 0;
                if (is_int_cast) {
                    assert(op->proc_call.arguments.length == 1 && "Int() cast requires exactly one argument");
                    PUSH_STR("(int)(");
                    emit_proc_call_args(const_cast<Array<ASTNode>*>(&op->proc_call.arguments), *callee);
                    PUSH_STR(")");
                }
                else if (is_length) {
                    assert(op->proc_call.arguments.length > 0 && "length() requires an argument");
                    PUSH_INT(static_cast<intmax_t>(
                        find_array_size(op->proc_call.arguments.data[0].identifier)
                    ));
                }
                else if (is_length_in_bytes) {
                    assert(op->proc_call.arguments.length == 1 && "length_in_bytes() requires exactly one argument");
                    auto *arg = &op->proc_call.arguments.data[0];
                    if (arg->type == ASTNodeType::MEMBER_ACCESS) {
                        PUSH_STR(arg->member_access.object_name);
                        PUSH_STR(is_pointer_var(arg->member_access.object_name) ? "->" : ".");
                        PUSH_STR(arg->member_access.field_name);
                    }
                    else {
                        PUSH_STR(arg->identifier);
                    }
                    PUSH_STR(".length");
                }
                else {
                    PUSH_STR(op->proc_call.caller_identifier);
                    PUSH_STR('(');
                    emit_proc_call_args(const_cast<Array<ASTNode>*>(&op->proc_call.arguments), *callee);
                    PUSH_STR(')');
                }
                break;
            }
            case BinaryOperandType::INTEGER_LITERAL:
                PUSH_INT(op->integer_literal.value);
                break;
            case BinaryOperandType::MEMBER_ACCESS:
                PUSH_STR(op->member_access.object_name);
                PUSH_STR(is_pointer_var(op->member_access.object_name) ? "->" : ".");
                PUSH_STR(op->member_access.field_name);
                break;
            case BinaryOperandType::STRING_LITERAL: {
                size_t const runtime_len = str_literal_runtime_length(op->string_literal);
                PUSH_STR("(BloomStr){.data = \"");
                PUSH_STR(op->string_literal);
                PUSH_STR("\", .length = ");
                PUSH_INT(static_cast<intmax_t>(runtime_len));
                PUSH_STR("}");
                break;
            }
            case BinaryOperandType::DEREF:
                PUSH_STR("*");
                PUSH_STR(op->identifier);
                break;
            case BinaryOperandType::EXPR_NODE:
                emit_expression(op->expr_node);
                break;
        }
    };

    PUSH_STR("#include <stdbool.h>\n");
    PUSH_STR("#include <stddef.h>\n");
    PUSH_STR("#include <stdint.h>\n");
    PUSH_STR("#include <stdio.h>\n");
    PUSH_STR("#include <string.h>\n\n");
    PUSH_STR("typedef struct { char const *data; size_t length; } BloomStr;\n");
    PUSH_STR("typedef struct { uint8_t *data; size_t length; } BloomSliceU8;\n");
    PUSH_STR("typedef struct { char bytes[4]; uint8_t len; } BloomChar;\n");
    PUSH_STR("typedef struct { char buf[4096]; size_t offset; } BloomTempAllocator;\n");
    PUSH_STR("typedef struct { BloomTempAllocator temp_allocator; } BloomContext;\n");
    PUSH_STR("typedef struct { BloomStr name; int size_in_bytes; } BloomTypeInfo;\n");
    PUSH_STR("typedef struct { BloomStr name; int value; } BloomEnumMember;\n");

    // Register TypeInfo as a built-in struct definition so member lookups work
    if (struct_def_count < 16) {
        auto *def = &struct_defs[struct_def_count++];
        def->name = { .data = "TypeInfo", .length = 8 };
        def->field_count = 0;
        def->fields[def->field_count++] = { .name = { .data = "name", .length = 4 }, .kind = VarKind::BLOOM_STR };
        def->fields[def->field_count++] = { .name = { .data = "size_in_bytes", .length = 13 }, .kind = VarKind::INT };
    }
    // Register BloomEnumMember so member.name resolves correctly
    if (struct_def_count < 16) {
        auto *def = &struct_defs[struct_def_count++];
        def->name = { .data = "BloomEnumMember", .length = 15 };
        def->field_count = 0;
        def->fields[def->field_count++] = { .name = { .data = "name", .length = 4 }, .kind = VarKind::BLOOM_STR };
        def->fields[def->field_count++] = { .name = { .data = "value", .length = 5 }, .kind = VarKind::INT };
    }
    PUSH_STR("static char* __bloom_clone_to_cstr(BloomStr s, BloomTempAllocator *alloc) {\n");
    PUSH_STR("\tchar *p = alloc->buf + alloc->offset;\n");
    PUSH_STR("\tmemcpy(p, s.data, s.length);\n");
    PUSH_STR("\tp[s.length] = '\\0';\n");
    PUSH_STR("\talloc->offset += s.length + 1;\n");
    PUSH_STR("\treturn p;\n");
    PUSH_STR("}\n");
    PUSH_STR("#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__\n");
    PUSH_STR("#  define __bloom_u32_le(x) ((uint32_t)__builtin_bswap32((uint32_t)(x)))\n");
    PUSH_STR("#  define __bloom_u32_be(x) ((uint32_t)(x))\n");
    PUSH_STR("#else\n");
    PUSH_STR("#  define __bloom_u32_le(x) ((uint32_t)(x))\n");
    PUSH_STR("#  define __bloom_u32_be(x) ((uint32_t)__builtin_bswap32((uint32_t)(x)))\n");
    PUSH_STR("#endif\n\n");

    for (size_t ni = 0; ni < ast_nodes->length; ni++) {
        auto *node = &ast_nodes->data[ni];
        switch (node->type) {
            case ASTNodeType::STRUCT_DEF: {
                register_struct_def(node);
                PUSH_STR("typedef struct {\n");
                for (size_t i = 0; i < node->struct_def.fields.length; i++) {
                    auto *field = &node->struct_def.fields.data[i];
                    PUSH_STR("\t");
                    if (field->type_name == "Int") {
                        PUSH_STR("int");
                    }
                    else if (field->type_name == "U8") {
                        PUSH_STR("uint8_t");
                    }
                    else if (field->type_name == "Str") {
                        PUSH_STR("BloomStr");
                    }
                    else if (field->type_name == "Bool") {
                        PUSH_STR("bool");
                    }
                    else {
                        PUSH_STR(field->type_name);
                    }
                    if (field->is_pointer) {
                        PUSH_STR(" *");
                    }
                    else {
                        PUSH_STR(" ");
                    }
                    PUSH_STR(field->name);
                    PUSH_STR(";\n");
                }
                PUSH_STR("} ");
                PUSH_STR(node->struct_def.name);
                PUSH_STR(";\n\n");
                break;
            }
            case ASTNodeType::ENUM_DEF: {
                // Register enum type
                if (enum_def_count < 16) {
                    auto *def = &enum_defs[enum_def_count++];
                    def->name = node->enum_def.name;
                    def->member_count = 0;
                    for (size_t i = 0; i < node->enum_def.members.length && i < 32; i++) {
                        def->member_names[def->member_count++] = node->enum_def.members.data[i].name;
                    }
                }
                // Emit typedef
                PUSH_STR("typedef int ");
                PUSH_STR(node->enum_def.name);
                PUSH_STR(";\n");
                // Emit member constants
                for (size_t i = 0; i < node->enum_def.members.length; i++) {
                    PUSH_STR("static int const __bloom_");
                    PUSH_STR(node->enum_def.name);
                    PUSH_STR("_");
                    PUSH_STR(node->enum_def.members.data[i].name);
                    PUSH_STR(" = ");
                    PUSH_INT(static_cast<intmax_t>(i));
                    PUSH_STR(";\n");
                }
                // Emit members array for type_info_of reflection
                PUSH_STR("static BloomEnumMember const __bloom_");
                PUSH_STR(node->enum_def.name);
                PUSH_STR("_members[] = {\n");
                for (size_t i = 0; i < node->enum_def.members.length; i++) {
                    Str mname = node->enum_def.members.data[i].name;
                    PUSH_STR("\t{.name = {.data = \"");
                    PUSH_STR(mname);
                    PUSH_STR("\", .length = ");
                    PUSH_INT(static_cast<intmax_t>(mname.length));
                    PUSH_STR("}, .value = ");
                    PUSH_INT(static_cast<intmax_t>(i));
                    PUSH_STR("},\n");
                }
                PUSH_STR("};\n\n");
                break;
            }
            case ASTNodeType::PROC_DEF: {
                if (node->proc_def.is_foreign) {
                    break;
                }
                array_var_count = 0;
                slice_var_count = 0;
                var_type_count = 0;
                size_t for_in_counter = 0;
                bool const has_return_type = node->proc_def.return_type != nullptr;
                bool const has_array_return_type = has_return_type && node->proc_def.return_type->is_array;
                bool const has_slice_return_type = has_return_type && node->proc_def.return_type->is_slice;
                auto *params = &node->proc_def.parameters;
                bool const is_main_with_str_args =
                    node->proc_def.name.length == 4 &&
                    strncmp(node->proc_def.name.data, "main", 4) == 0 &&
                    params->length == 1 &&
                    params->data[0].is_slice &&
                    params->data[0].type_name.length == 3 &&
                    strncmp(params->data[0].type_name.data, "Str", 3) == 0;
                Str return_type_c = {};
                if (has_return_type && !has_array_return_type && !has_slice_return_type) {
                    if (node->proc_def.return_type->name == "Int") {
                        return_type_c = cstr_to_str("int");
                    }
                    else if (node->proc_def.return_type->name == "U8") {
                        return_type_c = cstr_to_str("uint8_t");
                    }
                    else if (node->proc_def.return_type->name == "Bool") {
                        return_type_c = cstr_to_str("bool");
                    }
                    else if (node->proc_def.return_type->name == "Str") {
                        return_type_c = cstr_to_str("BloomStr");
                    }
                    else {
                        return_type_c = node->proc_def.return_type->name;
                    }
                }
                else if (!has_return_type) {
                    return_type_c = is_main_with_str_args ? cstr_to_str("int") : cstr_to_str("void");
                }
                if (has_array_return_type) {
                    Str const &arr_elem = node->proc_def.return_type->name;
                    int64_t const arr_len = node->proc_def.return_type->array_length;
                    bool already_emitted = false;
                    for (size_t i = 0; i < emitted_array_return_type_count; i++) {
                        if (emitted_array_return_types[i].count == arr_len &&
                            emitted_array_return_types[i].element_type.length == arr_elem.length &&
                            strncmp(emitted_array_return_types[i].element_type.data, arr_elem.data, arr_elem.length) == 0)
                        {
                            already_emitted = true;
                            break;
                        }
                    }
                    if (!already_emitted) {
                        if (emitted_array_return_type_count < 16) {
                            emitted_array_return_types[emitted_array_return_type_count++] = {
                                .element_type = arr_elem,
                                .count = arr_len,
                            };
                        }
                        bool const is_u8_elem = arr_elem.length == 2 &&
                            arr_elem.data[0] == 'U' && arr_elem.data[1] == '8';
                        PUSH_STR("typedef struct { ");
                        PUSH_STR(is_u8_elem ? "uint8_t" : "int");
                        PUSH_STR(" data[");
                        PUSH_INT(arr_len);
                        PUSH_STR("]; } __bloom_Array_");
                        PUSH_STR(arr_elem);
                        PUSH_STR("_");
                        PUSH_INT(arr_len);
                        PUSH_STR(";\n");
                    }
                    PUSH_STR("__bloom_Array_");
                    PUSH_STR(arr_elem);
                    PUSH_STR("_");
                    PUSH_INT(arr_len);
                }
                else if (has_slice_return_type) {
                    Str const &slice_elem = node->proc_def.return_type->name;
                    bool already_emitted = false;
                    for (size_t i = 0; i < emitted_slice_return_type_count; i++) {
                        if (emitted_slice_return_types[i].length == slice_elem.length &&
                            strncmp(emitted_slice_return_types[i].data, slice_elem.data, slice_elem.length) == 0)
                        {
                            already_emitted = true;
                            break;
                        }
                    }
                    if (!already_emitted) {
                        if (emitted_slice_return_type_count < 16) {
                            emitted_slice_return_types[emitted_slice_return_type_count++] = slice_elem;
                        }
                        char const *sl_typedef_c;
                        if (slice_elem.length == 3 && strncmp(slice_elem.data, "Str", 3) == 0) {
                            sl_typedef_c = "BloomStr";
                        }
                        else if (slice_elem.length == 2 && slice_elem.data[0] == 'U' && slice_elem.data[1] == '8') {
                            sl_typedef_c = "uint8_t";
                        }
                        else {
                            sl_typedef_c = "int";
                        }
                        PUSH_STR("typedef struct { ");
                        PUSH_STR(sl_typedef_c);
                        PUSH_STR(" *data; size_t length; } __bloom_Slice_");
                        PUSH_STR(slice_elem);
                        PUSH_STR(";\n");
                    }
                    PUSH_STR("__bloom_Slice_");
                    PUSH_STR(slice_elem);
                }
                else {
                    PUSH_STR(return_type_c);
                }
                PUSH_STR(' ');
                PUSH_STR(node->proc_def.name);
                PUSH_STR('(');
                if (is_main_with_str_args) {
                    PUSH_STR("int argc, char const **argv");
                }
                else {
                    for (size_t i = 0; i < params->length; i++) {
                        auto *param = &params->data[i];
                        if (i != 0) {
                            PUSH_STR(", ");
                        }
                        if (param->is_array) {
                            // [N]Type parameter — decays to pointer in C
                            bool const is_u8_par = param->type_name.length == 2 &&
                                param->type_name.data[0] == 'U' && param->type_name.data[1] == '8';
                            PUSH_STR(is_u8_par ? "uint8_t" : "int");
                            PUSH_STR(" *");
                        }
                        else if (param->is_slice) {
                            PUSH_STR("BloomSliceU8 ");
                        }
                        else if (param->type_name == "Int") {
                            PUSH_STR("int ");
                        }
                        else if (param->type_name == "U8") {
                            PUSH_STR("uint8_t ");
                        }
                        else if (param->type_name == "Bool") {
                            PUSH_STR("bool ");
                        }
                        else if (param->type_name == "Str") {
                            PUSH_STR("BloomStr ");
                        }
                        else if (param->type_name == "CStr") {
                            PUSH_STR("char *");
                        }
                        else {
                            PUSH_STR(param->type_name);
                            if (param->is_pointer) {
                                PUSH_STR(" *");
                            }
                            else {
                                PUSH_STR(" ");
                            }
                        }
                        PUSH_STR(param->name);
                    }
                }
                PUSH_STR(')');
                PUSH_STR("{\n");
                PUSH_STR("\tBloomContext __bloom_context = {0};\n");
                if (is_main_with_str_args) {
                    Str const &param_name = params->data[0].name;
                    PUSH_STR("\tBloomStr *__bloom_");
                    PUSH_STR(param_name);
                    PUSH_STR("_data = (BloomStr *)(__bloom_context.temp_allocator.buf + __bloom_context.temp_allocator.offset);\n");
                    PUSH_STR("\t__bloom_context.temp_allocator.offset += (size_t)argc * sizeof(BloomStr);\n");
                    PUSH_STR("\tfor (int __bloom_args_i = 0; __bloom_args_i < argc; __bloom_args_i++) {\n");
                    PUSH_STR("\t\t__bloom_");
                    PUSH_STR(param_name);
                    PUSH_STR("_data[__bloom_args_i] = (BloomStr){ .data = argv[__bloom_args_i], .length = (size_t)strlen(argv[__bloom_args_i]) };\n");
                    PUSH_STR("\t}\n");
                    PUSH_STR("\tsize_t __bloom_");
                    PUSH_STR(param_name);
                    PUSH_STR("_len = (size_t)argc;\n");
                    if (slice_var_count < 32) {
                        slice_vars[slice_var_count++] = { .name = param_name, .element_type = cstr_to_str("Str") };
                    }
                }
                for (size_t pi = 0; pi < params->length; pi++) {
                    auto *param = &params->data[pi];
                    if (param->is_slice) {
                        if (is_main_with_str_args) { continue; }
                        PUSH_STR("\tuint8_t *");
                        PUSH_STR(param->name);
                        PUSH_STR("_cstr = (uint8_t*)");
                        PUSH_STR(param->name);
                        PUSH_STR(".data;\n");
                    }
                }

                // Register proc parameters so member access and pointer checks work inside the body
                for (size_t pi = 0; pi < params->length; pi++) {
                    auto *param = &params->data[pi];
                    if (param->is_array) {
                        // Register as array var so element access and slicing work inside the body
                        if (array_var_count < 64) {
                            array_vars[array_var_count++] = {
                                .name = param->name,
                                .count = (size_t)param->array_length,
                                .element_type = param->type_name,
                            };
                        }
                        register_var(param->name, VarKind::INT);
                        continue;
                    }
                    if (param->is_slice) {
                        if (!is_main_with_str_args) {
                            register_var(param->name, VarKind::SLICE_U8);
                        }
                        continue;
                    }
                    if (param->is_pointer) {
                        bool param_is_struct = false;
                        for (size_t si = 0; si < struct_def_count; si++) {
                            if (struct_defs[si].name.length == param->type_name.length &&
                                strncmp(struct_defs[si].name.data, param->type_name.data, param->type_name.length) == 0)
                            {
                                param_is_struct = true;
                                break;
                            }
                        }
                        if (param_is_struct) {
                            register_struct_var_ptr(param->name, param->type_name);
                        }
                        else {
                            register_var(param->name, VarKind::PTR);
                        }
                    }
                    else if (param->type_name == "Str") {
                        register_var(param->name, VarKind::BLOOM_STR);
                    }
                    else if (param->type_name == "Bool") {
                        register_var(param->name, VarKind::BOOL);
                    }
                    else {
                        bool param_is_struct = false;
                        for (size_t si = 0; si < struct_def_count; si++) {
                            if (struct_defs[si].name.length == param->type_name.length &&
                                strncmp(struct_defs[si].name.data, param->type_name.data, param->type_name.length) == 0)
                            {
                                param_is_struct = true;
                                break;
                            }
                        }
                        if (param_is_struct) {
                            register_struct_var(param->name, param->type_name);
                        }
                        else {
                            register_var(param->name, VarKind::INT);
                        }
                    }
                }

                std::function<void(ASTNode*, ASTNode*, int, bool)> emit_stmt;
                std::function<void(Array<ASTNode>*, ASTNode*, int, bool)> emit_body;

                emit_body = [&](Array<ASTNode> *body, ASTNode *owner, int depth, bool last_as_return) {
                    auto push_tabs = [&]() {
                        for (int d = 0; d < depth; d++) { PUSH_STR('\t'); }
                    };

                    // Only consider nodes directly owned by this scope (parent == owner).
                    // The body array may also contain nodes from nested scopes (whose
                    // parent is a child scope node), and those must not affect this scope's
                    // defer or return-value logic.
                    bool has_defers = false;
                    for (size_t bi = 0; bi < body->length; bi++) {
                        auto *s = &body->data[bi];
                        if (s->type == ASTNodeType::DEFER && s->parent == owner) {
                            has_defers = true;
                            break;
                        }
                    }

                    size_t last_non_defer_bi = body->length;
                    if (last_as_return) {
                        for (size_t bi = 0; bi < body->length; bi++) {
                            auto *s = &body->data[bi];
                            if (s->type != ASTNodeType::DEFER && s->parent == owner) {
                                last_non_defer_bi = bi;
                            }
                        }
                    }

                    // When defers coexist with a return value, capture the return value
                    // into a temp variable first, run the defers, then return the variable.
                    bool const needs_return_var = last_as_return && has_defers && last_non_defer_bi < body->length;

                    for (size_t bi = 0; bi < body->length; bi++) {
                        auto *s = &body->data[bi];
                        if (s->type == ASTNodeType::DEFER) { continue; }
                        if (needs_return_var && bi == last_non_defer_bi) {
                            push_tabs();
                            PUSH_STR(return_type_c);
                            PUSH_STR(" __bloom_return_val = ");
                            PUSH_INT(s->integer_literal.value.value);
                            PUSH_STR(";\n");
                        }
                        else {
                            bool const is_return = !needs_return_var && last_as_return && bi == last_non_defer_bi;
                            emit_stmt(s, owner, depth, is_return);
                        }
                    }

                    for (size_t bi = body->length; bi-- > 0; ) {
                        auto *s = &body->data[bi];
                        if (s->type != ASTNodeType::DEFER) { continue; }
                        emit_stmt(s, owner, depth, false);
                    }

                    if (needs_return_var) {
                        push_tabs();
                        PUSH_STR("return __bloom_return_val;\n");
                    }
                };

                emit_stmt = [&](ASTNode *stmt, ASTNode *owner, int depth, bool emit_as_return) {
                    if (stmt->parent != owner) {
                        return;
                    }
                    auto push_tabs = [&]() {
                        for (int d = 0; d < depth; d++) { PUSH_STR('\t'); }
                    };
                    switch (stmt->type) {
                        case ASTNodeType::INTEGER_LITERAL: {
                            push_tabs();
                            if (emit_as_return) { PUSH_STR("return "); }
                            PUSH_INT(stmt->integer_literal.value.value);
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::BINARY_ADD: {
                            push_tabs();
                            if (emit_as_return) { PUSH_STR("return "); }
                            auto *operands = &stmt->binary_operation.operands;
                            for (size_t i = 0; i < operands->length; i++) {
                                if (i != 0) { PUSH_STR(" + "); }
                                emit_binary_operand(&operands->data[i]);
                            }
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::BINARY_MUL: {
                            push_tabs();
                            if (emit_as_return) { PUSH_STR("return "); }
                            auto *operands = &stmt->binary_operation.operands;
                            for (size_t i = 0; i < operands->length; i++) {
                                if (i != 0) { PUSH_STR(" * "); }
                                emit_binary_operand(&operands->data[i]);
                            }
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::BINARY_DIV: {
                            push_tabs();
                            if (emit_as_return) { PUSH_STR("return "); }
                            auto *operands = &stmt->binary_operation.operands;
                            for (size_t i = 0; i < operands->length; i++) {
                                if (i != 0) { PUSH_STR(" / "); }
                                emit_binary_operand(&operands->data[i]);
                            }
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::BINARY_SUB: {
                            push_tabs();
                            if (emit_as_return) { PUSH_STR("return "); }
                            auto *operands = &stmt->binary_operation.operands;
                            for (size_t i = 0; i < operands->length; i++) {
                                if (i != 0) { PUSH_STR(" - "); }
                                emit_binary_operand(&operands->data[i]);
                            }
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::PROC_CALL: {
                            if (stmt->proc_call.caller_identifier == "print") {
                                assert(stmt->proc_call.arguments.length >= 1 &&
                                    "print() requires at least a format string argument");
                                auto *fmt_arg = &stmt->proc_call.arguments.data[0];
                                assert(fmt_arg->type == ASTNodeType::STRING_LITERAL &&
                                    "First argument to print() must be a string literal");
                                Str const *fmt = &fmt_arg->string_literal.value;
                                size_t arg_idx = 1;
                                size_t seg_start = 0;
                                for (size_t k = 0; k < fmt->length; k++) {
                                    if (fmt->data[k] == '{' && k + 1 < fmt->length &&
                                        fmt->data[k + 1] == '}')
                                    {
                                        if (k > seg_start) {
                                            push_tabs();
                                            PUSH_STR("fputs(\"");
                                            PUSH_STR(str_from_data_and_length(
                                                fmt->data + seg_start, k - seg_start));
                                            PUSH_STR("\", stdout);\n");
                                        }
                                        if (arg_idx < stmt->proc_call.arguments.length) {
                                            auto *arg = &stmt->proc_call.arguments.data[arg_idx];
                                            push_tabs();
                                            if (arg->type == ASTNodeType::IDENTIFIER) {
                                                VarKind kind = lookup_var_kind(arg->identifier);
                                                if (kind == VarKind::BLOOM_CHAR) {
                                                    PUSH_STR("fwrite(");
                                                    PUSH_STR(arg->identifier);
                                                    PUSH_STR(".bytes, 1, ");
                                                    PUSH_STR(arg->identifier);
                                                    PUSH_STR(".len, stdout);\n");
                                                }
                                                else if (kind == VarKind::BLOOM_STR) {
                                                    PUSH_STR("fwrite(");
                                                    PUSH_STR(arg->identifier);
                                                    PUSH_STR(".data, 1, ");
                                                    PUSH_STR(arg->identifier);
                                                    PUSH_STR(".length, stdout);\n");
                                                }
                                                else if (kind == VarKind::SIZE_T) {
                                                    PUSH_STR("printf(\"%zu\", ");
                                                    PUSH_STR(arg->identifier);
                                                    PUSH_STR(");\n");
                                                }
                                                else if (kind == VarKind::BOOL) {
                                                    PUSH_STR("fputs(");
                                                    PUSH_STR(arg->identifier);
                                                    PUSH_STR(" ? \"true\" : \"false\", stdout);\n");
                                                }
                                                else if (kind == VarKind::STRUCT) {
                                                    Str struct_type_name = {};
                                                    for (size_t vi = 0; vi < var_type_count; vi++) {
                                                        if (var_types[vi].name.length == arg->identifier.length &&
                                                            strncmp(var_types[vi].name.data, arg->identifier.data, arg->identifier.length) == 0)
                                                        {
                                                            struct_type_name = var_types[vi].struct_type_name;
                                                            break;
                                                        }
                                                    }
                                                    for (size_t di = 0; di < struct_def_count; di++) {
                                                        auto *sdef = &struct_defs[di];
                                                        if (sdef->name.length != struct_type_name.length ||
                                                            strncmp(sdef->name.data, struct_type_name.data, struct_type_name.length) != 0) { continue; }
                                                        for (size_t fi = 0; fi < sdef->field_count; fi++) {
                                                            auto *sfield = &sdef->fields[fi];
                                                            if (sfield->kind == VarKind::BLOOM_STR) {
                                                                PUSH_STR("fwrite(");
                                                                PUSH_STR(arg->identifier);
                                                                PUSH_STR(".");
                                                                PUSH_STR(sfield->name);
                                                                PUSH_STR(".data, 1, ");
                                                                PUSH_STR(arg->identifier);
                                                                PUSH_STR(".");
                                                                PUSH_STR(sfield->name);
                                                                PUSH_STR(".length, stdout);\n");
                                                            }
                                                            else if (sfield->kind == VarKind::BOOL) {
                                                                PUSH_STR("fputs(");
                                                                PUSH_STR(arg->identifier);
                                                                PUSH_STR(".");
                                                                PUSH_STR(sfield->name);
                                                                PUSH_STR(" ? \"true\" : \"false\", stdout);\n");
                                                            }
                                                            else {
                                                                PUSH_STR("printf(\"%d\", ");
                                                                PUSH_STR(arg->identifier);
                                                                PUSH_STR(".");
                                                                PUSH_STR(sfield->name);
                                                                PUSH_STR(");\n");
                                                            }
                                                            if (fi + 1 < sdef->field_count) { push_tabs(); }
                                                        }
                                                        break;
                                                    }
                                                }
                                                else {
                                                    PUSH_STR("printf(\"%d\", ");
                                                    PUSH_STR(arg->identifier);
                                                    PUSH_STR(");\n");
                                                }
                                            }
                                            else if (arg->type == ASTNodeType::BOOLEAN_LITERAL) {
                                                PUSH_STR("fputs(");
                                                PUSH_STR(arg->boolean_literal.value ? "\"true\"" : "\"false\"");
                                                PUSH_STR(", stdout);\n");
                                            }
                                            else if (arg->type == ASTNodeType::INTEGER_LITERAL) {
                                                PUSH_STR("printf(\"%d\", ");
                                                PUSH_INT(arg->integer_literal.value.value);
                                                PUSH_STR(");\n");
                                            }
                                            else if (arg->type == ASTNodeType::ARRAY_ACCESS) {
                                                bool const is_sv_print = [&]() {
                                                    for (size_t i = slice_var_count; i-- > 0;) {
                                                        if (slice_vars[i].name.length == arg->array_access.variable_name.length &&
                                                            strncmp(slice_vars[i].name.data, arg->array_access.variable_name.data,
                                                                    arg->array_access.variable_name.length) == 0) { return true; }
                                                    }
                                                    return false;
                                                }();
                                                PUSH_STR("printf(\"%d\", ");
                                                if (is_sv_print) {
                                                    PUSH_STR("__bloom_");
                                                    PUSH_STR(arg->array_access.variable_name);
                                                    PUSH_STR("_data[");
                                                    PUSH_INT(arg->array_access.index);
                                                    PUSH_STR("]);\n");
                                                }
                                                else {
                                                    PUSH_STR(arg->array_access.variable_name);
                                                    PUSH_STR('[');
                                                    PUSH_INT(arg->array_access.index);
                                                    PUSH_STR("]);\n");
                                                }
                                            }
                                            else if (arg->type == ASTNodeType::BINARY_ADD) {
                                                PUSH_STR("printf(\"%d\", ");
                                                auto *ops = &arg->binary_operation.operands;
                                                for (size_t k = 0; k < ops->length; k++) {
                                                    if (k != 0) { PUSH_STR(" + "); }
                                                    emit_binary_operand(&ops->data[k]);
                                                }
                                                PUSH_STR(");\n");
                                            }
                                            else if (arg->type == ASTNodeType::BINARY_MUL) {
                                                PUSH_STR("printf(\"%d\", ");
                                                auto *ops = &arg->binary_operation.operands;
                                                for (size_t k = 0; k < ops->length; k++) {
                                                    if (k != 0) { PUSH_STR(" * "); }
                                                    emit_binary_operand(&ops->data[k]);
                                                }
                                                PUSH_STR(");\n");
                                            }
                                            else if (arg->type == ASTNodeType::BINARY_DIV) {
                                                PUSH_STR("printf(\"%d\", ");
                                                auto *ops = &arg->binary_operation.operands;
                                                for (size_t k = 0; k < ops->length; k++) {
                                                    if (k != 0) { PUSH_STR(" / "); }
                                                    emit_binary_operand(&ops->data[k]);
                                                }
                                                PUSH_STR(");\n");
                                            }
                                            else if (arg->type == ASTNodeType::BINARY_SUB) {
                                                PUSH_STR("printf(\"%d\", ");
                                                auto *ops = &arg->binary_operation.operands;
                                                for (size_t k = 0; k < ops->length; k++) {
                                                    if (k != 0) { PUSH_STR(" - "); }
                                                    emit_binary_operand(&ops->data[k]);
                                                }
                                                PUSH_STR(");\n");
                                            }
                                            else if (arg->type == ASTNodeType::MEMBER_ACCESS) {
                                                VarKind kind = lookup_member_kind(
                                                    arg->member_access.object_name,
                                                    arg->member_access.field_name
                                                );
                                                char const *acc = is_pointer_var(arg->member_access.object_name) ? "->" : ".";
                                                if (kind == VarKind::BOOL) {
                                                    PUSH_STR("fputs(");
                                                    PUSH_STR(arg->member_access.object_name);
                                                    PUSH_STR(acc);
                                                    PUSH_STR(arg->member_access.field_name);
                                                    PUSH_STR(" ? \"true\" : \"false\", stdout);\n");
                                                }
                                                else if (kind == VarKind::BLOOM_STR) {
                                                    PUSH_STR("fwrite(");
                                                    PUSH_STR(arg->member_access.object_name);
                                                    PUSH_STR(acc);
                                                    PUSH_STR(arg->member_access.field_name);
                                                    PUSH_STR(".data, 1, ");
                                                    PUSH_STR(arg->member_access.object_name);
                                                    PUSH_STR(acc);
                                                    PUSH_STR(arg->member_access.field_name);
                                                    PUSH_STR(".length, stdout);\n");
                                                }
                                                else {
                                                    PUSH_STR("printf(\"%d\", ");
                                                    PUSH_STR(arg->member_access.object_name);
                                                    PUSH_STR(acc);
                                                    PUSH_STR(arg->member_access.field_name);
                                                    PUSH_STR(");\n");
                                                }
                                            }
                                            else if (arg->type == ASTNodeType::DEREF) {
                                                PUSH_STR("printf(\"%d\", *");
                                                PUSH_STR(arg->identifier);
                                                PUSH_STR(");\n");
                                            }
                                            else if (arg->type == ASTNodeType::COMPARISON) {
                                                PUSH_STR("fputs((");
                                                auto *ops = &arg->binary_operation.operands;
                                                emit_binary_operand(&ops->data[0]);
                                                PUSH_STR(" == ");
                                                emit_binary_operand(&ops->data[1]);
                                                PUSH_STR(") ? \"true\" : \"false\", stdout);\n");
                                            }
                                            else if (arg->type == ASTNodeType::STRING_LITERAL) {
                                                size_t const runtime_len = str_literal_runtime_length(arg->string_literal.value);
                                                PUSH_STR("fwrite(\"");
                                                PUSH_STR(arg->string_literal.value);
                                                PUSH_STR("\", 1, ");
                                                PUSH_INT(static_cast<intmax_t>(runtime_len));
                                                PUSH_STR(", stdout);\n");
                                            }
                                            else if (arg->type == ASTNodeType::TYPE_INFO_NAME) {
                                                Str tn = arg->type_info_name.type_name;
                                                PUSH_STR("fwrite(\"");
                                                PUSH_STR(tn);
                                                PUSH_STR("\", 1, ");
                                                PUSH_INT(static_cast<intmax_t>(tn.length));
                                                PUSH_STR(", stdout);\n");
                                            }
                                            else if (arg->type == ASTNodeType::TYPE_INFO_ENUM_MEMBER_KEY) {
                                                push_tabs();
                                                PUSH_STR("fwrite(__bloom_");
                                                PUSH_STR(arg->type_info_enum_member_key.enum_type_name);
                                                PUSH_STR("_members[");
                                                PUSH_STR(arg->type_info_enum_member_key.index_var);
                                                PUSH_STR("].name.data, 1, __bloom_");
                                                PUSH_STR(arg->type_info_enum_member_key.enum_type_name);
                                                PUSH_STR("_members[");
                                                PUSH_STR(arg->type_info_enum_member_key.index_var);
                                                PUSH_STR("].name.length, stdout);\n");
                                            }
                                            else if (arg->type == ASTNodeType::TYPE_INFO_SIZE) {
                                                PUSH_STR("printf(\"%zu\", ");
                                                emit_expression(arg);
                                                PUSH_STR(");\n");
                                            }
                                            else if (arg->type == ASTNodeType::ARRAY_ELEMENT_MEMBER_ACCESS) {
                                                VarKind kind = lookup_array_element_member_kind(
                                                    arg->array_element_member_access.array_name,
                                                    arg->array_element_member_access.field_name
                                                );
                                                if (kind == VarKind::BLOOM_STR) {
                                                    PUSH_STR("fwrite(");
                                                    PUSH_STR(arg->array_element_member_access.array_name);
                                                    PUSH_STR("[");
                                                    PUSH_INT(arg->array_element_member_access.element_index);
                                                    PUSH_STR("].");
                                                    PUSH_STR(arg->array_element_member_access.field_name);
                                                    PUSH_STR(".data, 1, ");
                                                    PUSH_STR(arg->array_element_member_access.array_name);
                                                    PUSH_STR("[");
                                                    PUSH_INT(arg->array_element_member_access.element_index);
                                                    PUSH_STR("].");
                                                    PUSH_STR(arg->array_element_member_access.field_name);
                                                    PUSH_STR(".length, stdout);\n");
                                                }
                                                else if (kind == VarKind::BOOL) {
                                                    PUSH_STR("fputs(");
                                                    PUSH_STR(arg->array_element_member_access.array_name);
                                                    PUSH_STR("[");
                                                    PUSH_INT(arg->array_element_member_access.element_index);
                                                    PUSH_STR("].");
                                                    PUSH_STR(arg->array_element_member_access.field_name);
                                                    PUSH_STR(" ? \"true\" : \"false\", stdout);\n");
                                                }
                                                else {
                                                    PUSH_STR("printf(\"%d\", ");
                                                    PUSH_STR(arg->array_element_member_access.array_name);
                                                    PUSH_STR("[");
                                                    PUSH_INT(arg->array_element_member_access.element_index);
                                                    PUSH_STR("].");
                                                    PUSH_STR(arg->array_element_member_access.field_name);
                                                    PUSH_STR(");\n");
                                                }
                                            }
                                            arg_idx++;
                                        }
                                        k++;
                                        seg_start = k + 1;
                                    }
                                }
                                if (seg_start < fmt->length) {
                                    push_tabs();
                                    PUSH_STR("fputs(\"");
                                    PUSH_STR(str_from_data_and_length(
                                        fmt->data + seg_start, fmt->length - seg_start));
                                    PUSH_STR("\", stdout);\n");
                                }
                            }
                            else {
                                // For calls to C functions (not user procs), guard against
                                // null pointer member accesses to avoid UB.
                                ASTNode *ptr_field_arg = nullptr;
                                if (!is_user_proc(stmt->proc_call.caller_identifier)) {
                                    for (size_t ai = 0; ai < stmt->proc_call.arguments.length; ai++) {
                                        auto *a = &stmt->proc_call.arguments.data[ai];
                                        if (a->type == ASTNodeType::MEMBER_ACCESS &&
                                            lookup_member_kind(a->member_access.object_name, a->member_access.field_name) == VarKind::PTR)
                                        {
                                            ptr_field_arg = a;
                                            break;
                                        }
                                    }
                                }
                                if (ptr_field_arg != nullptr) {
                                    char const *acc = is_pointer_var(ptr_field_arg->member_access.object_name) ? "->" : ".";
                                    push_tabs();
                                    PUSH_STR("if (");
                                    PUSH_STR(ptr_field_arg->member_access.object_name);
                                    PUSH_STR(acc);
                                    PUSH_STR(ptr_field_arg->member_access.field_name);
                                    PUSH_STR(") {\n");
                                    push_tabs();
                                    PUSH_STR('\t');
                                    PUSH_STR(stmt->proc_call.caller_identifier);
                                    PUSH_STR('(');
                                    emit_proc_call_args(&stmt->proc_call.arguments, stmt->proc_call.caller_identifier);
                                    PUSH_STR(");\n");
                                    push_tabs();
                                    PUSH_STR("}\n");
                                }
                                else {
                                    push_tabs();
                                    if (emit_as_return) { PUSH_STR("return "); }
                                    if (stmt->proc_call.caller_identifier == "clone_to_cstr") {
                                        PUSH_STR("__bloom_clone_to_cstr");
                                    }
                                    else {
                                        PUSH_STR(stmt->proc_call.caller_identifier);
                                    }
                                    PUSH_STR('(');
                                    emit_proc_call_args(&stmt->proc_call.arguments, stmt->proc_call.caller_identifier);
                                    PUSH_STR(");\n");
                                }
                            }
                            break;
                        }
                        case ASTNodeType::CONSTANT_DEFINITION: {
                            push_tabs();
                            register_var(stmt->constant_def.name, VarKind::INT);
                            PUSH_STR("int const ");
                            PUSH_STR(stmt->constant_def.name);
                            PUSH_STR(" = ");
                            PUSH_INT(stmt->constant_def.value);
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::VARIABLE_DEFINITION: {
                            push_tabs();
                            ASTNode *expr = stmt->variable_definition.expr;
                            if (expr->type == ASTNodeType::MEMBER_ACCESS &&
                                is_enum_type(expr->member_access.object_name))
                            {
                                register_var(stmt->variable_definition.name, VarKind::INT);
                                PUSH_STR(expr->member_access.object_name);
                                PUSH_STR(" ");
                                PUSH_STR(stmt->variable_definition.name);
                                PUSH_STR(" = __bloom_");
                                PUSH_STR(expr->member_access.object_name);
                                PUSH_STR("_");
                                PUSH_STR(expr->member_access.field_name);
                                PUSH_STR(";\n");
                                break;
                            }
                            if (expr->type == ASTNodeType::TYPE_INFO_STORE) {
                                Str tn = expr->type_info_store.type_name;
                                Str typeinfo_type = { .data = "TypeInfo", .length = 8 };
                                register_struct_var(stmt->variable_definition.name, typeinfo_type);
                                PUSH_STR("BloomTypeInfo const ");
                                PUSH_STR(stmt->variable_definition.name);
                                PUSH_STR(" = {.name = (BloomStr){.data = \"");
                                PUSH_STR(tn);
                                PUSH_STR("\", .length = ");
                                PUSH_INT(static_cast<intmax_t>(tn.length));
                                PUSH_STR("}, .size_in_bytes = (int)sizeof(");
                                if (tn == "Int")       { PUSH_STR("int"); }
                                else if (tn == "U8")   { PUSH_STR("uint8_t"); }
                                else if (tn == "Bool") { PUSH_STR("bool"); }
                                else if (tn == "Str")  { PUSH_STR("BloomStr"); }
                                else if (tn == "CStr") { PUSH_STR("char const *"); }
                                else                   { PUSH_STR(tn); }
                                PUSH_STR(")};\n");
                                break;
                            }
                            if (expr->type == ASTNodeType::ADDRESS_OF) {
                                Str struct_type_name = {};
                                if (lookup_var_kind(expr->identifier) == VarKind::STRUCT) {
                                    for (size_t vi = 0; vi < var_type_count; vi++) {
                                        if (var_types[vi].name.length == expr->identifier.length &&
                                            strncmp(var_types[vi].name.data, expr->identifier.data, expr->identifier.length) == 0)
                                        {
                                            struct_type_name = var_types[vi].struct_type_name;
                                            break;
                                        }
                                    }
                                }
                                if (struct_type_name.length > 0) {
                                    register_struct_var_ptr(stmt->variable_definition.name, struct_type_name);
                                    PUSH_STR(struct_type_name);
                                    PUSH_STR(" *");
                                } else {
                                    register_var(stmt->variable_definition.name, VarKind::PTR);
                                    PUSH_STR("int *");
                                }
                                PUSH_STR(stmt->variable_definition.name);
                                PUSH_STR(" = &");
                                PUSH_STR(expr->identifier);
                                PUSH_STR(";\n");
                                break;
                            }
                            if (expr->type == ASTNodeType::STRUCT_INIT) {
                                register_struct_var(stmt->variable_definition.name, expr->struct_init.type_name);
                                PUSH_STR(expr->struct_init.type_name);
                                PUSH_STR(' ');
                                PUSH_STR(stmt->variable_definition.name);
                                PUSH_STR(" = ");
                                if (expr->struct_init.field_names.length == 0) {
                                    PUSH_STR("{0}");
                                }
                                else {
                                    PUSH_STR("{");
                                    for (size_t fi = 0; fi < expr->struct_init.field_names.length; fi++) {
                                        if (fi != 0) { PUSH_STR(", "); }
                                        PUSH_STR(".");
                                        PUSH_STR(expr->struct_init.field_names.data[fi].name);
                                        PUSH_STR(" = ");
                                        emit_binary_operand(&expr->struct_init.field_values.data[fi]);
                                    }
                                    PUSH_STR("}");
                                }
                                PUSH_STR(";\n");
                                break;
                            }
                            if (expr->type == ASTNodeType::ARRAY_INIT) {
                                if (array_var_count < 64) {
                                    array_vars[array_var_count++] = {
                                        .name = stmt->variable_definition.name,
                                        .count = expr->array_init.elements.length,
                                        .element_type = expr->array_init.element_type,
                                    };
                                }
                                bool const is_u8_array = expr->array_init.element_type == "U8";
                                register_var(stmt->variable_definition.name, VarKind::INT);
                                PUSH_STR(is_u8_array ? "uint8_t " : "int ");
                                PUSH_STR(stmt->variable_definition.name);
                                auto *elems = &expr->array_init.elements;
                                bool all_same = elems->length > 1;
                                for (size_t i = 1; i < elems->length && all_same; i++) {
                                    if (elems->data[i] != elems->data[0]) { all_same = false; }
                                }
                                if (all_same && elems->length > 1) {
                                    PUSH_STR("[");
                                    PUSH_INT(static_cast<int64_t>(elems->length));
                                    PUSH_STR("] = {");
                                    PUSH_INT(elems->data[0]);
                                } else {
                                    PUSH_STR("[] = {");
                                    for (size_t i = 0; i < elems->length; i++) {
                                        if (i != 0) { PUSH_STR(", "); }
                                        PUSH_INT(elems->data[i]);
                                    }
                                }
                                PUSH_STR("};\n");
                                break;
                            }
                            if (expr->type == ASTNodeType::ARRAY_ACCESS) {
                                Str elem_type = {};
                                for (size_t i = 0; i < array_struct_var_count; i++) {
                                    if (array_struct_vars[i].name.length == expr->array_access.variable_name.length &&
                                        strncmp(array_struct_vars[i].name.data, expr->array_access.variable_name.data, expr->array_access.variable_name.length) == 0)
                                    {
                                        elem_type = array_struct_vars[i].element_type;
                                        break;
                                    }
                                }
                                if (elem_type.length > 0) {
                                    register_struct_var(stmt->variable_definition.name, elem_type);
                                    PUSH_STR(elem_type);
                                    PUSH_STR(' ');
                                    PUSH_STR(stmt->variable_definition.name);
                                    PUSH_STR(" = ");
                                    PUSH_STR(expr->array_access.variable_name);
                                    PUSH_STR("[");
                                    PUSH_INT(expr->array_access.index);
                                    PUSH_STR("];\n");
                                    break;
                                }
                            }
                            if (expr->type == ASTNodeType::ARRAY_STRUCT_INIT) {
                                register_array_struct_var(stmt->variable_definition.name, expr->array_struct_init.element_type, expr->array_struct_init.elements.length);
                                register_var(stmt->variable_definition.name, VarKind::STRUCT);
                                PUSH_STR(expr->array_struct_init.element_type);
                                PUSH_STR(" ");
                                PUSH_STR(stmt->variable_definition.name);
                                PUSH_STR("[] = {");
                                auto *elems = &expr->array_struct_init.elements;
                                for (size_t ei = 0; ei < elems->length; ei++) {
                                    if (ei != 0) { PUSH_STR(", "); }
                                    auto *elem = &elems->data[ei];
                                    PUSH_STR("{");
                                    for (size_t fi = 0; fi < elem->struct_init.field_names.length; fi++) {
                                        if (fi != 0) { PUSH_STR(", "); }
                                        PUSH_STR(".");
                                        PUSH_STR(elem->struct_init.field_names.data[fi].name);
                                        PUSH_STR(" = ");
                                        emit_binary_operand(&elem->struct_init.field_values.data[fi]);
                                    }
                                    PUSH_STR("}");
                                }
                                PUSH_STR("};\n");
                                break;
                            }
                            if (expr->type == ASTNodeType::PROC_CALL &&
                                expr->proc_call.caller_identifier == "clone_to_cstr") {
                                register_var(stmt->variable_definition.name, VarKind::PTR);
                                PUSH_STR("char *");
                                PUSH_STR(stmt->variable_definition.name);
                                PUSH_STR(" = __bloom_clone_to_cstr(");
                                emit_proc_call_args(&expr->proc_call.arguments, expr->proc_call.caller_identifier);
                                PUSH_STR(");\n");
                                break;
                            }
                            if (expr->type == ASTNodeType::PROC_CALL) {
                                TypeASTNode *ret_type = lookup_proc_return_type(expr->proc_call.caller_identifier);
                                if (ret_type != nullptr && ret_type->is_array) {
                                    bool const is_u8_arr = ret_type->name.length == 2 &&
                                        ret_type->name.data[0] == 'U' && ret_type->name.data[1] == '8';
                                    if (array_var_count < 64) {
                                        array_vars[array_var_count++] = {
                                            .name = stmt->variable_definition.name,
                                            .count = (size_t)ret_type->array_length,
                                            .element_type = ret_type->name,
                                        };
                                    }
                                    register_var(stmt->variable_definition.name, VarKind::INT);
                                    PUSH_STR("__bloom_Array_");
                                    PUSH_STR(ret_type->name);
                                    PUSH_STR("_");
                                    PUSH_INT(ret_type->array_length);
                                    PUSH_STR(" __bloom_tmp_");
                                    PUSH_STR(stmt->variable_definition.name);
                                    PUSH_STR(" = ");
                                    emit_expression(expr);
                                    PUSH_STR(";\n");
                                    push_tabs();
                                    PUSH_STR(is_u8_arr ? "uint8_t" : "int");
                                    PUSH_STR(" *");
                                    PUSH_STR(stmt->variable_definition.name);
                                    PUSH_STR(" = __bloom_tmp_");
                                    PUSH_STR(stmt->variable_definition.name);
                                    PUSH_STR(".data;\n");
                                    break;
                                }
                                if (ret_type != nullptr && ret_type->is_slice) {
                                    char const *sl_unpack_c;
                                    if (ret_type->name.length == 3 && strncmp(ret_type->name.data, "Str", 3) == 0) {
                                        sl_unpack_c = "BloomStr";
                                    }
                                    else if (ret_type->name.length == 2 && ret_type->name.data[0] == 'U' && ret_type->name.data[1] == '8') {
                                        sl_unpack_c = "uint8_t";
                                    }
                                    else {
                                        sl_unpack_c = "int";
                                    }
                                    if (slice_var_count < 32) {
                                        slice_vars[slice_var_count++] = {
                                            .name = stmt->variable_definition.name,
                                            .element_type = ret_type->name,
                                        };
                                    }
                                    register_var(stmt->variable_definition.name, VarKind::INT);
                                    PUSH_STR("__bloom_Slice_");
                                    PUSH_STR(ret_type->name);
                                    PUSH_STR(" __bloom_tmp_");
                                    PUSH_STR(stmt->variable_definition.name);
                                    PUSH_STR(" = ");
                                    emit_expression(expr);
                                    PUSH_STR(";\n");
                                    push_tabs();
                                    PUSH_STR(sl_unpack_c);
                                    PUSH_STR(" *__bloom_");
                                    PUSH_STR(stmt->variable_definition.name);
                                    PUSH_STR("_data = __bloom_tmp_");
                                    PUSH_STR(stmt->variable_definition.name);
                                    PUSH_STR(".data;\n");
                                    push_tabs();
                                    PUSH_STR("size_t __bloom_");
                                    PUSH_STR(stmt->variable_definition.name);
                                    PUSH_STR("_len = __bloom_tmp_");
                                    PUSH_STR(stmt->variable_definition.name);
                                    PUSH_STR(".length;\n");
                                    break;
                                }
                                if (ret_type != nullptr && ret_type->is_pointer) {
                                    register_var(stmt->variable_definition.name, VarKind::PTR);
                                    PUSH_STR(ret_type->name);
                                    PUSH_STR(" *");
                                    PUSH_STR(stmt->variable_definition.name);
                                    PUSH_STR(" = ");
                                    emit_expression(expr);
                                    PUSH_STR(";\n");
                                    break;
                                }
                                if (ret_type != nullptr &&
                                    !(ret_type->name == "Int") &&
                                    !(ret_type->name == "Bool") &&
                                    !(ret_type->name == "Str"))
                                {
                                    register_struct_var(stmt->variable_definition.name, ret_type->name);
                                    PUSH_STR(ret_type->name);
                                    PUSH_STR(' ');
                                    PUSH_STR(stmt->variable_definition.name);
                                    PUSH_STR(" = ");
                                    emit_expression(expr);
                                    PUSH_STR(";\n");
                                    break;
                                }
                            }
                            if (expr->type == ASTNodeType::ARRAY_SLICE) {
                                Str elem_type = {};
                                size_t arr_count = 0;
                                for (size_t i = array_var_count; i-- > 0;) {
                                    if (array_vars[i].name.length == expr->array_slice.variable_name.length &&
                                        strncmp(array_vars[i].name.data, expr->array_slice.variable_name.data, expr->array_slice.variable_name.length) == 0)
                                    {
                                        elem_type = array_vars[i].element_type;
                                        arr_count = array_vars[i].count;
                                        break;
                                    }
                                }
                                bool const is_u8 = elem_type.length == 2 &&
                                    elem_type.data[0] == 'U' && elem_type.data[1] == '8';
                                char const *c_elem = is_u8 ? "uint8_t" : "int";
                                int64_t const start = (expr->array_slice.start_index < 0) ? 0 : expr->array_slice.start_index;
                                int64_t const end = (expr->array_slice.end_index < 0)
                                    ? (int64_t)arr_count
                                    : expr->array_slice.end_index;
                                Str const varname = stmt->variable_definition.name;
                                if (slice_var_count < 32) {
                                    slice_vars[slice_var_count++] = { .name = varname, .element_type = elem_type };
                                }
                                // emit: int *__bloom_as_data = arr + offset;
                                push_tabs();
                                PUSH_STR(c_elem);
                                PUSH_STR(" *__bloom_");
                                PUSH_STR(varname);
                                PUSH_STR("_data = ");
                                PUSH_STR(expr->array_slice.variable_name);
                                if (start > 0) {
                                    PUSH_STR(" + ");
                                    PUSH_INT(start);
                                }
                                PUSH_STR(";\n");
                                // emit: size_t __bloom_as_len = length;
                                push_tabs();
                                PUSH_STR("size_t __bloom_");
                                PUSH_STR(varname);
                                PUSH_STR("_len = ");
                                PUSH_INT(end - start);
                                PUSH_STR(";\n");
                                break;
                            }
                            char const *c_type = nullptr;
                            VarKind var_kind = VarKind::INT;
                            if (expr->type == ASTNodeType::BOOLEAN_LITERAL) {
                                c_type = "bool";
                                var_kind = VarKind::BOOL;
                            }
                            else if (expr->type == ASTNodeType::STRING_LITERAL) {
                                c_type = "BloomStr";
                                var_kind = VarKind::BLOOM_STR;
                            }
                            else if (expr->type == ASTNodeType::PROC_CALL) {
                                TypeASTNode *ret_type = lookup_proc_return_type(expr->proc_call.caller_identifier);
                                if (ret_type != nullptr) {
                                    if (ret_type->name == "Bool") {
                                        c_type = "bool";
                                        var_kind = VarKind::BOOL;
                                    }
                                    else if (ret_type->name == "Str") {
                                        c_type = "BloomStr";
                                        var_kind = VarKind::BLOOM_STR;
                                    }
                                    else {
                                        c_type = "int";
                                        var_kind = VarKind::INT;
                                    }
                                }
                                else {
                                    c_type = "int";
                                    var_kind = VarKind::INT;
                                }
                            }
                            else {
                                c_type = "int";
                                var_kind = VarKind::INT;
                            }
                            register_var(stmt->variable_definition.name, var_kind);
                            PUSH_STR(c_type);
                            PUSH_STR(' ');
                            PUSH_STR(stmt->variable_definition.name);
                            PUSH_STR(" = ");
                            emit_expression(expr);
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::STRUCT_INIT: {
                            push_tabs();
                            if (emit_as_return) { PUSH_STR("return "); }
                            PUSH_STR("(");
                            PUSH_STR(stmt->struct_init.type_name);
                            PUSH_STR("){");
                            for (size_t fi = 0; fi < stmt->struct_init.field_names.length; fi++) {
                                if (fi != 0) { PUSH_STR(", "); }
                                PUSH_STR(".");
                                PUSH_STR(stmt->struct_init.field_names.data[fi].name);
                                PUSH_STR(" = ");
                                emit_binary_operand(&stmt->struct_init.field_values.data[fi]);
                            }
                            PUSH_STR("};\n");
                            break;
                        }
                        case ASTNodeType::ADD_ASSIGN: {
                            push_tabs();
                            PUSH_STR(stmt->add_assign.variable_name);
                            PUSH_STR(" += ");
                            emit_binary_operand(&stmt->add_assign.operand);
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::MEMBER_ASSIGN: {
                            push_tabs();
                            PUSH_STR(stmt->member_assign.object_name);
                            PUSH_STR(is_pointer_var(stmt->member_assign.object_name) ? "->" : ".");
                            PUSH_STR(stmt->member_assign.field_name);
                            PUSH_STR(" = ");
                            emit_expression(stmt->member_assign.expr);
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::ARRAY_ELEMENT_ASSIGN: {
                            push_tabs();
                            PUSH_STR(stmt->array_element_assign.variable_name);
                            PUSH_STR('[');
                            PUSH_INT(stmt->array_element_assign.index);
                            PUSH_STR(']');
                            PUSH_STR(" = ");
                            emit_expression(stmt->array_element_assign.expr);
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::ARRAY_RANGE_ASSIGN: {
                            push_tabs();
                            ASTNode *val = stmt->array_range_assign.value_expr;
                            ASTNode *inner = (val->type == ASTNodeType::SLICE_CAST) ? val->slice_cast.expr : val;
                            PUSH_STR("memcpy((uint8_t*)");
                            PUSH_STR(stmt->array_range_assign.variable_name);
                            PUSH_STR(" + ");
                            PUSH_INT(stmt->array_range_assign.start_index);
                            PUSH_STR(", ");
                            if (inner->type == ASTNodeType::INTLE_CAST ||
                                inner->type == ASTNodeType::INTBE_CAST)
                            {
                                ASTNode *int_inner = inner->intle_cast;
                                bool const is_le = (inner->type == ASTNodeType::INTLE_CAST);
                                PUSH_STR("&(uint32_t){");
                                PUSH_STR(is_le ? "__bloom_u32_le(" : "__bloom_u32_be(");
                                if (int_inner->type == ASTNodeType::MEMBER_ACCESS) {
                                    char const *acc = is_pointer_var(int_inner->member_access.object_name) ? "->" : ".";
                                    PUSH_STR(int_inner->member_access.object_name);
                                    PUSH_STR(acc);
                                    PUSH_STR(int_inner->member_access.field_name);
                                }
                                else {
                                    PUSH_STR(int_inner->identifier);
                                }
                                PUSH_STR(")}, 4);\n");
                            }
                            else if (inner->type == ASTNodeType::MEMBER_ACCESS) {
                                char const *acc = is_pointer_var(inner->member_access.object_name) ? "->" : ".";
                                PUSH_STR(inner->member_access.object_name);
                                PUSH_STR(acc);
                                PUSH_STR(inner->member_access.field_name);
                                PUSH_STR(".data, ");
                                PUSH_STR(inner->member_access.object_name);
                                PUSH_STR(acc);
                                PUSH_STR(inner->member_access.field_name);
                                PUSH_STR(".length);\n");
                            }
                            else {
                                PUSH_STR(inner->identifier);
                                PUSH_STR(".data, ");
                                PUSH_STR(inner->identifier);
                                PUSH_STR(".length);\n");
                            }
                            break;
                        }
                        case ASTNodeType::RETURN: {
                            push_tabs();
                            PUSH_STR("return");
                            if (stmt->return_value != nullptr &&
                                stmt->return_value->type == ASTNodeType::INTEGER_LITERAL)
                            {
                                PUSH_STR(' ');
                                PUSH_INT(stmt->return_value->integer_literal.value.value);
                            }
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::BREAK: {
                            push_tabs();
                            PUSH_STR("break;\n");
                            break;
                        }
                        case ASTNodeType::SCOPE: {
                            push_tabs();
                            PUSH_STR("{\n");
                            emit_body(&stmt->scope.body, stmt, depth + 1, false);
                            push_tabs();
                            PUSH_STR("}\n");
                            break;
                        }
                        case ASTNodeType::FOR_IN_LOOP: {
                            size_t const loop_idx = for_in_counter++;
                            Str const *elem = &stmt->for_in_loop.element_name;
                            Str const *coll = &stmt->for_in_loop.collection_name;
                            Str const *idx = &stmt->for_in_loop.index_name;

                            // Inline array literal: for x in []Type { ... }
                            if (stmt->for_in_loop.inline_element_type.length > 0) {
                                bool const is_u8 = stmt->for_in_loop.inline_element_type == "U8";
                                auto const *elems = &stmt->for_in_loop.inline_elements;
                                register_var(*elem, VarKind::INT);
                                if (idx->length > 0) {
                                    register_var(*idx, VarKind::SIZE_T);
                                }
                                push_tabs();
                                PUSH_STR(is_u8 ? "uint8_t" : "int");
                                PUSH_STR(" __bloom_tmp");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR("[] = {");
                                for (size_t i = 0; i < elems->length; i++) {
                                    if (i != 0) { PUSH_STR(", "); }
                                    PUSH_INT(elems->data[i]);
                                }
                                PUSH_STR("};\n");
                                push_tabs();
                                PUSH_STR("for (size_t __bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR(" = 0; __bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR(" < ");
                                PUSH_INT(static_cast<intmax_t>(elems->length));
                                PUSH_STR("; __bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR("++) {\n");
                                push_tabs(); PUSH_STR("\t");
                                PUSH_STR(is_u8 ? "uint8_t" : "int");
                                PUSH_STR(" ");
                                PUSH_STR(*elem);
                                PUSH_STR(" = __bloom_tmp");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR("[__bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR("];\n");
                                if (idx->length > 0) {
                                    push_tabs(); PUSH_STR("\tsize_t ");
                                    PUSH_STR(*idx); PUSH_STR(" = __bloom_i");
                                    PUSH_INT(static_cast<intmax_t>(loop_idx));
                                    PUSH_STR(";\n");
                                }
                                emit_body(&stmt->for_in_loop.body, stmt, depth + 1, false);
                                push_tabs(); PUSH_STR("}\n");
                                break;
                            }

                            // Enum members iteration: for x in type_info_of(var).members
                            if (stmt->for_in_loop.enum_members_type_name.length > 0) {
                                Str const *etype = &stmt->for_in_loop.enum_members_type_name;
                                // Look up member count from enum_defs
                                size_t member_count = 0;
                                for (size_t i = 0; i < enum_def_count; i++) {
                                    if (enum_defs[i].name.length == etype->length &&
                                        strncmp(enum_defs[i].name.data, etype->data, etype->length) == 0)
                                    {
                                        member_count = enum_defs[i].member_count;
                                        break;
                                    }
                                }
                                register_struct_var(*elem, { .data = "BloomEnumMember", .length = 15 });
                                if (idx->length > 0) {
                                    register_var(*idx, VarKind::SIZE_T);
                                }
                                push_tabs();
                                PUSH_STR("for (size_t __bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR(" = 0; __bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR(" < ");
                                PUSH_INT(static_cast<intmax_t>(member_count));
                                PUSH_STR("; __bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR("++) {\n");
                                push_tabs(); PUSH_STR("\tBloomEnumMember ");
                                PUSH_STR(*elem);
                                PUSH_STR(" = __bloom_");
                                PUSH_STR(*etype);
                                PUSH_STR("_members[__bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR("];\n");
                                if (idx->length > 0) {
                                    push_tabs(); PUSH_STR("\tsize_t ");
                                    PUSH_STR(*idx); PUSH_STR(" = __bloom_i");
                                    PUSH_INT(static_cast<intmax_t>(loop_idx));
                                    PUSH_STR(";\n");
                                }
                                emit_body(&stmt->for_in_loop.body, stmt, depth + 1, false);
                                push_tabs(); PUSH_STR("}\n");
                                break;
                            }

                            // Check if collection is a struct array
                            Str struct_elem_type = {};
                            size_t struct_arr_count = 0;
                            for (size_t i = 0; i < array_struct_var_count; i++) {
                                if (array_struct_vars[i].name.length == coll->length &&
                                    strncmp(array_struct_vars[i].name.data, coll->data, coll->length) == 0)
                                {
                                    struct_elem_type = array_struct_vars[i].element_type;
                                    struct_arr_count = array_struct_vars[i].count;
                                    break;
                                }
                            }

                            if (struct_elem_type.length > 0) {
                                register_struct_var(*elem, struct_elem_type);
                                if (idx->length > 0) {
                                    register_var(*idx, VarKind::SIZE_T);
                                }
                                push_tabs();
                                PUSH_STR("for (size_t __bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR(" = 0; __bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR(" < ");
                                PUSH_INT(static_cast<intmax_t>(struct_arr_count));
                                PUSH_STR("; __bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR("++) {\n");
                                push_tabs(); PUSH_STR("\t");
                                PUSH_STR(struct_elem_type); PUSH_STR(" ");
                                PUSH_STR(*elem); PUSH_STR(" = ");
                                PUSH_STR(*coll); PUSH_STR("[__bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR("];\n");
                                if (idx->length > 0) {
                                    push_tabs(); PUSH_STR("\tsize_t ");
                                    PUSH_STR(*idx); PUSH_STR(" = __bloom_i");
                                    PUSH_INT(static_cast<intmax_t>(loop_idx));
                                    PUSH_STR(";\n");
                                }
                                emit_body(&stmt->for_in_loop.body, stmt, depth + 1, false);
                                push_tabs(); PUSH_STR("}\n");
                                break;
                            }

                            // Check if collection is a primitive array
                            Str prim_elem_type = {};
                            size_t prim_arr_count = 0;
                            for (size_t i = 0; i < array_var_count; i++) {
                                if (array_vars[i].name.length == coll->length &&
                                    strncmp(array_vars[i].name.data, coll->data, coll->length) == 0)
                                {
                                    prim_elem_type = array_vars[i].element_type;
                                    prim_arr_count = array_vars[i].count;
                                    break;
                                }
                            }

                            if (prim_elem_type.length > 0) {
                                register_var(*elem, VarKind::INT);
                                if (idx->length > 0) {
                                    register_var(*idx, VarKind::SIZE_T);
                                }
                                bool const is_u8 = prim_elem_type == "U8";
                                push_tabs();
                                PUSH_STR("for (size_t __bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR(" = 0; __bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR(" < ");
                                PUSH_INT(static_cast<intmax_t>(prim_arr_count));
                                PUSH_STR("; __bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR("++) {\n");
                                push_tabs(); PUSH_STR("\t");
                                PUSH_STR(is_u8 ? "uint8_t" : "int");
                                PUSH_STR(" ");
                                PUSH_STR(*elem); PUSH_STR(" = ");
                                PUSH_STR(*coll); PUSH_STR("[__bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR("];\n");
                                if (idx->length > 0) {
                                    push_tabs(); PUSH_STR("\tsize_t ");
                                    PUSH_STR(*idx); PUSH_STR(" = __bloom_i");
                                    PUSH_INT(static_cast<intmax_t>(loop_idx));
                                    PUSH_STR(";\n");
                                }
                                emit_body(&stmt->for_in_loop.body, stmt, depth + 1, false);
                                push_tabs(); PUSH_STR("}\n");
                                break;
                            }

                            // Check if collection is a slice variable
                            Str slice_elem_type = {};
                            for (size_t i = slice_var_count; i-- > 0;) {
                                if (slice_vars[i].name.length == coll->length &&
                                    strncmp(slice_vars[i].name.data, coll->data, coll->length) == 0)
                                {
                                    slice_elem_type = slice_vars[i].element_type;
                                    break;
                                }
                            }

                            if (slice_elem_type.length > 0) {
                                register_var(*elem, VarKind::INT);
                                if (idx->length > 0) {
                                    register_var(*idx, VarKind::SIZE_T);
                                }
                                bool const is_u8 = slice_elem_type == "U8";
                                push_tabs();
                                PUSH_STR("for (size_t __bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR(" = 0; __bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR(" < __bloom_");
                                PUSH_STR(*coll);
                                PUSH_STR("_len; __bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR("++) {\n");
                                push_tabs(); PUSH_STR("\t");
                                PUSH_STR(is_u8 ? "uint8_t" : "int");
                                PUSH_STR(" ");
                                PUSH_STR(*elem);
                                PUSH_STR(" = __bloom_");
                                PUSH_STR(*coll);
                                PUSH_STR("_data[__bloom_i");
                                PUSH_INT(static_cast<intmax_t>(loop_idx));
                                PUSH_STR("];\n");
                                if (idx->length > 0) {
                                    push_tabs(); PUSH_STR("\tsize_t ");
                                    PUSH_STR(*idx); PUSH_STR(" = __bloom_i");
                                    PUSH_INT(static_cast<intmax_t>(loop_idx));
                                    PUSH_STR(";\n");
                                }
                                emit_body(&stmt->for_in_loop.body, stmt, depth + 1, false);
                                push_tabs(); PUSH_STR("}\n");
                                break;
                            }

                            register_var(*elem, VarKind::BLOOM_CHAR);

                            push_tabs(); PUSH_STR("{\n");
                            push_tabs(); PUSH_STR("\tsize_t __bloom_i");
                            PUSH_INT(static_cast<intmax_t>(loop_idx));
                            PUSH_STR(" = 0;\n");
                            if (idx->length > 0) {
                                register_var(*idx, VarKind::SIZE_T);
                                push_tabs(); PUSH_STR("\tsize_t "); PUSH_STR(*idx); PUSH_STR(" = 0;\n");
                            }
                            push_tabs(); PUSH_STR("\twhile (__bloom_i");
                            PUSH_INT(static_cast<intmax_t>(loop_idx));
                            PUSH_STR(" < "); PUSH_STR(*coll); PUSH_STR(".length) {\n");
                            push_tabs(); PUSH_STR("\t\tBloomChar "); PUSH_STR(*elem); PUSH_STR(";\n");
                            push_tabs(); PUSH_STR("\t\tunsigned char __bloom_f");
                            PUSH_INT(static_cast<intmax_t>(loop_idx));
                            PUSH_STR(" = (unsigned char)");
                            PUSH_STR(*coll); PUSH_STR(".data[__bloom_i");
                            PUSH_INT(static_cast<intmax_t>(loop_idx)); PUSH_STR("];\n");
                            push_tabs(); PUSH_STR("\t\tif (__bloom_f");
                            PUSH_INT(static_cast<intmax_t>(loop_idx)); PUSH_STR(" < 0x80) {\n");
                            push_tabs(); PUSH_STR("\t\t\t"); PUSH_STR(*elem); PUSH_STR(".len = 1;\n");
                            push_tabs(); PUSH_STR("\t\t}\n");
                            push_tabs(); PUSH_STR("\t\telse if (__bloom_f");
                            PUSH_INT(static_cast<intmax_t>(loop_idx)); PUSH_STR(" < 0xE0) {\n");
                            push_tabs(); PUSH_STR("\t\t\t"); PUSH_STR(*elem); PUSH_STR(".len = 2;\n");
                            push_tabs(); PUSH_STR("\t\t}\n");
                            push_tabs(); PUSH_STR("\t\telse if (__bloom_f");
                            PUSH_INT(static_cast<intmax_t>(loop_idx)); PUSH_STR(" < 0xF0) {\n");
                            push_tabs(); PUSH_STR("\t\t\t"); PUSH_STR(*elem); PUSH_STR(".len = 3;\n");
                            push_tabs(); PUSH_STR("\t\t}\n");
                            push_tabs(); PUSH_STR("\t\telse {\n");
                            push_tabs(); PUSH_STR("\t\t\t"); PUSH_STR(*elem); PUSH_STR(".len = 4;\n");
                            push_tabs(); PUSH_STR("\t\t}\n");
                            push_tabs(); PUSH_STR("\t\tfor (uint8_t __bloom_k");
                            PUSH_INT(static_cast<intmax_t>(loop_idx));
                            PUSH_STR(" = 0; __bloom_k");
                            PUSH_INT(static_cast<intmax_t>(loop_idx));
                            PUSH_STR(" < "); PUSH_STR(*elem); PUSH_STR(".len; __bloom_k");
                            PUSH_INT(static_cast<intmax_t>(loop_idx)); PUSH_STR("++) {\n");
                            push_tabs(); PUSH_STR("\t\t\t"); PUSH_STR(*elem); PUSH_STR(".bytes[__bloom_k");
                            PUSH_INT(static_cast<intmax_t>(loop_idx));
                            PUSH_STR("] = "); PUSH_STR(*coll); PUSH_STR(".data[__bloom_i");
                            PUSH_INT(static_cast<intmax_t>(loop_idx));
                            PUSH_STR(" + __bloom_k");
                            PUSH_INT(static_cast<intmax_t>(loop_idx)); PUSH_STR("];\n");
                            push_tabs(); PUSH_STR("\t\t}\n");
                            push_tabs(); PUSH_STR("\t\t__bloom_i");
                            PUSH_INT(static_cast<intmax_t>(loop_idx));
                            PUSH_STR(" += "); PUSH_STR(*elem); PUSH_STR(".len;\n");

                            emit_body(&stmt->for_in_loop.body, stmt, depth + 2, false);
                            if (idx->length > 0) {
                                push_tabs(); PUSH_STR("\t\t"); PUSH_STR(*idx); PUSH_STR("++;\n");
                            }

                            push_tabs(); PUSH_STR("\t}\n");
                            push_tabs(); PUSH_STR("}\n");
                            break;
                        }
                        case ASTNodeType::FOR_COND_LOOP: {
                            push_tabs();
                            PUSH_STR("while (");
                            auto *cond = &stmt->for_cond_loop;
                            if (cond->condition_left.is_identifier) {
                                PUSH_STR(cond->condition_left.identifier);
                            }
                            else {
                                PUSH_INT(cond->condition_left.integer_literal.value);
                            }
                            PUSH_STR(" < ");
                            if (cond->condition_right.is_identifier) {
                                PUSH_STR(cond->condition_right.identifier);
                            }
                            else {
                                PUSH_INT(cond->condition_right.integer_literal.value);
                            }
                            PUSH_STR(") {\n");
                            emit_body(&stmt->for_cond_loop.body, stmt, depth + 1, false);
                            push_tabs();
                            PUSH_STR("}\n");
                            break;
                        }
                        case ASTNodeType::FOR_LOOP: {
                            push_tabs();
                            PUSH_STR("while (1) {\n");
                            emit_body(&stmt->for_loop.body, stmt, depth + 1, false);
                            push_tabs();
                            PUSH_STR("}\n");
                            break;
                        }
                        case ASTNodeType::FOR_RANGE_LOOP: {
                            Str const *elem = &stmt->for_range_loop.element_name;
                            register_var(*elem, VarKind::INT);
                            push_tabs();
                            PUSH_STR("for (int ");
                            PUSH_STR(*elem);
                            PUSH_STR(" = ");
                            PUSH_INT(stmt->for_range_loop.range_start);
                            PUSH_STR("; ");
                            PUSH_STR(*elem);
                            PUSH_STR(" < ");
                            if (stmt->for_range_loop.range_count_identifier.length > 0) {
                                PUSH_INT(stmt->for_range_loop.range_start);
                                PUSH_STR(" + ");
                                PUSH_STR(stmt->for_range_loop.range_count_identifier);
                                PUSH_STR(" + 1");
                            }
                            else {
                                PUSH_INT(stmt->for_range_loop.range_end);
                            }
                            PUSH_STR("; ");
                            PUSH_STR(*elem);
                            PUSH_STR("++) {\n");
                            emit_body(&stmt->for_range_loop.body, stmt, depth + 1, false);
                            push_tabs();
                            PUSH_STR("}\n");
                            break;
                        }
                        case ASTNodeType::IF_ELSE: {
                            auto emit_cond_op = [&](ConditionOperand const *operand) {
                                if (operand->is_enum_shorthand) {
                                    PUSH_STR("__bloom_");
                                    PUSH_STR(operand->enum_shorthand.enum_type_name);
                                    PUSH_STR("_");
                                    PUSH_STR(operand->enum_shorthand.member_name);
                                }
                                else if (operand->is_proc_call) {
                                    Str const &callee = operand->proc_call.caller;
                                    Str const &arg = operand->proc_call.arg_identifier;
                                    bool const is_length = callee.length == 6 &&
                                        strncmp(callee.data, "length", 6) == 0;
                                    if (is_length) {
                                        bool found_slice = false;
                                        for (size_t si = 0; si < slice_var_count; si++) {
                                            if (slice_vars[si].name.length == arg.length &&
                                                strncmp(slice_vars[si].name.data, arg.data, arg.length) == 0)
                                            {
                                                PUSH_STR("__bloom_");
                                                PUSH_STR(arg);
                                                PUSH_STR("_len");
                                                found_slice = true;
                                                break;
                                            }
                                        }
                                        if (!found_slice) {
                                            PUSH_INT(static_cast<intmax_t>(find_array_size(arg)));
                                        }
                                    }
                                    else {
                                        PUSH_STR(callee);
                                        PUSH_STR('(');
                                        PUSH_STR(arg);
                                        PUSH_STR(')');
                                    }
                                }
                                else if (operand->is_identifier) {
                                    PUSH_STR(operand->identifier);
                                }
                                else {
                                    PUSH_INT(operand->integer_literal.value);
                                }
                            };
                            auto emit_condition = [&](ASTNode *if_node) {
                                auto *cond = &if_node->if_else;
                                PUSH_STR("if (");
                                emit_cond_op(&cond->condition_left);
                                if (cond->comparison_op.length > 0) {
                                    PUSH_STR(' ');
                                    PUSH_STR(cond->comparison_op);
                                    PUSH_STR(' ');
                                }
                                else {
                                    PUSH_STR(" == ");
                                }
                                emit_cond_op(&cond->condition_right);
                                PUSH_STR(") {\n");
                            };

                            push_tabs();
                            emit_condition(stmt);

                            ASTNode *current_if = stmt;
                            while (true) {
                                auto *cur = &current_if->if_else;
                                emit_body(&cur->then_body, current_if, depth + 1, false);

                                if (cur->else_body.data == nullptr) {
                                    push_tabs();
                                    PUSH_STR("}\n");
                                    break;
                                }

                                ASTNode *next_if = nullptr;
                                for (size_t bi = 0; bi < cur->else_body.length; bi++) {
                                    auto *s = &cur->else_body.data[bi];
                                    if (s->parent == current_if && s->type == ASTNodeType::IF_ELSE) {
                                        next_if = s;
                                        break;
                                    }
                                }

                                if (next_if != nullptr) {
                                    push_tabs();
                                    PUSH_STR("} else ");
                                    emit_condition(next_if);
                                    current_if = next_if;
                                }
                                else {
                                    push_tabs();
                                    PUSH_STR("} else {\n");
                                    emit_body(&cur->else_body, current_if, depth + 1, false);
                                    push_tabs();
                                    PUSH_STR("}\n");
                                    break;
                                }
                            }
                            break;
                        }
                        case ASTNodeType::IDENTIFIER: {
                            if (!emit_as_return) {
                                break;
                            }
                            // Check if identifier refers to an array variable
                            for (size_t i = array_var_count; i-- > 0;) {
                                if (array_vars[i].name.length == stmt->identifier.length &&
                                    strncmp(array_vars[i].name.data, stmt->identifier.data, stmt->identifier.length) == 0)
                                {
                                    int64_t const arr_count_val = (int64_t)array_vars[i].count;
                                    push_tabs();
                                    PUSH_STR("__bloom_Array_");
                                    PUSH_STR(array_vars[i].element_type);
                                    PUSH_STR("_");
                                    PUSH_INT(arr_count_val);
                                    PUSH_STR(" __bloom_ret;\n");
                                    push_tabs();
                                    PUSH_STR("memcpy(__bloom_ret.data, ");
                                    PUSH_STR(stmt->identifier);
                                    PUSH_STR(", sizeof(__bloom_ret.data));\n");
                                    push_tabs();
                                    PUSH_STR("return __bloom_ret;\n");
                                    return;
                                }
                            }
                            push_tabs();
                            PUSH_STR("return ");
                            PUSH_STR(stmt->identifier);
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::ARRAY_SLICE: {
                            if (!emit_as_return) {
                                break;
                            }
                            // Return a slice struct: __bloom_Slice_Int { .data = arr+start, .length = len }
                            Str const &arr_name = stmt->array_slice.variable_name;
                            int64_t const start = (stmt->array_slice.start_index < 0) ? 0 : stmt->array_slice.start_index;
                            int64_t const end = stmt->array_slice.end_index;
                            int64_t arr_count_val = 0;
                            Str elem_type = {};
                            for (size_t i = array_var_count; i-- > 0;) {
                                if (array_vars[i].name.length == arr_name.length &&
                                    strncmp(array_vars[i].name.data, arr_name.data, arr_name.length) == 0)
                                {
                                    arr_count_val = (int64_t)array_vars[i].count;
                                    elem_type = array_vars[i].element_type;
                                    break;
                                }
                            }
                            int64_t const length = (end < 0) ? (arr_count_val - start) : (end - start);
                            if (elem_type.length == 0) {
                                elem_type = cstr_to_str("Int");
                            }
                            push_tabs();
                            PUSH_STR("return (__bloom_Slice_");
                            PUSH_STR(elem_type);
                            PUSH_STR("){ .data = ");
                            PUSH_STR(arr_name);
                            if (start > 0) {
                                PUSH_STR(" + ");
                                PUSH_INT(start);
                            }
                            PUSH_STR(", .length = ");
                            PUSH_INT(length);
                            PUSH_STR(" };\n");
                            break;
                        }
                        case ASTNodeType::DEFER: {
                            ASTNode deferred_call = {
                                .type = ASTNodeType::PROC_CALL,
                                .parent = stmt->parent,
                                .proc_call = {
                                    .arguments = stmt->defer_stmt.arguments,
                                    .caller_identifier = stmt->defer_stmt.caller_identifier,
                                },
                            };
                            emit_stmt(&deferred_call, owner, depth, false);
                            break;
                        }
                        default:
                            break;
                    }
                };

                emit_body(&node->proc_def.body, node, 1, has_return_type);
                if (is_main_with_str_args) {
                    PUSH_STR("\treturn 0;\n");
                }
                PUSH_STR("}\n\n");
                break;
            }
            default:
                break;
        }
    }

    #undef PUSH_STR
    #undef PUSH_INT
    #undef PUSH_BOOL

    return str_from_data_and_length(str_buffer.data, str_buffer.length);
}