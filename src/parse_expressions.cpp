#include <bloom/parsing_internal.h>

auto parse_indented_body(
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
    size_t current_indent_level,
    Array<ASTNode> *body
) -> bool {
    while (tokens_iter->current_index < tokens_iter->elements.length) {
        if (iter_peek(tokens_iter)->type == TokenType::NEWLINE) {
            (void)iter_next(tokens_iter);
            continue;
        }
        if (iter_peek(tokens_iter)->type != TokenType::INDENT ||
            iter_peek(tokens_iter)->indent.level <= current_indent_level)
        {
            break;
        }
        auto *body_indent = iter_next(tokens_iter);
        if (!parse_statement(tokens_iter, context, nodes_block_iter, parent_node,
                             proc_params_block, proc_params_iter, types_iter,
                             operands_iter, array_elements_iter, errors,
                             body_indent->indent.level)) {
            return false;
        }
    }
    body->length =
        nodes_block_iter->current_index
        - ptr_sub(body->data, nodes_block_iter->elements.data);
    return true;
}

struct ReturnAnalysis {
    bool returns;
    Token::Position failing_pos;
    bool has_failing_pos;
    bool failing_is_after_stmt;  // true = return must come AFTER the statement at failing_pos
};

static auto proc_call_has_return_type(
    Str proc_name,
    Iterator<ASTNode> const *nodes_block_iter
) -> bool {
    for (size_t i = 0; i < nodes_block_iter->current_index; i++) {
        ASTNode const *node = &nodes_block_iter->elements.data[i];
        if (node->type == ASTNodeType::PROC_DEF &&
            str_equal(node->proc_def.name, proc_name))
        {
            return node->proc_def.return_type != nullptr;
        }
    }
    return false;
}

// Returns true if the body contains a BREAK that would exit a loop whose node is
// body_parent. Recurses into if_else branches but not into nested loops (a break
// inside a nested loop exits that loop, not the outer one).
static auto body_has_breakout(
    Array<ASTNode> const body,
    ASTNode const *body_parent
) -> bool {
    for (size_t i = 0; i < body.length; i++) {
        ASTNode const *node = &body.data[i];
        if (node->parent != body_parent) {
            continue;
        }
        if (node->type == ASTNodeType::BREAK) {
            return true;
        }
        if (node->type == ASTNodeType::IF_ELSE) {
            if (body_has_breakout(node->if_else.then_body, node)) {
                return true;
            }
            if (body_has_breakout(node->if_else.else_body, node)) {
                return true;
            }
        }
        // Do not recurse into nested loops — their breaks don't exit the outer loop
    }
    return false;
}

// The body array is a flat arena slice that includes both direct-child statements
// and their sub-nodes (expression args, etc.). Use the parent pointer to find the
// last direct child: iterate backward until we hit a node whose parent == body_parent.
static auto find_last_stmt(
    Array<ASTNode> const body,
    ASTNode const *body_parent
) -> ASTNode const * {
    for (size_t i = body.length; i > 0; i--) {
        if (body.data[i - 1].parent == body_parent) {
            return &body.data[i - 1];
        }
    }
    return nullptr;
}

static auto check_returns_on_all_paths(
    Array<ASTNode> const body,
    ASTNode const *body_parent,
    Iterator<ASTNode> const *nodes_block_iter
) -> ReturnAnalysis {
    ASTNode const *last = find_last_stmt(body, body_parent);
    if (last == nullptr) {
        return { false, {}, false, false };
    }
    switch (last->type) {
        case ASTNodeType::RETURN:
            return { last->return_value != nullptr, {}, false, false };
        case ASTNodeType::IF_ELSE: {
            if (last->if_else.else_body.length == 0) {
                return { false, last->if_else.if_pos, true, false };
            }
            ReturnAnalysis then_r = check_returns_on_all_paths(
                last->if_else.then_body, last, nodes_block_iter);
            ReturnAnalysis else_r = check_returns_on_all_paths(
                last->if_else.else_body, last, nodes_block_iter);
            if (then_r.returns && else_r.returns) {
                return { true, {}, false, false };
            }
            if (!then_r.returns) {
                Token::Position pos = then_r.has_failing_pos ? then_r.failing_pos : last->if_else.if_pos;
                bool is_after = then_r.has_failing_pos && then_r.failing_is_after_stmt;
                return { false, pos, true, is_after };
            }
            Token::Position pos = else_r.has_failing_pos ? else_r.failing_pos : last->if_else.else_pos;
            bool is_after = else_r.has_failing_pos && else_r.failing_is_after_stmt;
            return { false, pos, true, is_after };
        }
        case ASTNodeType::FOR_LOOP:
            // An infinite loop that contains a direct break can exit without returning.
            // The fix is to add a return AFTER the loop, not inside it.
            if (body_has_breakout(last->for_loop.body, last)) {
                return { false, last->for_loop.for_pos, true, true };
            }
            return { true, {}, false, false };
        case ASTNodeType::FOR_IN_LOOP:
        case ASTNodeType::FOR_RANGE_LOOP:
        case ASTNodeType::FOR_COND_LOOP:
            return { true, {}, false, false };
        case ASTNodeType::PROC_CALL: {
            bool const has_ret = proc_call_has_return_type(last->proc_call.caller_identifier, nodes_block_iter);
            return { has_ret, {}, false, false };
        }
        case ASTNodeType::BINARY_ADD:
        case ASTNodeType::BINARY_SUB:
        case ASTNodeType::BINARY_MUL:
        case ASTNodeType::BINARY_DIV:
        case ASTNodeType::IDENTIFIER:
        case ASTNodeType::INTEGER_LITERAL:
        case ASTNodeType::BOOLEAN_LITERAL:
        case ASTNodeType::STRING_LITERAL:
        case ASTNodeType::INTERPOLATED_STRING_LITERAL:
        case ASTNodeType::MEMBER_ACCESS:
        case ASTNodeType::ARRAY_ACCESS:
        case ASTNodeType::ARRAY_SLICE:
        case ASTNodeType::STRUCT_INIT:
            return { true, {}, false, false };
        default:
            return { false, {}, false, false };
    }
}

