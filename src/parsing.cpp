#include <functional>
#include <bloom/assert.h>
#include <bloom/error-reporting.h>
#include <bloom/print.h>
#include <bloom/ptr.h>
#include <bloom/result.h>
#include <bloom/parsing.h>

/**
 * Converts an AllocatedArrayBlock to an Array.
 */
template<typename PointerT>
static inline auto to_array(AllocatedArrayBlock<PointerT> *block) -> Array<PointerT> {
    return Array<PointerT>(block->data, block->length);
}

template<typename ElementType>
struct Iterator {
    Array<ElementType> elements;
    size_t current_index;
};

template<typename ElementType>
static inline auto to_array(Iterator<ElementType> *iter) -> Array<ElementType> {
    return Array<ElementType>(
        iter->elements.data,
        iter->current_index
    );
}

template<typename ElementType>
static inline auto to_iterator(AllocatedArrayBlock<ElementType> *block) -> Iterator<ElementType> {
    return {
        .elements = to_array(block),
        .current_index = 0,
    };
}
static inline auto to_iterator(Array<Token> *tokens) -> Iterator<Token> {
    return {
        .elements = *tokens,
        .current_index = 0,
    };
}

/**
 * Advances the iterator and sets the next element to the given value, and returns it.
 */
template<typename ElementType>
static auto iter_append(
    Iterator<ElementType> *iter,
    ElementType &&value
) -> ElementType* {
    auto next_elem = iter_next(iter);
    *next_elem = value;
    return next_elem;
}

template<typename ElementType>
static auto iter_current(Iterator<ElementType> *iter) -> ElementType* {
    return &iter->elements.data[iter->current_index];
}

/**
 * Peeks until the given condition is met, returning the index of the element that meets the condition.
 * If no such element is found, returns -1.
 */
template<typename ElementType>
static auto iter_get_index_at_if(
    Iterator<ElementType> *iter,
    std::function<bool(ElementType const*)> &&condition_fn
) -> int64_t {
    size_t start_index = iter->current_index;
    for (size_t i = start_index; i < iter->elements.length; i++) {
        if (condition_fn(&iter->elements.data[i])) {
            return static_cast<int64_t>(i);
        }
    }
    return -1;
}

/**
 * Advances the iterator and returns the next element.
 */
template<typename ElementType>
static auto iter_next(Iterator<ElementType> *iter) -> ElementType* {
    assert (iter->elements.length > 0 &&
        "Cannot advance iterator because the iterator length is 0.");
    assertf(
        iter->current_index < iter->elements.length,
        "Iterator out of bounds while advancing to the next element:\n"
        "\tElement type size: %\n"
        "\tCurrent index: %\n"
        "\tMax length: %",
        sizeof(ElementType), iter->current_index, iter->elements.length
    );
    return &iter->elements.data[iter->current_index++];
}

/**
 * Peeks at the current element without advancing the iterator.
 */
template<typename ElementType>
static inline auto iter_peek(Iterator<ElementType> *iter) -> ElementType* {
    assert(iter->current_index < iter->elements.length &&
        "Iterator out of bounds while peeking");
    return &iter->elements.data[iter->current_index];
}

/**
 * Peeks at the previous element without changing the iterator.
 */
template<typename ElementType>
static inline auto iter_peek_prev(Iterator<ElementType> *iter) -> ElementType* {
    assert(iter->current_index > 0 &&
        "Iterator out of bounds while peeking prev");
    return &iter->elements.data[iter->current_index - 1];
}

/**
 * Creates a new iterator that is a slice of the given iterator from begin to end offsets.
 */
template<typename ElementType>
static auto iter_slice_by_offset(
    Iterator<ElementType> *iter,
    size_t begin,
    int64_t end
) -> Iterator<ElementType> {
    // Copy the iterator
    auto new_iter = *iter;
    assert(end >= 0 && begin <= end && end <= iter->elements.length &&
        "Slice end out of bounds");
    new_iter.elements = slice_by_offset(&new_iter.elements, begin, static_cast<size_t>(end));
    new_iter.current_index = 0;
    return new_iter;
}

