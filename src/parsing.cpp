#include <bloom/parsing_internal.h>

// For debugging purposes
auto print_value(FILE *file, Context *context) -> void {
    auto *current_identifier = static_cast<void*>(context->current_identifier);
    auto *current_proc_node = static_cast<void*>(context->current_proc_node);
    char const *in_proc_definition = nullptr;
    if (context->in_proc_definition) {
        in_proc_definition = "true";
    }
    else {
        in_proc_definition = "false";
    }
    fprintf(
        _bloom_test_get_file(file),
        "{current_identifier=%p, current_proc_node=%p, in_proc_definition=%s}",
        current_identifier, current_proc_node, in_proc_definition
    );
}


auto expect_token_or_append_error(
    Iterator<Token> *tokens_iter,
    TokenType expected,
    DynamicArray<ParseError> *errors
) -> Token * {
    auto *tok = iter_next(tokens_iter);
    if (tok->type != expected) {
        append(errors, ParseError {
            .code = ParseErrorCode::UNEXPECTED_TOKEN,
            .position = tok->position,
            .src_code_line = __LINE__,
            .token_type = tok->type,
        });
        return nullptr;
    }
    return tok;
}

auto expect_arrow_newline(
    Iterator<Token> *tokens_iter,
    DynamicArray<ParseError> *errors
) -> bool {
    if (expect_token_or_append_error(tokens_iter, TokenType::ARROW, errors) == nullptr) { return false; }
    if (expect_token_or_append_error(tokens_iter, TokenType::NEWLINE, errors) == nullptr) { return false; }
    return true;
}

auto slice_expression_tokens(
    Iterator<Token> *tokens_iter,
    DynamicArray<ParseError> *errors,
    Iterator<Token> *out_iter
) -> bool {
    int brace_depth = 0;
    int64_t expr_end_token_index = iter_get_index_at_if<Token>(
        tokens_iter, [&brace_depth](auto *token) {
            if (token->type == TokenType::BRACE_OPEN) { brace_depth++; return false; }
            if (token->type == TokenType::BRACE_CLOSE && brace_depth > 0) { brace_depth--; return false; }
            return brace_depth == 0 && (
                token->type == TokenType::NEWLINE ||
                token->type == TokenType::END
            );
        }
    );
    if (expr_end_token_index == -1 ||
        expr_end_token_index == static_cast<int64_t>(tokens_iter->current_index))
    {
        append(errors, ParseError {
            .code = ParseErrorCode::UNEXPECTED_TOKEN,
            .position = iter_current(tokens_iter)->position,
            .src_code_line = __LINE__,
            .token_type = iter_current(tokens_iter)->type,
        });
        return false;
    }
    *out_iter = iter_slice_by_offset(tokens_iter, tokens_iter->current_index, expr_end_token_index);
    return true;
}

auto find_proc_def_node(
    Iterator<ASTNode> const *nodes_block_iter,
    Str const *name
) -> ASTNode const * {
    for (size_t i = 0; i < nodes_block_iter->current_index; i++) {
        auto const *node = &nodes_block_iter->elements.data[i];
        if (node->type == ASTNodeType::PROC_DEF && str_equal(node->proc_def.name, *name)) {
            return node;
        }
    }
    return nullptr;
}

auto infer_arg_type_name(ASTNode const *arg, Context const *context) -> Str {
    switch (arg->type) {
        case ASTNodeType::BOOLEAN_LITERAL:
            return cstr_to_str("Bool");
        case ASTNodeType::INTEGER_LITERAL:
            return cstr_to_str("Int");
        case ASTNodeType::STRING_LITERAL:
            return cstr_to_str("Str");
        case ASTNodeType::IDENTIFIER:
            for (size_t i = context->var_type_count; i-- > 0;) {
                Str const *vname = &context->var_types[i].name;
                if (str_equal(*vname, arg->identifier)) {
                    return context->var_types[i].is_bool ? cstr_to_str("Bool") : cstr_to_str("Int");
                }
            }
            return {};
        default:
            return {};
    }
}