auto parse_expression(
    Iterator<Token> *tokens_iter,
    Context *context,
    Iterator<ASTNode> *nodes_block_iter,
    AllocatedArrayBlock<ProcParameterASTNode> *proc_params_block,
    Iterator<ProcParameterASTNode> *proc_params_iter,
    Iterator<TypeASTNode> *types_iter,
    Iterator<BinaryOperand> *operands_iter,
    Iterator<int64_t> *array_elements_iter,
    DynamicArray<ParseError> *errors
) -> Result<ASTNode, ParseError> {
    auto next_token = iter_next(tokens_iter);
    assert(next_token != nullptr &&
        "Next token should not be null when parsing an expression");
    switch(next_token->type) {
        case TokenType::BRACKET_OPEN: {
            // Parse [const]ElementType{ ... } or [N]ElementType{ ... } or []Type(expr) slice cast
            auto *size_token = iter_next(tokens_iter);
            if (size_token->type == TokenType::BRACKET_CLOSE) {
                auto *type_tok = iter_next(tokens_iter);
                if (type_tok->type != TokenType::IDENTIFIER) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, type_tok));
                }
                Str const elem_type = type_tok->identifier.content;
                if (expect_token_or_append_error(tokens_iter, TokenType::PARENTHESIS_OPEN, errors) == nullptr) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, type_tok));
                }
                auto *inner_tok = iter_next(tokens_iter);
                ASTNode inner_node = {};
                if (inner_tok->type == TokenType::IDENTIFIER &&
                    (inner_tok->identifier.content == "IntLE" || inner_tok->identifier.content == "IntBE") &&
                    tokens_iter->current_index < tokens_iter->elements.length &&
                    iter_peek(tokens_iter)->type == TokenType::PARENTHESIS_OPEN)
                {
                    bool const is_le = (inner_tok->identifier.content == "IntLE");
                    ASTNodeType const int_cast_type = is_le ? ASTNodeType::INTLE_CAST : ASTNodeType::INTBE_CAST;
                    (void)iter_next(tokens_iter); // consume IntLE/IntBE's (
                    auto *inner_int_tok = iter_next(tokens_iter);
                    ASTNode intle_inner_node = {};
                    if (inner_int_tok->type == TokenType::IDENTIFIER &&
                        tokens_iter->current_index < tokens_iter->elements.length &&
                        iter_peek(tokens_iter)->type == TokenType::DOT)
                    {
                        (void)iter_next(tokens_iter); // consume .
                        auto *field_tok = iter_next(tokens_iter);
                        if (field_tok->type != TokenType::IDENTIFIER) {
                            return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, field_tok));
                        }
                        intle_inner_node = ASTNode {
                            .type = ASTNodeType::MEMBER_ACCESS,
                            .parent = nullptr,
                            .member_access = {
                                .object_name = inner_int_tok->identifier.content,
                                .field_name = field_tok->identifier.content,
                            },
                        };
                    }
                    else if (inner_int_tok->type == TokenType::IDENTIFIER) {
                        intle_inner_node = ASTNode {
                            .type = ASTNodeType::IDENTIFIER,
                            .parent = nullptr,
                            .identifier = inner_int_tok->identifier.content,
                        };
                    }
                    else {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, inner_int_tok));
                    }
                    if (expect_token_or_append_error(tokens_iter, TokenType::PARENTHESIS_CLOSE, errors) == nullptr) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, inner_int_tok));
                    }
                    if (expect_token_or_append_error(tokens_iter, TokenType::PARENTHESIS_CLOSE, errors) == nullptr) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, inner_int_tok));
                    }
                    ASTNode *intle_inner_ptr = iter_append(nodes_block_iter, std::move(intle_inner_node));
                    ASTNode *intle_node_ptr = iter_append(nodes_block_iter, ASTNode {
                        .type = int_cast_type,
                        .parent = nullptr,
                        .intle_cast = intle_inner_ptr,
                    });
                    return ok<ASTNode, ParseError>(ASTNode {
                        .type = ASTNodeType::SLICE_CAST,
                        .parent = nullptr,
                        .slice_cast = {
                            .element_type = elem_type,
                            .expr = intle_node_ptr,
                        },
                    });
                }
                else if (inner_tok->type == TokenType::IDENTIFIER &&
                    tokens_iter->current_index < tokens_iter->elements.length &&
                    iter_peek(tokens_iter)->type == TokenType::DOT)
                {
                    (void)iter_next(tokens_iter);
                    auto *field_tok = iter_next(tokens_iter);
                    if (field_tok->type != TokenType::IDENTIFIER) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, field_tok));
                    }
                    inner_node = ASTNode {
                        .type = ASTNodeType::MEMBER_ACCESS,
                        .parent = nullptr,
                        .member_access = {
                            .object_name = inner_tok->identifier.content,
                            .field_name = field_tok->identifier.content,
                        },
                    };
                }
                else if (inner_tok->type == TokenType::IDENTIFIER) {
                    inner_node = ASTNode {
                        .type = ASTNodeType::IDENTIFIER,
                        .parent = nullptr,
                        .identifier = inner_tok->identifier.content,
                    };
                }
                else {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, inner_tok));
                }
                if (expect_token_or_append_error(tokens_iter, TokenType::PARENTHESIS_CLOSE, errors) == nullptr) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, inner_tok));
                }
                ASTNode *inner_ptr = iter_append(nodes_block_iter, std::move(inner_node));
                return ok<ASTNode, ParseError>(ASTNode {
                    .type = ASTNodeType::SLICE_CAST,
                    .parent = nullptr,
                    .slice_cast = {
                        .element_type = elem_type,
                        .expr = inner_ptr,
                    },
                });
            }
            int64_t explicit_length = -1;
            if (size_token->type == TokenType::KEYWORD_CONST) {
                // no explicit length check
            }
            else {
                tokens_iter->current_index--;
                int64_t const bracket_close_idx = iter_get_index_at_if<Token>(
                    tokens_iter, [](Token const *t) {
                        return t->type == TokenType::BRACKET_CLOSE;
                    }
                );
                if (bracket_close_idx == -1 ||
                    bracket_close_idx == static_cast<int64_t>(tokens_iter->current_index))
                {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, size_token));
                }
                auto expr_toks = iter_slice_by_offset(
                    tokens_iter, tokens_iter->current_index, bracket_close_idx);
                if (!eval_const_additive(&expr_toks, context, nodes_block_iter, errors, &explicit_length)) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, size_token));
                }
                tokens_iter->current_index = static_cast<size_t>(bracket_close_idx);
            }
            auto *bracket_close = iter_next(tokens_iter);
            if (bracket_close->type != TokenType::BRACKET_CLOSE) {
                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, bracket_close));
            }
            auto *elem_type_token = iter_next(tokens_iter);
            if (elem_type_token->type != TokenType::IDENTIFIER) {
                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, elem_type_token));
            }
            Str element_type = elem_type_token->identifier.content;
            auto *brace_open = iter_next(tokens_iter);
            if (brace_open->type != TokenType::BRACE_OPEN) {
                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, brace_open));
            }
            Token::Position const brace_open_pos = brace_open->position;
            // Detect struct-init elements: TypeName { ... } or .{ ... }
            {
                size_t peek_idx = tokens_iter->current_index;
                while (peek_idx < tokens_iter->elements.length) {
                    TokenType const tt = tokens_iter->elements.data[peek_idx].type;
                    if (tt != TokenType::NEWLINE && tt != TokenType::INDENT && tt != TokenType::COMMA) {
                        break;
                    }
                    peek_idx++;
                }
                bool is_struct_array = false;
                if (peek_idx < tokens_iter->elements.length) {
                    TokenType const first = tokens_iter->elements.data[peek_idx].type;
                    if (first == TokenType::DOT &&
                        peek_idx + 1 < tokens_iter->elements.length &&
                        tokens_iter->elements.data[peek_idx + 1].type == TokenType::BRACE_OPEN)
                    {
                        is_struct_array = true;
                    }
                    else if (first == TokenType::IDENTIFIER &&
                        peek_idx + 1 < tokens_iter->elements.length &&
                        tokens_iter->elements.data[peek_idx + 1].type == TokenType::BRACE_OPEN)
                    {
                        is_struct_array = true;
                    }
                }
                if (is_struct_array) {
                    size_t const struct_inits_begin = nodes_block_iter->current_index;
                    size_t struct_elem_count = 0;
                    while (true) {
                        auto *tok = iter_next(tokens_iter);
                        if (tok->type == TokenType::BRACE_CLOSE) {
                            break;
                        }
                        if (tok->type == TokenType::NEWLINE ||
                            tok->type == TokenType::INDENT ||
                            tok->type == TokenType::COMMA)
                        {
                            continue;
                        }
                        bool const is_dot_syntax = (tok->type == TokenType::DOT);
                        if (!is_dot_syntax && tok->type != TokenType::IDENTIFIER) {
                            return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, tok));
                        }
                        auto *bopen = iter_next(tokens_iter);
                        if (bopen->type != TokenType::BRACE_OPEN) {
                            return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, bopen));
                        }
                        size_t const field_names_begin = proc_params_iter->current_index;
                        size_t const field_values_begin = operands_iter->current_index;
                        while (true) {
                            auto *ftok = iter_next(tokens_iter);
                            if (ftok->type == TokenType::BRACE_CLOSE) {
                                break;
                            }
                            if (ftok->type == TokenType::COMMA ||
                                ftok->type == TokenType::NEWLINE ||
                                ftok->type == TokenType::INDENT)
                            {
                                continue;
                            }
                            if (ftok->type != TokenType::IDENTIFIER) {
                                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, ftok));
                            }
                            Str const field_name = ftok->identifier.content;
                            auto *eq = iter_next(tokens_iter);
                            if (eq->type != TokenType::EQUALS) {
                                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, eq));
                            }
                            auto *val_tok = iter_next(tokens_iter);
                            switch (val_tok->type) {
                                case TokenType::INTEGER_LITERAL:
                                    (void)iter_append(proc_params_iter, ProcParameterASTNode { .name = field_name, .type_name = {} });
                                    (void)iter_append(operands_iter, BinaryOperand {
                                        .type = BinaryOperandType::INTEGER_LITERAL,
                                        .integer_literal = IntegerLiteralASTNode { .value = val_tok->integer_literal.value },
                                    });
                                    break;
                                case TokenType::STRING_LITERAL:
                                    (void)iter_append(proc_params_iter, ProcParameterASTNode { .name = field_name, .type_name = {} });
                                    (void)iter_append(operands_iter, BinaryOperand {
                                        .type = BinaryOperandType::STRING_LITERAL,
                                        .string_literal = val_tok->string_literal.content,
                                    });
                                    break;
                                case TokenType::IDENTIFIER:
                                    (void)iter_append(proc_params_iter, ProcParameterASTNode { .name = field_name, .type_name = {} });
                                    (void)iter_append(operands_iter, BinaryOperand {
                                        .type = BinaryOperandType::IDENTIFIER,
                                        .identifier = val_tok->identifier.content,
                                    });
                                    break;
                                default:
                                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, val_tok));
                            }
                        }
                        size_t const field_count = proc_params_iter->current_index - field_names_begin;
                        (void)iter_append(nodes_block_iter, ASTNode {
                            .type = ASTNodeType::STRUCT_INIT,
                            .parent = nullptr,
                            .struct_init = {
                                .type_name = element_type,
                                .field_names = Array<ProcParameterASTNode>(
                                    proc_params_iter->elements.data + field_names_begin,
                                    field_count
                                ),
                                .field_values = Array<BinaryOperand>(
                                    operands_iter->elements.data + field_values_begin,
                                    field_count
                                ),
                            },
                        });
                        struct_elem_count++;
                    }
                    return ok<ASTNode, ParseError>(ASTNode {
                        .type = ASTNodeType::ARRAY_STRUCT_INIT,
                        .parent = nullptr,
                        .array_struct_init = {
                            .element_type = element_type,
                            .elements = Array<ASTNode>(
                                nodes_block_iter->elements.data + struct_inits_begin,
                                struct_elem_count
                            ),
                        },
                    });
                }
            }
            size_t elements_begin = array_elements_iter->current_index;
            bool in_range_mode = false;
            Token::Position brace_close_pos = {};
            while (true) {
                auto *elem_token = iter_next(tokens_iter);
                if (elem_token->type == TokenType::BRACE_CLOSE) {
                    brace_close_pos = elem_token->position;
                    break;
                }
                if (elem_token->type == TokenType::COMMA ||
                    elem_token->type == TokenType::NEWLINE ||
                    elem_token->type == TokenType::INDENT) {
                    continue;
                }
                if (elem_token->type != TokenType::INTEGER_LITERAL) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, elem_token));
                }

                if (tokens_iter->current_index < tokens_iter->elements.length &&
                    iter_peek(tokens_iter)->type == TokenType::RANGE_EXCLUSIVE)
                {
                    // Range element: start..<end = value
                    in_range_mode = true;
                    int64_t range_start = elem_token->integer_literal.value;
                    size_t current_count = array_elements_iter->current_index - elements_begin;
                    if (range_start != static_cast<int64_t>(current_count)) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, elem_token));
                    }
                    (void)iter_next(tokens_iter); // consume ..<

                    auto *end_token = iter_next(tokens_iter);
                    if (end_token->type != TokenType::INTEGER_LITERAL) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, end_token));
                    }
                    int64_t range_end = end_token->integer_literal.value;
                    if (range_end <= range_start) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, end_token));
                    }

                    auto *equals_token = iter_next(tokens_iter);
                    if (equals_token->type != TokenType::EQUALS) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, equals_token));
                    }

                    auto *value_token = iter_next(tokens_iter);
                    if (value_token->type != TokenType::INTEGER_LITERAL) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, value_token));
                    }
                    int64_t range_value = value_token->integer_literal.value;

                    for (int64_t idx = range_start; idx < range_end; idx++) {
                        int64_t val = range_value;
                        (void)iter_append(array_elements_iter, std::move(val));
                    }
                }
                else {
                    // Positional element — not allowed after range
                    if (in_range_mode) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, elem_token));
                    }
                    int64_t element_value = elem_token->integer_literal.value;
                    (void)iter_append(array_elements_iter, std::move(element_value));
                }
            }
            if (array_elements_iter->current_index == elements_begin && explicit_length > 0) {
                for (int64_t i = 0; i < explicit_length; i++) {
                    int64_t zero = 0;
                    (void)iter_append(array_elements_iter, std::move(zero));
                }
            }
            size_t const actual_count = array_elements_iter->current_index - elements_begin;
            if (explicit_length >= 0 && static_cast<int64_t>(actual_count) != explicit_length) {
                return err<ASTNode, ParseError>(ParseError {
                    .code = ParseErrorCode::ARRAY_LENGTH_MISMATCH,
                    .position = size_token->position,
                    .src_code_line = __LINE__,
                    .explicit_length = explicit_length,
                    .actual_count = actual_count,
                    .size_token_width = (size_token->type == TokenType::IDENTIFIER)
                        ? size_token->identifier.content.length
                        : 1,
                    .brace_open_pos = brace_open_pos,
                    .brace_close_pos = brace_close_pos,
                });
            }
            return ok<ASTNode, ParseError>(ASTNode {
                .type = ASTNodeType::ARRAY_INIT,
                .parent = nullptr,
                .array_init = {
                    .element_type = element_type,
                    .elements = Array<int64_t>(
                        array_elements_iter->elements.data + elements_begin,
                        actual_count
                    ),
                },
            });
        }
        case TokenType::KEYWORD_FALSE:
        case TokenType::KEYWORD_TRUE: {
            return ok<ASTNode, ParseError>(ASTNode {
                .type = ASTNodeType::BOOLEAN_LITERAL,
                .parent = nullptr,
                .boolean_literal = {
                    .value = (next_token->type == TokenType::KEYWORD_TRUE),
                },
            });
        }
        case TokenType::STRING_LITERAL: {
            return ok<ASTNode, ParseError>(ASTNode {
                .type = ASTNodeType::STRING_LITERAL,
                .parent = nullptr,
                .string_literal = {
                    .value = next_token->string_literal.content,
                },
            });
        }
        case TokenType::INTERP_STRING_LITERAL: {
            return ok<ASTNode, ParseError>(ASTNode {
                .type = ASTNodeType::INTERPOLATED_STRING_LITERAL,
                .parent = nullptr,
                .string_literal = {
                    .value = next_token->string_literal.content,
                },
            });
        }
        case TokenType::IDENTIFIER:
        case TokenType::INTEGER_LITERAL: {
            // type_info_of(expr).size_in_bytes — compile-time type size query
            if (next_token->type == TokenType::IDENTIFIER &&
                next_token->identifier.content == "type_info_of" &&
                tokens_iter->current_index < tokens_iter->elements.length &&
                iter_peek(tokens_iter)->type == TokenType::PARENTHESIS_OPEN)
            {
                (void)iter_next(tokens_iter); // consume (
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
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, next_token));
                }
                auto inner_iter = iter_slice_by_offset(
                    tokens_iter, inner_start, static_cast<int64_t>(inner_end));
                tokens_iter->current_index = inner_end + 1; // skip past )
                Str const type_name = infer_bloom_type_from_tokens(
                    &inner_iter, context, nodes_block_iter);
                if (type_name.length == 0) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, next_token));
                }
                if (tokens_iter->current_index >= tokens_iter->elements.length ||
                    iter_next(tokens_iter)->type != TokenType::DOT)
                {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, next_token));
                }
                if (tokens_iter->current_index >= tokens_iter->elements.length) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, next_token));
                }
                auto *field_tok = iter_next(tokens_iter);
                if (field_tok->type != TokenType::IDENTIFIER) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, field_tok));
                }
                if (field_tok->identifier.content == "size_in_bytes") {
                    return ok<ASTNode, ParseError>(ASTNode {
                        .type = ASTNodeType::TYPE_INFO_SIZE,
                        .parent = nullptr,
                        .type_info_size = { .type_name = type_name },
                    });
                }
                if (field_tok->identifier.content == "name") {
                    return ok<ASTNode, ParseError>(ASTNode {
                        .type = ASTNodeType::TYPE_INFO_NAME,
                        .parent = nullptr,
                        .type_info_name = { .type_name = type_name },
                    });
                }
                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, field_tok));
            }
            // Struct init: TypeName {} or TypeName { field = value, ... }
            if (next_token->type == TokenType::IDENTIFIER &&
                tokens_iter->current_index < tokens_iter->elements.length &&
                iter_peek(tokens_iter)->type == TokenType::BRACE_OPEN)
            {
                (void)iter_next(tokens_iter); // consume {
                Token::Position const brace_open_position = iter_peek_prev(tokens_iter)->position;
                size_t const field_names_begin = proc_params_iter->current_index;
                size_t const field_values_begin = operands_iter->current_index;
                Token::Position brace_close_position = {};
                size_t inner_indent_level = SIZE_MAX;
                size_t prev_indent_level = SIZE_MAX;
                bool last_was_indent = false;
                while (true) {
                    auto *tok = iter_next(tokens_iter);
                    if (tok->type == TokenType::BRACE_CLOSE) {
                        if (last_was_indent &&
                            inner_indent_level != SIZE_MAX &&
                            prev_indent_level >= inner_indent_level)
                        {
                            return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, tok));
                        }
                        brace_close_position = tok->position;
                        break;
                    }
                    if (tok->type == TokenType::NEWLINE) {
                        last_was_indent = false;
                        continue;
                    }
                    if (tok->type == TokenType::INDENT) {
                        prev_indent_level = tok->indent.level;
                        if (inner_indent_level == SIZE_MAX) {
                            inner_indent_level = prev_indent_level;
                        }
                        last_was_indent = true;
                        continue;
                    }
                    last_was_indent = false;
                    if (tok->type == TokenType::COMMA) {
                        continue;
                    }
                    if (tok->type != TokenType::IDENTIFIER) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, tok));
                    }
                    Str const field_name = tok->identifier.content;
                    for (size_t fi = 0; fi < proc_params_iter->current_index - field_names_begin; fi++) {
                        if (str_equal(proc_params_iter->elements.data[field_names_begin + fi].name, field_name)) {
                            ParseError dup_err = {};
                            dup_err.code = ParseErrorCode::STRUCT_DUPLICATE_FIELD;
                            dup_err.position = tok->position;
                            dup_err.src_code_line = __LINE__;
                            dup_err.token_type = tok->type;
                            dup_err.size_token_width = field_name.length;
                            dup_err.struct_type_name = next_token->identifier.content;
                            dup_err.duplicate_field_name = field_name;
                            return err<ASTNode, ParseError>(dup_err);
                        }
                    }
                    auto *equals = iter_next(tokens_iter);
                    if (equals->type != TokenType::EQUALS) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, equals));
                    }
                    auto *value_tok = iter_next(tokens_iter);
                    switch (value_tok->type) {
                        case TokenType::INTEGER_LITERAL:
                            (void)iter_append(proc_params_iter, ProcParameterASTNode {
                                .name = field_name,
                                .type_name = {},
                            });
                            (void)iter_append(operands_iter, BinaryOperand {
                                .type = BinaryOperandType::INTEGER_LITERAL,
                                .integer_literal = IntegerLiteralASTNode { .value = value_tok->integer_literal.value },
                            });
                            break;
                        case TokenType::STRING_LITERAL:
                            (void)iter_append(proc_params_iter, ProcParameterASTNode {
                                .name = field_name,
                                .type_name = {},
                            });
                            (void)iter_append(operands_iter, BinaryOperand {
                                .type = BinaryOperandType::STRING_LITERAL,
                                .string_literal = value_tok->string_literal.content,
                            });
                            break;
                        case TokenType::IDENTIFIER: {
                            if (value_tok->identifier.content == "make" &&
                                tokens_iter->current_index < tokens_iter->elements.length &&
                                iter_peek(tokens_iter)->type == TokenType::PARENTHESIS_OPEN)
                            {
                                (void)iter_next(tokens_iter); // consume (
                                auto *bopen = iter_next(tokens_iter);
                                if (bopen->type != TokenType::BRACKET_OPEN) {
                                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, bopen));
                                }
                                auto *dyn_kw = iter_next(tokens_iter);
                                if (dyn_kw->type != TokenType::KEYWORD_DYNAMIC) {
                                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, dyn_kw));
                                }
                                auto *bclose = iter_next(tokens_iter);
                                if (bclose->type != TokenType::BRACKET_CLOSE) {
                                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, bclose));
                                }
                                auto *type_tok = iter_next(tokens_iter);
                                if (type_tok->type != TokenType::IDENTIFIER) {
                                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, type_tok));
                                }
                                auto *comma = iter_next(tokens_iter);
                                if (comma->type != TokenType::COMMA) {
                                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, comma));
                                }
                                auto *size_tok = iter_next(tokens_iter);
                                if (size_tok->type != TokenType::INTEGER_LITERAL) {
                                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, size_tok));
                                }
                                auto *pclose_or_comma = iter_next(tokens_iter);
                                bool has_alloc = false;
                                Str alloc_ident = {};
                                bool alloc_is_context_temp = false;
                                bool alloc_is_context = false;
                                if (pclose_or_comma->type == TokenType::COMMA) {
                                    has_alloc = true;
                                    auto *alloc_tok = iter_next(tokens_iter);
                                    if (alloc_tok->type == TokenType::IDENTIFIER &&
                                        alloc_tok->identifier.content == "context")
                                    {
                                        (void)iter_next(tokens_iter); // consume '.'
                                        auto *field_tok = iter_next(tokens_iter);
                                        alloc_is_context_temp = field_tok->identifier.content == "temp_allocator";
                                        alloc_is_context = !alloc_is_context_temp;
                                    }
                                    else if (alloc_tok->type == TokenType::IDENTIFIER) {
                                        alloc_ident = alloc_tok->identifier.content;
                                    }
                                    else {
                                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, alloc_tok));
                                    }
                                    auto *pclose2 = iter_next(tokens_iter);
                                    if (pclose2->type != TokenType::PARENTHESIS_CLOSE) {
                                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, pclose2));
                                    }
                                }
                                else if (pclose_or_comma->type != TokenType::PARENTHESIS_CLOSE) {
                                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, pclose_or_comma));
                                }
                                auto *dyn_node = iter_append(nodes_block_iter, ASTNode {
                                    .type = ASTNodeType::MAKE_DYNAMIC_ARRAY,
                                    .parent = nullptr,
                                    .make_dynamic_array = {
                                        .element_type = type_tok->identifier.content,
                                        .has_explicit_allocator = has_alloc,
                                        .allocator_identifier = alloc_ident,
                                        .allocator_is_context_temp = alloc_is_context_temp,
                                        .allocator_is_context = alloc_is_context,
                                    },
                                });
                                (void)iter_append(proc_params_iter, ProcParameterASTNode { .name = field_name, .type_name = {} });
                                (void)iter_append(operands_iter, BinaryOperand {
                                    .type = BinaryOperandType::EXPR_NODE,
                                    .expr_node = dyn_node,
                                });
                            }
                            else {
                                (void)iter_append(proc_params_iter, ProcParameterASTNode {
                                    .name = field_name,
                                    .type_name = {},
                                });
                                (void)iter_append(operands_iter, BinaryOperand {
                                    .type = BinaryOperandType::IDENTIFIER,
                                    .identifier = value_tok->identifier.content,
                                });
                            }
                            break;
                        }
                        default:
                            return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, value_tok));
                    }
                }
                size_t const field_count = proc_params_iter->current_index - field_names_begin;

                // Check for missing fields when at least one field was explicitly provided
                if (field_count > 0) {
                    ASTNode const *struct_def_node = nullptr;
                    for (size_t ni = 0; ni < nodes_block_iter->current_index; ni++) {
                        auto const *n = &nodes_block_iter->elements.data[ni];
                        if (n->type == ASTNodeType::STRUCT_DEF &&
                            str_equal(n->struct_def.name, next_token->identifier.content))
                        {
                            struct_def_node = n;
                            break;
                        }
                    }
                    if (struct_def_node != nullptr) {
                        // Pointer-typed and dynamic array fields default to NULL/{0} and are not required
                        size_t required_count = 0;
                        for (size_t di = 0; di < struct_def_node->struct_def.fields.length; di++) {
                            auto const *f = &struct_def_node->struct_def.fields.data[di];
                            if (!f->is_pointer && !f->is_dynamic_array) {
                                required_count++;
                            }
                        }
                        if (field_count < required_count) {
                            ParseError missing_err = {};
                            missing_err.code = ParseErrorCode::STRUCT_MISSING_FIELDS;
                            missing_err.position = next_token->position;
                            missing_err.src_code_line = __LINE__;
                            missing_err.token_type = next_token->type;
                            missing_err.size_token_width = next_token->identifier.content.length;
                            missing_err.brace_open_pos = brace_open_position;
                            missing_err.brace_close_pos = brace_close_position;
                            missing_err.struct_type_name = next_token->identifier.content;

                            size_t const def_count = struct_def_node->struct_def.fields.length;
                            missing_err.struct_field_count = def_count < 8 ? def_count : 8;
                            for (size_t di = 0; di < missing_err.struct_field_count; di++) {
                                auto const *def_field = &struct_def_node->struct_def.fields.data[di];
                                Str const *def_name = &def_field->name;
                                missing_err.struct_field_names[di] = *def_name;
                                missing_err.struct_field_type_names[di] = def_field->type_name;
                                bool found = false;
                                for (size_t fi = 0; fi < field_count; fi++) {
                                    Str const *init_name =
                                        &proc_params_iter->elements.data[field_names_begin + fi].name;
                                    if (str_equal(*def_name, *init_name)) {
                                        found = true;
                                        break;
                                    }
                                }
                                missing_err.struct_field_is_missing[di] = !found && !def_field->is_pointer && !def_field->is_dynamic_array;
                            }
                            return err<ASTNode, ParseError>(missing_err);
                        }
                    }
                }

                return ok<ASTNode, ParseError>(ASTNode {
                    .type = ASTNodeType::STRUCT_INIT,
                    .parent = nullptr,
                    .struct_init = {
                        .type_name = next_token->identifier.content,
                        .field_names = Array<ProcParameterASTNode>(
                            proc_params_iter->elements.data + field_names_begin,
                            field_count
                        ),
                        .field_values = Array<BinaryOperand>(
                            operands_iter->elements.data + field_values_begin,
                            field_count
                        ),
                    },
                });
            }

            // Returns a BinaryOperand for the given token, consuming any proc call tokens
            // from tokens_iter. Never default-constructs BinaryOperand to avoid union issues.
            auto parse_operand = [&](Token *token) -> Result<BinaryOperand, ParseError> {
                if (token->type == TokenType::IDENTIFIER &&
                    tokens_iter->current_index < tokens_iter->elements.length &&
                    iter_peek(tokens_iter)->type == TokenType::PARENTHESIS_OPEN)
                {
                    int paren_depth = 0;
                    int64_t close_paren_index = iter_get_index_at_if<Token>(
                        tokens_iter, [&paren_depth](auto *t) {
                            if (t->type == TokenType::PARENTHESIS_OPEN)  { paren_depth++; return false; }
                            if (t->type == TokenType::PARENTHESIS_CLOSE) {
                                if (paren_depth == 1) { return true; }
                                paren_depth--;
                            }
                            return false;
                        }
                    );
                    auto arg_tokens_iter = iter_slice_by_offset(
                        tokens_iter,
                        tokens_iter->current_index + 1,
                        close_paren_index
                    );
                    ASTNode temp_node = {
                        .type = ASTNodeType::PROC_CALL,
                        .parent = nullptr,
                        .proc_call = { .caller_identifier = token->identifier.content },
                    };
                    if (!parse_proc_call_arguments(&arg_tokens_iter, &temp_node, nodes_block_iter, context, operands_iter, errors, token->position)) {
                        return err<BinaryOperand, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, token));
                    }
                    tokens_iter->current_index = close_paren_index + 1;
                    return ok<BinaryOperand, ParseError>(BinaryOperand {
                        .type = BinaryOperandType::PROC_CALL,
                        .proc_call = {
                            .caller_identifier = temp_node.proc_call.caller_identifier,
                            .arguments = temp_node.proc_call.arguments,
                        },
                    });
                }
                if (token->type == TokenType::IDENTIFIER &&
                    tokens_iter->current_index < tokens_iter->elements.length &&
                    iter_peek(tokens_iter)->type == TokenType::BRACKET_OPEN)
                {
                    (void)iter_next(tokens_iter); // consume [
                    auto *index_token = iter_next(tokens_iter);
                    if (index_token->type == TokenType::RANGE) {
                        // a[..] — whole array slice
                        auto *close_tok = iter_next(tokens_iter);
                        if (close_tok->type != TokenType::BRACKET_CLOSE) {
                            return err<BinaryOperand, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, close_tok));
                        }
                        return ok<BinaryOperand, ParseError>(BinaryOperand {
                            .type = BinaryOperandType::ARRAY_SLICE,
                            .array_slice = {
                                .variable_name = token->identifier.content,
                                .start_index = -1,
                                .end_index = -1,
                            },
                        });
                    }
                    if (index_token->type == TokenType::INTEGER_LITERAL &&
                        tokens_iter->current_index < tokens_iter->elements.length &&
                        iter_peek(tokens_iter)->type == TokenType::RANGE_EXCLUSIVE)
                    {
                        // a[N..<M] — exclusive range slice
                        (void)iter_next(tokens_iter); // consume ..<
                        auto *end_tok = iter_next(tokens_iter);
                        int64_t end_value = 0;
                        if (end_tok->type == TokenType::INTEGER_LITERAL) {
                            end_value = end_tok->integer_literal.value;
                        }
                        else if (end_tok->type == TokenType::IDENTIFIER) {
                            bool found = false;
                            for (size_t i = 0; i < context->constant_count; i++) {
                                if (str_equal(context->constants[i].name, end_tok->identifier.content)) {
                                    end_value = context->constants[i].value;
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                return err<BinaryOperand, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, end_tok));
                            }
                        }
                        else {
                            return err<BinaryOperand, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, end_tok));
                        }
                        auto *close_tok = iter_next(tokens_iter);
                        if (close_tok->type != TokenType::BRACKET_CLOSE) {
                            return err<BinaryOperand, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, close_tok));
                        }
                        return ok<BinaryOperand, ParseError>(BinaryOperand {
                            .type = BinaryOperandType::ARRAY_SLICE,
                            .array_slice = {
                                .variable_name = token->identifier.content,
                                .start_index = index_token->integer_literal.value,
                                .end_index = end_value,
                            },
                        });
                    }
                    if (index_token->type == TokenType::INTEGER_LITERAL &&
                        tokens_iter->current_index < tokens_iter->elements.length &&
                        iter_peek(tokens_iter)->type == TokenType::RANGE_COUNTED)
                    {
                        // a[N..+M] or a[N..+VAR] — counted range slice
                        (void)iter_next(tokens_iter); // consume ..+
                        auto *count_tok = iter_next(tokens_iter);
                        auto *close_tok = iter_next(tokens_iter);
                        if (close_tok->type != TokenType::BRACKET_CLOSE) {
                            return err<BinaryOperand, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, close_tok));
                        }
                        int64_t const start = index_token->integer_literal.value;
                        if (count_tok->type == TokenType::INTEGER_LITERAL) {
                            return ok<BinaryOperand, ParseError>(BinaryOperand {
                                .type = BinaryOperandType::ARRAY_SLICE,
                                .array_slice = {
                                    .variable_name = token->identifier.content,
                                    .start_index = start,
                                    .end_index = start + count_tok->integer_literal.value,
                                },
                            });
                        }
                        if (count_tok->type == TokenType::IDENTIFIER) {
                            return ok<BinaryOperand, ParseError>(BinaryOperand {
                                .type = BinaryOperandType::ARRAY_SLICE,
                                .array_slice = {
                                    .variable_name = token->identifier.content,
                                    .start_index = start,
                                    .end_index = -1,
                                    .count_identifier = count_tok->identifier.content,
                                },
                            });
                        }
                        return err<BinaryOperand, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, count_tok));
                    }
                    int64_t index_value = 0;
                    if (index_token->type == TokenType::INTEGER_LITERAL) {
                        index_value = index_token->integer_literal.value;
                    }
                    else if (index_token->type == TokenType::IDENTIFIER) {
                        bool found = false;
                        for (size_t i = 0; i < context->constant_count; i++) {
                            if (str_equal(context->constants[i].name, index_token->identifier.content)) {
                                index_value = context->constants[i].value;
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            return err<BinaryOperand, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, index_token));
                        }
                    }
                    else {
                        return err<BinaryOperand, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, index_token));
                    }
                    auto *close_token = iter_next(tokens_iter);
                    if (close_token->type != TokenType::BRACKET_CLOSE) {
                        return err<BinaryOperand, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, close_token));
                    }
                    return ok<BinaryOperand, ParseError>(BinaryOperand {
                        .type = BinaryOperandType::ARRAY_ACCESS,
                        .array_access = {
                            .variable_name = token->identifier.content,
                            .index = index_value,
                        },
                    });
                }
                if (token->type == TokenType::IDENTIFIER &&
                    tokens_iter->current_index < tokens_iter->elements.length &&
                    iter_peek(tokens_iter)->type == TokenType::DOT)
                {
                    (void)iter_next(tokens_iter); // consume .
                    auto *field_token = iter_next(tokens_iter);
                    if (field_token->type != TokenType::IDENTIFIER) {
                        return err<BinaryOperand, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, field_token));
                    }
                    Str const object_name = token->identifier.content;
                    Str const field_name = field_token->identifier.content;
                    for (size_t i = 0; i < nodes_block_iter->current_index; i++) {
                        auto const *n = &nodes_block_iter->elements.data[i];
                        if (n->type != ASTNodeType::ENUM_DEF || !str_equal(n->enum_def.name, object_name)) {
                            continue;
                        }
                        bool found = false;
                        for (size_t j = 0; j < n->enum_def.members.length; j++) {
                            if (str_equal(n->enum_def.members.data[j].name, field_name)) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            ParseError err_val = {
                                .code = ParseErrorCode::ENUM_INVALID_KEY,
                                .position = field_token->position,
                                .src_code_line = __LINE__,
                                .size_token_width = field_name.length,
                                .enum_invalid_key_name = field_name,
                                .enum_invalid_key_type = object_name,
                                .enum_member_count = n->enum_def.members.length < 32
                                    ? n->enum_def.members.length : 32,
                            };
                            for (size_t j = 0; j < err_val.enum_member_count; j++) {
                                err_val.enum_member_names[j] = n->enum_def.members.data[j].name;
                            }
                            return err<BinaryOperand, ParseError>(err_val);
                        }
                        break;
                    }
                    return ok<BinaryOperand, ParseError>(BinaryOperand {
                        .type = BinaryOperandType::MEMBER_ACCESS,
                        .member_access = {
                            .object_name = object_name,
                            .field_name = field_name,
                        },
                    });
                }
                if (token->type == TokenType::IDENTIFIER &&
                    tokens_iter->current_index < tokens_iter->elements.length &&
                    iter_peek(tokens_iter)->type == TokenType::CARET)
                {
                    (void)iter_next(tokens_iter); // consume ^
                    return ok<BinaryOperand, ParseError>(BinaryOperand {
                        .type = BinaryOperandType::DEREF,
                        .identifier = token->identifier.content,
                    });
                }
                if (token->type == TokenType::IDENTIFIER) {
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
                return err<BinaryOperand, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, token));
            };

            auto left_result = parse_operand(next_token);
            if (!is_ok(&left_result)) {
                return err<ASTNode, ParseError>(left_result.err);
            }
            auto left_operand = left_result.ok;

            // Peek for a binary operator; if found, collect all operands
            auto const peek_op = (tokens_iter->current_index < tokens_iter->elements.length)
                ? iter_peek(tokens_iter)->type
                : TokenType::UNKNOWN;
            if (peek_op == TokenType::ADD      || peek_op == TokenType::SUBTRACT ||
                peek_op == TokenType::MULTIPLY || peek_op == TokenType::DIVIDE) {
                TokenType const op_type = peek_op;
                if (op_type == TokenType::ADD &&
                    left_operand.type == BinaryOperandType::IDENTIFIER)
                {
                    Str const *iname = &left_operand.identifier;
                    for (size_t vi = context->var_type_count; vi-- > 0; ) {
                        Str const *vname = &context->var_types[vi].name;
                        if (context->var_types[vi].is_bool &&
                            vname->length == iname->length &&
                            strncmp(vname->data, iname->data, vname->length) == 0)
                        {
                            return err<ASTNode, ParseError>(ParseError {
                                .code = ParseErrorCode::BOOL_IN_ADDITION,
                                .position = next_token->position,
                                .src_code_line = __LINE__,
                                .size_token_width = iname->length,
                            });
                        }
                    }
                }

                size_t operands_begin = operands_iter->current_index;
                (void)iter_append(operands_iter, std::move(left_operand));

                while (tokens_iter->current_index < tokens_iter->elements.length &&
                       iter_peek(tokens_iter)->type == op_type)
                {
                    (void)iter_next(tokens_iter); // consume operator
                    Token *operand_token = iter_next(tokens_iter);
                    if (op_type == TokenType::ADD &&
                        (operand_token->type == TokenType::KEYWORD_TRUE ||
                         operand_token->type == TokenType::KEYWORD_FALSE))
                    {
                        size_t const bool_width =
                            (operand_token->type == TokenType::KEYWORD_TRUE) ? 4 : 5;
                        return err<ASTNode, ParseError>(ParseError {
                            .code = ParseErrorCode::BOOL_IN_ADDITION,
                            .position = operand_token->position,
                            .src_code_line = __LINE__,
                            .size_token_width = bool_width,
                        });
                    }
                    if (op_type == TokenType::ADD &&
                        operand_token->type == TokenType::IDENTIFIER)
                    {
                        Str const *iname = &operand_token->identifier.content;
                        for (size_t vi = context->var_type_count; vi-- > 0; ) {
                            Str const *vname = &context->var_types[vi].name;
                            if (context->var_types[vi].is_bool &&
                                vname->length == iname->length &&
                                strncmp(vname->data, iname->data, vname->length) == 0)
                            {
                                return err<ASTNode, ParseError>(ParseError {
                                    .code = ParseErrorCode::BOOL_IN_ADDITION,
                                    .position = operand_token->position,
                                    .src_code_line = __LINE__,
                                    .size_token_width = iname->length,
                                });
                            }
                        }
                    }
                    auto operand_result = parse_operand(operand_token);
                    if (!is_ok(&operand_result)) {
                        return err<ASTNode, ParseError>(operand_result.err);
                    }
                    (void)iter_append(operands_iter, std::move(operand_result.ok));
                }

                ASTNodeType const node_type =
                    (op_type == TokenType::MULTIPLY) ? ASTNodeType::BINARY_MUL :
                    (op_type == TokenType::DIVIDE)   ? ASTNodeType::BINARY_DIV :
                    (op_type == TokenType::SUBTRACT) ? ASTNodeType::BINARY_SUB :
                                                       ASTNodeType::BINARY_ADD;
                BinaryOperatorType const op =
                    (op_type == TokenType::MULTIPLY) ? BinaryOperatorType::MUL :
                    (op_type == TokenType::DIVIDE)   ? BinaryOperatorType::DIV :
                    (op_type == TokenType::SUBTRACT) ? BinaryOperatorType::SUB :
                                                       BinaryOperatorType::ADD;
                return ok<ASTNode, ParseError>(ASTNode {
                    .type = node_type,
                    .parent = nullptr,
                    .binary_operation = {
                        .oprt = op,
                        .operands = Array<BinaryOperand>(
                            operands_iter->elements.data + operands_begin,
                            operands_iter->current_index - operands_begin
                        ),
                    },
                });
            }

            // No binary operator — return single-operand expression
            switch (left_operand.type) {
                case BinaryOperandType::DEREF:
                    return ok<ASTNode, ParseError>(ASTNode {
                        .type = ASTNodeType::DEREF,
                        .parent = nullptr,
                        .identifier = left_operand.identifier,
                    });
                case BinaryOperandType::ARRAY_ACCESS:
                    return ok<ASTNode, ParseError>(ASTNode {
                        .type = ASTNodeType::ARRAY_ACCESS,
                        .parent = nullptr,
                        .array_access = {
                            .variable_name = left_operand.array_access.variable_name,
                            .index = left_operand.array_access.index,
                        },
                    });
                case BinaryOperandType::PROC_CALL:
                    return ok<ASTNode, ParseError>(ASTNode {
                        .type = ASTNodeType::PROC_CALL,
                        .parent = nullptr,
                        .proc_call = {
                            .arguments = left_operand.proc_call.arguments,
                            .caller_identifier = left_operand.proc_call.caller_identifier,
                        },
                    });
                case BinaryOperandType::IDENTIFIER:
                    return ok<ASTNode, ParseError>(ASTNode {
                        .type = ASTNodeType::IDENTIFIER,
                        .parent = nullptr,
                        .identifier = left_operand.identifier,
                    });
                case BinaryOperandType::MEMBER_ACCESS:
                    return ok<ASTNode, ParseError>(ASTNode {
                        .type = ASTNodeType::MEMBER_ACCESS,
                        .parent = nullptr,
                        .member_access = {
                            .object_name = left_operand.member_access.object_name,
                            .field_name = left_operand.member_access.field_name,
                        },
                    });
                case BinaryOperandType::ARRAY_SLICE:
                    return ok<ASTNode, ParseError>(ASTNode {
                        .type = ASTNodeType::ARRAY_SLICE,
                        .parent = nullptr,
                        .array_slice = {
                            .variable_name = left_operand.array_slice.variable_name,
                            .start_index = left_operand.array_slice.start_index,
                            .end_index = left_operand.array_slice.end_index,
                            .count_identifier = left_operand.array_slice.count_identifier,
                        },
                    });
                case BinaryOperandType::EXPR_NODE:
                    return ok<ASTNode, ParseError>(*left_operand.expr_node);
                default:
                    return ok<ASTNode, ParseError>(ASTNode {
                        .type = ASTNodeType::INTEGER_LITERAL,
                        .parent = nullptr,
                        .integer_literal = { .value = left_operand.integer_literal },
                    });
            }
        }
        case TokenType::KEYWORD_PROC: {
            // Expect procedure definition

            // Parse procedure parameters
            size_t params_start_index = proc_params_iter->current_index;
            if (!parse_proc_params(tokens_iter, proc_params_iter, errors)) {
                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, next_token));
            }

            // Parse procedure return type (if there is one)
            // - If the procedure params are followed by an arrow token immediately,
            //   then there is no return type.
            // - If the procedure params are followed by an identifier token before the
            //   arrow token, then that identifier token is the return type.
            // - If the procedure params are followed by a caret token, then the return
            //   type is a pointer to the following identifier.
            Token *proc_return_type_token = iter_next(tokens_iter);
            TypeASTNode *return_type_node = nullptr;
            if (proc_return_type_token->type == TokenType::ARROW) {
                // no return type
            }
            else if (proc_return_type_token->type == TokenType::CARET) {
                auto *type_name_token = iter_next(tokens_iter);
                if (type_name_token->type == TokenType::KEYWORD_CVOID) {
                    return_type_node = iter_append(types_iter, TypeASTNode {
                        .name = cstr_to_str("CVoid"),
                        .is_pointer = true,
                    });
                }
                else if (type_name_token->type != TokenType::IDENTIFIER) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, type_name_token));
                }
                else {
                    return_type_node = iter_append(types_iter, TypeASTNode {
                        .name = type_name_token->identifier.content,
                        .is_pointer = true,
                    });
                }
                if (
                    auto *arrow_tok = iter_next(tokens_iter);
                    arrow_tok->type != TokenType::ARROW
                ) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, arrow_tok));
                }
            }
            else if (proc_return_type_token->type == TokenType::KEYWORD_CVOID) {
                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, proc_return_type_token));
            }
            else if (proc_return_type_token->type == TokenType::IDENTIFIER) {
                if (
                    auto next_token = iter_next(tokens_iter);
                    next_token->type != TokenType::ARROW
                ) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, next_token));
                }

                return_type_node = iter_append(types_iter, TypeASTNode {
                    .name = proc_return_type_token->identifier.content,
                });
            }
            else if (proc_return_type_token->type == TokenType::BRACKET_OPEN) {
                auto *inner_tok = iter_next(tokens_iter);
                if (inner_tok->type == TokenType::INTEGER_LITERAL) {
                    // [N]Type return
                    auto *close_tok = iter_next(tokens_iter);
                    if (close_tok->type != TokenType::BRACKET_CLOSE) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, close_tok));
                    }
                    auto *type_name_tok = iter_next(tokens_iter);
                    if (type_name_tok->type != TokenType::IDENTIFIER) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, type_name_tok));
                    }
                    return_type_node = iter_append(types_iter, TypeASTNode {
                        .name = type_name_tok->identifier.content,
                        .is_pointer = false,
                        .is_array = true,
                        .array_length = inner_tok->integer_literal.value,
                    });
                }
                else if (inner_tok->type == TokenType::BRACKET_CLOSE) {
                    // []Type return — slice
                    auto *type_name_tok = iter_next(tokens_iter);
                    if (type_name_tok->type != TokenType::IDENTIFIER) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, type_name_tok));
                    }
                    return_type_node = iter_append(types_iter, TypeASTNode {
                        .name = type_name_tok->identifier.content,
                        .is_pointer = false,
                        .is_array = false,
                        .is_slice = true,
                    });
                }
                else {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, inner_tok));
                }
                if (
                    auto *arrow_tok = iter_next(tokens_iter);
                    arrow_tok->type != TokenType::ARROW
                ) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, arrow_tok));
                }
            }
            bool is_foreign_proc = false;
            {
                auto *after_arrow = iter_next(tokens_iter);
                if (after_arrow->type == TokenType::KEYWORD_FOREIGN) {
                    is_foreign_proc = true;
                    auto *nl = iter_next(tokens_iter);
                    if (nl->type != TokenType::NEWLINE && nl->type != TokenType::END) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, nl));
                    }
                }
                else if (after_arrow->type != TokenType::NEWLINE) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, after_arrow));
                }
            }

            auto proc_node = iter_append(nodes_block_iter, ASTNode {
                .type = ASTNodeType::PROC_DEF,
                .parent = nullptr,
                .proc_def = {
                    .name = context->current_identifier->identifier.content,
                    .parameters = Array<ProcParameterASTNode>(
                        proc_params_block->data + params_start_index,
                        proc_params_iter->current_index - params_start_index
                    ),
                    .return_type = return_type_node,
                    .body = Array<ASTNode>(
                        context->nodes_block->data + nodes_block_iter->current_index + 1,
                        0 // Will be updated later
                    ),
                    .is_foreign = is_foreign_proc,
                },
            });

            if (is_foreign_proc) {
                return ok<ASTNode, ParseError>(*proc_node);
            }

            // Parse procedure body
            // - Expect each line to be indented and contain a single statement
            ASTNode *saved_proc_node = context->current_proc_node;
            context->current_proc_node = proc_node;
            while(tokens_iter->current_index < tokens_iter->elements.length) {
                // Skip blank lines
                if (iter_peek(tokens_iter)->type == TokenType::NEWLINE) {
                    (void)iter_next(tokens_iter);
                    continue;
                }
                // If the line doesn't begin with an indent token, the procedure body has ended
                if (iter_peek(tokens_iter)->type != TokenType::INDENT) {
                    break;
                }
                auto *indent_token = iter_next(tokens_iter); // Consume the indent token

                if (!parse_statement(
                    tokens_iter,
                    context,
                    nodes_block_iter,
                    proc_node,
                    proc_params_block,
                    proc_params_iter,
                    types_iter,
                    operands_iter,
                    array_elements_iter,
                    errors,
                    indent_token->indent.level
                )) {
                    return err<ASTNode, ParseError>(
                        PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, iter_peek_prev(tokens_iter))
                    );
                }

                // Now, at the end of a statement, the previous token
                // should be either a newline or an end token
                #if ASSERTIONS_ENABLED
                    auto *prev_token = iter_peek_prev(tokens_iter);
                    auto prev_token_str = to_string(prev_token->type);
                    assertf(
                        (prev_token->type == TokenType::NEWLINE ||
                        prev_token->type == TokenType::END),
                        "Expected newline or end token after procedure body statement, but got % at %:%\n",
                        prev_token_str,
                        prev_token->position.line,
                        prev_token->position.col
                    );
                #endif // ASSERTIONS_ENABLED
            }

            context->current_proc_node = saved_proc_node;

            // Update the procedure body length so that it includes all parsed body statements
            proc_node->proc_def.body.length =
                nodes_block_iter->current_index
                - ptr_sub(
                    proc_node->proc_def.body.data,
                    nodes_block_iter->elements.data
                );

            assert(proc_node->proc_def.body.length > 0 &&
                "Procedure body should contain at least one statement");
            assert(
                nodes_block_iter->elements[nodes_block_iter->current_index].type == ASTNodeType::UNKNOWN &&
                "Next node after procedure body should be of UNKNOWN type");

            if (return_type_node != nullptr) {
                ReturnAnalysis analysis = check_returns_on_all_paths(
                    proc_node->proc_def.body, proc_node, nodes_block_iter
                );
                if (!analysis.returns) {
                    ParseError missing_ret_err = {};
                    missing_ret_err.code = ParseErrorCode::PROC_MISSING_RETURN;
                    missing_ret_err.position = context->current_identifier->position;
                    missing_ret_err.src_code_line = __LINE__;
                    missing_ret_err.token_type = context->current_identifier->type;
                    missing_ret_err.size_token_width = proc_node->proc_def.name.length;
                    missing_ret_err.missing_return_proc_name = proc_node->proc_def.name;
                    missing_ret_err.missing_return_type_name = return_type_node->name;
                    missing_ret_err.missing_return_branch_pos = analysis.failing_pos;
                    missing_ret_err.missing_return_has_branch_pos = analysis.has_failing_pos;
                    missing_ret_err.missing_return_is_after_stmt = analysis.failing_is_after_stmt;
                    append(errors, missing_ret_err);
                }
            }

            return ok<ASTNode, ParseError>(*proc_node);
        }
        case TokenType::ADDRESS_OF: {
            auto *ident_tok = iter_next(tokens_iter);
            if (ident_tok->type != TokenType::IDENTIFIER) {
                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, ident_tok));
            }
            return ok<ASTNode, ParseError>(ASTNode {
                .type = ASTNodeType::ADDRESS_OF,
                .parent = nullptr,
                .identifier = ident_tok->identifier.content,
            });
        }
        case TokenType::KEYWORD_STRUCT: {
            auto *arrow = iter_next(tokens_iter);
            if (arrow->type != TokenType::ARROW) {
                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, arrow));
            }
            auto *newline = iter_next(tokens_iter);
            if (newline->type != TokenType::NEWLINE) {
                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, newline));
            }

            size_t const fields_start = proc_params_iter->current_index;

            while (tokens_iter->current_index < tokens_iter->elements.length &&
                   iter_peek(tokens_iter)->type == TokenType::INDENT)
            {
                (void)iter_next(tokens_iter); // consume indent
                auto *field_name_token = iter_next(tokens_iter);
                if (field_name_token->type != TokenType::IDENTIFIER) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, field_name_token));
                }
                auto *colon = iter_next(tokens_iter);
                if (colon->type != TokenType::TYPE_SEPARATOR) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, colon));
                }
                auto *maybe_caret = iter_next(tokens_iter);
                bool field_is_ptr = false;
                bool field_is_dynamic_array = false;
                Str field_type_name = {};
                if (maybe_caret->type == TokenType::CARET) {
                    field_is_ptr = true;
                    auto *type_token = iter_next(tokens_iter);
                    if (type_token->type == TokenType::KEYWORD_CVOID) {
                        field_type_name = cstr_to_str("CVoid");
                    }
                    else if (type_token->type != TokenType::IDENTIFIER) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, type_token));
                    }
                    else {
                        field_type_name = type_token->identifier.content;
                    }
                }
                else if (maybe_caret->type == TokenType::BRACKET_OPEN) {
                    auto *dynamic_kw = iter_next(tokens_iter);
                    if (dynamic_kw->type != TokenType::KEYWORD_DYNAMIC) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, dynamic_kw));
                    }
                    auto *bracket_close = iter_next(tokens_iter);
                    if (bracket_close->type != TokenType::BRACKET_CLOSE) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, bracket_close));
                    }
                    auto *type_token = iter_next(tokens_iter);
                    if (type_token->type != TokenType::IDENTIFIER) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, type_token));
                    }
                    field_is_dynamic_array = true;
                    field_type_name = type_token->identifier.content;
                }
                else if (maybe_caret->type != TokenType::IDENTIFIER) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, maybe_caret));
                }
                else {
                    field_type_name = maybe_caret->identifier.content;
                }
                (void)iter_append(proc_params_iter, ProcParameterASTNode {
                    .name = field_name_token->identifier.content,
                    .type_name = field_type_name,
                    .is_pointer = field_is_ptr,
                    .is_dynamic_array = field_is_dynamic_array,
                });
                if (tokens_iter->current_index < tokens_iter->elements.length) {
                    auto *nl = iter_peek(tokens_iter);
                    if (nl->type == TokenType::NEWLINE || nl->type == TokenType::END) {
                        (void)iter_next(tokens_iter);
                    }
                }
            }

            auto *struct_node = iter_append(nodes_block_iter, ASTNode {
                .type = ASTNodeType::STRUCT_DEF,
                .parent = nullptr,
                .struct_def = {
                    .name = context->current_identifier->identifier.content,
                    .fields = Array<ProcParameterASTNode>(
                        proc_params_block->data + fields_start,
                        proc_params_iter->current_index - fields_start
                    ),
                },
            });

            return ok<ASTNode, ParseError>(*struct_node);
        }
        case TokenType::KEYWORD_ENUM: {
            bool is_str_typed = false;
            if (tokens_iter->current_index < tokens_iter->elements.length &&
                iter_peek(tokens_iter)->type == TokenType::IDENTIFIER &&
                iter_peek(tokens_iter)->identifier.content == "Str")
            {
                is_str_typed = true;
                (void)iter_next(tokens_iter); // consume "Str"
            }

            auto *arrow = iter_next(tokens_iter);
            if (arrow->type != TokenType::ARROW) {
                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, arrow));
            }
            auto *newline = iter_next(tokens_iter);
            if (newline->type != TokenType::NEWLINE) {
                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, newline));
            }

            size_t const members_start = proc_params_iter->current_index;

            while (tokens_iter->current_index < tokens_iter->elements.length &&
                   iter_peek(tokens_iter)->type == TokenType::INDENT)
            {
                (void)iter_next(tokens_iter); // consume indent
                auto *member_name_token = iter_next(tokens_iter);
                if (member_name_token->type != TokenType::IDENTIFIER) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, member_name_token));
                }
                Str member_str_value = {};
                if (is_str_typed) {
                    auto *eq = iter_next(tokens_iter);
                    if (eq->type != TokenType::EQUALS) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, eq));
                    }
                    auto *str_tok = iter_next(tokens_iter);
                    if (str_tok->type != TokenType::STRING_LITERAL) {
                        return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, str_tok));
                    }
                    member_str_value = str_tok->string_literal.content;
                }
                (void)iter_append(proc_params_iter, ProcParameterASTNode {
                    .name = member_name_token->identifier.content,
                    .type_name = member_str_value,
                    .is_pointer = false,
                });
                if (tokens_iter->current_index < tokens_iter->elements.length) {
                    auto *nl = iter_peek(tokens_iter);
                    if (nl->type == TokenType::NEWLINE || nl->type == TokenType::END) {
                        (void)iter_next(tokens_iter);
                    }
                }
            }

            auto *enum_node = iter_append(nodes_block_iter, ASTNode {
                .type = ASTNodeType::ENUM_DEF,
                .parent = nullptr,
                .enum_def = {
                    .name = context->current_identifier->identifier.content,
                    .members = Array<ProcParameterASTNode>(
                        proc_params_block->data + members_start,
                        proc_params_iter->current_index - members_start
                    ),
                    .is_str_typed = is_str_typed,
                },
            });

            return ok<ASTNode, ParseError>(*enum_node);
        }
        case TokenType::CARET: {
            auto *cvoid_tok = iter_next(tokens_iter);
            if (cvoid_tok->type != TokenType::KEYWORD_CVOID) {
                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, cvoid_tok));
            }
            if (tokens_iter->current_index >= tokens_iter->elements.length ||
                iter_peek(tokens_iter)->type != TokenType::PARENTHESIS_OPEN)
            {
                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, cvoid_tok));
            }
            // current_index points to '(' — scan for matching ')' without consuming '(' yet
            int paren_depth = 0;
            int64_t close_paren_index = iter_get_index_at_if<Token>(
                tokens_iter, [&paren_depth](auto *t) {
                    if (t->type == TokenType::PARENTHESIS_OPEN)  { paren_depth++; return false; }
                    if (t->type == TokenType::PARENTHESIS_CLOSE) {
                        if (paren_depth == 1) { return true; }
                        paren_depth--;
                    }
                    return false;
                }
            );
            auto arg_tokens_iter = iter_slice_by_offset(
                tokens_iter,
                tokens_iter->current_index + 1,  // skip '('
                close_paren_index
            );
            ASTNode temp_node = {
                .type = ASTNodeType::PROC_CALL,
                .parent = nullptr,
                .proc_call = { .caller_identifier = cstr_to_str("^CVoid") },
            };
            if (!parse_proc_call_arguments(&arg_tokens_iter, &temp_node, nodes_block_iter, context, operands_iter, errors, next_token->position)) {
                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, cvoid_tok));
            }
            tokens_iter->current_index = close_paren_index + 1;
            return ok<ASTNode, ParseError>(ASTNode {
                .type = ASTNodeType::PROC_CALL,
                .parent = nullptr,
                .proc_call = {
                    .arguments = temp_node.proc_call.arguments,
                    .caller_identifier = temp_node.proc_call.caller_identifier,
                },
            });
        }
        default:
            return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, next_token));
    }
}