/**
 * Tries to advance the iterator and returns the next element.
 * If the next element is not found, null is returned.
 */
template<typename ElementType>
static auto iter_try_next(Iterator<ElementType> *iter) -> ElementType* {
    if (iter->current_index >= iter->elements.length) {
        return nullptr;
    }
    return &iter->elements.data[iter->current_index++];
}

template<typename ElementType>
struct DynamicArray {
    ElementType *data;
    size_t length;
    size_t max_length;

    DynamicArray(AllocatedArrayBlock<ElementType> *target_block):
        data(target_block->data),
        length(0),
        max_length(target_block->length)
    {}
};

template<typename ElementType>
static auto append(DynamicArray<ElementType> *array, ElementType value) -> void {
    assert(array->length < array->max_length &&
        "DynamicArray out of capacity");
    array->data[array->length++] = value;
}

/**
 * Converts a DynamicArray to an Array.
 */
template<typename T>
static inline auto to_array(DynamicArray<T> *value) -> Array<T> {
    return Array<T>(value->data, value->length);
}

struct Context {
    Token *current_identifier = nullptr;
    ASTNode *current_proc_node = nullptr;
    bool in_proc_definition = false;
    AllocatedArrayBlock<ASTNode> *nodes_block;
    struct ConstEntry { Str name; int64_t value; };
    ConstEntry constants[64] = {};
    size_t constant_count = 0;
    struct VarTypeEntry { Str name; bool is_bool; };
    VarTypeEntry var_types[128] = {};
    size_t var_type_count = 0;
    struct ArraySizeEntry { Str name; int64_t size; };
    ArraySizeEntry array_sizes[64] = {};
    size_t array_size_count = 0;
};