auto parse(Array<Token> *tokens, ArenaAllocator *allocator, Str source_content, Str filename, bool *had_errors) -> Array<ASTNode> {
    // Allocate all necessary blocks upfront
    // TODO Adjust the max error count so that it is exact
    size_t constexpr MAX_ERROR_COUNT = 16; // Should be enough for now
    auto errors_block = allocate_array<ParseError>(allocator, MAX_ERROR_COUNT);

    auto types_block = allocate_array<TypeASTNode>(allocator, tokens->length);
    auto types_iter = to_iterator(&types_block);

    // Store the initial allocation offset in order to resize
    // both the nodes array and the proc params array later
    auto initial_marker = allocator_marker_from_current_offset(allocator);

    auto nodes_block = allocate_array<ASTNode>(allocator, tokens->length);
    auto nodes_block_iter = to_iterator(&nodes_block);

    auto proc_params_block = allocate_array<ProcParameterASTNode>(allocator, tokens->length);
    ProcParameterASTNode *const orig_proc_params_data = proc_params_block.data;
    auto proc_params_iter = to_iterator(&proc_params_block);
    assert (proc_params_iter.current_index == 0 &&
        "Procedure parameters iterator current index should be 0 at the start");

    auto operands_block = allocate_array<BinaryOperand>(allocator, tokens->length);
    auto operands_iter = to_iterator(&operands_block);
    BinaryOperand *orig_operands_data = operands_block.data;

    auto array_elements_block = allocate_array<int64_t>(allocator, tokens->length);
    auto array_elements_iter = to_iterator(&array_elements_block);
    int64_t *orig_array_elements_data = array_elements_block.data;

    // Parse the tokens into AST nodes
    auto context = Context{};
    assert(context.current_identifier == nullptr &&
        "Current identifier in context should be null at the start");
    context.nodes_block = &nodes_block;
    auto errors = DynamicArray<ParseError>(&errors_block);

    // Parse tokens
    auto tokens_iter = to_iterator(tokens);
    while (tokens_iter.current_index < tokens_iter.elements.length) {
        auto *current_token = iter_next(&tokens_iter);

        if (current_token->type == TokenType::END) {
            break;
        }

        if (current_token->type == TokenType::KEYWORD_PACKAGE) {
            auto *id_tok = iter_next(&tokens_iter);
            if (id_tok->type != TokenType::IDENTIFIER) {
                append(&errors, ParseError {
                    .code = ParseErrorCode::UNEXPECTED_TOKEN,
                    .position = id_tok->position,
                    .src_code_line = __LINE__,
                    .token_type = id_tok->type,
                });
                goto after_parsing;
            }
            Str package_name = id_tok->identifier.content;
            while (tokens_iter.current_index < tokens_iter.elements.length &&
                   iter_peek(&tokens_iter)->type == TokenType::DOT)
            {
                (void)iter_next(&tokens_iter); // consume dot
                auto *next_id = iter_next(&tokens_iter);
                if (next_id->type != TokenType::IDENTIFIER) {
                    append(&errors, ParseError {
                        .code = ParseErrorCode::UNEXPECTED_TOKEN,
                        .position = next_id->position,
                        .src_code_line = __LINE__,
                        .token_type = next_id->type,
                    });
                    goto after_parsing;
                }
                package_name.length = static_cast<size_t>(
                    next_id->identifier.content.data - package_name.data
                ) + next_id->identifier.content.length;
            }
            if (tokens_iter.current_index < tokens_iter.elements.length &&
                iter_peek(&tokens_iter)->type == TokenType::NEWLINE)
            {
                (void)iter_next(&tokens_iter);
            }
            (void)iter_append(&nodes_block_iter, ASTNode {
                .type = ASTNodeType::PACKAGE_DEF,
                .parent = nullptr,
                .package_def = { .name = package_name },
            });
            continue;
        }

        // Parse top-level nodes
        parse_top_level_node:
            if (current_token->type == TokenType::IDENTIFIER) {
                auto peeked_token = iter_next(&tokens_iter);
                if (peeked_token->type == TokenType::CONST_DEF) {
                    context.current_identifier = current_token;
                    if (tokens_iter.current_index < tokens_iter.elements.length) {
                        TokenType const first_expr_type = iter_peek(&tokens_iter)->type;
                        if (first_expr_type != TokenType::KEYWORD_PROC &&
                            first_expr_type != TokenType::KEYWORD_STRUCT &&
                            first_expr_type != TokenType::KEYWORD_ENUM)
                        {
                            int64_t const expr_end_idx = iter_get_index_at_if<Token>(
                                &tokens_iter, [](Token const *t) {
                                    return t->type == TokenType::NEWLINE || t->type == TokenType::END;
                                }
                            );
                            if (expr_end_idx != -1 &&
                                expr_end_idx > static_cast<int64_t>(tokens_iter.current_index))
                            {
                                auto expr_toks = iter_slice_by_offset(
                                    &tokens_iter, tokens_iter.current_index, expr_end_idx);
                                size_t const saved_error_count = errors.length;
                                int64_t const_value = 0;
                                if (eval_const_additive(&expr_toks, &context, &nodes_block_iter, &errors, &const_value)) {
                                    if (context.constant_count < 64) {
                                        context.constants[context.constant_count++] = {
                                            .name = current_token->identifier.content,
                                            .value = const_value,
                                        };
                                    }
                                }
                                else {
                                    errors.length = saved_error_count;
                                }
                            }
                            if (expr_end_idx != -1) {
                                tokens_iter.current_index = static_cast<size_t>(expr_end_idx);
                                if (tokens_iter.current_index < tokens_iter.elements.length) {
                                    (void)iter_next(&tokens_iter);
                                }
                            }
                            continue;
                        }
                    }
                    goto parse_expr;
                }
            }
            continue;

        parse_expr:
            auto expr_result = parse_expression(
                &tokens_iter,
                &context,
                &nodes_block_iter,
                &proc_params_block,
                &proc_params_iter,
                &types_iter,
                &operands_iter,
                &array_elements_iter,
                &errors
            );
            if (!is_ok(&expr_result)) {
                if (errors.length == 0) {
                    append(&errors, expr_result.err);
                }
                goto after_parsing;
            }
            continue;
    }

    after_parsing:
        report_parse_errors(to_array(&errors), had_errors, source_content, filename);

        // Allocate new blocks with the exact sizes and copy the data over to them
        auto nodes_block_arr = to_array(&nodes_block);
        nodes_block_arr = slice_by_offset(&nodes_block_arr, 0, nodes_block_iter.current_index);
        auto new_nodes_block = allocate_array_from_copy<ASTNode>(allocator, &nodes_block_arr);

        auto old_proc_params_arr = to_array(&proc_params_block);
        old_proc_params_arr = slice_by_offset(&old_proc_params_arr, 0, proc_params_iter.current_index);
        auto old_proc_params_block = allocate_array_from_copy<ProcParameterASTNode>(allocator, &old_proc_params_arr);

        auto old_operands_arr = to_array(&operands_block);
        old_operands_arr = slice_by_offset(&old_operands_arr, 0, operands_iter.current_index);
        auto old_operands_block = allocate_array_from_copy<BinaryOperand>(allocator, &old_operands_arr);

        auto old_array_elements_arr = to_array(&array_elements_block);
        old_array_elements_arr = slice_by_offset(&old_array_elements_arr, 0, array_elements_iter.current_index);
        auto old_array_elements_block = allocate_array_from_copy<int64_t>(allocator, &old_array_elements_arr);

        // Reset the allocator offset to the initial value and re-allocate
        // the nodes and proc params blocks to be tightly packed
        allocator->offset = initial_marker.offset;

        auto new_nodes_block_arr = to_array(&new_nodes_block);
        new_nodes_block = allocate_array_from_copy<ASTNode>(allocator, &new_nodes_block_arr);
        assert(new_nodes_block.length == nodes_block_iter.current_index &&
            "Node count mismatch after re-allocation");

        auto new_proc_params_arr = to_array(&old_proc_params_block);
        auto new_proc_params_block = allocate_array_from_copy<ProcParameterASTNode>(allocator, &new_proc_params_arr);
        assert(new_proc_params_block.length == proc_params_iter.current_index &&
            "Proc parameter count mismatch after re-allocation");

        auto new_operands_arr = to_array(&old_operands_block);
        auto new_operands_block = allocate_array_from_copy<BinaryOperand>(allocator, &new_operands_arr);
        assert(new_operands_block.length == operands_iter.current_index &&
            "Operand count mismatch after re-allocation");

        auto new_array_elements_arr = to_array(&old_array_elements_block);
        auto new_array_elements_block = allocate_array_from_copy<int64_t>(allocator, &new_array_elements_arr);
        assert(new_array_elements_block.length == array_elements_iter.current_index &&
            "Array element count mismatch after re-allocation");

        // Update the proc parameters and struct field pointers in the AST nodes
        // to point to the new tightly packed block.
        for (auto &node : new_nodes_block) {
            switch (node.type) {
                case ASTNodeType::PROC_DEF: {
                    ptrdiff_t const offset = node.proc_def.parameters.data - orig_proc_params_data;
                    node.proc_def.parameters.data = new_proc_params_block.data + offset;
                    break;
                }
                case ASTNodeType::STRUCT_DEF: {
                    ptrdiff_t const offset = node.struct_def.fields.data - orig_proc_params_data;
                    node.struct_def.fields.data = new_proc_params_block.data + offset;
                    break;
                }
                case ASTNodeType::ENUM_DEF: {
                    ptrdiff_t const offset = node.enum_def.members.data - orig_proc_params_data;
                    node.enum_def.members.data = new_proc_params_block.data + offset;
                    break;
                }
                case ASTNodeType::BINARY_ADD:
                case ASTNodeType::BINARY_MUL:
                case ASTNodeType::BINARY_DIV:
                case ASTNodeType::BINARY_SUB:
                case ASTNodeType::COMPARISON:
                    node.binary_operation.operands.data =
                        new_operands_block.data + (node.binary_operation.operands.data - orig_operands_data);
                    break;
                case ASTNodeType::ARRAY_INIT:
                    node.array_init.elements.data =
                        new_array_elements_block.data +
                        (node.array_init.elements.data - orig_array_elements_data);
                    break;
                case ASTNodeType::FOR_IN_LOOP:
                    if (node.for_in_loop.inline_elements.length > 0) {
                        node.for_in_loop.inline_elements.data =
                            new_array_elements_block.data +
                            (node.for_in_loop.inline_elements.data - orig_array_elements_data);
                    }
                    break;
                case ASTNodeType::STRUCT_INIT:
                    if (node.struct_init.field_names.length > 0) {
                        node.struct_init.field_names.data =
                            new_proc_params_block.data + (node.struct_init.field_names.data - orig_proc_params_data);
                        node.struct_init.field_values.data =
                            new_operands_block.data + (node.struct_init.field_values.data - orig_operands_data);
                    }
                    break;
                default:
                    break;
            }
        }

        return to_array(&new_nodes_block);
}