// Infer the Bloom type name of the expression described by the tokens in expr_iter.
// Handles: plain type names, plain identifiers (looked up from AST nodes),
// array-element access (identifier[N]), member access (.field), and chains of both.
auto infer_bloom_type_from_tokens(
    Iterator<Token> *expr_iter,
    Context *context,
    Iterator<ASTNode> const *nodes_block_iter
) -> Str {
    if (expr_iter->current_index >= expr_iter->elements.length) {
        return {};
    }
    auto *tok = iter_next(expr_iter);
    // Literal expressions with known types
    if (tok->type == TokenType::STRING_LITERAL &&
        expr_iter->current_index >= expr_iter->elements.length)
    {
        return { .data = "Str", .length = 3 };
    }
    if (tok->type == TokenType::INTEGER_LITERAL &&
        expr_iter->current_index >= expr_iter->elements.length)
    {
        return { .data = "Int", .length = 3 };
    }
    if (tok->type != TokenType::IDENTIFIER) {
        return {};
    }
    Str name = tok->identifier.content;

    // If the expression is just a primitive/builtin type name, return it directly.
    static char const *const BUILTIN_TYPES[] = { "Int", "U8", "Bool", "Str", "CStr", "CVoid" };
    for (auto const *bt : BUILTIN_TYPES) {
        if (name == bt &&
            expr_iter->current_index >= expr_iter->elements.length)
        {
            return name;
        }
    }

    // If the expression is a struct type name used directly, return it immediately.
    for (size_t i = 0; i < nodes_block_iter->current_index; i++) {
        auto const *node = &nodes_block_iter->elements.data[i];
        if (node->type == ASTNodeType::STRUCT_DEF &&
            str_equal(node->struct_def.name, name) &&
            expr_iter->current_index >= expr_iter->elements.length)
        {
            return name;
        }
    }

    // If the expression is an enum type name used directly, return it immediately.
    for (size_t i = 0; i < nodes_block_iter->current_index; i++) {
        auto const *node = &nodes_block_iter->elements.data[i];
        if (node->type == ASTNodeType::ENUM_DEF &&
            str_equal(node->enum_def.name, name) &&
            expr_iter->current_index >= expr_iter->elements.length)
        {
            return name;
        }
    }

    // Look up the variable definition in already-parsed nodes.
    Str current_type = {};
    for (size_t i = 0; i < nodes_block_iter->current_index; i++) {
        auto const *node = &nodes_block_iter->elements.data[i];
        if (node->type == ASTNodeType::VARIABLE_DEFINITION &&
            str_equal(node->variable_definition.name, name))
        {
            ASTNode const *expr = node->variable_definition.expr;
            if (expr->type == ASTNodeType::ARRAY_STRUCT_INIT) {
                current_type = expr->array_struct_init.element_type;
            }
            else if (expr->type == ASTNodeType::STRUCT_INIT) {
                current_type = expr->struct_init.type_name;
            }
            else if (expr->type == ASTNodeType::ARRAY_INIT) {
                current_type = expr->array_init.element_type;
            }
            else if (expr->type == ASTNodeType::MEMBER_ACCESS) {
                // Check if object_name is an enum type
                for (size_t j = 0; j < nodes_block_iter->current_index; j++) {
                    auto const *n = &nodes_block_iter->elements.data[j];
                    if (n->type == ASTNodeType::ENUM_DEF &&
                        str_equal(n->enum_def.name, expr->member_access.object_name))
                    {
                        current_type = n->enum_def.name;
                        break;
                    }
                }
            }
            break;
        }
    }
    // If not found as a variable definition, check the current proc's parameters.
    if (current_type.length == 0 && context != nullptr && context->current_proc_node != nullptr) {
        auto const &params = context->current_proc_node->proc_def.parameters;
        for (size_t pi = 0; pi < params.length; pi++) {
            auto const &param = params.data[pi];
            if (str_equal(param.name, name)) {
                if (param.is_slice || param.is_array) {
                    current_type = param.type_name;
                }
                break;
            }
        }
    }

    if (current_type.length == 0) {
        return {};
    }

    // Consume optional array-element access: [N]
    if (expr_iter->current_index < expr_iter->elements.length &&
        iter_peek(expr_iter)->type == TokenType::BRACKET_OPEN)
    {
        (void)iter_next(expr_iter); // consume [
        while (expr_iter->current_index < expr_iter->elements.length &&
               iter_peek(expr_iter)->type != TokenType::BRACKET_CLOSE)
        {
            (void)iter_next(expr_iter);
        }
        if (expr_iter->current_index < expr_iter->elements.length) {
            (void)iter_next(expr_iter); // consume ]
        }
        // current_type is the element type — unchanged.
    }

    // Consume optional member access: .field
    if (expr_iter->current_index < expr_iter->elements.length &&
        iter_peek(expr_iter)->type == TokenType::DOT)
    {
        (void)iter_next(expr_iter); // consume .
        if (expr_iter->current_index >= expr_iter->elements.length) {
            return {};
        }
        auto *field_tok = iter_next(expr_iter);
        if (field_tok->type != TokenType::IDENTIFIER) {
            return {};
        }
        // Find the struct definition matching current_type.
        for (size_t i = 0; i < nodes_block_iter->current_index; i++) {
            auto const *node = &nodes_block_iter->elements.data[i];
            if (node->type == ASTNodeType::STRUCT_DEF &&
                str_equal(node->struct_def.name, current_type))
            {
                for (size_t fi = 0; fi < node->struct_def.fields.length; fi++) {
                    auto const *field = &node->struct_def.fields.data[fi];
                    if (str_equal(field->name, field_tok->identifier.content)) {
                        return field->type_name;
                    }
                }
                return {};
            }
        }
        return {};
    }

    return current_type;
}

