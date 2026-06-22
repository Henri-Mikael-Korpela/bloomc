#include <bloom/parsing_internal.h>

auto parse_statement(
    Iterator<Token> *tokens_iter,
    Context *context,
    Iterator<ASTNode> *nodes_block_iter,
    ASTNode *parent_node,
    AllocatedArrayBlock<ProcParameterASTNode> *proc_params_block,
    Iterator<ProcParameterASTNode> *proc_params_iter,
    Iterator<TypeASTNode> *types_iter,
    Iterator<BinaryOperand> *operands_iter,
    Iterator<int64_t> *array_elements_iter,
    DynamicArray<ParseError> *errors,
    size_t current_indent_level
) -> bool {
    auto *next_token = iter_next(tokens_iter);
    switch (next_token->type) {
    case TokenType::IDENTIFIER: {
        switch (auto peeked_token = iter_peek(tokens_iter); peeked_token->type) {
            case TokenType::PARENTHESIS_OPEN: {
                // Expect a procedure call

                int paren_depth = 0;
                int64_t proc_call_end_token_index = iter_get_index_at_if<Token>(
                    tokens_iter, [&paren_depth](auto *token) {
                        if (token->type == TokenType::PARENTHESIS_OPEN) {
                            paren_depth++;
                            return false;
                        }
                        if (token->type == TokenType::PARENTHESIS_CLOSE) {
                            if (paren_depth == 1) {
                                return true;
                            }
                            paren_depth--;
                        }
                        return false;
                    }
                );
                auto proc_call_arg_tokens_iter = iter_slice_by_offset(
                    tokens_iter,
                    tokens_iter->current_index + 1,
                    proc_call_end_token_index
                );

                auto *proc_call_node = iter_append(nodes_block_iter, ASTNode {
                    .type = ASTNodeType::PROC_CALL,
                    .parent = parent_node,
                    .proc_call = {
                        .caller_identifier = next_token->identifier.content,
                    },
                });
                bool proc_call_args_parsed_ok = parse_proc_call_arguments(
                    &proc_call_arg_tokens_iter,
                    proc_call_node,
                    nodes_block_iter,
                    context,
                    operands_iter,
                    errors,
                    next_token->position
                );
                if (!proc_call_args_parsed_ok) {
                    if (errors->length == 0) {
                        append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, iter_current(tokens_iter)));
                    }
                    return false;
                }

                tokens_iter->current_index = proc_call_end_token_index + 1; // Skip the closing parenthesis token
                assert(iter_peek_prev(tokens_iter)->type == TokenType::PARENTHESIS_CLOSE &&
                    "Expected closing parenthesis token after parsing procedure call arguments");

                // Consume the newline or end token
                assert(
                    iter_current(tokens_iter)->type == TokenType::NEWLINE ||
                    iter_current(tokens_iter)->type == TokenType::END &&
                    "Expected newline or end token after variable definition");
                (void)iter_next(tokens_iter);
                break;
            }
            case TokenType::ADD:
            case TokenType::SUBTRACT:
            case TokenType::MULTIPLY:
            case TokenType::DIVIDE: {
                TokenType const op_type = peeked_token->type;
                size_t operands_begin = operands_iter->current_index;
                (void)iter_append(operands_iter, BinaryOperand {
                    .type = BinaryOperandType::IDENTIFIER,
                    .identifier = next_token->identifier.content,
                });

                while (tokens_iter->current_index < tokens_iter->elements.length &&
                       iter_peek(tokens_iter)->type == op_type)
                {
                    (void)iter_next(tokens_iter); // consume operator
                    Token *operand_token = iter_next(tokens_iter);
                    switch (operand_token->type) {
                        case TokenType::IDENTIFIER:
                            (void)iter_append(operands_iter, BinaryOperand {
                                .type = BinaryOperandType::IDENTIFIER,
                                .identifier = operand_token->identifier.content,
                            });
                            break;
                        case TokenType::INTEGER_LITERAL:
                            (void)iter_append(operands_iter, BinaryOperand {
                                .type = BinaryOperandType::INTEGER_LITERAL,
                                .integer_literal = IntegerLiteralASTNode { .value = operand_token->integer_literal.value },
                            });
                            break;
                        default:
                            return false;
                    }
                }

                ASTNodeType const node_type =
                    (op_type == TokenType::MULTIPLY) ? ASTNodeType::BINARY_MUL :
                    (op_type == TokenType::DIVIDE)   ? ASTNodeType::BINARY_DIV :
                    (op_type == TokenType::SUBTRACT) ? ASTNodeType::BINARY_SUB :
                                                       ASTNodeType::BINARY_ADD;
                BinaryOperatorType const bin_op =
                    (op_type == TokenType::MULTIPLY) ? BinaryOperatorType::MUL :
                    (op_type == TokenType::DIVIDE)   ? BinaryOperatorType::DIV :
                    (op_type == TokenType::SUBTRACT) ? BinaryOperatorType::SUB :
                                                       BinaryOperatorType::ADD;
                iter_append(nodes_block_iter, ASTNode {
                    .type = node_type,
                    .parent = parent_node,
                    .binary_operation = {
                        .oprt = bin_op,
                        .operands = Array<BinaryOperand>(
                            operands_iter->elements.data + operands_begin,
                            operands_iter->current_index - operands_begin
                        ),
                    },
                });
                assert(
                    iter_current(tokens_iter)->type == TokenType::NEWLINE ||
                    iter_current(tokens_iter)->type == TokenType::END &&
                    "Expected newline or end token after binary expression statement");
                (void)iter_next(tokens_iter);
                break;
            }
            case TokenType::ADD_ASSIGN: {
                (void)iter_next(tokens_iter); // consume +=

                Iterator<Token> expr_tokens_iter;
                if (!slice_expression_tokens(tokens_iter, errors, &expr_tokens_iter)) {
                    return false;
                }
                auto expr_parse_result = parse_expression(
                    &expr_tokens_iter, context, nodes_block_iter,
                    proc_params_block, proc_params_iter, types_iter,
                    operands_iter, array_elements_iter, errors
                );
                if (!is_ok(&expr_parse_result)) {
                    append(errors, expr_parse_result.err);
                    return false;
                }
                ASTNode *expr_ptr = iter_append(nodes_block_iter, std::move(expr_parse_result.ok));
                (void)iter_append(nodes_block_iter, ASTNode {
                    .type = ASTNodeType::ADD_ASSIGN,
                    .parent = parent_node,
                    .add_assign = {
                        .variable_name = next_token->identifier.content,
                        .operand = BinaryOperand {
                            .type = BinaryOperandType::EXPR_NODE,
                            .expr_node = expr_ptr,
                        },
                    },
                });
                tokens_iter->current_index += expr_tokens_iter.current_index;
                assert(
                    iter_current(tokens_iter)->type == TokenType::NEWLINE ||
                    iter_current(tokens_iter)->type == TokenType::END &&
                    "Expected newline or end token after add-assign statement");
                (void)iter_next(tokens_iter);
                break;
            }
            case TokenType::VAR_DEF: {
                (void)iter_next(tokens_iter); // Consume VAR_DEF token

                // Special handling for make([]U8, size), make([]U8, size, allocator), or make([dynamic]Int, 0)
                if (tokens_iter->current_index < tokens_iter->elements.length &&
                    iter_peek(tokens_iter)->type == TokenType::IDENTIFIER &&
                    iter_peek(tokens_iter)->identifier.content == "make" &&
                    tokens_iter->current_index + 1 < tokens_iter->elements.length &&
                    tokens_iter->elements.data[tokens_iter->current_index + 1].type == TokenType::PARENTHESIS_OPEN)
                {
                    (void)iter_next(tokens_iter); // consume 'make'
                    (void)iter_next(tokens_iter); // consume '('
                    auto *bopen = iter_next(tokens_iter);
                    if (bopen->type != TokenType::BRACKET_OPEN) {
                        append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, bopen)); return false;
                    }
                    // Check for [dynamic] vs []
                    bool is_dynamic = iter_peek(tokens_iter)->type == TokenType::KEYWORD_DYNAMIC;
                    if (is_dynamic) {
                        (void)iter_next(tokens_iter); // consume 'dynamic'
                    }
                    auto *bclose = iter_next(tokens_iter);
                    if (bclose->type != TokenType::BRACKET_CLOSE) {
                        append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, bclose)); return false;
                    }
                    auto *elem_type = iter_next(tokens_iter);
                    if (elem_type->type != TokenType::IDENTIFIER) {
                        append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, elem_type)); return false;
                    }
                    if (expect_token_or_append_error(tokens_iter, TokenType::COMMA, errors) == nullptr) {
                        return false;
                    }
                    ASTNode make_node = {};
                    make_node.parent = nullptr;
                    if (is_dynamic) {
                        make_node.type = ASTNodeType::MAKE_DYNAMIC_ARRAY;
                        make_node.make_dynamic_array.element_type = elem_type->identifier.content;
                        // Consume and discard initial capacity argument
                        auto *init_tok = iter_next(tokens_iter);
                        if (init_tok->type != TokenType::INTEGER_LITERAL && init_tok->type != TokenType::IDENTIFIER) {
                            append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, init_tok)); return false;
                        }
                        auto *pclose = iter_next(tokens_iter);
                        if (pclose->type != TokenType::PARENTHESIS_CLOSE) {
                            append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, pclose)); return false;
                        }
                    }
                    else {
                        make_node.type = ASTNodeType::MAKE_SLICE;
                        // Parse size argument
                        auto *size_tok = iter_next(tokens_iter);
                        if (size_tok->type == TokenType::INTEGER_LITERAL) {
                            make_node.make_slice.size_is_literal = true;
                            make_node.make_slice.size_literal = size_tok->integer_literal.value;
                        }
                        else if (size_tok->type == TokenType::IDENTIFIER) {
                            make_node.make_slice.size_is_literal = false;
                            make_node.make_slice.size_identifier = size_tok->identifier.content;
                        }
                        else {
                            append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, size_tok)); return false;
                        }
                        // Optional allocator argument
                        auto *next_sep = iter_next(tokens_iter);
                        if (next_sep->type == TokenType::COMMA) {
                            auto *alloc_tok = iter_next(tokens_iter);
                            if (alloc_tok->type != TokenType::IDENTIFIER) {
                                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, alloc_tok)); return false;
                            }
                            make_node.make_slice.has_explicit_allocator = true;
                            make_node.make_slice.allocator_identifier = alloc_tok->identifier.content;
                            next_sep = iter_next(tokens_iter); // should be ')'
                        }
                        if (next_sep->type != TokenType::PARENTHESIS_CLOSE) {
                            append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, next_sep)); return false;
                        }
                    }
                    auto *nl = iter_next(tokens_iter);
                    if (nl->type != TokenType::NEWLINE && nl->type != TokenType::END) {
                        append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, nl)); return false;
                    }
                    auto *make_node_ptr = iter_append(nodes_block_iter, std::move(make_node));
                    (void)iter_append(nodes_block_iter, ASTNode {
                        .type = ASTNodeType::VARIABLE_DEFINITION,
                        .parent = parent_node,
                        .variable_definition = {
                            .name = next_token->identifier.content,
                            .expr = make_node_ptr,
                        },
                    });
                    break;
                }

                Iterator<Token> expr_tokens_iter;
                if (!slice_expression_tokens(tokens_iter, errors, &expr_tokens_iter)) {
                    return false;
                }

                auto expr_parse_result = parse_expression(
                    &expr_tokens_iter,
                    context,
                    nodes_block_iter,
                    proc_params_block,
                    proc_params_iter,
                    types_iter,
                    operands_iter,
                    array_elements_iter,
                    errors
                );
                if (!is_ok(&expr_parse_result)) {
                    append(errors, expr_parse_result.err);
                    return false;
                }

                auto *expr_node_ptr = iter_append(nodes_block_iter, std::move(expr_parse_result.ok));

                if (expr_node_ptr->type == ASTNodeType::BOOLEAN_LITERAL &&
                    context->var_type_count < 128)
                {
                    context->var_types[context->var_type_count++] = {
                        .name = next_token->identifier.content,
                        .is_bool = true,
                    };
                }

                if (expr_node_ptr->type == ASTNodeType::ARRAY_INIT &&
                    context->array_size_count < 64)
                {
                    context->array_sizes[context->array_size_count++] = {
                        .name = next_token->identifier.content,
                        .size = static_cast<int64_t>(expr_node_ptr->array_init.elements.length),
                    };
                }

                (void)iter_append(nodes_block_iter, ASTNode {
                    .type = ASTNodeType::VARIABLE_DEFINITION,
                    .parent = parent_node,
                    .variable_definition = {
                        .name = next_token->identifier.content,
                        .expr = expr_node_ptr,
                    },
                });

                tokens_iter->current_index += expr_tokens_iter.current_index;

                // Consume the newline or end token
                assert(
                    iter_current(tokens_iter)->type == TokenType::NEWLINE ||
                    iter_current(tokens_iter)->type == TokenType::END &&
                    "Expected newline or end token after variable definition");
                (void)iter_next(tokens_iter);
                break;
            }
            case TokenType::TYPE_SEPARATOR: {
                // name : Type  — zero-initialised typed variable declaration
                (void)iter_next(tokens_iter); // consume ':'
                auto *type_tok = iter_next(tokens_iter);
                if (type_tok->type != TokenType::IDENTIFIER) {
                    append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, type_tok));
                    return false;
                }
                auto *nl = iter_next(tokens_iter);
                if (nl->type != TokenType::NEWLINE && nl->type != TokenType::END) {
                    append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, nl));
                    return false;
                }
                (void)iter_append(nodes_block_iter, ASTNode {
                    .type = ASTNodeType::TYPED_VAR_DECL,
                    .parent = parent_node,
                    .typed_var_decl = {
                        .name = next_token->identifier.content,
                        .type_name = type_tok->identifier.content,
                    },
                });
                break;
            }
            case TokenType::DOT: {
                (void)iter_next(tokens_iter); // consume .
                auto *field_token = expect_token_or_append_error(tokens_iter, TokenType::IDENTIFIER, errors);
                if (field_token == nullptr) { return false; }
                if (expect_token_or_append_error(tokens_iter, TokenType::EQUALS, errors) == nullptr) { return false; }
                Iterator<Token> expr_tokens_iter;
                if (!slice_expression_tokens(tokens_iter, errors, &expr_tokens_iter)) {
                    return false;
                }
                auto expr_parse_result = parse_expression(
                    &expr_tokens_iter, context, nodes_block_iter,
                    proc_params_block, proc_params_iter, types_iter,
                    operands_iter, array_elements_iter, errors
                );
                if (!is_ok(&expr_parse_result)) {
                    append(errors, expr_parse_result.err);
                    return false;
                }
                auto *expr_node_ptr = iter_append(nodes_block_iter, std::move(expr_parse_result.ok));
                (void)iter_append(nodes_block_iter, ASTNode {
                    .type = ASTNodeType::MEMBER_ASSIGN,
                    .parent = parent_node,
                    .member_assign = {
                        .object_name = next_token->identifier.content,
                        .field_name = field_token->identifier.content,
                        .expr = expr_node_ptr,
                    },
                });
                tokens_iter->current_index += expr_tokens_iter.current_index;
                assert(
                    iter_current(tokens_iter)->type == TokenType::NEWLINE ||
                    iter_current(tokens_iter)->type == TokenType::END &&
                    "Expected newline or end token after member assignment");
                (void)iter_next(tokens_iter);
                break;
            }
            case TokenType::CONST_DEF: {
                (void)iter_next(tokens_iter); // consume ::

                // type_info_of(expr) without field access → TypeInfo variable
                if (tokens_iter->current_index < tokens_iter->elements.length &&
                    tokens_iter->elements.data[tokens_iter->current_index].type == TokenType::IDENTIFIER &&
                    tokens_iter->elements.data[tokens_iter->current_index].identifier.content == "type_info_of" &&
                    tokens_iter->current_index + 1 < tokens_iter->elements.length &&
                    tokens_iter->elements.data[tokens_iter->current_index + 1].type == TokenType::PARENTHESIS_OPEN)
                {
                    // Scan ahead to find the matching ) without consuming tokens yet
                    size_t scan_idx = tokens_iter->current_index + 2;
                    int depth = 1;
                    while (scan_idx < tokens_iter->elements.length) {
                        TokenType const tt = tokens_iter->elements.data[scan_idx].type;
                        if (tt == TokenType::PARENTHESIS_OPEN)  { depth++; }
                        if (tt == TokenType::PARENTHESIS_CLOSE) { depth--; if (depth == 0) { break; } }
                        scan_idx++;
                    }
                    // Only handle as TypeInfo store if nothing follows ) on the same line
                    bool const is_store = (depth == 0) && (
                        scan_idx + 1 >= tokens_iter->elements.length ||
                        tokens_iter->elements.data[scan_idx + 1].type == TokenType::NEWLINE ||
                        tokens_iter->elements.data[scan_idx + 1].type == TokenType::END
                    );
                    if (is_store) {
                        Token *type_info_of_tok = iter_next(tokens_iter); // consume type_info_of
                        (void)iter_next(tokens_iter); // consume (
                        size_t const inner_start = tokens_iter->current_index;
                        auto inner_iter = iter_slice_by_offset(
                            tokens_iter, inner_start, static_cast<int64_t>(scan_idx));
                        tokens_iter->current_index = scan_idx + 1; // skip past )
                        Str type_name = infer_bloom_type_from_tokens(
                            &inner_iter, context, nodes_block_iter);
                        if (type_name.length == 0) {
                            append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, type_info_of_tok));
                            return false;
                        }
                        ASTNode *store_node = iter_append(nodes_block_iter, ASTNode {
                            .type = ASTNodeType::TYPE_INFO_STORE,
                            .parent = parent_node,
                            .type_info_store = { .type_name = type_name },
                        });
                        (void)iter_append(nodes_block_iter, ASTNode {
                            .type = ASTNodeType::VARIABLE_DEFINITION,
                            .parent = parent_node,
                            .variable_definition = {
                                .name = next_token->identifier.content,
                                .expr = store_node,
                            },
                        });
                        (void)iter_next(tokens_iter); // consume NEWLINE/END
                        break;
                    }
                }

                if (iter_peek(tokens_iter)->type == TokenType::STRING_LITERAL ||
                    iter_peek(tokens_iter)->type == TokenType::INTERP_STRING_LITERAL)
                {
                    auto *str_token = iter_next(tokens_iter);
                    bool const is_interp = str_token->type == TokenType::INTERP_STRING_LITERAL;
                    auto *str_node = iter_append(nodes_block_iter, ASTNode {
                        .type = is_interp ? ASTNodeType::INTERPOLATED_STRING_LITERAL : ASTNodeType::STRING_LITERAL,
                        .parent = nullptr,
                        .string_literal = { .value = str_token->string_literal.content },
                    });
                    (void)iter_append(nodes_block_iter, ASTNode {
                        .type = ASTNodeType::VARIABLE_DEFINITION,
                        .parent = parent_node,
                        .variable_definition = {
                            .name = next_token->identifier.content,
                            .expr = str_node,
                        },
                    });
                    assert(
                        iter_current(tokens_iter)->type == TokenType::NEWLINE ||
                        iter_current(tokens_iter)->type == TokenType::END &&
                        "Expected newline or end token after string constant definition");
                    (void)iter_next(tokens_iter);
                }
                else if (iter_peek(tokens_iter)->type == TokenType::INTEGER_LITERAL) {
                    auto *value_token = iter_next(tokens_iter);
                    if (context->constant_count < 64) {
                        context->constants[context->constant_count++] = {
                            .name = next_token->identifier.content,
                            .value = value_token->integer_literal.value,
                        };
                    }
                    (void)iter_append(nodes_block_iter, ASTNode {
                        .type = ASTNodeType::CONSTANT_DEFINITION,
                        .parent = parent_node,
                        .constant_def = {
                            .name = next_token->identifier.content,
                            .value = value_token->integer_literal.value,
                        },
                    });
                    assert(
                        iter_current(tokens_iter)->type == TokenType::NEWLINE ||
                        iter_current(tokens_iter)->type == TokenType::END &&
                        "Expected newline or end token after constant definition");
                    (void)iter_next(tokens_iter);
                }
                else {
                    // Compile-time constant expression: evaluate at parse time
                    int64_t expr_end_idx = iter_get_index_at_if<Token>(
                        tokens_iter, [](Token const *t) {
                            return t->type == TokenType::NEWLINE || t->type == TokenType::END;
                        }
                    );
                    if (expr_end_idx == -1 ||
                        expr_end_idx == static_cast<int64_t>(tokens_iter->current_index))
                    {
                        append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, iter_current(tokens_iter)));
                        return false;
                    }
                    auto expr_toks = iter_slice_by_offset(
                        tokens_iter, tokens_iter->current_index, expr_end_idx);
                    int64_t const_value = 0;
                    if (!eval_const_additive(&expr_toks, context, nodes_block_iter, errors, &const_value)) {
                        return false;
                    }
                    tokens_iter->current_index = static_cast<size_t>(expr_end_idx);
                    assert(
                        iter_current(tokens_iter)->type == TokenType::NEWLINE ||
                        iter_current(tokens_iter)->type == TokenType::END &&
                        "Expected newline or end token after constant expression definition");
                    (void)iter_next(tokens_iter);
                    if (context->constant_count < 64) {
                        context->constants[context->constant_count++] = {
                            .name = next_token->identifier.content,
                            .value = const_value,
                        };
                    }
                    (void)iter_append(nodes_block_iter, ASTNode {
                        .type = ASTNodeType::CONSTANT_DEFINITION,
                        .parent = parent_node,
                        .constant_def = {
                            .name = next_token->identifier.content,
                            .value = const_value,
                        },
                    });
                }
                break;
            }
            case TokenType::BRACKET_OPEN: {
                // Look ahead past the matching ] to decide: assignment (=) or return expression
                // current_index points at [; scan from the token after [ with depth=1
                {
                    int scan_depth = 1;
                    bool after_bracket_is_assign = false;
                    for (size_t si = tokens_iter->current_index + 1;
                         si < tokens_iter->elements.length; si++) {
                        TokenType st = tokens_iter->elements.data[si].type;
                        if (st == TokenType::BRACKET_OPEN) { scan_depth++; }
                        else if (st == TokenType::BRACKET_CLOSE) {
                            scan_depth--;
                            if (scan_depth == 0) {
                                if (si + 1 < tokens_iter->elements.length &&
                                    tokens_iter->elements.data[si + 1].type == TokenType::EQUALS) {
                                    after_bracket_is_assign = true;
                                }
                                break;
                            }
                        }
                    }
                    if (!after_bracket_is_assign) {
                        // Return expression: back up to the identifier (one step before [)
                        tokens_iter->current_index -= 1;
                        Iterator<Token> expr_tokens_iter;
                        if (!slice_expression_tokens(tokens_iter, errors, &expr_tokens_iter)) {
                            return false;
                        }
                        auto expr_parse_result = parse_expression(
                            &expr_tokens_iter, context, nodes_block_iter,
                            proc_params_block, proc_params_iter, types_iter,
                            operands_iter, array_elements_iter, errors
                        );
                        if (!is_ok(&expr_parse_result)) {
                            append(errors, expr_parse_result.err);
                            return false;
                        }
                        auto *slice_node_ptr = iter_append(nodes_block_iter, std::move(expr_parse_result.ok));
                        slice_node_ptr->parent = parent_node;
                        tokens_iter->current_index += expr_tokens_iter.current_index;
                        assert(
                            iter_current(tokens_iter)->type == TokenType::NEWLINE ||
                            iter_current(tokens_iter)->type == TokenType::END &&
                            "Expected newline or end token after slice return expression");
                        (void)iter_next(tokens_iter);
                        break;
                    }
                }
                (void)iter_next(tokens_iter); // consume [
                auto *index_token = iter_next(tokens_iter);
                int64_t index = 0;
                bool consumed_range_op = false;
                if (index_token->type == TokenType::INTEGER_LITERAL) {
                    index = index_token->integer_literal.value;
                }
                else if (index_token->type == TokenType::RANGE ||
                         index_token->type == TokenType::RANGE_COUNTED ||
                         index_token->type == TokenType::RANGE_EXCLUSIVE ||
                         index_token->type == TokenType::RANGE_INCLUSIVE)
                {
                    // [..<end] shorthand for [0..<end]: start defaults to 0
                    // [..] alone means full-slice copy (slice[..] = src)
                    index = 0;
                    consumed_range_op = true;
                }
                else {
                    append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, index_token));
                    return false;
                }

                if (consumed_range_op || (tokens_iter->current_index < tokens_iter->elements.length && (
                    iter_peek(tokens_iter)->type == TokenType::RANGE ||
                    iter_peek(tokens_iter)->type == TokenType::RANGE_COUNTED ||
                    iter_peek(tokens_iter)->type == TokenType::RANGE_EXCLUSIVE ||
                    iter_peek(tokens_iter)->type == TokenType::RANGE_INCLUSIVE)))
                {
                    if (!consumed_range_op) {
                        (void)iter_next(tokens_iter); // consume range operator
                    }
                    // Consume end expression tokens (scan forward to ])
                    while (tokens_iter->current_index < tokens_iter->elements.length &&
                           iter_peek(tokens_iter)->type != TokenType::BRACKET_CLOSE)
                    {
                        (void)iter_next(tokens_iter);
                    }
                    if (expect_token_or_append_error(tokens_iter, TokenType::BRACKET_CLOSE, errors) == nullptr) {
                        return false;
                    }
                    if (expect_token_or_append_error(tokens_iter, TokenType::EQUALS, errors) == nullptr) {
                        return false;
                    }
                    Iterator<Token> expr_tokens_iter;
                    if (!slice_expression_tokens(tokens_iter, errors, &expr_tokens_iter)) {
                        return false;
                    }
                    auto expr_parse_result = parse_expression(
                        &expr_tokens_iter, context, nodes_block_iter,
                        proc_params_block, proc_params_iter, types_iter,
                        operands_iter, array_elements_iter, errors
                    );
                    if (!is_ok(&expr_parse_result)) {
                        append(errors, expr_parse_result.err);
                        return false;
                    }
                    ASTNode *value_expr = iter_append(nodes_block_iter, std::move(expr_parse_result.ok));
                    // is_full_slice: [..] with RANGE and no end expression before ']'
                    bool const is_full_slice_assign = (
                        consumed_range_op &&
                        index_token->type == TokenType::RANGE
                    );
                    (void)iter_append(nodes_block_iter, ASTNode {
                        .type = ASTNodeType::ARRAY_RANGE_ASSIGN,
                        .parent = parent_node,
                        .array_range_assign = {
                            .variable_name = next_token->identifier.content,
                            .start_index = index,
                            .is_full_slice = is_full_slice_assign,
                            .value_expr = value_expr,
                        },
                    });
                    tokens_iter->current_index += expr_tokens_iter.current_index;
                    assert(
                        iter_current(tokens_iter)->type == TokenType::NEWLINE ||
                        iter_current(tokens_iter)->type == TokenType::END &&
                        "Expected newline or end token after array range assign");
                    (void)iter_next(tokens_iter);
                    break;
                }

                auto *bracket_close = iter_next(tokens_iter);
                if (bracket_close->type != TokenType::BRACKET_CLOSE) {
                    append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, bracket_close));
                    return false;
                }
                if (expect_token_or_append_error(tokens_iter, TokenType::EQUALS, errors) == nullptr) {
                    return false;
                }
                for (size_t ai = 0; ai < context->array_size_count; ai++) {
                    Str const *aname = &context->array_sizes[ai].name;
                    if (aname->length == next_token->identifier.content.length &&
                        strncmp(aname->data, next_token->identifier.content.data, aname->length) == 0)
                    {
                        int64_t const known_size = context->array_sizes[ai].size;
                        if (index < 0 || index >= known_size) {
                            ParseError err = {};
                            err.code = ParseErrorCode::ARRAY_INDEX_OUT_OF_BOUNDS;
                            err.position = index_token->position;
                            err.src_code_line = __LINE__;
                            err.token_type = index_token->type;
                            err.size_token_width = 1;
                            err.array_name = next_token->identifier.content;
                            err.array_index = index;
                            err.known_array_size = known_size;
                            append(errors, err);
                            return false;
                        }
                        break;
                    }
                }
                Iterator<Token> expr_tokens_iter;
                if (!slice_expression_tokens(tokens_iter, errors, &expr_tokens_iter)) {
                    return false;
                }
                auto expr_parse_result = parse_expression(
                    &expr_tokens_iter, context, nodes_block_iter,
                    proc_params_block, proc_params_iter, types_iter,
                    operands_iter, array_elements_iter, errors
                );
                if (!is_ok(&expr_parse_result)) {
                    append(errors, expr_parse_result.err);
                    return false;
                }
                auto *expr_node_ptr = iter_append(nodes_block_iter, std::move(expr_parse_result.ok));
                (void)iter_append(nodes_block_iter, ASTNode {
                    .type = ASTNodeType::ARRAY_ELEMENT_ASSIGN,
                    .parent = parent_node,
                    .array_element_assign = {
                        .variable_name = next_token->identifier.content,
                        .index = index,
                        .expr = expr_node_ptr,
                    },
                });
                tokens_iter->current_index += expr_tokens_iter.current_index;
                assert(
                    iter_current(tokens_iter)->type == TokenType::NEWLINE ||
                    iter_current(tokens_iter)->type == TokenType::END &&
                    "Expected newline or end token after array element assignment");
                (void)iter_next(tokens_iter);
                break;
            }
            case TokenType::NEWLINE:
            case TokenType::END: {
                // Bare identifier as statement — typically the return value of a procedure
                (void)iter_append(nodes_block_iter, ASTNode {
                    .type = ASTNodeType::IDENTIFIER,
                    .parent = parent_node,
                    .identifier = next_token->identifier.content,
                });
                (void)iter_next(tokens_iter); // consume NEWLINE/END
                break;
            }
            case TokenType::BRACE_OPEN: {
                // Standalone struct init expression (e.g. last expression as return value)
                // Back up so the identifier is re-read by parse_expression
                tokens_iter->current_index -= 1;
                Iterator<Token> expr_tokens_iter;
                if (!slice_expression_tokens(tokens_iter, errors, &expr_tokens_iter)) {
                    return false;
                }
                auto expr_parse_result = parse_expression(
                    &expr_tokens_iter, context, nodes_block_iter,
                    proc_params_block, proc_params_iter, types_iter,
                    operands_iter, array_elements_iter, errors
                );
                if (!is_ok(&expr_parse_result)) {
                    append(errors, expr_parse_result.err);
                    return false;
                }
                auto *struct_init_ptr = iter_append(nodes_block_iter, std::move(expr_parse_result.ok));
                struct_init_ptr->parent = parent_node;
                tokens_iter->current_index += expr_tokens_iter.current_index;
                assert(
                    iter_current(tokens_iter)->type == TokenType::NEWLINE ||
                    iter_current(tokens_iter)->type == TokenType::END &&
                    "Expected newline or end token after struct init statement");
                (void)iter_next(tokens_iter);
                break;
            }
        }
        break;
    }
    case TokenType::KEYWORD_FOR: {
        if (tokens_iter->current_index < tokens_iter->elements.length &&
            iter_peek(tokens_iter)->type == TokenType::KEYWORD_IF)
        {
            (void)iter_next(tokens_iter); // consume 'if'
            auto *lhs_token = expect_token_or_append_error(tokens_iter, TokenType::IDENTIFIER, errors);
            if (lhs_token == nullptr) { return false; }
            if (expect_token_or_append_error(tokens_iter, TokenType::LESS_THAN, errors) == nullptr) { return false; }
            auto *rhs_token = iter_next(tokens_iter);
            ConditionOperand cond_left = {
                .is_identifier = true,
                .identifier = lhs_token->identifier.content,
            };
            ConditionOperand cond_right;
            switch (rhs_token->type) {
                case TokenType::IDENTIFIER:
                    cond_right = {
                        .is_identifier = true,
                        .identifier = rhs_token->identifier.content,
                    };
                    break;
                case TokenType::INTEGER_LITERAL:
                    cond_right = {
                        .is_identifier = false,
                        .integer_literal = IntegerLiteralASTNode { .value = rhs_token->integer_literal.value },
                    };
                    break;
                default:
                    append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, rhs_token));
                    return false;
            }
            if (!expect_arrow_newline(tokens_iter, errors)) { return false; }

            auto *for_cond_node = iter_append(nodes_block_iter, ASTNode {
                .type = ASTNodeType::FOR_COND_LOOP,
                .parent = parent_node,
                .for_cond_loop = {
                    .condition_left = cond_left,
                    .condition_right = cond_right,
                    .body = Array<ASTNode>(
                        nodes_block_iter->elements.data + nodes_block_iter->current_index + 1,
                        0
                    ),
                },
            });

            if (!parse_indented_body(tokens_iter, context, nodes_block_iter, for_cond_node,
                                     proc_params_block, proc_params_iter, types_iter,
                                     operands_iter, array_elements_iter, errors,
                                     current_indent_level, &for_cond_node->for_cond_loop.body)) {
                return false;
            }
        }
        else if (tokens_iter->current_index < tokens_iter->elements.length &&
            iter_peek(tokens_iter)->type == TokenType::IDENTIFIER)
        {
            auto *elem_token = iter_next(tokens_iter);
            auto *op_token = iter_next(tokens_iter);

            Str index_name = {};
            if (op_token->type == TokenType::COMMA) {
                auto *index_token = expect_token_or_append_error(tokens_iter, TokenType::IDENTIFIER, errors);
                if (index_token == nullptr) { return false; }
                index_name = index_token->identifier.content;
                op_token = iter_next(tokens_iter);
            }

            if (op_token->type == TokenType::KEYWORD_IN) {
                TokenType const start_tok_type = iter_peek(tokens_iter)->type;
                // An IDENTIFIER is a range start only when immediately followed by a range operator.
                // Otherwise it's a collection name for a for-in loop.
                bool const next_is_range_op = tokens_iter->current_index + 1 < tokens_iter->elements.length &&
                    (tokens_iter->elements.data[tokens_iter->current_index + 1].type == TokenType::RANGE_EXCLUSIVE ||
                     tokens_iter->elements.data[tokens_iter->current_index + 1].type == TokenType::RANGE_INCLUSIVE ||
                     tokens_iter->elements.data[tokens_iter->current_index + 1].type == TokenType::RANGE_COUNTED);
                if (start_tok_type == TokenType::INTEGER_LITERAL ||
                    (start_tok_type == TokenType::IDENTIFIER && next_is_range_op))
                {
                    auto *start_token = iter_next(tokens_iter);
                    int64_t range_start = 0;
                    Str range_start_identifier = {};
                    if (start_token->type == TokenType::INTEGER_LITERAL) {
                        range_start = start_token->integer_literal.value;
                    }
                    else {
                        range_start_identifier = start_token->identifier.content;
                    }

                    auto *range_op_token = iter_next(tokens_iter);
                    int64_t range_end = 0;
                    Str range_end_identifier = {};
                    bool range_end_inclusive = false;
                    Str range_count_identifier = {};
                    Str range_end_proc_call_name = {};
                    Str range_end_proc_call_arg = {};
                    int64_t range_end_offset = 0;
                    if (range_op_token->type == TokenType::RANGE_COUNTED) {
                        auto *count_token = iter_next(tokens_iter);
                        if (count_token->type == TokenType::INTEGER_LITERAL) {
                            range_end = range_start + count_token->integer_literal.value + 1;
                        }
                        else if (count_token->type == TokenType::IDENTIFIER) {
                            range_count_identifier = count_token->identifier.content;
                        }
                        else {
                            append(errors, ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = count_token->position,
                                .src_code_line = __LINE__,
                                .token_type = count_token->type,
                            });
                            return false;
                        }
                    }
                    else if (range_op_token->type == TokenType::RANGE_EXCLUSIVE ||
                             range_op_token->type == TokenType::RANGE_INCLUSIVE) {
                        bool const is_inclusive = range_op_token->type == TokenType::RANGE_INCLUSIVE;
                        auto *end_token = iter_next(tokens_iter);
                        if (end_token->type == TokenType::INTEGER_LITERAL) {
                            range_end = end_token->integer_literal.value;
                            // Check for arithmetic: 3 + 7
                            if (tokens_iter->current_index < tokens_iter->elements.length &&
                                iter_peek(tokens_iter)->type == TokenType::ADD)
                            {
                                (void)iter_next(tokens_iter); // consume '+'
                                auto *rhs = iter_next(tokens_iter);
                                if (rhs->type != TokenType::INTEGER_LITERAL) {
                                    append(errors, ParseError {
                                        .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                        .position = rhs->position,
                                        .src_code_line = __LINE__,
                                        .token_type = rhs->type,
                                    });
                                    return false;
                                }
                                range_end += rhs->integer_literal.value;
                            }
                            if (is_inclusive) { range_end += 1; }
                        }
                        else if (end_token->type == TokenType::IDENTIFIER) {
                            // Check if it's a proc call: name(arg)
                            if (tokens_iter->current_index < tokens_iter->elements.length &&
                                iter_peek(tokens_iter)->type == TokenType::PARENTHESIS_OPEN)
                            {
                                (void)iter_next(tokens_iter); // consume '('
                                auto *arg_tok = iter_next(tokens_iter);
                                if (arg_tok->type != TokenType::IDENTIFIER) {
                                    append(errors, ParseError {
                                        .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                        .position = arg_tok->position,
                                        .src_code_line = __LINE__,
                                        .token_type = arg_tok->type,
                                    });
                                    return false;
                                }
                                range_end_proc_call_name = end_token->identifier.content;
                                range_end_proc_call_arg  = arg_tok->identifier.content;
                                auto *pclose = iter_next(tokens_iter);
                                if (pclose->type != TokenType::PARENTHESIS_CLOSE) {
                                    append(errors, ParseError {
                                        .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                        .position = pclose->position,
                                        .src_code_line = __LINE__,
                                        .token_type = pclose->type,
                                    });
                                    return false;
                                }
                            }
                            else {
                                range_end_identifier = end_token->identifier.content;
                                if (is_inclusive) { range_end_inclusive = true; }
                            }
                            // Check for arithmetic offset: expr + N
                            if (tokens_iter->current_index < tokens_iter->elements.length &&
                                iter_peek(tokens_iter)->type == TokenType::ADD)
                            {
                                (void)iter_next(tokens_iter); // consume '+'
                                auto *rhs = iter_next(tokens_iter);
                                if (rhs->type != TokenType::INTEGER_LITERAL) {
                                    append(errors, ParseError {
                                        .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                        .position = rhs->position,
                                        .src_code_line = __LINE__,
                                        .token_type = rhs->type,
                                    });
                                    return false;
                                }
                                range_end_offset = rhs->integer_literal.value;
                            }
                        }
                        else {
                            append(errors, ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = end_token->position,
                                .src_code_line = __LINE__,
                                .token_type = end_token->type,
                            });
                            return false;
                        }
                    }
                    else {
                        append(errors, ParseError {
                            .code = ParseErrorCode::UNEXPECTED_TOKEN,
                            .position = range_op_token->position,
                            .src_code_line = __LINE__,
                            .token_type = range_op_token->type,
                        });
                        return false;
                    }

                    if (!expect_arrow_newline(tokens_iter, errors)) { return false; }

                    auto *for_range_node = iter_append(nodes_block_iter, ASTNode {
                        .type = ASTNodeType::FOR_RANGE_LOOP,
                        .parent = parent_node,
                        .for_range_loop = {
                            .element_name = elem_token->identifier.content,
                            .range_start = range_start,
                            .range_end = range_end,
                            .range_start_identifier = range_start_identifier,
                            .range_end_identifier = range_end_identifier,
                            .range_end_inclusive = range_end_inclusive,
                            .range_count_identifier = range_count_identifier,
                            .range_end_proc_call_name = range_end_proc_call_name,
                            .range_end_proc_call_arg  = range_end_proc_call_arg,
                            .range_end_offset = range_end_offset,
                            .body = Array<ASTNode>(
                                nodes_block_iter->elements.data + nodes_block_iter->current_index + 1,
                                0
                            ),
                        },
                    });

                    if (!parse_indented_body(tokens_iter, context, nodes_block_iter, for_range_node,
                                             proc_params_block, proc_params_iter, types_iter,
                                             operands_iter, array_elements_iter, errors,
                                             current_indent_level, &for_range_node->for_range_loop.body)) {
                        return false;
                    }
                }
                else if (iter_peek(tokens_iter)->type == TokenType::BRACKET_OPEN) {
                    (void)iter_next(tokens_iter); // consume [
                    if (expect_token_or_append_error(tokens_iter, TokenType::BRACKET_CLOSE, errors) == nullptr) { return false; }
                    auto *elem_type_tok = expect_token_or_append_error(tokens_iter, TokenType::IDENTIFIER, errors);
                    if (elem_type_tok == nullptr) { return false; }
                    Str const inline_elem_type = elem_type_tok->identifier.content;
                    if (expect_token_or_append_error(tokens_iter, TokenType::BRACE_OPEN, errors) == nullptr) { return false; }
                    size_t const elements_begin = array_elements_iter->current_index;
                    bool in_range_mode = false;
                    while (true) {
                        auto *tok = iter_next(tokens_iter);
                        if (tok->type == TokenType::BRACE_CLOSE) { break; }
                        if (tok->type == TokenType::COMMA ||
                            tok->type == TokenType::NEWLINE ||
                            tok->type == TokenType::INDENT) { continue; }
                        if (tok->type != TokenType::INTEGER_LITERAL) {
                            append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, tok));
                            return false;
                        }
                        if (tokens_iter->current_index < tokens_iter->elements.length &&
                            iter_peek(tokens_iter)->type == TokenType::RANGE_EXCLUSIVE)
                        {
                            in_range_mode = true;
                            int64_t range_start = tok->integer_literal.value;
                            size_t current_count = array_elements_iter->current_index - elements_begin;
                            if (range_start != static_cast<int64_t>(current_count)) {
                                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, tok));
                                return false;
                            }
                            (void)iter_next(tokens_iter); // consume ..<
                            auto *end_tok = iter_next(tokens_iter);
                            if (end_tok->type != TokenType::INTEGER_LITERAL) {
                                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, end_tok));
                                return false;
                            }
                            int64_t range_end = end_tok->integer_literal.value;
                            if (range_end <= range_start) {
                                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, end_tok));
                                return false;
                            }
                            auto *eq_tok = iter_next(tokens_iter);
                            if (eq_tok->type != TokenType::EQUALS) {
                                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, eq_tok));
                                return false;
                            }
                            auto *val_tok = iter_next(tokens_iter);
                            if (val_tok->type != TokenType::INTEGER_LITERAL) {
                                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, val_tok));
                                return false;
                            }
                            int64_t range_value = val_tok->integer_literal.value;
                            for (int64_t ri = range_start; ri < range_end; ri++) {
                                int64_t v = range_value;
                                (void)iter_append(array_elements_iter, std::move(v));
                            }
                        }
                        else {
                            if (in_range_mode) {
                                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, tok));
                                return false;
                            }
                            int64_t val = tok->integer_literal.value;
                            (void)iter_append(array_elements_iter, std::move(val));
                        }
                    }
                    size_t const inline_count = array_elements_iter->current_index - elements_begin;
                    if (!expect_arrow_newline(tokens_iter, errors)) { return false; }

                    auto *for_in_node = iter_append(nodes_block_iter, ASTNode {
                        .type = ASTNodeType::FOR_IN_LOOP,
                        .parent = parent_node,
                        .for_in_loop = {
                            .element_name = elem_token->identifier.content,
                            .index_name = index_name,
                            .collection_name = {},
                            .inline_element_type = inline_elem_type,
                            .inline_elements = Array<int64_t>(
                                array_elements_iter->elements.data + elements_begin,
                                inline_count
                            ),
                            .body = Array<ASTNode>(
                                nodes_block_iter->elements.data + nodes_block_iter->current_index + 1,
                                0
                            ),
                        },
                    });

                    if (!parse_indented_body(tokens_iter, context, nodes_block_iter, for_in_node,
                                             proc_params_block, proc_params_iter, types_iter,
                                             operands_iter, array_elements_iter, errors,
                                             current_indent_level, &for_in_node->for_in_loop.body)) {
                        return false;
                    }
                }
                else if (tokens_iter->current_index < tokens_iter->elements.length &&
                         iter_peek(tokens_iter)->type == TokenType::IDENTIFIER &&
                         iter_peek(tokens_iter)->identifier.content == "type_info_of")
                {
                    // for elem in type_info_of(var).members
                    (void)iter_next(tokens_iter); // consume type_info_of
                    if (expect_token_or_append_error(tokens_iter, TokenType::PARENTHESIS_OPEN, errors) == nullptr) { return false; }
                    size_t const inner_start = tokens_iter->current_index;
                    int tio_depth = 1;
                    size_t tio_end = inner_start;
                    while (tio_end < tokens_iter->elements.length) {
                        TokenType const tt = tokens_iter->elements.data[tio_end].type;
                        if (tt == TokenType::PARENTHESIS_OPEN)  { tio_depth++; }
                        if (tt == TokenType::PARENTHESIS_CLOSE) { tio_depth--; if (tio_depth == 0) { break; } }
                        tio_end++;
                    }
                    auto inner_iter = iter_slice_by_offset(tokens_iter, inner_start, static_cast<int64_t>(tio_end));
                    tokens_iter->current_index = tio_end + 1; // skip past )
                    Str enum_type_name = infer_bloom_type_from_tokens(&inner_iter, context, nodes_block_iter);
                    if (expect_token_or_append_error(tokens_iter, TokenType::DOT, errors) == nullptr) { return false; }
                    auto *members_tok = expect_token_or_append_error(tokens_iter, TokenType::IDENTIFIER, errors);
                    if (members_tok == nullptr) { return false; }
                    if (!expect_arrow_newline(tokens_iter, errors)) { return false; }

                    auto *for_in_node = iter_append(nodes_block_iter, ASTNode {
                        .type = ASTNodeType::FOR_IN_LOOP,
                        .parent = parent_node,
                        .for_in_loop = {
                            .element_name = elem_token->identifier.content,
                            .index_name = index_name,
                            .collection_name = {},
                            .enum_members_type_name = enum_type_name,
                            .body = Array<ASTNode>(
                                nodes_block_iter->elements.data + nodes_block_iter->current_index + 1,
                                0
                            ),
                        },
                    });

                    if (!parse_indented_body(tokens_iter, context, nodes_block_iter, for_in_node,
                                             proc_params_block, proc_params_iter, types_iter,
                                             operands_iter, array_elements_iter, errors,
                                             current_indent_level, &for_in_node->for_in_loop.body)) {
                        return false;
                    }
                }
                else {
                    auto *coll_token = expect_token_or_append_error(tokens_iter, TokenType::IDENTIFIER, errors);
                    if (coll_token == nullptr) { return false; }
                    if (!expect_arrow_newline(tokens_iter, errors)) { return false; }

                    auto *for_in_node = iter_append(nodes_block_iter, ASTNode {
                        .type = ASTNodeType::FOR_IN_LOOP,
                        .parent = parent_node,
                        .for_in_loop = {
                            .element_name = elem_token->identifier.content,
                            .index_name = index_name,
                            .collection_name = coll_token->identifier.content,
                            .body = Array<ASTNode>(
                                nodes_block_iter->elements.data + nodes_block_iter->current_index + 1,
                                0
                            ),
                        },
                    });

                    if (!parse_indented_body(tokens_iter, context, nodes_block_iter, for_in_node,
                                             proc_params_block, proc_params_iter, types_iter,
                                             operands_iter, array_elements_iter, errors,
                                             current_indent_level, &for_in_node->for_in_loop.body)) {
                        return false;
                    }
                }
            }
            else {
                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, op_token));
                return false;
            }
        }
        else {
            if (!expect_arrow_newline(tokens_iter, errors)) { return false; }

            auto *for_loop_node = iter_append(nodes_block_iter, ASTNode {
                .type = ASTNodeType::FOR_LOOP,
                .parent = parent_node,
                .for_loop = {
                    .body = Array<ASTNode>(
                        nodes_block_iter->elements.data + nodes_block_iter->current_index + 1,
                        0
                    ),
                    .for_pos = next_token->position,
                },
            });

            if (!parse_indented_body(tokens_iter, context, nodes_block_iter, for_loop_node,
                                     proc_params_block, proc_params_iter, types_iter,
                                     operands_iter, array_elements_iter, errors,
                                     current_indent_level, &for_loop_node->for_loop.body)) {
                return false;
            }
        }
        break;
    }
    case TokenType::KEYWORD_RETURN: {
        auto *value_tok = iter_next(tokens_iter);
        ASTNode *value_ptr = nullptr;
        if (value_tok->type == TokenType::INTEGER_LITERAL) {
            ASTNode value_node = ASTNode {
                .type = ASTNodeType::INTEGER_LITERAL,
                .parent = nullptr,
                .integer_literal = { .value = { .value = value_tok->integer_literal.value } },
            };
            value_ptr = iter_append(nodes_block_iter, std::move(value_node));
            auto *nl = iter_next(tokens_iter);
            if (nl->type != TokenType::NEWLINE && nl->type != TokenType::END) {
                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, nl));
                return false;
            }
        }
        else if (value_tok->type == TokenType::KEYWORD_TRUE ||
                 value_tok->type == TokenType::KEYWORD_FALSE)
        {
            ASTNode value_node = ASTNode {
                .type = ASTNodeType::BOOLEAN_LITERAL,
                .parent = nullptr,
                .boolean_literal = { .value = value_tok->type == TokenType::KEYWORD_TRUE },
            };
            value_ptr = iter_append(nodes_block_iter, std::move(value_node));
            auto *nl = iter_next(tokens_iter);
            if (nl->type != TokenType::NEWLINE && nl->type != TokenType::END) {
                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, nl));
                return false;
            }
        }
        else if (value_tok->type == TokenType::IDENTIFIER &&
                 tokens_iter->current_index < tokens_iter->elements.length &&
                 iter_peek(tokens_iter)->type == TokenType::BRACE_OPEN)
        {
            tokens_iter->current_index -= 1; // back up to re-read the identifier
            Iterator<Token> expr_tokens_iter;
            if (!slice_expression_tokens(tokens_iter, errors, &expr_tokens_iter)) {
                return false;
            }
            auto expr_result = parse_expression(
                &expr_tokens_iter, context, nodes_block_iter,
                proc_params_block, proc_params_iter, types_iter,
                operands_iter, array_elements_iter, errors
            );
            if (!is_ok(&expr_result)) {
                append(errors, expr_result.err);
                return false;
            }
            value_ptr = iter_append(nodes_block_iter, std::move(expr_result.ok));
            value_ptr->parent = nullptr;
            tokens_iter->current_index += expr_tokens_iter.current_index;
            auto *nl = iter_next(tokens_iter);
            if (nl->type != TokenType::NEWLINE && nl->type != TokenType::END) {
                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, nl));
                return false;
            }
        }
        else if (value_tok->type == TokenType::IDENTIFIER)
        {
            value_ptr = iter_append(nodes_block_iter, ASTNode {
                .type = ASTNodeType::IDENTIFIER,
                .parent = nullptr,
                .identifier = value_tok->identifier.content,
            });
            auto *nl = iter_next(tokens_iter);
            if (nl->type != TokenType::NEWLINE && nl->type != TokenType::END) {
                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, nl));
                return false;
            }
        }
        else if (value_tok->type != TokenType::NEWLINE && value_tok->type != TokenType::END) {
            append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, value_tok));
            return false;
        }
        (void)iter_append(nodes_block_iter, ASTNode {
            .type = ASTNodeType::RETURN,
            .parent = parent_node,
            .return_value = value_ptr,
        });
        break;
    }
    case TokenType::KEYWORD_BREAK:
        (void)iter_append(nodes_block_iter, ASTNode {
            .type = ASTNodeType::BREAK,
            .parent = parent_node,
        });
        assert(
            iter_current(tokens_iter)->type == TokenType::NEWLINE ||
            iter_current(tokens_iter)->type == TokenType::END &&
            "Expected newline or end token after break statement");
        (void)iter_next(tokens_iter);
        break;
    case TokenType::KEYWORD_DEFER: {
        auto *proc_name_token = expect_token_or_append_error(tokens_iter, TokenType::IDENTIFIER, errors);
        if (proc_name_token == nullptr) { return false; }

        int paren_depth = 0;
        int64_t proc_call_end_token_index = iter_get_index_at_if<Token>(
            tokens_iter, [&paren_depth](auto *token) {
                if (token->type == TokenType::PARENTHESIS_OPEN) {
                    paren_depth++;
                    return false;
                }
                if (token->type == TokenType::PARENTHESIS_CLOSE) {
                    if (paren_depth == 1) { return true; }
                    paren_depth--;
                }
                return false;
            }
        );

        auto arg_tokens_iter = iter_slice_by_offset(
            tokens_iter,
            tokens_iter->current_index + 1,
            proc_call_end_token_index
        );

        ASTNode temp_node = {
            .type = ASTNodeType::PROC_CALL,
            .parent = parent_node,
            .proc_call = { .caller_identifier = proc_name_token->identifier.content },
        };
        if (!parse_proc_call_arguments(&arg_tokens_iter, &temp_node, nodes_block_iter, context, operands_iter, errors, proc_name_token->position)) {
            if (errors->length == 0) {
                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, iter_current(tokens_iter)));
            }
            return false;
        }

        (void)iter_append(nodes_block_iter, ASTNode {
            .type = ASTNodeType::DEFER,
            .parent = parent_node,
            .defer_stmt = {
                .arguments = temp_node.proc_call.arguments,
                .caller_identifier = temp_node.proc_call.caller_identifier,
            },
        });

        tokens_iter->current_index = proc_call_end_token_index + 1;
        assert(
            iter_current(tokens_iter)->type == TokenType::NEWLINE ||
            iter_current(tokens_iter)->type == TokenType::END &&
            "Expected newline or end token after defer statement");
        (void)iter_next(tokens_iter);
        break;
    }
    case TokenType::KEYWORD_IF: {
        auto parse_cond_operand = [&](Token *token, ConditionOperand *out) -> bool {
            switch (token->type) {
                case TokenType::IDENTIFIER:
                    if (tokens_iter->current_index < tokens_iter->elements.length &&
                        iter_peek(tokens_iter)->type == TokenType::PARENTHESIS_OPEN)
                    {
                        (void)iter_next(tokens_iter); // consume (
                        out->is_proc_call = true;
                        out->proc_call.caller = token->identifier.content;
                        auto *arg_tok = iter_next(tokens_iter);
                        if (arg_tok->type != TokenType::IDENTIFIER) {
                            append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, arg_tok));
                            return false;
                        }
                        out->proc_call.arg_identifier = arg_tok->identifier.content;
                        auto *close_tok = iter_next(tokens_iter);
                        if (close_tok->type != TokenType::PARENTHESIS_CLOSE) {
                            append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, close_tok));
                            return false;
                        }
                    }
                    else if (tokens_iter->current_index < tokens_iter->elements.length &&
                             iter_peek(tokens_iter)->type == TokenType::DOT)
                    {
                        (void)iter_next(tokens_iter); // consume .
                        auto *field_tok = iter_next(tokens_iter);
                        if (field_tok->type != TokenType::IDENTIFIER) {
                            append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, field_tok));
                            return false;
                        }
                        out->is_member_access = true;
                        out->member_access.object_name = token->identifier.content;
                        out->member_access.field_name = field_tok->identifier.content;
                    }
                    else if (tokens_iter->current_index < tokens_iter->elements.length &&
                             iter_peek(tokens_iter)->type == TokenType::BRACKET_OPEN)
                    {
                        (void)iter_next(tokens_iter); // consume [
                        out->is_array_access = true;
                        out->array_access.variable_name = token->identifier.content;
                        auto *idx_tok = iter_next(tokens_iter);
                        if (idx_tok->type != TokenType::INTEGER_LITERAL) {
                            append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, idx_tok));
                            return false;
                        }
                        out->array_access.index = idx_tok->integer_literal.value;
                        auto *close_tok = iter_next(tokens_iter);
                        if (close_tok->type != TokenType::BRACKET_CLOSE) {
                            append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, close_tok));
                            return false;
                        }
                    }
                    else {
                        out->is_identifier = true;
                        out->identifier = token->identifier.content;
                    }
                    return true;
                case TokenType::INTEGER_LITERAL:
                    out->is_identifier = false;
                    out->integer_literal = IntegerLiteralASTNode { .value = token->integer_literal.value };
                    return true;
                case TokenType::STRING_LITERAL:
                    out->is_string_literal = true;
                    out->string_literal = token->string_literal.content;
                    return true;
                case TokenType::KEYWORD_NIL:
                    out->is_nil = true;
                    return true;
                default:
                    append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, token));
                    return false;
            }
        };

        ConditionOperand cond_left = {};
        if (!parse_cond_operand(iter_next(tokens_iter), &cond_left)) {
            return false;
        }
        auto *cond_op_tok = iter_next(tokens_iter);
        Str comparison_op = {};
        if (cond_op_tok->type == TokenType::EQUAL_EQUAL) {
            // leave comparison_op empty; transpiler defaults to ==
        }
        else if (cond_op_tok->type == TokenType::LESS_THAN) {
            comparison_op = cstr_to_str("<");
        }
        else if (cond_op_tok->type == TokenType::NOT_EQUAL) {
            comparison_op = cstr_to_str("!=");
        }
        else {
            append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, cond_op_tok));
            return false;
        }
        ConditionOperand cond_right = {};
        if (tokens_iter->current_index < tokens_iter->elements.length &&
            iter_peek(tokens_iter)->type == TokenType::DOT)
        {
            (void)iter_next(tokens_iter); // consume DOT
            auto *member_tok = iter_next(tokens_iter);
            if (member_tok->type != TokenType::IDENTIFIER) {
                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, member_tok));
                return false;
            }
            Str enum_type = {};
            if (cond_left.is_identifier) {
                for (size_t i = 0; i < nodes_block_iter->current_index; i++) {
                    auto const *node = &nodes_block_iter->elements.data[i];
                    if (node->type == ASTNodeType::VARIABLE_DEFINITION &&
                        str_equal(node->variable_definition.name, cond_left.identifier))
                    {
                        ASTNode const *expr = node->variable_definition.expr;
                        if (expr->type == ASTNodeType::MEMBER_ACCESS) {
                            for (size_t j = 0; j < nodes_block_iter->current_index; j++) {
                                auto const *n = &nodes_block_iter->elements.data[j];
                                if (n->type == ASTNodeType::ENUM_DEF &&
                                    str_equal(n->enum_def.name, expr->member_access.object_name))
                                {
                                    enum_type = n->enum_def.name;
                                    break;
                                }
                            }
                        }
                        break;
                    }
                }
                if (enum_type.length == 0) {
                    ASTNode const *p = parent_node;
                    while (p != nullptr) {
                        if (p->type == ASTNodeType::PROC_DEF) {
                            for (size_t i = 0; i < p->proc_def.parameters.length; i++) {
                                auto const *param = &p->proc_def.parameters.data[i];
                                if (str_equal(param->name, cond_left.identifier)) {
                                    for (size_t j = 0; j < nodes_block_iter->current_index; j++) {
                                        auto const *n = &nodes_block_iter->elements.data[j];
                                        if (n->type == ASTNodeType::ENUM_DEF &&
                                            str_equal(n->enum_def.name, param->type_name))
                                        {
                                            enum_type = n->enum_def.name;
                                            break;
                                        }
                                    }
                                    break;
                                }
                            }
                            break;
                        }
                        p = p->parent;
                    }
                }
            }
            cond_right.is_enum_shorthand = true;
            cond_right.enum_shorthand.enum_type_name = enum_type;
            cond_right.enum_shorthand.member_name = member_tok->identifier.content;
        }
        else {
            if (!parse_cond_operand(iter_next(tokens_iter), &cond_right)) {
                return false;
            }
        }

        // Parse 'and' chains: collect additional conditions before the arrow
        ConditionOperand and_lefts[4] = {};
        ConditionOperand and_rights[4] = {};
        Str and_ops[4] = {};
        size_t and_count = 0;
        while (and_count < 4 &&
               tokens_iter->current_index < tokens_iter->elements.length &&
               iter_peek(tokens_iter)->type == TokenType::KEYWORD_AND)
        {
            (void)iter_next(tokens_iter); // consume 'and'
            ConditionOperand and_left = {};
            if (!parse_cond_operand(iter_next(tokens_iter), &and_left)) {
                return false;
            }
            auto *and_op_tok = iter_next(tokens_iter);
            Str and_op = {};
            if (and_op_tok->type == TokenType::EQUAL_EQUAL) {
                // leave empty; transpiler defaults to ==
            }
            else if (and_op_tok->type == TokenType::LESS_THAN) {
                and_op = cstr_to_str("<");
            }
            else if (and_op_tok->type == TokenType::NOT_EQUAL) {
                and_op = cstr_to_str("!=");
            }
            else {
                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, and_op_tok));
                return false;
            }
            ConditionOperand and_right = {};
            if (iter_peek(tokens_iter)->type == TokenType::DOT) {
                (void)iter_next(tokens_iter); // consume DOT
                auto *member_tok = iter_next(tokens_iter);
                if (member_tok->type != TokenType::IDENTIFIER) {
                    append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, member_tok));
                    return false;
                }
                and_right.is_enum_shorthand = true;
                and_right.enum_shorthand.member_name = member_tok->identifier.content;
            }
            else {
                if (!parse_cond_operand(iter_next(tokens_iter), &and_right)) {
                    return false;
                }
            }
            and_lefts[and_count] = and_left;
            and_rights[and_count] = and_right;
            and_ops[and_count] = and_op;
            and_count++;
        }

        if (!expect_arrow_newline(tokens_iter, errors)) { return false; }

        ASTNode if_node_data = {
            .type = ASTNodeType::IF_ELSE,
            .parent = parent_node,
            .if_else = {
                .condition_left = cond_left,
                .condition_right = cond_right,
                .comparison_op = comparison_op,
                .and_count = and_count,
                .then_body = Array<ASTNode>(
                    nodes_block_iter->elements.data + nodes_block_iter->current_index + 1,
                    0
                ),
                .else_body = Array<ASTNode>(nullptr, 0),
                .if_pos = next_token->position,
            },
        };
        for (size_t ai = 0; ai < and_count; ai++) {
            if_node_data.if_else.and_condition_lefts[ai]  = and_lefts[ai];
            if_node_data.if_else.and_condition_rights[ai] = and_rights[ai];
            if_node_data.if_else.and_comparison_ops[ai]   = and_ops[ai];
        }
        auto *if_else_node = iter_append(nodes_block_iter, std::move(if_node_data));

        if (!parse_indented_body(tokens_iter, context, nodes_block_iter, if_else_node,
                                 proc_params_block, proc_params_iter, types_iter,
                                 operands_iter, array_elements_iter, errors,
                                 current_indent_level, &if_else_node->if_else.then_body)) {
            return false;
        }

        bool const has_else = (
            tokens_iter->current_index + 1 < tokens_iter->elements.length &&
            iter_peek(tokens_iter)->type == TokenType::INDENT &&
            iter_peek(tokens_iter)->indent.level == current_indent_level &&
            tokens_iter->elements.data[tokens_iter->current_index + 1].type == TokenType::KEYWORD_ELSE
        );
        if (has_else) {
            iter_next(tokens_iter); // INDENT before else
            auto *else_token = iter_next(tokens_iter); // KEYWORD_ELSE
            if_else_node->if_else.else_pos = else_token->position;

            bool const has_else_if = (
                tokens_iter->current_index < tokens_iter->elements.length &&
                iter_peek(tokens_iter)->type == TokenType::KEYWORD_IF
            );

            if_else_node->if_else.else_body.data =
                nodes_block_iter->elements.data + nodes_block_iter->current_index;

            if (has_else_if) {
                if (!parse_statement(tokens_iter, context, nodes_block_iter, if_else_node,
                                     proc_params_block, proc_params_iter, types_iter,
                                     operands_iter, array_elements_iter, errors,
                                     current_indent_level)) {
                    return false;
                }
                if_else_node->if_else.else_body.length =
                    nodes_block_iter->current_index
                    - ptr_sub(if_else_node->if_else.else_body.data, nodes_block_iter->elements.data);
            }
            else {
                if (!expect_arrow_newline(tokens_iter, errors)) { return false; }
                if (!parse_indented_body(tokens_iter, context, nodes_block_iter, if_else_node,
                                         proc_params_block, proc_params_iter, types_iter,
                                         operands_iter, array_elements_iter, errors,
                                         current_indent_level, &if_else_node->if_else.else_body)) {
                    return false;
                }
            }
        }
        break;
    }
    case TokenType::INTEGER_LITERAL: {
        (void)iter_append(nodes_block_iter, ASTNode {
            .type = ASTNodeType::INTEGER_LITERAL,
            .parent = parent_node,
            .integer_literal = { .value = { .value = next_token->integer_literal.value } },
        });
        assert(
            iter_current(tokens_iter)->type == TokenType::NEWLINE ||
            iter_current(tokens_iter)->type == TokenType::END &&
            "Expected newline or end token after integer literal");
        (void)iter_next(tokens_iter);
        break;
    }
    case TokenType::ARROW: {
        if (expect_token_or_append_error(tokens_iter, TokenType::NEWLINE, errors) == nullptr) { return false; }

        auto *scope_node = iter_append(nodes_block_iter, ASTNode {
            .type = ASTNodeType::SCOPE,
            .parent = parent_node,
            .scope = {
                .body = Array<ASTNode>(
                    nodes_block_iter->elements.data + nodes_block_iter->current_index + 1,
                    0
                ),
            },
        });

        if (!parse_indented_body(tokens_iter, context, nodes_block_iter, scope_node,
                                 proc_params_block, proc_params_iter, types_iter,
                                 operands_iter, array_elements_iter, errors,
                                 current_indent_level, &scope_node->scope.body)) {
            return false;
        }
        break;
    }
    default:
        break;
    }
    return true;
}
