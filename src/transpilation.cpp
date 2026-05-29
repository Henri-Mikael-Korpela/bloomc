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

    PUSH_STR("#include <stdbool.h>\n");
    PUSH_STR("#include <stdio.h>\n\n");

    for (auto &node : *ast_nodes) {
        switch (node.type) {
            case ASTNodeType::PROC_DEF: {
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
                for (auto &statement : node.proc_def.body) {
                    if (statement.parent != &node) {
                        continue;
                    }
                    switch (statement.type) {
                        case ASTNodeType::BINARY_ADD: {
                            PUSH_STR('\t');
                            PUSH_STR("return ");
                            auto &operands = statement.binary_operation.operands;
                            for (size_t i = 0; i < operands.length; i++) {
                                if (i != 0) {
                                    PUSH_STR(" + ");
                                }
                                auto &op = operands[i];
                                if (op.type == BinaryOperandType::IDENTIFIER) {
                                    PUSH_STR(op.identifier);
                                }
                                else if (op.type == BinaryOperandType::PROC_CALL) {
                                    PUSH_STR(op.proc_call.caller_identifier);
                                    PUSH_STR('(');
                                    for (size_t j = 0; j < op.proc_call.arguments.length; j++) {
                                        if (j != 0) { PUSH_STR(", "); }
                                        auto *arg = &op.proc_call.arguments[j];
                                        if (arg->type == ASTNodeType::IDENTIFIER) {
                                            PUSH_STR(arg->identifier);
                                        }
                                        else if (arg->type == ASTNodeType::INTEGER_LITERAL) {
                                            PUSH_INT(arg->integer_literal.value.value);
                                        }
                                        else if (arg->type == ASTNodeType::STRING_LITERAL) {
                                            PUSH_STR('"'); PUSH_STR(arg->string_literal.value); PUSH_STR('"');
                                        }
                                    }
                                    PUSH_STR(')');
                                }
                                else {
                                    PUSH_INT(op.integer_literal.value);
                                }
                            }
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::PROC_CALL: {
                            // For simplicity, assume procedure calls return void
                            PUSH_STR('\t');
                            PUSH_STR(statement.proc_call.caller_identifier);
                            PUSH_STR('(');
                            auto args_len = statement.proc_call.arguments.length;
                            for (size_t i = 0; i < args_len; i++) {
                                auto *arg = &statement.proc_call.arguments[i];
                                if (arg->type == ASTNodeType::IDENTIFIER) {
                                    PUSH_STR(arg->identifier);
                                    goto add_comma_inbetween;
                                }
                                else if (arg->type == ASTNodeType::ARRAY_ACCESS) {
                                    PUSH_STR(arg->array_access.variable_name);
                                    PUSH_STR('[');
                                    PUSH_INT(arg->array_access.index);
                                    PUSH_STR(']');
                                    goto add_comma_inbetween;
                                }
                                else if (arg->type != ASTNodeType::STRING_LITERAL) {
                                    assert(false && "Only identifier, array access, and string literal arguments are supported in transpilation");
                                }
                                PUSH_STR('"');
                                PUSH_STR(arg->string_literal.value);
                                PUSH_STR('"');

                                add_comma_inbetween:
                                    if (i != args_len - 1) {
                                        PUSH_STR(", ");
                                    }
                            }
                            PUSH_STR(");\n");
                            break;
                        }
                        case ASTNodeType::VARIABLE_DEFINITION: {
                            PUSH_STR('\t');
                            if (statement.variable_definition.deduced_type == DeducedType::ARRAY_INT) {
                                PUSH_STR("int ");
                                PUSH_STR(statement.variable_definition.name);
                                PUSH_STR("[] = {");
                                auto &elems = statement.variable_definition.array_init_expr.elements;
                                for (size_t i = 0; i < elems.length; i++) {
                                    if (i != 0) { PUSH_STR(", "); }
                                    PUSH_INT(elems[i]);
                                }
                                PUSH_STR("};\n");
                                break;
                            }
                            // Emit C type from deduced Bloom type
                            char const *c_type = nullptr;
                            switch (statement.variable_definition.deduced_type) {
                                case DeducedType::BOOLEAN: c_type = "bool"; break;
                                case DeducedType::INTEGER: c_type = "int";  break;
                                default: assert(false && "Unsupported deduced type in transpilation");
                            }
                            PUSH_STR(c_type);
                            PUSH_STR(' ');
                            PUSH_STR(statement.variable_definition.name);
                            PUSH_STR(" = ");
                            // Emit value from expression type
                            switch (statement.variable_definition.expr_type) {
                                case ASTNodeType::INTEGER_LITERAL:
                                    PUSH_INT(statement.variable_definition.integer_value.value);
                                    break;
                                case ASTNodeType::BOOLEAN_LITERAL:
                                    PUSH_BOOL(statement.variable_definition.boolean_value);
                                    break;
                                case ASTNodeType::BINARY_ADD: {
                                    auto &operands = statement.variable_definition.add_expr;
                                    for (size_t i = 0; i < operands.length; i++) {
                                        if (i != 0) {
                                            PUSH_STR(" + ");
                                        }
                                        auto &op = operands[i];
                                        if (op.type == BinaryOperandType::IDENTIFIER) {
                                            PUSH_STR(op.identifier);
                                        }
                                        else if (op.type == BinaryOperandType::PROC_CALL) {
                                            PUSH_STR(op.proc_call.caller_identifier);
                                            PUSH_STR('(');
                                            for (size_t j = 0; j < op.proc_call.arguments.length; j++) {
                                                if (j != 0) { PUSH_STR(", "); }
                                                auto *arg = &op.proc_call.arguments[j];
                                                if (arg->type == ASTNodeType::IDENTIFIER) {
                                                    PUSH_STR(arg->identifier);
                                                }
                                                else if (arg->type == ASTNodeType::INTEGER_LITERAL) {
                                                    PUSH_INT(arg->integer_literal.value.value);
                                                }
                                                else if (arg->type == ASTNodeType::STRING_LITERAL) {
                                                    PUSH_STR('"'); PUSH_STR(arg->string_literal.value); PUSH_STR('"');
                                                }
                                            }
                                            PUSH_STR(')');
                                        }
                                        else {
                                            PUSH_INT(op.integer_literal.value);
                                        }
                                    }
                                    break;
                                }
                                case ASTNodeType::PROC_CALL: {
                                    auto &pc = statement.variable_definition.proc_call_expr;
                                    PUSH_STR(pc.caller_identifier);
                                    PUSH_STR('(');
                                    for (size_t i = 0; i < pc.arguments.length; i++) {
                                        if (i != 0) {
                                            PUSH_STR(", ");
                                        }
                                        auto *arg = &pc.arguments[i];
                                        if (arg->type == ASTNodeType::IDENTIFIER) {
                                            PUSH_STR(arg->identifier);
                                        }
                                        else if (arg->type == ASTNodeType::INTEGER_LITERAL) {
                                            PUSH_INT(arg->integer_literal.value.value);
                                        }
                                        else if (arg->type == ASTNodeType::STRING_LITERAL) {
                                            PUSH_STR('"');
                                            PUSH_STR(arg->string_literal.value);
                                            PUSH_STR('"');
                                        }
                                    }
                                    PUSH_STR(')');
                                    break;
                                }
                                case ASTNodeType::ARRAY_ACCESS: {
                                    auto &aa = statement.variable_definition.array_access_expr;
                                    PUSH_STR(aa.variable_name);
                                    PUSH_STR('[');
                                    PUSH_INT(aa.index);
                                    PUSH_STR(']');
                                    break;
                                }
                                default:
                                    assert(false && "Unsupported expression type in variable definition transpilation");
                            }
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::IF_ELSE: {
                            auto emit_proc_call = [&](ASTNode *stmt) {
                                PUSH_STR("\t\t");
                                PUSH_STR(stmt->proc_call.caller_identifier);
                                PUSH_STR('(');
                                for (size_t i = 0; i < stmt->proc_call.arguments.length; i++) {
                                    if (i != 0) { PUSH_STR(", "); }
                                    auto *arg = &stmt->proc_call.arguments[i];
                                    if (arg->type == ASTNodeType::IDENTIFIER) {
                                        PUSH_STR(arg->identifier);
                                    }
                                    else if (arg->type == ASTNodeType::INTEGER_LITERAL) {
                                        PUSH_INT(arg->integer_literal.value.value);
                                    }
                                    else if (arg->type == ASTNodeType::STRING_LITERAL) {
                                        PUSH_STR('"'); PUSH_STR(arg->string_literal.value); PUSH_STR('"');
                                    }
                                }
                                PUSH_STR(");\n");
                            };

                            auto emit_body = [&](Array<ASTNode> *body, ASTNode *owner) {
                                for (auto &stmt : *body) {
                                    if (stmt.parent != owner || stmt.type != ASTNodeType::PROC_CALL) {
                                        continue;
                                    }
                                    emit_proc_call(&stmt);
                                }
                            };

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

                            PUSH_STR('\t');
                            emit_condition(&statement);

                            ASTNode *current_if = &statement;
                            while (true) {
                                auto &cur = current_if->if_else;
                                emit_body(&cur.then_body, current_if);

                                if (cur.else_body.data == nullptr) {
                                    PUSH_STR('\t');
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
                                    PUSH_STR('\t');
                                    PUSH_STR("} else ");
                                    emit_condition(next_if);
                                    current_if = next_if;
                                }
                                else {
                                    PUSH_STR('\t');
                                    PUSH_STR("} else {\n");
                                    emit_body(&cur.else_body, current_if);
                                    PUSH_STR('\t');
                                    PUSH_STR("}\n");
                                    break;
                                }
                            }
                            break;
                        }
                        default:
                            break;
                    }
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