// Returns the size in bytes for a Bloom built-in type, or -1 if unknown.
auto bloom_type_size_in_bytes(Str type_name) -> int64_t {
    if (type_name == "Int")  { return 4; }
    if (type_name == "U8")   { return 1; }
    if (type_name == "Bool") { return 1; }
    if (type_name == "Str")  { return 16; } // sizeof(BloomStr): data ptr + length
    if (type_name == "CStr")    { return 8; }  // pointer on 64-bit
    if (type_name == "CVoid") { return 8; }  // void pointer on 64-bit
    return -1;
}

// Returns the C type name string for a Bloom type, used in sizeof() emission.
auto bloom_type_to_c_type(Str type_name) -> char const * {
    if (type_name == "Int")  { return "int"; }
    if (type_name == "U8")   { return "uint8_t"; }
    if (type_name == "Bool") { return "bool"; }
    if (type_name == "Str")  { return "BloomStr"; }
    if (type_name == "CStr")    { return "char const *"; }
    if (type_name == "CVoid") { return "void *"; }
    return nullptr;
}

auto eval_const_primary(
    Iterator<Token> *tokens_iter,
    Context *context,
    Iterator<ASTNode> const *nodes_block_iter,
    DynamicArray<ParseError> *errors,
    int64_t *out_value
) -> bool {
    if (tokens_iter->current_index >= tokens_iter->elements.length) {
        return false;
    }
    auto *tok = iter_next(tokens_iter);
    if (tok->type == TokenType::INTEGER_LITERAL) {
        *out_value = tok->integer_literal.value;
        return true;
    }
    if (tok->type == TokenType::IDENTIFIER) {
        // type_info_of(expr).size_in_bytes — compile-time size query
        if (tok->identifier.content == "type_info_of" &&
            tokens_iter->current_index < tokens_iter->elements.length &&
            iter_peek(tokens_iter)->type == TokenType::PARENTHESIS_OPEN)
        {
            (void)iter_next(tokens_iter); // consume (
            // Find the matching close paren.
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
                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, tok));
                return false;
            }
            auto inner_iter = iter_slice_by_offset(
                tokens_iter, inner_start, static_cast<int64_t>(inner_end));
            tokens_iter->current_index = inner_end + 1; // skip past )
            // Infer the type of the inner expression.
            Str const type_name = infer_bloom_type_from_tokens(
                &inner_iter, context, nodes_block_iter);
            if (type_name.length == 0) {
                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, tok));
                return false;
            }
            // Consume .size_in_bytes
            if (tokens_iter->current_index >= tokens_iter->elements.length ||
                iter_next(tokens_iter)->type != TokenType::DOT)
            {
                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, tok));
                return false;
            }
            if (tokens_iter->current_index >= tokens_iter->elements.length) {
                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, tok));
                return false;
            }
            auto *field_tok = iter_next(tokens_iter);
            if (field_tok->type != TokenType::IDENTIFIER ||
                !(field_tok->identifier.content == "size_in_bytes"))
            {
                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, field_tok));
                return false;
            }
            int64_t const sz = bloom_type_size_in_bytes(type_name);
            if (sz < 0) {
                append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, tok));
                return false;
            }
            *out_value = sz;
            return true;
        }
        // Regular constant reference
        for (size_t i = 0; i < context->constant_count; i++) {
            Str const *cname = &context->constants[i].name;
            if (cname->length == tok->identifier.content.length &&
                strncmp(cname->data, tok->identifier.content.data, cname->length) == 0)
            {
                *out_value = context->constants[i].value;
                return true;
            }
        }
    }
    append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, tok));
    return false;
}