// For debugging purposes
static auto print_value(FILE *file, Context *context) -> void {
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

static inline auto str_equal(Str a, Str b) -> bool {
    return a.length == b.length && strncmp(a.data, b.data, a.length) == 0;
}

static auto expect_token_or_append_error(
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

static auto expect_arrow_newline(
    Iterator<Token> *tokens_iter,
    DynamicArray<ParseError> *errors
) -> bool {
    if (expect_token_or_append_error(tokens_iter, TokenType::ARROW, errors) == nullptr) { return false; }
    if (expect_token_or_append_error(tokens_iter, TokenType::NEWLINE, errors) == nullptr) { return false; }
    return true;
}

static auto slice_expression_tokens(
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

static auto find_proc_def_node(
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

static auto infer_arg_type_name(ASTNode const *arg, Context const *context) -> Str {
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

static auto infer_bloom_type_from_tokens(
    Iterator<Token> *expr_iter,
    Context *context,
    Iterator<ASTNode> const *nodes_block_iter
) -> Str;

/**
 * Parses procedure call parameters and appends them to the given procedure call AST node.
 *
 * @return true on success, false on failure.
 */
static auto parse_proc_call_arguments(
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
                        if (tokens_iter->current_index >= tokens_iter->elements.length ||
                            iter_peek(tokens_iter)->type != TokenType::INTEGER_LITERAL)
                        {
                            return err<BinaryOperand, ParseError>(ParseError {
                                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                                .position = idx_tok->position,
                                .src_code_line = __LINE__,
                                .token_type = idx_tok->type,
                            });
                        }
                        int64_t const end_index = iter_next(tokens_iter)->integer_literal.value;
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
        if (proc_def != nullptr &&
            !proc_def->proc_def.is_foreign &&
            arg_count < proc_def->proc_def.parameters.length)
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
static auto parse_proc_params(
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

                if (
                    auto next_token = iter_next(tokens_iter);
                    next_token->type != TokenType::TYPE_SEPARATOR
                ) {
                    append(errors, ParseError {
                        .code = ParseErrorCode::UNEXPECTED_TOKEN,
                        .position = iter_current(tokens_iter)->position,
                        .src_code_line = __LINE__,
                        .token_type = iter_peek_prev(tokens_iter)->type,
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

static auto parse_statement(
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
) -> bool;

#define PARSE_ERROR_CREATE(error_code, token) \
    ParseError { .code = ParseErrorCode::error_code, .position = token->position, .src_code_line = __LINE__, .token_type = token->type }

static auto parse_indented_body(
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

// Forward declaration — defined after eval_const_* helpers below parse_statement.
static auto infer_bloom_type_from_tokens(
    Iterator<Token> *expr_iter,
    Context *context,
    Iterator<ASTNode> const *nodes_block_iter
) -> Str;

// Forward declaration — defined after parse_statement.
static auto eval_const_additive(
    Iterator<Token> *tokens_iter,
    Context *context,
    Iterator<ASTNode> const *nodes_block_iter,
    DynamicArray<ParseError> *errors,
    int64_t *out_value
) -> bool;

static auto parse_expression(
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
                        case TokenType::IDENTIFIER:
                            (void)iter_append(proc_params_iter, ProcParameterASTNode {
                                .name = field_name,
                                .type_name = {},
                            });
                            (void)iter_append(operands_iter, BinaryOperand {
                                .type = BinaryOperandType::IDENTIFIER,
                                .identifier = value_tok->identifier.content,
                            });
                            break;
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
                        // Pointer-typed fields default to NULL and are not required
                        size_t required_count = 0;
                        for (size_t di = 0; di < struct_def_node->struct_def.fields.length; di++) {
                            if (!struct_def_node->struct_def.fields.data[di].is_pointer) {
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
                                missing_err.struct_field_is_missing[di] = !found && !def_field->is_pointer;
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
                        if (end_tok->type != TokenType::INTEGER_LITERAL) {
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
                                .end_index = end_tok->integer_literal.value,
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
                    if (index_token->type != TokenType::INTEGER_LITERAL) {
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
                            .index = index_token->integer_literal.value,
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
                if (type_name_token->type != TokenType::IDENTIFIER) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, type_name_token));
                }
                return_type_node = iter_append(types_iter, TypeASTNode {
                    .name = type_name_token->identifier.content,
                    .is_pointer = true,
                });
                if (
                    auto *arrow_tok = iter_next(tokens_iter);
                    arrow_tok->type != TokenType::ARROW
                ) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, arrow_tok));
                }
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
                Token *type_token;
                if (maybe_caret->type == TokenType::CARET) {
                    field_is_ptr = true;
                    type_token = iter_next(tokens_iter);
                }
                else {
                    type_token = maybe_caret;
                }
                if (type_token->type != TokenType::IDENTIFIER) {
                    return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, type_token));
                }
                (void)iter_append(proc_params_iter, ProcParameterASTNode {
                    .name = field_name_token->identifier.content,
                    .type_name = type_token->identifier.content,
                    .is_pointer = field_is_ptr,
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
                (void)iter_append(proc_params_iter, ProcParameterASTNode {
                    .name = member_name_token->identifier.content,
                    .type_name = {},
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
                },
            });

            return ok<ASTNode, ParseError>(*enum_node);
        }
        default:
            return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, next_token));
    }
}

// Infer the Bloom type name of the expression described by the tokens in expr_iter.
// Handles: plain type names, plain identifiers (looked up from AST nodes),
// array-element access (identifier[N]), member access (.field), and chains of both.
static auto infer_bloom_type_from_tokens(
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
    static char const *const BUILTIN_TYPES[] = { "Int", "U8", "Bool", "Str", "CStr", "RawPtr" };
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
static auto bloom_type_size_in_bytes(Str type_name) -> int64_t {
    if (type_name == "Int")  { return 4; }
    if (type_name == "U8")   { return 1; }
    if (type_name == "Bool") { return 1; }
    if (type_name == "Str")  { return 16; } // sizeof(BloomStr): data ptr + length
    if (type_name == "CStr")    { return 8; }  // pointer on 64-bit
    if (type_name == "RawPtr") { return 8; }  // void pointer on 64-bit
    return -1;
}

// Returns the C type name string for a Bloom type, used in sizeof() emission.
static auto bloom_type_to_c_type(Str type_name) -> char const * {
    if (type_name == "Int")  { return "int"; }
    if (type_name == "U8")   { return "uint8_t"; }
    if (type_name == "Bool") { return "bool"; }
    if (type_name == "Str")  { return "BloomStr"; }
    if (type_name == "CStr")    { return "char const *"; }
    if (type_name == "RawPtr") { return "void *"; }
    return nullptr;
}

static auto eval_const_primary(
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

static auto eval_const_multiplicative(
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

static auto eval_const_additive(
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

static auto parse_statement(
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

                if (iter_peek(tokens_iter)->type == TokenType::INTEGER_LITERAL) {
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
                    (void)iter_append(nodes_block_iter, ASTNode {
                        .type = ASTNodeType::ARRAY_RANGE_ASSIGN,
                        .parent = parent_node,
                        .array_range_assign = {
                            .variable_name = next_token->identifier.content,
                            .start_index = index,
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
                if (iter_peek(tokens_iter)->type == TokenType::INTEGER_LITERAL) {
                    auto *start_token = iter_next(tokens_iter);
                    int64_t range_start = start_token->integer_literal.value;

                    auto *range_op_token = iter_next(tokens_iter);
                    int64_t range_end;
                    Str range_count_identifier = {};
                    if (range_op_token->type == TokenType::RANGE_COUNTED) {
                        auto *count_token = iter_next(tokens_iter);
                        if (count_token->type == TokenType::INTEGER_LITERAL) {
                            range_end = range_start + count_token->integer_literal.value + 1;
                        }
                        else if (count_token->type == TokenType::IDENTIFIER) {
                            range_count_identifier = count_token->identifier.content;
                            range_end = 0;
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
                    else if (range_op_token->type == TokenType::RANGE_EXCLUSIVE) {
                        auto *end_token = expect_token_or_append_error(tokens_iter, TokenType::INTEGER_LITERAL, errors);
                        if (end_token == nullptr) { return false; }
                        range_end = end_token->integer_literal.value;
                    }
                    else if (range_op_token->type == TokenType::RANGE_INCLUSIVE) {
                        auto *end_token = expect_token_or_append_error(tokens_iter, TokenType::INTEGER_LITERAL, errors);
                        if (end_token == nullptr) { return false; }
                        range_end = end_token->integer_literal.value + 1;
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
                            .range_count_identifier = range_count_identifier,
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
        ASTNode value_node = {};
        if (value_tok->type == TokenType::INTEGER_LITERAL) {
            value_node = ASTNode {
                .type = ASTNodeType::INTEGER_LITERAL,
                .parent = nullptr,
                .integer_literal = { .value = { .value = value_tok->integer_literal.value } },
            };
        }
        else {
            append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, value_tok));
            return false;
        }
        auto *value_ptr = iter_append(nodes_block_iter, std::move(value_node));
        auto *nl = iter_next(tokens_iter);
        if (nl->type != TokenType::NEWLINE && nl->type != TokenType::END) {
            append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, nl));
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
        if (!expect_arrow_newline(tokens_iter, errors)) { return false; }

        auto *if_else_node = iter_append(nodes_block_iter, ASTNode {
            .type = ASTNodeType::IF_ELSE,
            .parent = parent_node,
            .if_else = {
                .condition_left = cond_left,
                .condition_right = cond_right,
                .comparison_op = comparison_op,
                .then_body = Array<ASTNode>(
                    nodes_block_iter->elements.data + nodes_block_iter->current_index + 1,
                    0
                ),
                .else_body = Array<ASTNode>(nullptr, 0),
            },
        });

        if (!parse_indented_body(tokens_iter, context, nodes_block_iter, if_else_node,
                                 proc_params_block, proc_params_iter, types_iter,
                                 operands_iter, array_elements_iter, errors,
                                 current_indent_level, &if_else_node->if_else.then_body)) {
            return false;
        }

        bool const has_else = (
            tokens_iter->current_index + 1 < tokens_iter->elements.length &&
            iter_peek(tokens_iter)->type == TokenType::INDENT &&
            tokens_iter->elements.data[tokens_iter->current_index + 1].type == TokenType::KEYWORD_ELSE
        );
        if (has_else) {
            iter_next(tokens_iter); // INDENT before else
            iter_next(tokens_iter); // KEYWORD_ELSE

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

#undef PARSE_ERROR_CREATE

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
