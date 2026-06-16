#include <bloom/parsing_internal.h>

/**
 * Parses procedure call parameters and appends them to the given procedure call AST node.
 *
 * @return true on success, false on failure.
 */
auto parse_proc_call_arguments(
    Iterator<Token> *tokens_iter,
    ASTNode *proc_call_node,
    Iterator<ASTNode> *nodes_block_iter,
    Context *context,
    Iterator<BinaryOperand> *operands_iter,
    DynamicArray<ParseError> *errors,
    Token::Position call_pos = {}
) -> bool {
    assert(proc_call_node->type == ASTNodeType::PROC_CALL &&
        "Procedure call node should be of PROC_CALL type after parsing arguments");

    // Nested proc calls are parsed in two phases:
    //   Phase 1: append all top-level argument nodes (nested calls get placeholder nodes)
    //   Phase 2: fill in each nested call's sub-arguments after the top-level args
    // This keeps the outer call's arguments slice contiguous and correctly sized.
    struct PendingNestedCall {
        ASTNode *node;
        size_t token_begin;
        size_t token_end;
        Token::Position identifier_pos;
    };
    constexpr size_t MAX_PENDING_NESTED_CALLS = 16;
    PendingNestedCall pending_nested_calls[MAX_PENDING_NESTED_CALLS];
    size_t pending_nested_call_count = 0;

    size_t const proc_call_nodes_begin_index = nodes_block_iter->current_index;
    size_t arg_count = 0;

    // Parses one simple (non-proc-call) operand from the token stream.
    auto parse_simple_operand = [&](Token *token) -> Result<BinaryOperand, ParseError> {
        if (token->type == TokenType::IDENTIFIER) {
            if (tokens_iter->current_index < tokens_iter->elements.length &&
                iter_peek(tokens_iter)->type == TokenType::DOT)
            {
                (void)iter_next(tokens_iter); // consume .
                auto *field_tok = iter_next(tokens_iter);
                if (field_tok->type != TokenType::IDENTIFIER) {
                    return err<BinaryOperand, ParseError>(ParseError {
                        .code = ParseErrorCode::UNEXPECTED_TOKEN,
                        .position = field_tok->position,
                        .src_code_line = __LINE__,
                        .token_type = field_tok->type,
                    });
                }
                return ok<BinaryOperand, ParseError>(BinaryOperand {
                    .type = BinaryOperandType::MEMBER_ACCESS,
                    .member_access = {
                        .object_name = token->identifier.content,
                        .field_name = field_tok->identifier.content,
                    },
                });
            }
            if (tokens_iter->current_index < tokens_iter->elements.length &&
                iter_peek(tokens_iter)->type == TokenType::BRACKET_OPEN)
            {
                (void)iter_next(tokens_iter); // consume [
                auto *idx_tok = iter_next(tokens_iter);
                if (idx_tok->type == TokenType::RANGE_COUNTED) {
                    // [..+M] or [..+VAR] — counted slice from beginning: start at 0, take M elements
                    auto *count_tok = iter_next(tokens_iter);
                    auto *cl_tok = iter_next(tokens_iter);
                    if (cl_tok->type != TokenType::BRACKET_CLOSE) {
                        return err<BinaryOperand, ParseError>(ParseError {
                            .code = ParseErrorCode::UNEXPECTED_TOKEN,
                            .position = cl_tok->position,
                            .src_code_line = __LINE__,
                            .token_type = cl_tok->type,
                        });
                    }
                    ASTNode *slice_node;
                    if (count_tok->type == TokenType::INTEGER_LITERAL) {
                        slice_node = iter_append(nodes_block_iter, ASTNode {
                            .type = ASTNodeType::ARRAY_SLICE,
                            .parent = proc_call_node,
                            .array_slice = {
                                .variable_name = token->identifier.content,
                                .start_index = -1,
                                .end_index = count_tok->integer_literal.value,
                            },
                        });
                    }
                    else if (count_tok->type == TokenType::IDENTIFIER) {
                        slice_node = iter_append(nodes_block_iter, ASTNode {
                            .type = ASTNodeType::ARRAY_SLICE,
                            .parent = proc_call_node,
                            .array_slice = {
                                .variable_name = token->identifier.content,
                                .start_index = -1,
                                .end_index = -1,
                                .count_identifier = count_tok->identifier.content,
                            },
                        });
                    }
                    else {
                        return err<BinaryOperand, ParseError>(ParseError {
                            .code = ParseErrorCode::UNEXPECTED_TOKEN,
                            .position = count_tok->position,
                            .src_code_line = __LINE__,
                            .token_type = count_tok->type,
                        });
                    }
                    return ok<BinaryOperand, ParseError>(BinaryOperand {
                        .type = BinaryOperandType::EXPR_NODE,
                        .expr_node = slice_node,
                    });
                }
                if (idx_tok->type == TokenType::RANGE_EXCLUSIVE) {
                    // [..<N] — exclusive slice from beginning: start at 0, up to but not including N
                    auto *end_tok = iter_next(tokens_iter);
                    if (end_tok->type != TokenType::INTEGER_LITERAL) {
                        return err<BinaryOperand, ParseError>(ParseError {
                            .code = ParseErrorCode::UNEXPECTED_TOKEN,
                            .position = end_tok->position,
                            .src_code_line = __LINE__,
                            .token_type = end_tok->type,
                        });
                    }
                    auto *cl_tok = iter_next(tokens_iter);
                    if (cl_tok->type != TokenType::BRACKET_CLOSE) {
                        return err<BinaryOperand, ParseError>(ParseError {
                            .code = ParseErrorCode::UNEXPECTED_TOKEN,
                            .position = cl_tok->position,
                            .src_code_line = __LINE__,
                            .token_type = cl_tok->type,
                        });
                    }
                    ASTNode *slice_node = iter_append(nodes_block_iter, ASTNode {
                        .type = ASTNodeType::ARRAY_SLICE,
                        .parent = proc_call_node,
                        .array_slice = {
                            .variable_name = token->identifier.content,
                            .start_index = -1,
                            .end_index = end_tok->integer_literal.value,
                        },
                    });
                    return ok<BinaryOperand, ParseError>(BinaryOperand {
                        .type = BinaryOperandType::EXPR_NODE,
                        .expr_node = slice_node,
                    });
                }
                if (idx_tok->type == TokenType::RANGE) {
                    // [..] or [..N] — start from beginning, optional end bound
                    int64_t end_index = -1;
                    if (tokens_iter->current_index < tokens_iter->elements.length &&
                        iter_peek(tokens_iter)->type == TokenType::INTEGER_LITERAL)
                    {
                        end_index = iter_next(tokens_iter)->integer_literal.value;
                    }
                    auto *cl_tok = iter_next(tokens_iter);
                    if (cl_tok->type != TokenType::BRACKET_CLOSE) {
                        return err<BinaryOperand, ParseError>(ParseError {
                            .code = ParseErrorCode::UNEXPECTED_TOKEN,
                            .position = cl_tok->position,
                            .src_code_line = __LINE__,
                            .token_type = cl_tok->type,
                        });
                    }
                    ASTNode *slice_node = iter_append(nodes_block_iter, ASTNode {
                        .type = ASTNodeType::ARRAY_SLICE,
                        .parent = proc_call_node,
                        .array_slice = {
                            .variable_name = token->identifier.content,
                            .start_index = -1,
                            .end_index = end_index,
                        },
                    });
                    return ok<BinaryOperand, ParseError>(BinaryOperand {
                        .type = BinaryOperandType::EXPR_NODE,
                        .expr_node = slice_node,
                    });
                }
                if (idx_tok->type == TokenType::INTEGER_LITERAL) {
                    if (tokens_iter->current_index < tokens_iter->elements.length &&
                        iter_peek(tokens_iter)->type == TokenType::RANGE_COUNTED)
                    {
                        // [N..+M] or [N..+VAR] — counted slice: start at N, take M elements
                        int64_t const start_index = idx_tok->integer_literal.value;
                        (void)iter_next(tokens_iter); // consume ..+
                        auto *count_tok = iter_next(tokens_iter);
                        auto *cl_tok = iter_next(tokens_iter);
                        if (cl_tok->type != TokenType::BRACKET_CLOSE) {
                            return err<BinaryOperand, ParseError>(ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = cl_tok->position,
                                .src_code_line = __LINE__,
                                .token_type = cl_tok->type,
                            });
                        }
                        ASTNode *slice_node;
                        if (count_tok->type == TokenType::INTEGER_LITERAL) {
                            slice_node = iter_append(nodes_block_iter, ASTNode {
                                .type = ASTNodeType::ARRAY_SLICE,
                                .parent = proc_call_node,
                                .array_slice = {
                                    .variable_name = token->identifier.content,
                                    .start_index = start_index,
                                    .end_index = start_index + count_tok->integer_literal.value,
                                },
                            });
                        }
                        else if (count_tok->type == TokenType::IDENTIFIER) {
                            slice_node = iter_append(nodes_block_iter, ASTNode {
                                .type = ASTNodeType::ARRAY_SLICE,
                                .parent = proc_call_node,
                                .array_slice = {
                                    .variable_name = token->identifier.content,
                                    .start_index = start_index,
                                    .end_index = -1,
                                    .count_identifier = count_tok->identifier.content,
                                },
                            });
                        }
                        else {
                            return err<BinaryOperand, ParseError>(ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = count_tok->position,
                                .src_code_line = __LINE__,
                                .token_type = count_tok->type,
                            });
                        }
                        return ok<BinaryOperand, ParseError>(BinaryOperand {
                            .type = BinaryOperandType::EXPR_NODE,
                            .expr_node = slice_node,
                        });
                    }
                    if (tokens_iter->current_index < tokens_iter->elements.length &&
                        iter_peek(tokens_iter)->type == TokenType::RANGE)
                    {
                        // [N..] or [N..M] — explicit start, optional end bound
                        int64_t const start_index = idx_tok->integer_literal.value;
                        (void)iter_next(tokens_iter); // consume ..
                        int64_t end_index = -1;
                        if (tokens_iter->current_index < tokens_iter->elements.length &&
                            iter_peek(tokens_iter)->type == TokenType::INTEGER_LITERAL)
                        {
                            end_index = iter_next(tokens_iter)->integer_literal.value;
                        }
                        auto *cl_tok = iter_next(tokens_iter);
                        if (cl_tok->type != TokenType::BRACKET_CLOSE) {
                            return err<BinaryOperand, ParseError>(ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = cl_tok->position,
                                .src_code_line = __LINE__,
                                .token_type = cl_tok->type,
                            });
                        }
                        ASTNode *slice_node = iter_append(nodes_block_iter, ASTNode {
                            .type = ASTNodeType::ARRAY_SLICE,
                            .parent = proc_call_node,
                            .array_slice = {
                                .variable_name = token->identifier.content,
                                .start_index = start_index,
                                .end_index = end_index,
                            },
                        });
                        return ok<BinaryOperand, ParseError>(BinaryOperand {
                            .type = BinaryOperandType::EXPR_NODE,
                            .expr_node = slice_node,
                        });
                    }
                    if (tokens_iter->current_index < tokens_iter->elements.length &&
                        iter_peek(tokens_iter)->type == TokenType::RANGE_EXCLUSIVE)
                    {
                        // [N..<M] — exclusive range: start at N, up to but not including M
                        int64_t const start_index = idx_tok->integer_literal.value;
                        (void)iter_next(tokens_iter); // consume ..<
                        if (tokens_iter->current_index >= tokens_iter->elements.length) {
                            return err<BinaryOperand, ParseError>(ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = idx_tok->position,
                                .src_code_line = __LINE__,
                                .token_type = idx_tok->type,
                            });
                        }
                        auto *end_tok = iter_next(tokens_iter);
                        int64_t end_index = 0;
                        if (end_tok->type == TokenType::INTEGER_LITERAL) {
                            end_index = end_tok->integer_literal.value;
                        }
                        else if (end_tok->type == TokenType::IDENTIFIER) {
                            bool found = false;
                            for (size_t i = 0; i < context->constant_count; i++) {
                                if (str_equal(context->constants[i].name, end_tok->identifier.content)) {
                                    end_index = context->constants[i].value;
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                return err<BinaryOperand, ParseError>(ParseError {
                                    .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                    .position = end_tok->position,
                                    .src_code_line = __LINE__,
                                    .token_type = end_tok->type,
                                });
                            }
                        }
                        else {
                            return err<BinaryOperand, ParseError>(ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = end_tok->position,
                                .src_code_line = __LINE__,
                                .token_type = end_tok->type,
                            });
                        }
                        auto *cl_tok = iter_next(tokens_iter);
                        if (cl_tok->type != TokenType::BRACKET_CLOSE) {
                            return err<BinaryOperand, ParseError>(ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = cl_tok->position,
                                .src_code_line = __LINE__,
                                .token_type = cl_tok->type,
                            });
                        }
                        ASTNode *slice_node = iter_append(nodes_block_iter, ASTNode {
                            .type = ASTNodeType::ARRAY_SLICE,
                            .parent = proc_call_node,
                            .array_slice = {
                                .variable_name = token->identifier.content,
                                .start_index = start_index,
                                .end_index = end_index,
                            },
                        });
                        return ok<BinaryOperand, ParseError>(BinaryOperand {
                            .type = BinaryOperandType::EXPR_NODE,
                            .expr_node = slice_node,
                        });
                    }
                    // [N] — regular element access, or [N].field — array element member access
                    auto *cl_tok = iter_next(tokens_iter);
                    if (cl_tok->type != TokenType::BRACKET_CLOSE) {
                        return err<BinaryOperand, ParseError>(ParseError {
                            .code = ParseErrorCode::UNEXPECTED_TOKEN,
                            .position = cl_tok->position,
                            .src_code_line = __LINE__,
                            .token_type = cl_tok->type,
                        });
                    }
                    if (tokens_iter->current_index < tokens_iter->elements.length &&
                        iter_peek(tokens_iter)->type == TokenType::DOT)
                    {
                        (void)iter_next(tokens_iter); // consume .
                        auto *field_tok = iter_next(tokens_iter);
                        if (field_tok->type != TokenType::IDENTIFIER) {
                            return err<BinaryOperand, ParseError>(ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = field_tok->position,
                                .src_code_line = __LINE__,
                                .token_type = field_tok->type,
                            });
                        }
                        ASTNode *elem_member_node = iter_append(nodes_block_iter, ASTNode {
                            .type = ASTNodeType::ARRAY_ELEMENT_MEMBER_ACCESS,
                            .parent = proc_call_node,
                            .array_element_member_access = {
                                .array_name = token->identifier.content,
                                .element_index = idx_tok->integer_literal.value,
                                .field_name = field_tok->identifier.content,
                            },
                        });
                        return ok<BinaryOperand, ParseError>(BinaryOperand {
                            .type = BinaryOperandType::EXPR_NODE,
                            .expr_node = elem_member_node,
                        });
                    }
                    return ok<BinaryOperand, ParseError>(BinaryOperand {
                        .type = BinaryOperandType::ARRAY_ACCESS,
                        .array_access = {
                            .variable_name = token->identifier.content,
                            .index = idx_tok->integer_literal.value,
                        },
                    });
                }
                return err<BinaryOperand, ParseError>(ParseError {
                    .code = ParseErrorCode::UNEXPECTED_TOKEN,
                    .position = idx_tok->position,
                    .src_code_line = __LINE__,
                    .token_type = idx_tok->type,
                });
            }
            if (tokens_iter->current_index < tokens_iter->elements.length &&
                iter_peek(tokens_iter)->type == TokenType::CARET)
            {
                (void)iter_next(tokens_iter); // consume ^
                return ok<BinaryOperand, ParseError>(BinaryOperand {
                    .type = BinaryOperandType::DEREF,
                    .identifier = token->identifier.content,
                });
            }
            return ok<BinaryOperand, ParseError>(BinaryOperand {
                .type = BinaryOperandType::IDENTIFIER,
                .identifier = token->identifier.content,
            });
        }
        if (token->type == TokenType::INTEGER_LITERAL) {
            return ok<BinaryOperand, ParseError>(BinaryOperand {
                .type = BinaryOperandType::INTEGER_LITERAL,
                .integer_literal = IntegerLiteralASTNode { .value = token->integer_literal.value },
            });
        }
        return err<BinaryOperand, ParseError>(ParseError {
            .code = ParseErrorCode::UNEXPECTED_TOKEN,
            .position = token->position,
            .src_code_line = __LINE__,
            .token_type = token->type,
        });
    };

    // Parses a multiplicative expression (handles * and /) starting from tok.
    // Builds binary tree nodes in nodes_block_iter; returns the root as a BinaryOperand.
    auto parse_multiplicative = [&](Token *tok) -> Result<BinaryOperand, ParseError> {
        auto left_result = parse_simple_operand(tok);
        if (!is_ok(&left_result)) { return left_result; }
        BinaryOperand left = std::move(left_result.ok);

        while (tokens_iter->current_index < tokens_iter->elements.length) {
            TokenType const op_type = iter_peek(tokens_iter)->type;
            if (op_type != TokenType::MULTIPLY && op_type != TokenType::DIVIDE) { break; }
            (void)iter_next(tokens_iter);
            Token *rhs_tok = iter_next(tokens_iter);
            auto rhs_result = parse_simple_operand(rhs_tok);
            if (!is_ok(&rhs_result)) { append(errors, rhs_result.err); return rhs_result; }

            size_t const ops_begin = operands_iter->current_index;
            { BinaryOperand tmp = left; (void)iter_append(operands_iter, std::move(tmp)); }
            (void)iter_append(operands_iter, std::move(rhs_result.ok));

            ASTNodeType const node_type = (op_type == TokenType::MULTIPLY)
                ? ASTNodeType::BINARY_MUL : ASTNodeType::BINARY_DIV;
            BinaryOperatorType const bin_op = (op_type == TokenType::MULTIPLY)
                ? BinaryOperatorType::MUL : BinaryOperatorType::DIV;
            ASTNode *new_node = iter_append(nodes_block_iter, ASTNode {
                .type = node_type,
                .parent = proc_call_node,
                .binary_operation = {
                    .oprt = bin_op,
                    .operands = Array<BinaryOperand>(
                        operands_iter->elements.data + ops_begin, 2
                    ),
                },
            });
            left = BinaryOperand { .type = BinaryOperandType::EXPR_NODE, .expr_node = new_node };
        }
        return ok<BinaryOperand, ParseError>(std::move(left));
    };

    // Parses an additive expression (handles + and -) starting from tok.
    auto parse_additive = [&](Token *tok) -> Result<BinaryOperand, ParseError> {
        auto left_result = parse_multiplicative(tok);
        if (!is_ok(&left_result)) { return left_result; }
        BinaryOperand left = std::move(left_result.ok);

        while (tokens_iter->current_index < tokens_iter->elements.length) {
            TokenType const op_type = iter_peek(tokens_iter)->type;
            if (op_type != TokenType::ADD && op_type != TokenType::SUBTRACT) { break; }
            (void)iter_next(tokens_iter);
            Token *rhs_tok = iter_next(tokens_iter);
            auto rhs_result = parse_multiplicative(rhs_tok);
            if (!is_ok(&rhs_result)) { append(errors, rhs_result.err); return rhs_result; }

            size_t const ops_begin = operands_iter->current_index;
            { BinaryOperand tmp = left; (void)iter_append(operands_iter, std::move(tmp)); }
            (void)iter_append(operands_iter, std::move(rhs_result.ok));

            ASTNodeType const node_type = (op_type == TokenType::ADD)
                ? ASTNodeType::BINARY_ADD : ASTNodeType::BINARY_SUB;
            BinaryOperatorType const bin_op = (op_type == TokenType::ADD)
                ? BinaryOperatorType::ADD : BinaryOperatorType::SUB;
            ASTNode *new_node = iter_append(nodes_block_iter, ASTNode {
                .type = node_type,
                .parent = proc_call_node,
                .binary_operation = {
                    .oprt = bin_op,
                    .operands = Array<BinaryOperand>(
                        operands_iter->elements.data + ops_begin, 2
                    ),
                },
            });
            left = BinaryOperand { .type = BinaryOperandType::EXPR_NODE, .expr_node = new_node };
        }
        return ok<BinaryOperand, ParseError>(std::move(left));
    };

    // Reserves an arg slot first, then parses a full expression (with optional ==
    // comparison) into that slot. Intermediate binary nodes go after the slot so the
    // arguments array slice remains contiguous from proc_call_nodes_begin_index.
    auto parse_expr_arg = [&](Token *tok) -> bool {
        ASTNode *arg_slot = iter_append(nodes_block_iter, ASTNode {
            .type = ASTNodeType::UNKNOWN,
            .parent = proc_call_node,
        });

        auto left_result = parse_additive(tok);
        if (!is_ok(&left_result)) { append(errors, left_result.err); return false; }
        BinaryOperand left = std::move(left_result.ok);

        if (tokens_iter->current_index < tokens_iter->elements.length &&
            iter_peek(tokens_iter)->type == TokenType::EQUAL_EQUAL)
        {
            (void)iter_next(tokens_iter);
            Token *rhs_tok = iter_next(tokens_iter);
            auto rhs_result = parse_additive(rhs_tok);
            if (!is_ok(&rhs_result)) { append(errors, rhs_result.err); return false; }

            size_t const ops_begin = operands_iter->current_index;
            { BinaryOperand tmp = left; (void)iter_append(operands_iter, std::move(tmp)); }
            (void)iter_append(operands_iter, std::move(rhs_result.ok));

            arg_slot->type = ASTNodeType::COMPARISON;
            arg_slot->binary_operation = {
                .oprt = BinaryOperatorType::ADD,
                .operands = Array<BinaryOperand>(
                    operands_iter->elements.data + ops_begin, 2
                ),
            };
            return true;
        }

        if (left.type == BinaryOperandType::EXPR_NODE) {
            *arg_slot = *left.expr_node;
            arg_slot->parent = proc_call_node;
        }
        else {
            switch (left.type) {
                case BinaryOperandType::INTEGER_LITERAL:
                    arg_slot->type = ASTNodeType::INTEGER_LITERAL;
                    arg_slot->integer_literal = { .value = left.integer_literal };
                    break;
                case BinaryOperandType::MEMBER_ACCESS:
                    arg_slot->type = ASTNodeType::MEMBER_ACCESS;
                    arg_slot->member_access.object_name = left.member_access.object_name;
                    arg_slot->member_access.field_name = left.member_access.field_name;
                    break;
                case BinaryOperandType::ARRAY_ACCESS:
                    arg_slot->type = ASTNodeType::ARRAY_ACCESS;
                    arg_slot->array_access.variable_name = left.array_access.variable_name;
                    arg_slot->array_access.index = left.array_access.index;
                    break;
                case BinaryOperandType::DEREF:
                    arg_slot->type = ASTNodeType::DEREF;
                    arg_slot->identifier = left.identifier;
                    break;
                default:
                    arg_slot->type = ASTNodeType::IDENTIFIER;
                    arg_slot->identifier = left.identifier;
                    break;
            }
        }
        return true;
    };

    auto check_arg_type = [&](Token const *arg_token) -> bool {
        if (context == nullptr) { return true; }
        ASTNode const *proc_def = find_proc_def_node(nodes_block_iter, &proc_call_node->proc_call.caller_identifier);
        if (proc_def == nullptr || arg_count >= proc_def->proc_def.parameters.length) {
            return true;
        }
        ProcParameterASTNode const *param = &proc_def->proc_def.parameters.data[arg_count];
        if (param->type_name.length == 0) { return true; }
        ASTNode const *appended_arg = &nodes_block_iter->elements.data[nodes_block_iter->current_index - 1];

        auto compute_token_width = [&]() -> size_t {
            switch (arg_token->type) {
                case TokenType::KEYWORD_TRUE:
                    return 4;
                case TokenType::KEYWORD_FALSE:
                    return 5;
                case TokenType::IDENTIFIER:
                    return arg_token->identifier.content.length;
                case TokenType::STRING_LITERAL:
                    return arg_token->string_literal.content.length + 2;
                default:
                    return 1;
            }
        };

        if (param->is_pointer) {
            if (appended_arg->type == ASTNodeType::ADDRESS_OF) { return true; }
            Str actual_type = infer_arg_type_name(appended_arg, context);
            if (actual_type.length == 0) { return true; }
            append(errors, ParseError {
                .code = ParseErrorCode::PROC_ARG_TYPE_MISMATCH,
                .position = arg_token->position,
                .src_code_line = __LINE__,
                .token_type = arg_token->type,
                .size_token_width = compute_token_width(),
                .expected_type_name = param->type_name,
                .expected_type_is_pointer = true,
                .actual_type_name = actual_type,
                .param_name = param->name,
            });
            return false;
        }

        Str actual_type = infer_arg_type_name(appended_arg, context);
        if (actual_type.length == 0 || str_equal(actual_type, param->type_name)) { return true; }
        append(errors, ParseError {
            .code = ParseErrorCode::PROC_ARG_TYPE_MISMATCH,
            .position = arg_token->position,
            .src_code_line = __LINE__,
            .token_type = arg_token->type,
            .size_token_width = compute_token_width(),
            .expected_type_name = param->type_name,
            .expected_type_is_pointer = false,
            .actual_type_name = actual_type,
            .param_name = param->name,
        });
        return false;
    };

    Token *next_token;
    while(true) {
        next_token = iter_try_next(tokens_iter);
        if (next_token == nullptr) {
            break;
        }
        assert(next_token->type != TokenType::PARENTHESIS_CLOSE &&
            "Unexpected closing parenthesis while parsing procedure call arguments. "
            "Closing parenthesis should have been handled by the caller function."
        );
        switch (next_token->type) {
            case TokenType::COMMA:
                continue;
            case TokenType::IDENTIFIER: {
                if (tokens_iter->current_index < tokens_iter->elements.length &&
                    iter_peek(tokens_iter)->type == TokenType::PARENTHESIS_OPEN)
                {
                    bool const is_builtin = (
                        next_token->identifier.content == "length" ||
                        next_token->identifier.content == "length_in_bytes"
                    );
                    (void)iter_next(tokens_iter); // consume (
                    if (is_builtin) {
                        auto *inner_id_token = iter_next(tokens_iter);
                        if (inner_id_token->type != TokenType::IDENTIFIER) {
                            append(errors, ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = inner_id_token->position,
                                .src_code_line = __LINE__,
                                .token_type = inner_id_token->type,
                            });
                            return false;
                        }
                        auto *close_paren = iter_next(tokens_iter);
                        if (close_paren->type != TokenType::PARENTHESIS_CLOSE) {
                            append(errors, ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = close_paren->position,
                                .src_code_line = __LINE__,
                                .token_type = close_paren->type,
                            });
                            return false;
                        }
                        ASTNodeType const builtin_type =
                            (next_token->identifier.content == "length_in_bytes")
                            ? ASTNodeType::BUILTIN_LENGTH_IN_BYTES
                            : ASTNodeType::BUILTIN_LENGTH;
                        (void)iter_append(nodes_block_iter, ASTNode {
                            .type = builtin_type,
                            .parent = proc_call_node,
                            .identifier = inner_id_token->identifier.content,
                        });
                        if (!check_arg_type(next_token)) { return false; }
                        arg_count++;
                    }
                    else if (next_token->identifier.content == "type_info_of") {
                        // type_info_of(expr).size_in_bytes or .name — parse-time resolution
                        size_t const inner_start = tokens_iter->current_index;
                        int depth = 1;
                        size_t inner_end = inner_start;
                        while (inner_end < tokens_iter->elements.length) {
                            TokenType const tt = tokens_iter->elements.data[inner_end].type;
                            if (tt == TokenType::PARENTHESIS_OPEN)  { depth++; }
                            if (tt == TokenType::PARENTHESIS_CLOSE) { depth--; if (depth == 0) { break; } }
                            inner_end++;
                        }
                        if (depth != 0) {
                            append(errors, ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = next_token->position,
                                .src_code_line = __LINE__,
                                .token_type = next_token->type,
                            });
                            return false;
                        }
                        auto inner_iter = iter_slice_by_offset(
                            tokens_iter, inner_start, static_cast<int64_t>(inner_end));
                        tokens_iter->current_index = inner_end + 1; // skip past )
                        Str type_name = infer_bloom_type_from_tokens(
                            &inner_iter, context, nodes_block_iter);
                        if (type_name.length == 0 ||
                            tokens_iter->current_index >= tokens_iter->elements.length ||
                            iter_next(tokens_iter)->type != TokenType::DOT ||
                            tokens_iter->current_index >= tokens_iter->elements.length)
                        {
                            append(errors, ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = next_token->position,
                                .src_code_line = __LINE__,
                                .token_type = next_token->type,
                            });
                            return false;
                        }
                        auto *field_tok = iter_next(tokens_iter);
                        if (field_tok->type != TokenType::IDENTIFIER) {
                            append(errors, ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = field_tok->position,
                                .src_code_line = __LINE__,
                                .token_type = field_tok->type,
                            });
                            return false;
                        }
                        if (field_tok->identifier.content == "size_in_bytes") {
                            (void)iter_append(nodes_block_iter, ASTNode {
                                .type = ASTNodeType::TYPE_INFO_SIZE,
                                .parent = proc_call_node,
                                .type_info_size = { .type_name = type_name },
                            });
                        }
                        else if (field_tok->identifier.content == "name") {
                            (void)iter_append(nodes_block_iter, ASTNode {
                                .type = ASTNodeType::TYPE_INFO_NAME,
                                .parent = proc_call_node,
                                .type_info_name = { .type_name = type_name },
                            });
                        }
                        else if (field_tok->identifier.content == "members") {
                            if (expect_token_or_append_error(tokens_iter, TokenType::BRACKET_OPEN, errors) == nullptr) { return false; }
                            auto *idx_tok = iter_next(tokens_iter);
                            if (idx_tok->type != TokenType::IDENTIFIER) {
                                append(errors, ParseError {
                                    .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                    .position = idx_tok->position,
                                    .src_code_line = __LINE__,
                                    .token_type = idx_tok->type,
                                });
                                return false;
                            }
                            Str index_var = idx_tok->identifier.content;
                            if (expect_token_or_append_error(tokens_iter, TokenType::BRACKET_CLOSE, errors) == nullptr) { return false; }
                            if (expect_token_or_append_error(tokens_iter, TokenType::DOT, errors) == nullptr) { return false; }
                            auto *key_tok = iter_next(tokens_iter);
                            if (key_tok->type != TokenType::IDENTIFIER) {
                                append(errors, ParseError {
                                    .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                    .position = key_tok->position,
                                    .src_code_line = __LINE__,
                                    .token_type = key_tok->type,
                                });
                                return false;
                            }
                            (void)iter_append(nodes_block_iter, ASTNode {
                                .type = ASTNodeType::TYPE_INFO_ENUM_MEMBER_KEY,
                                .parent = proc_call_node,
                                .type_info_enum_member_key = {
                                    .enum_type_name = type_name,
                                    .index_var = index_var,
                                },
                            });
                        }
                        else {
                            append(errors, ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = field_tok->position,
                                .src_code_line = __LINE__,
                                .token_type = field_tok->type,
                            });
                            return false;
                        }
                        if (!check_arg_type(next_token)) { return false; }
                        arg_count++;
                    }
                    else {
                        // General nested proc call: append a placeholder node for phase 1,
                        // defer sub-argument parsing to phase 2 so other top-level arguments
                        // added after this call remain contiguous with the outer arg slice.
                        assert(pending_nested_call_count < MAX_PENDING_NESTED_CALLS &&
                            "Too many nested proc call arguments");
                        size_t const nested_tokens_begin = tokens_iter->current_index;
                        int paren_depth = 0;
                        int64_t const close_paren_idx = iter_get_index_at_if<Token>(
                            tokens_iter, [&paren_depth](Token const *t) {
                                if (t->type == TokenType::PARENTHESIS_OPEN) {
                                    paren_depth++;
                                    return false;
                                }
                                if (t->type == TokenType::PARENTHESIS_CLOSE) {
                                    if (paren_depth == 0) return true;
                                    paren_depth--;
                                }
                                return false;
                            }
                        );
                        auto *nested_node = iter_append(nodes_block_iter, ASTNode {
                            .type = ASTNodeType::PROC_CALL,
                            .parent = proc_call_node,
                            .proc_call = { .caller_identifier = next_token->identifier.content },
                        });
                        if (!check_arg_type(next_token)) { return false; }
                        arg_count++;
                        pending_nested_calls[pending_nested_call_count++] = {
                            nested_node,
                            nested_tokens_begin,
                            static_cast<size_t>(close_paren_idx),
                            next_token->position,
                        };
                        tokens_iter->current_index = static_cast<size_t>(close_paren_idx) + 1;
                    }
                }
                else {
                    if (!parse_expr_arg(next_token)) { return false; }
                    if (!check_arg_type(next_token)) { return false; }
                    arg_count++;
                }
                break;
            }
            case TokenType::STRING_LITERAL:
                (void)iter_append(nodes_block_iter, ASTNode {
                    .type = ASTNodeType::STRING_LITERAL,
                    .parent = proc_call_node,
                    .string_literal = {
                        .value = next_token->string_literal.content,
                    },
                });
                if (!check_arg_type(next_token)) { return false; }
                arg_count++;
                break;
            case TokenType::INTEGER_LITERAL: {
                if (!parse_expr_arg(next_token)) { return false; }
                if (!check_arg_type(next_token)) { return false; }
                arg_count++;
                break;
            }
            case TokenType::KEYWORD_TRUE:
            case TokenType::KEYWORD_FALSE:
                (void)iter_append(nodes_block_iter, ASTNode {
                    .type = ASTNodeType::BOOLEAN_LITERAL,
                    .parent = proc_call_node,
                    .boolean_literal = {
                        .value = next_token->type == TokenType::KEYWORD_TRUE,
                    },
                });
                if (!check_arg_type(next_token)) { return false; }
                arg_count++;
                break;
            case TokenType::ADDRESS_OF: {
                auto *ident_tok = iter_next(tokens_iter);
                if (ident_tok->type != TokenType::IDENTIFIER) {
                    append(errors, ParseError {
                        .code = ParseErrorCode::UNEXPECTED_TOKEN,
                        .position = ident_tok->position,
                        .src_code_line = __LINE__,
                        .token_type = ident_tok->type,
                    });
                    return false;
                }
                (void)iter_append(nodes_block_iter, ASTNode {
                    .type = ASTNodeType::ADDRESS_OF,
                    .parent = proc_call_node,
                    .identifier = ident_tok->identifier.content,
                });
                if (!check_arg_type(next_token)) { return false; }
                arg_count++;
                break;
            }
            case TokenType::DOT: {
                auto *member_tok = iter_next(tokens_iter);
                if (member_tok->type != TokenType::IDENTIFIER) {
                    append(errors, ParseError {
                        .code = ParseErrorCode::UNEXPECTED_TOKEN,
                        .position = member_tok->position,
                        .src_code_line = __LINE__,
                        .token_type = member_tok->type,
                    });
                    return false;
                }
                Str enum_type = {};
                ASTNode const *callee_def = find_proc_def_node(nodes_block_iter, &proc_call_node->proc_call.caller_identifier);
                if (callee_def != nullptr && arg_count < callee_def->proc_def.parameters.length) {
                    Str const type_name = callee_def->proc_def.parameters.data[arg_count].type_name;
                    for (size_t i = 0; i < nodes_block_iter->current_index; i++) {
                        auto const *n = &nodes_block_iter->elements.data[i];
                        if (n->type == ASTNodeType::ENUM_DEF && str_equal(n->enum_def.name, type_name)) {
                            enum_type = type_name;
                            break;
                        }
                    }
                }
                (void)iter_append(nodes_block_iter, ASTNode {
                    .type = ASTNodeType::MEMBER_ACCESS,
                    .parent = proc_call_node,
                    .member_access = {
                        .object_name = enum_type,
                        .field_name = member_tok->identifier.content,
                    },
                });
                if (!check_arg_type(next_token)) { return false; }
                arg_count++;
                break;
            }
            default:
                append(errors, ParseError {
                    .code = ParseErrorCode::UNEXPECTED_TOKEN,
                    .position = next_token->position,
                    .src_code_line = __LINE__,
                    .token_type = next_token->type,
                });
                return false;
        }
    }

    // Phase 2: fill in sub-arguments for each deferred nested call.
    // Sub-arguments are appended after all top-level argument nodes.
    for (size_t p = 0; p < pending_nested_call_count; p++) {
        auto *pending = &pending_nested_calls[p];
        auto nested_args_iter = iter_slice_by_offset(
            tokens_iter,
            pending->token_begin,
            static_cast<int64_t>(pending->token_end)
        );
        if (!parse_proc_call_arguments(&nested_args_iter, pending->node, nodes_block_iter, context, operands_iter, errors, pending->identifier_pos)) {
            return false;
        }
    }

    // Check for too few arguments compared to the procedure definition.
    {
        ASTNode const *proc_def = find_proc_def_node(nodes_block_iter, &proc_call_node->proc_call.caller_identifier);
        if (proc_def != nullptr && !proc_def->proc_def.is_foreign) {
            size_t required_min = 0;
            for (size_t pi = 0; pi < proc_def->proc_def.parameters.length; pi++) {
                if (!proc_def->proc_def.parameters.data[pi].has_default_context_allocator) {
                    required_min++;
                }
            }
            if (arg_count < required_min)
            {
            size_t const required = proc_def->proc_def.parameters.length;
            ParseError too_few_err = {};
            too_few_err.code = ParseErrorCode::PROC_TOO_FEW_ARGS;
            too_few_err.position = call_pos;
            too_few_err.src_code_line = __LINE__;
            too_few_err.size_token_width = proc_call_node->proc_call.caller_identifier.length;
            too_few_err.proc_call_name = proc_call_node->proc_call.caller_identifier;
            too_few_err.proc_given_arg_count = arg_count;
            too_few_err.proc_required_arg_count = required;
            too_few_err.proc_param_count = required < 8 ? required : 8;
            for (size_t pi = 0; pi < too_few_err.proc_param_count; pi++) {
                ProcParameterASTNode const *p = &proc_def->proc_def.parameters.data[pi];
                too_few_err.proc_param_names[pi] = p->name;
                too_few_err.proc_param_type_names[pi] = p->type_name;
                too_few_err.proc_param_is_pointer[pi] = p->is_pointer;
                too_few_err.proc_param_is_slice[pi] = p->is_slice;
            }
            append(errors, too_few_err);
            return false;
            }
        }
    }

    // Check for too many arguments compared to the procedure definition.
    {
        ASTNode const *proc_def = find_proc_def_node(nodes_block_iter, &proc_call_node->proc_call.caller_identifier);
        if (proc_def != nullptr &&
            !proc_def->proc_def.is_foreign &&
            arg_count > proc_def->proc_def.parameters.length)
        {
            size_t const required = proc_def->proc_def.parameters.length;
            ParseError too_many_err = {};
            too_many_err.code = ParseErrorCode::PROC_TOO_MANY_ARGS;
            too_many_err.position = call_pos;
            too_many_err.src_code_line = __LINE__;
            too_many_err.size_token_width = proc_call_node->proc_call.caller_identifier.length;
            too_many_err.proc_call_name = proc_call_node->proc_call.caller_identifier;
            too_many_err.proc_given_arg_count = arg_count;
            too_many_err.proc_required_arg_count = required;
            too_many_err.proc_param_count = required < 8 ? required : 8;
            for (size_t pi = 0; pi < too_many_err.proc_param_count; pi++) {
                ProcParameterASTNode const *p = &proc_def->proc_def.parameters.data[pi];
                too_many_err.proc_param_names[pi] = p->name;
                too_many_err.proc_param_type_names[pi] = p->type_name;
                too_many_err.proc_param_is_pointer[pi] = p->is_pointer;
                too_many_err.proc_param_is_slice[pi] = p->is_slice;
            }
            append(errors, too_many_err);
            return false;
        }
    }

    // Set the arguments array to the contiguous slice of top-level argument nodes only.
    proc_call_node->proc_call.arguments = Array<ASTNode>(
        nodes_block_iter->elements.data + proc_call_nodes_begin_index,
        arg_count
    );

    return true;
}

