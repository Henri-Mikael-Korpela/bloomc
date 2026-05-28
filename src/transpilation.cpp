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
                    switch (statement.type) {
                        case ASTNodeType::BINARY_ADD: {
                            PUSH_STR('\t');
                            PUSH_STR("return ");
                            auto &bl = statement.binary_operation.left;
                            auto &br = statement.binary_operation.right;
                            if (bl.is_identifier) {
                                PUSH_STR(bl.identifier);
                            }
                            else {
                                PUSH_INT(bl.integer_literal.value);
                            }
                            PUSH_STR(" + ");
                            if (br.is_identifier) {
                                PUSH_STR(br.identifier);
                            }
                            else {
                                PUSH_INT(br.integer_literal.value);
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
                                else if (arg->type != ASTNodeType::STRING_LITERAL) {
                                    assert(false && "Only identifier and string literal arguments are supported in transpilation");
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
                                    auto &vl = statement.variable_definition.add_expr.left;
                                    auto &vr = statement.variable_definition.add_expr.right;
                                    if (vl.is_identifier) {
                                        PUSH_STR(vl.identifier);
                                    }
                                    else {
                                        PUSH_INT(vl.integer_literal.value);
                                    }
                                    PUSH_STR(" + ");
                                    if (vr.is_identifier) {
                                        PUSH_STR(vr.identifier);
                                    }
                                    else {
                                        PUSH_INT(vr.integer_literal.value);
                                    }
                                    break;
                                }
                                default:
                                    assert(false && "Unsupported expression type in variable definition transpilation");
                            }
                            PUSH_STR(";\n");
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