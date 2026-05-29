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

    auto emit_proc_call_args = [&](Array<ASTNode> *arguments) {
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
            case BinaryOperandType::PROC_CALL:
                PUSH_STR(op.proc_call.caller_identifier);
                PUSH_STR('(');
                emit_proc_call_args(const_cast<Array<ASTNode>*>(&op.proc_call.arguments));
                PUSH_STR(')');
                break;
            case BinaryOperandType::INTEGER_LITERAL:
                PUSH_INT(op.integer_literal.value);
                break;
        }
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
                PUSH_STR(expr->proc_call.caller_identifier);
                PUSH_STR('(');
                emit_proc_call_args(&expr->proc_call.arguments);
                PUSH_STR(')');
                break;
            case ASTNodeType::BINARY_ADD: {
                auto &operands = expr->binary_operation.operands;
                for (size_t i = 0; i < operands.length; i++) {
                    if (i != 0) { PUSH_STR(" + "); }
                    emit_binary_operand(operands[i]);
                }
                break;
            }
            default:
                assert(false && "Unsupported expression type in emit_expression");
        }
    };

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
                                if (i != 0) { PUSH_STR(" + "); }
                                emit_binary_operand(operands[i]);
                            }
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::PROC_CALL: {
                            PUSH_STR('\t');
                            PUSH_STR(statement.proc_call.caller_identifier);
                            PUSH_STR('(');
                            emit_proc_call_args(&statement.proc_call.arguments);
                            PUSH_STR(");\n");
                            break;
                        }
                        case ASTNodeType::VARIABLE_DEFINITION: {
                            PUSH_STR('\t');
                            ASTNode *expr = statement.variable_definition.expr;
                            if (expr->type == ASTNodeType::ARRAY_INIT) {
                                PUSH_STR("int ");
                                PUSH_STR(statement.variable_definition.name);
                                PUSH_STR("[] = {");
                                auto &elems = expr->array_init.elements;
                                for (size_t i = 0; i < elems.length; i++) {
                                    if (i != 0) { PUSH_STR(", "); }
                                    PUSH_INT(elems[i]);
                                }
                                PUSH_STR("};\n");
                                break;
                            }
                            char const *c_type = (expr->type == ASTNodeType::BOOLEAN_LITERAL)
                                ? "bool" : "int";
                            PUSH_STR(c_type);
                            PUSH_STR(' ');
                            PUSH_STR(statement.variable_definition.name);
                            PUSH_STR(" = ");
                            emit_expression(expr);
                            PUSH_STR(";\n");
                            break;
                        }
                        case ASTNodeType::IF_ELSE: {
                            auto emit_proc_call = [&](ASTNode *stmt) {
                                PUSH_STR("\t\t");
                                PUSH_STR(stmt->proc_call.caller_identifier);
                                PUSH_STR('(');
                                emit_proc_call_args(&stmt->proc_call.arguments);
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