auto eval_const_multiplicative(
    Iterator<Token> *tokens_iter,
    Context *context,
    Iterator<ASTNode> const *nodes_block_iter,
    DynamicArray<ParseError> *errors,
    int64_t *out_value
) -> bool {
    if (!eval_const_primary(tokens_iter, context, nodes_block_iter, errors, out_value)) {
        return false;
    }
    while (tokens_iter->current_index < tokens_iter->elements.length) {
        TokenType const op = iter_peek(tokens_iter)->type;
        if (op != TokenType::MULTIPLY && op != TokenType::DIVIDE) {
            break;
        }
        (void)iter_next(tokens_iter);
        int64_t rhs = 0;
        if (!eval_const_primary(tokens_iter, context, nodes_block_iter, errors, &rhs)) {
            return false;
        }
        if (op == TokenType::MULTIPLY) {
            *out_value *= rhs;
        }
        else {
            if (rhs == 0) {
                return false;
            }
            *out_value /= rhs;
        }
    }
    return true;
}

auto eval_const_additive(
    Iterator<Token> *tokens_iter,
    Context *context,
    Iterator<ASTNode> const *nodes_block_iter,
    DynamicArray<ParseError> *errors,
    int64_t *out_value
) -> bool {
    if (!eval_const_multiplicative(tokens_iter, context, nodes_block_iter, errors, out_value)) {
        return false;
    }
    while (tokens_iter->current_index < tokens_iter->elements.length) {
        TokenType const op = iter_peek(tokens_iter)->type;
        if (op != TokenType::ADD && op != TokenType::SUBTRACT) {
            break;
        }
        (void)iter_next(tokens_iter);
        int64_t rhs = 0;
        if (!eval_const_multiplicative(tokens_iter, context, nodes_block_iter, errors, &rhs)) {
            return false;
        }
        if (op == TokenType::ADD) {
            *out_value += rhs;
        }
        else {
            *out_value -= rhs;
        }
    }
    return true;
}
