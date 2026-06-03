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
    };
    ArrayVarEntry array_vars[64];
    size_t array_var_count = 0;

    enum class VarKind : uint8_t { INT, BOOL, BLOOM_STR, BLOOM_CHAR, SIZE_T, STRUCT };
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

    auto lookup_var_kind = [&](Str name) -> VarKind {
        for (size_t i = 0; i < var_type_count; i++) {
            if (var_types[i].name.length == name.length &&
                strncmp(var_types[i].name.data, name.data, name.length) == 0) {
                return var_types[i].kind;
            }
        }
        return VarKind::INT;
    };

    auto find_array_size = [&](Str var_name) -> size_t {
        for (size_t i = 0; i < array_var_count; i++) {
            Str const &name = array_vars[i].name;
            if (name.length == var_name.length &&
                strncmp(name.data, var_name.data, name.length) == 0)
            {
                return array_vars[i].count;
            }
        }
        assert(false && "Array variable not found for length() call");
        return 0;
    };

    std::function<void(Array<ASTNode>*)> emit_proc_call_args = [&](Array<ASTNode> *arguments) {
        for (size_t i = 0; i < arguments->length; i++) {
            if (i != 0) { PUSH_STR(", "); }
            auto *arg = &(*arguments)[i];
            switch (arg->type) {
                case ASTNodeType::IDENTIFIER:
                    PUSH_STR(arg->identifier);
                    break;
                case ASTNodeType::ARRAY_ACCESS:
                    PUSH_STR(arg->array_access.variable_name);
                    PUSH_STR('[');
                    PUSH_INT(arg->array_access.index);
                    PUSH_STR(']');
                    break;
                case ASTNodeType::INTEGER_LITERAL:
                    PUSH_INT(arg->integer_literal.value.value);
                    break;
                case ASTNodeType::STRING_LITERAL:
                    PUSH_STR('"');
                    PUSH_STR(arg->string_literal.value);
                    PUSH_STR('"');
                    break;
                case ASTNodeType::BUILTIN_LENGTH:
                    PUSH_INT(static_cast<intmax_t>(find_array_size(arg->identifier)));
                    break;
                case ASTNodeType::BUILTIN_LENGTH_IN_BYTES:
                    PUSH_STR(arg->identifier);
                    PUSH_STR(".length");
                    break;
                case ASTNodeType::BOOLEAN_LITERAL:
                    PUSH_BOOL(arg->boolean_literal.value);
                    break;
                case ASTNodeType::PROC_CALL:
                    PUSH_STR(arg->proc_call.caller_identifier);
                    PUSH_STR('(');
                    emit_proc_call_args(&arg->proc_call.arguments);
                    PUSH_STR(')');
                    break;
                default:
                    assert(false && "Unsupported argument type in emit_proc_call_args");
            }
        }
    };

    auto emit_binary_operand = [&](BinaryOperand const &op) {
        switch (op.type) {
            case BinaryOperandType::IDENTIFIER:
                PUSH_STR(op.identifier);
                break;
            case BinaryOperandType::ARRAY_ACCESS:
                PUSH_STR(op.array_access.variable_name);
                PUSH_STR('[');
                PUSH_INT(op.array_access.index);
                PUSH_STR(']');
                break;
            case BinaryOperandType::PROC_CALL: {
                Str const &callee = op.proc_call.caller_identifier;
                bool const is_length = callee.length == 6 &&
                    strncmp(callee.data, "length", 6) == 0;
                bool const is_int_cast = callee.length == 3 &&
                    strncmp(callee.data, "Int", 3) == 0;
                if (is_int_cast) {
                    assert(op.proc_call.arguments.length == 1 && "Int() cast requires exactly one argument");
                    PUSH_STR("(int)(");
                    emit_proc_call_args(const_cast<Array<ASTNode>*>(&op.proc_call.arguments));
                    PUSH_STR(")");
                }
                else if (is_length) {
                    assert(op.proc_call.arguments.length > 0 && "length() requires an argument");
                    PUSH_INT(static_cast<intmax_t>(
                        find_array_size(op.proc_call.arguments.data[0].identifier)
                    ));
                }
                else {
                    PUSH_STR(op.proc_call.caller_identifier);
                    PUSH_STR('(');
                    emit_proc_call_args(const_cast<Array<ASTNode>*>(&op.proc_call.arguments));
                    PUSH_STR(')');
                }
                break;
            }
            case BinaryOperandType::INTEGER_LITERAL:
                PUSH_INT(op.integer_literal.value);
                break;
        }
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
            case ASTNodeType::ARRAY_ACCESS:
                PUSH_STR(expr->array_access.variable_name);
                PUSH_STR('[');
                PUSH_INT(expr->array_access.index);
                PUSH_STR(']');
                break;
            case ASTNodeType::PROC_CALL:
                if (expr->proc_call.caller_identifier == "Int") {
                    assert(expr->proc_call.arguments.length == 1 && "Int() cast requires exactly one argument");
                    PUSH_STR("(int)(");
                    emit_proc_call_args(&expr->proc_call.arguments);
                    PUSH_STR(")");
                }
                else if (expr->proc_call.caller_identifier == "length") {
                    assert(expr->proc_call.arguments.length > 0 && "length() requires an argument");
                    PUSH_INT(static_cast<intmax_t>(find_array_size(expr->proc_call.arguments[0].identifier)));
                }
                else {
                    PUSH_STR(expr->proc_call.caller_identifier);
                    PUSH_STR('(');
                    emit_proc_call_args(&expr->proc_call.arguments);
                    PUSH_STR(')');
                }
                break;
            case ASTNodeType::BINARY_ADD: {
                auto &operands = expr->binary_operation.operands;
                for (size_t i = 0; i < operands.length; i++) {
                    if (i != 0) { PUSH_STR(" + "); }
                    emit_binary_operand(operands[i]);
                }
                break;
            }
            case ASTNodeType::STRING_LITERAL: {
                Str const &content = expr->string_literal.value;
                size_t const runtime_len = str_literal_runtime_length(content);
                PUSH_STR("(BloomStr){.data = \"");
                PUSH_STR(content);
                PUSH_STR("\", .length = ");
                PUSH_INT(static_cast<intmax_t>(runtime_len));
                PUSH_STR("}");
                break;
            }
            default:
                assert(false && "Unsupported expression type in emit_expression");
        }
    };

    PUSH_STR("#include <stdbool.h>\n");
    PUSH_STR("#include <stddef.h>\n");
    PUSH_STR("#include <stdint.h>\n");
    PUSH_STR("#include <stdio.h>\n\n");
    PUSH_STR("typedef struct { char const *data; size_t length; } BloomStr;\n");
    PUSH_STR("typedef struct { char bytes[4]; uint8_t len; } BloomChar;\n\n");

    for (auto &node : *ast_nodes) {
        switch (node.type) {
            case ASTNodeType::STRUCT_DEF: {
                PUSH_STR("typedef struct {\n");
                for (size_t i = 0; i < node.struct_def.fields.length; i++) {
                    auto &field = node.struct_def.fields.data[i];
                    PUSH_STR("\t");
                    if (field.type_name == "Int") {
                        PUSH_STR("int");
                    }
                    else {
                        PUSH_STR(field.type_name);
                    }
                    PUSH_STR(" ");
                    PUSH_STR(field.name);
                    PUSH_STR(";\n");
                }
                PUSH_STR("} ");
                PUSH_STR(node.struct_def.name);
                PUSH_STR(";\n\n");
                break;
            }
            case ASTNodeType::PROC_DEF: {
                array_var_count = 0;
                var_type_count = 0;
                size_t for_in_counter = 0;
                char const *return_type_name = nullptr;
                if (node.proc_def.return_type != nullptr) {
                    if (node.proc_def.return_type->name == "Int") {
                        return_type_name = "int";
                    }
                }
                else {
                    return_type_name = "void";
                }
                assert(return_type_name != nullptr && "Unsupported return type in transpilation");
                PUSH_STR(return_type_name);
                PUSH_STR(' ');
                PUSH_STR(node.proc_def.name);
                PUSH_STR('(');
                auto *params = &node.proc_def.parameters;
                for (size_t i = 0; i < params->length; i++) {
                    auto *param = &params->data[i];
                    if (i != 0) {
                        PUSH_STR(", ");
                    }
                    // For simplicity, assume all parameters are of type int
                    PUSH_STR("int ");
                    PUSH_STR(param->name);
                }
                PUSH_STR(')');
                PUSH_STR("{\n");

                std::function<void(ASTNode*, ASTNode*, int)> emit_stmt;
                emit_stmt = [&](ASTNode *stmt, ASTNode *owner, int depth) {
                    if (stmt->parent != owner) {
                        return;
                    }
                    auto push_tabs = [&]() {
                        for (int d = 0; d < depth; d++) { PUSH_STR('\t'); }
                    };
                    switch (stmt->type) {
                        case ASTNodeType::BINARY_ADD: {
                            push_tabs();
                            PUSH_STR("return ");
                            auto &operands = stmt->binary_operation.operands;
                            for (size_t i = 0; i < operands.length; i++) {
                                if (i != 0) { PUSH_STR(" + "); }
                                emit_binary_operand(operands[i]);
                            }
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::PROC_CALL: {
                            if (stmt->proc_call.caller_identifier == "print") {
                                assert(stmt->proc_call.arguments.length >= 1 &&
                                    "print() requires at least a format string argument");
                                auto &fmt_arg = stmt->proc_call.arguments[0];
                                assert(fmt_arg.type == ASTNodeType::STRING_LITERAL &&
                                    "First argument to print() must be a string literal");
                                Str const &fmt = fmt_arg.string_literal.value;
                                size_t arg_idx = 1;
                                size_t seg_start = 0;
                                for (size_t k = 0; k < fmt.length; k++) {
                                    if (fmt.data[k] == '{' && k + 1 < fmt.length &&
                                        fmt.data[k + 1] == '}')
                                    {
                                        if (k > seg_start) {
                                            push_tabs();
                                            PUSH_STR("fputs(\"");
                                            PUSH_STR(str_from_data_and_length(
                                                fmt.data + seg_start, k - seg_start));
                                            PUSH_STR("\", stdout);\n");
                                        }
                                        if (arg_idx < stmt->proc_call.arguments.length) {
                                            auto &arg = stmt->proc_call.arguments[arg_idx];
                                            push_tabs();
                                            if (arg.type == ASTNodeType::IDENTIFIER) {
                                                VarKind kind = lookup_var_kind(arg.identifier);
                                                if (kind == VarKind::BLOOM_CHAR) {
                                                    PUSH_STR("fwrite(");
                                                    PUSH_STR(arg.identifier);
                                                    PUSH_STR(".bytes, 1, ");
                                                    PUSH_STR(arg.identifier);
                                                    PUSH_STR(".len, stdout);\n");
                                                }
                                                else if (kind == VarKind::BLOOM_STR) {
                                                    PUSH_STR("fwrite(");
                                                    PUSH_STR(arg.identifier);
                                                    PUSH_STR(".data, 1, ");
                                                    PUSH_STR(arg.identifier);
                                                    PUSH_STR(".length, stdout);\n");
                                                }
                                                else if (kind == VarKind::SIZE_T) {
                                                    PUSH_STR("printf(\"%zu\", ");
                                                    PUSH_STR(arg.identifier);
                                                    PUSH_STR(");\n");
                                                }
                                                else if (kind == VarKind::BOOL) {
                                                    PUSH_STR("fputs(");
                                                    PUSH_STR(arg.identifier);
                                                    PUSH_STR(" ? \"true\" : \"false\", stdout);\n");
                                                }
                                                else {
                                                    PUSH_STR("printf(\"%d\", ");
                                                    PUSH_STR(arg.identifier);
                                                    PUSH_STR(");\n");
                                                }
                                            }
                                            else if (arg.type == ASTNodeType::BOOLEAN_LITERAL) {
                                                PUSH_STR("fputs(");
                                                PUSH_STR(arg.boolean_literal.value ? "\"true\"" : "\"false\"");
                                                PUSH_STR(", stdout);\n");
                                            }
                                            else if (arg.type == ASTNodeType::INTEGER_LITERAL) {
                                                PUSH_STR("printf(\"%d\", ");
                                                PUSH_INT(arg.integer_literal.value.value);
                                                PUSH_STR(");\n");
                                            }
                                            else if (arg.type == ASTNodeType::ARRAY_ACCESS) {
                                                PUSH_STR("printf(\"%d\", ");
                                                PUSH_STR(arg.array_access.variable_name);
                                                PUSH_STR('[');
                                                PUSH_INT(arg.array_access.index);
                                                PUSH_STR("]);\n");
                                            }
                                            else if (arg.type == ASTNodeType::MEMBER_ACCESS) {
                                                PUSH_STR("printf(\"%d\", ");
                                                PUSH_STR(arg.member_access.object_name);
                                                PUSH_STR(".");
                                                PUSH_STR(arg.member_access.field_name);
                                                PUSH_STR(");\n");
                                            }
                                            arg_idx++;
                                        }
                                        k++;
                                        seg_start = k + 1;
                                    }
                                }
                                if (seg_start < fmt.length) {
                                    push_tabs();
                                    PUSH_STR("fputs(\"");
                                    PUSH_STR(str_from_data_and_length(
                                        fmt.data + seg_start, fmt.length - seg_start));
                                    PUSH_STR("\", stdout);\n");
                                }
                            }
                            else {
                                push_tabs();
                                PUSH_STR(stmt->proc_call.caller_identifier);
                                PUSH_STR('(');
                                emit_proc_call_args(&stmt->proc_call.arguments);
                                PUSH_STR(");\n");
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
                            if (expr->type == ASTNodeType::STRUCT_INIT) {
                                register_struct_var(stmt->variable_definition.name, expr->struct_init.type_name);
                                PUSH_STR(expr->struct_init.type_name);
                                PUSH_STR(' ');
                                PUSH_STR(stmt->variable_definition.name);
                                PUSH_STR(" = {0};\n");
                                break;
                            }
                            if (expr->type == ASTNodeType::ARRAY_INIT) {
                                if (array_var_count < 64) {
                                    array_vars[array_var_count++] = {
                                        .name = stmt->variable_definition.name,
                                        .count = expr->array_init.elements.length,
                                    };
                                }
                                register_var(stmt->variable_definition.name, VarKind::INT);
                                PUSH_STR("int ");
                                PUSH_STR(stmt->variable_definition.name);
                                PUSH_STR("[] = {");
                                auto &elems = expr->array_init.elements;
                                for (size_t i = 0; i < elems.length; i++) {
                                    if (i != 0) { PUSH_STR(", "); }
                                    PUSH_INT(elems[i]);
                                }
                                PUSH_STR("};\n");
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
                        case ASTNodeType::ADD_ASSIGN: {
                            push_tabs();
                            PUSH_STR(stmt->add_assign.variable_name);
                            PUSH_STR(" += ");
                            emit_binary_operand(stmt->add_assign.operand);
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::BREAK: {
                            push_tabs();
                            PUSH_STR("break;\n");
                            break;
                        }
                        case ASTNodeType::FOR_IN_LOOP: {
                            size_t const loop_idx = for_in_counter++;
                            Str const &elem = stmt->for_in_loop.element_name;
                            Str const &coll = stmt->for_in_loop.collection_name;
                            Str const &idx = stmt->for_in_loop.index_name;
                            register_var(elem, VarKind::BLOOM_CHAR);

                            push_tabs(); PUSH_STR("{\n");
                            push_tabs(); PUSH_STR("\tsize_t __bloom_i");
                            PUSH_INT(static_cast<intmax_t>(loop_idx));
                            PUSH_STR(" = 0;\n");
                            if (idx.length > 0) {
                                register_var(idx, VarKind::SIZE_T);
                                push_tabs(); PUSH_STR("\tsize_t "); PUSH_STR(idx); PUSH_STR(" = 0;\n");
                            }
                            push_tabs(); PUSH_STR("\twhile (__bloom_i");
                            PUSH_INT(static_cast<intmax_t>(loop_idx));
                            PUSH_STR(" < "); PUSH_STR(coll); PUSH_STR(".length) {\n");
                            push_tabs(); PUSH_STR("\t\tBloomChar "); PUSH_STR(elem); PUSH_STR(";\n");
                            push_tabs(); PUSH_STR("\t\tunsigned char __bloom_f");
                            PUSH_INT(static_cast<intmax_t>(loop_idx));
                            PUSH_STR(" = (unsigned char)");
                            PUSH_STR(coll); PUSH_STR(".data[__bloom_i");
                            PUSH_INT(static_cast<intmax_t>(loop_idx)); PUSH_STR("];\n");
                            push_tabs(); PUSH_STR("\t\tif (__bloom_f");
                            PUSH_INT(static_cast<intmax_t>(loop_idx)); PUSH_STR(" < 0x80) {\n");
                            push_tabs(); PUSH_STR("\t\t\t"); PUSH_STR(elem); PUSH_STR(".len = 1;\n");
                            push_tabs(); PUSH_STR("\t\t}\n");
                            push_tabs(); PUSH_STR("\t\telse if (__bloom_f");
                            PUSH_INT(static_cast<intmax_t>(loop_idx)); PUSH_STR(" < 0xE0) {\n");
                            push_tabs(); PUSH_STR("\t\t\t"); PUSH_STR(elem); PUSH_STR(".len = 2;\n");
                            push_tabs(); PUSH_STR("\t\t}\n");
                            push_tabs(); PUSH_STR("\t\telse if (__bloom_f");
                            PUSH_INT(static_cast<intmax_t>(loop_idx)); PUSH_STR(" < 0xF0) {\n");
                            push_tabs(); PUSH_STR("\t\t\t"); PUSH_STR(elem); PUSH_STR(".len = 3;\n");
                            push_tabs(); PUSH_STR("\t\t}\n");
                            push_tabs(); PUSH_STR("\t\telse {\n");
                            push_tabs(); PUSH_STR("\t\t\t"); PUSH_STR(elem); PUSH_STR(".len = 4;\n");
                            push_tabs(); PUSH_STR("\t\t}\n");
                            push_tabs(); PUSH_STR("\t\tfor (uint8_t __bloom_k");
                            PUSH_INT(static_cast<intmax_t>(loop_idx));
                            PUSH_STR(" = 0; __bloom_k");
                            PUSH_INT(static_cast<intmax_t>(loop_idx));
                            PUSH_STR(" < "); PUSH_STR(elem); PUSH_STR(".len; __bloom_k");
                            PUSH_INT(static_cast<intmax_t>(loop_idx)); PUSH_STR("++) {\n");
                            push_tabs(); PUSH_STR("\t\t\t"); PUSH_STR(elem); PUSH_STR(".bytes[__bloom_k");
                            PUSH_INT(static_cast<intmax_t>(loop_idx));
                            PUSH_STR("] = "); PUSH_STR(coll); PUSH_STR(".data[__bloom_i");
                            PUSH_INT(static_cast<intmax_t>(loop_idx));
                            PUSH_STR(" + __bloom_k");
                            PUSH_INT(static_cast<intmax_t>(loop_idx)); PUSH_STR("];\n");
                            push_tabs(); PUSH_STR("\t\t}\n");
                            push_tabs(); PUSH_STR("\t\t__bloom_i");
                            PUSH_INT(static_cast<intmax_t>(loop_idx));
                            PUSH_STR(" += "); PUSH_STR(elem); PUSH_STR(".len;\n");

                            for (auto &body_stmt : stmt->for_in_loop.body) {
                                emit_stmt(&body_stmt, stmt, depth + 2);
                            }
                            if (idx.length > 0) {
                                push_tabs(); PUSH_STR("\t\t"); PUSH_STR(idx); PUSH_STR("++;\n");
                            }

                            push_tabs(); PUSH_STR("\t}\n");
                            push_tabs(); PUSH_STR("}\n");
                            break;
                        }
                        case ASTNodeType::FOR_COND_LOOP: {
                            push_tabs();
                            PUSH_STR("while (");
                            auto &cond = stmt->for_cond_loop;
                            if (cond.condition_left.is_identifier) {
                                PUSH_STR(cond.condition_left.identifier);
                            }
                            else {
                                PUSH_INT(cond.condition_left.integer_literal.value);
                            }
                            PUSH_STR(" < ");
                            if (cond.condition_right.is_identifier) {
                                PUSH_STR(cond.condition_right.identifier);
                            }
                            else {
                                PUSH_INT(cond.condition_right.integer_literal.value);
                            }
                            PUSH_STR(") {\n");
                            for (auto &body_stmt : stmt->for_cond_loop.body) {
                                emit_stmt(&body_stmt, stmt, depth + 1);
                            }
                            push_tabs();
                            PUSH_STR("}\n");
                            break;
                        }
                        case ASTNodeType::FOR_LOOP: {
                            push_tabs();
                            PUSH_STR("while (1) {\n");
                            for (auto &body_stmt : stmt->for_loop.body) {
                                emit_stmt(&body_stmt, stmt, depth + 1);
                            }
                            push_tabs();
                            PUSH_STR("}\n");
                            break;
                        }
                        case ASTNodeType::FOR_RANGE_LOOP: {
                            Str const &elem = stmt->for_range_loop.element_name;
                            register_var(elem, VarKind::INT);
                            push_tabs();
                            PUSH_STR("for (int ");
                            PUSH_STR(elem);
                            PUSH_STR(" = ");
                            PUSH_INT(stmt->for_range_loop.range_start);
                            PUSH_STR("; ");
                            PUSH_STR(elem);
                            PUSH_STR(" < ");
                            PUSH_INT(stmt->for_range_loop.range_end);
                            PUSH_STR("; ");
                            PUSH_STR(elem);
                            PUSH_STR("++) {\n");
                            for (auto &body_stmt : stmt->for_range_loop.body) {
                                emit_stmt(&body_stmt, stmt, depth + 1);
                            }
                            push_tabs();
                            PUSH_STR("}\n");
                            break;
                        }
                        case ASTNodeType::IF_ELSE: {
                            auto emit_condition = [&](ASTNode *if_node) {
                                auto &cond = if_node->if_else;
                                PUSH_STR("if (");
                                if (cond.condition_left.is_identifier) {
                                    PUSH_STR(cond.condition_left.identifier);
                                }
                                else {
                                    PUSH_INT(cond.condition_left.integer_literal.value);
                                }
                                PUSH_STR(" == ");
                                if (cond.condition_right.is_identifier) {
                                    PUSH_STR(cond.condition_right.identifier);
                                }
                                else {
                                    PUSH_INT(cond.condition_right.integer_literal.value);
                                }
                                PUSH_STR(") {\n");
                            };

                            push_tabs();
                            emit_condition(stmt);

                            ASTNode *current_if = stmt;
                            while (true) {
                                auto &cur = current_if->if_else;
                                for (auto &body_stmt : cur.then_body) {
                                    emit_stmt(&body_stmt, current_if, depth + 1);
                                }

                                if (cur.else_body.data == nullptr) {
                                    push_tabs();
                                    PUSH_STR("}\n");
                                    break;
                                }

                                ASTNode *next_if = nullptr;
                                for (auto &s : cur.else_body) {
                                    if (s.parent == current_if && s.type == ASTNodeType::IF_ELSE) {
                                        next_if = &s;
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
                                    for (auto &body_stmt : cur.else_body) {
                                        emit_stmt(&body_stmt, current_if, depth + 1);
                                    }
                                    push_tabs();
                                    PUSH_STR("}\n");
                                    break;
                                }
                            }
                            break;
                        }
                        default:
                            break;
                    }
                };

                for (auto &statement : node.proc_def.body) {
                    emit_stmt(&statement, &node, 1);
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