/**
 * Parses procedure parameters and appends them to the given procedure definition AST node.
 * 
 * The parsing begins with an opening parenthesis token and ends with a closing parenthesis token.
 * 
 * @return true on success, false on failure.
 */
auto parse_proc_params(
    Iterator<Token> *tokens_iter,
    Iterator<ProcParameterASTNode> *proc_params_iter,
    DynamicArray<ParseError> *errors
) -> bool {
    assert(proc_params_iter != nullptr &&
        "Procedure parameters iterator should not be null in proc definition context");

    if (expect_token_or_append_error(tokens_iter, TokenType::PARENTHESIS_OPEN, errors) == nullptr) {
        return false;
    }
    Token *current_token = nullptr;
    while(true) {
        current_token = iter_next(tokens_iter);
        switch (current_token->type) {
            case TokenType::PARENTHESIS_CLOSE:
                return true;
            case TokenType::COMMA:
                // Just skip commas
                continue;
            case TokenType::IDENTIFIER: {
                auto *param_node = iter_append(proc_params_iter, ProcParameterASTNode {
                    .name = current_token->identifier.content
                });

                auto *sep_tok = iter_next(tokens_iter);
                if (sep_tok->type == TokenType::VAR_DEF) {
                    // Default parameter: name := context.allocator
                    // Consume: context . allocator
                    auto *ctx_tok = iter_next(tokens_iter);
                    if (ctx_tok->type == TokenType::IDENTIFIER &&
                        ctx_tok->identifier.content == "context" &&
                        tokens_iter->current_index < tokens_iter->elements.length &&
                        iter_peek(tokens_iter)->type == TokenType::DOT)
                    {
                        (void)iter_next(tokens_iter); // consume '.'
                        auto *field_tok = iter_next(tokens_iter);
                        if (field_tok->type == TokenType::IDENTIFIER &&
                            field_tok->identifier.content == "allocator")
                        {
                            param_node->has_default_context_allocator = true;
                            param_node->type_name = cstr_to_str("Allocator");
                            continue;
                        }
                    }
                    append(errors, ParseError {
                        .code = ParseErrorCode::UNEXPECTED_TOKEN,
                        .position = ctx_tok->position,
                        .src_code_line = __LINE__,
                        .token_type = ctx_tok->type,
                    });
                    return false;
                }
                if (sep_tok->type != TokenType::TYPE_SEPARATOR) {
                    append(errors, ParseError {
                        .code = ParseErrorCode::UNEXPECTED_TOKEN,
                        .position = sep_tok->position,
                        .src_code_line = __LINE__,
                        .token_type = sep_tok->type,
                    });
                    return false;
                }

                auto *maybe_caret = iter_next(tokens_iter);
                if (maybe_caret->type == TokenType::CARET) {
                    param_node->is_pointer = true;
                    auto *type_tok = iter_next(tokens_iter);
                    param_node->type_name = type_tok->identifier.content;
                }
                else if (maybe_caret->type == TokenType::BRACKET_OPEN) {
                    auto *inner_tok = iter_next(tokens_iter);
                    if (inner_tok->type == TokenType::INTEGER_LITERAL) {
                        // [N]Type parameter — fixed-size array
                        auto *close_tok = iter_next(tokens_iter);
                        if (close_tok->type != TokenType::BRACKET_CLOSE) {
                            append(errors, ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = close_tok->position,
                                .src_code_line = __LINE__,
                                .token_type = close_tok->type,
                            });
                            return false;
                        }
                        auto *type_tok = iter_next(tokens_iter);
                        if (type_tok->type != TokenType::IDENTIFIER) {
                            append(errors, ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = type_tok->position,
                                .src_code_line = __LINE__,
                                .token_type = type_tok->type,
                            });
                            return false;
                        }
                        param_node->is_array = true;
                        param_node->array_length = inner_tok->integer_literal.value;
                        param_node->type_name = type_tok->identifier.content;
                    }
                    else if (inner_tok->type == TokenType::BRACKET_CLOSE) {
                        // []Type parameter — slice
                        auto *type_tok = iter_next(tokens_iter);
                        if (type_tok->type != TokenType::IDENTIFIER) {
                            append(errors, ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = type_tok->position,
                                .src_code_line = __LINE__,
                                .token_type = type_tok->type,
                            });
                            return false;
                        }
                        param_node->is_slice = true;
                        param_node->type_name = type_tok->identifier.content;
                    }
                    else {
                        append(errors, ParseError {
                            .code = ParseErrorCode::UNEXPECTED_TOKEN,
                            .position = inner_tok->position,
                            .src_code_line = __LINE__,
                            .token_type = inner_tok->type,
                        });
                        return false;
                    }
                }
                else {
                    param_node->type_name = maybe_caret->identifier.content;
                }
                break;
            }
            default:
                append(errors, ParseError {
                    .code = ParseErrorCode::UNEXPECTED_TOKEN,
                    .position = current_token->position,
                    .src_code_line = __LINE__,
                    .token_type = current_token->type,
                });
                return false;
        }
    }

    assert(current_token->type == TokenType::PARENTHESIS_CLOSE &&
        "Expected closing parenthesis after procedure parameters");
    return true;
}
