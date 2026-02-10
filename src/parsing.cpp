#include <functional>
#include <bloom/assert.h>
#include <bloom/print.h>
#include <bloom/ptr.h>
#include <bloom/parsing.h>

template<typename T, typename E>
struct Result {
    bool is_ok;
    union {
        T ok;
        E err;
    };
};

template<typename T, typename E>
static inline auto err(E error) -> Result<T, E> {
    return Result<T, E> {
        .is_ok = false,
        .err = error,
    };
}

template<typename T, typename E>
static inline auto ok(T value) -> Result<T, E> {
    return Result<T, E> {
        .is_ok = true,
        .ok = value,
    };
}

template<typename T, typename E>
static inline auto is_ok(Result<T, E> const &result) -> bool {
    return result.is_ok;
}

enum class ParseErrorCode {
    UNEXPECTED_TOKEN,
};

struct ParseError {
    ParseErrorCode code;
    Token::Position position;
    size_t src_code_line;
};

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
};

// For debugging purposes
static auto print_value(FILE *file, Context *context) -> void {
    auto *current_identifier = static_cast<void*>(context->current_identifier);
    auto *current_proc_node = static_cast<void*>(context->current_proc_node);
    char const *in_proc_definition = context->in_proc_definition ? "true" : "false";
    fprintf(
        _bloom_test_get_file(file),
        "{current_identifier=%p, current_proc_node=%p, in_proc_definition=%s}",
        current_identifier, current_proc_node, in_proc_definition
    );
}

/**
 * Parses procedure call parameters and appends them to the given procedure call AST node.
 * 
 * @return true on success, false on failure.
 */
static auto parse_proc_call_arguments(
    Iterator<Token> *tokens_iter,
    ASTNode *proc_call_node,
    Iterator<ASTNode> *nodes_block_iter,
    DynamicArray<ParseError> *errors
) -> bool {
    assert(proc_call_node->type == ASTNodeType::PROC_CALL &&
        "Procedure call node should be of PROC_CALL type after parsing arguments");

    size_t proc_call_nodes_begin_index = nodes_block_iter->current_index;
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
        if (next_token->type == TokenType::COMMA) {
            continue;
        }
        // TODO: Hardcoded argument parsing for now, fix later
        else if (next_token->type == TokenType::IDENTIFIER) {
            (void)iter_append(nodes_block_iter, ASTNode {
                .type = ASTNodeType::IDENTIFIER,
                .parent = proc_call_node,
                .identifier = next_token->identifier.content,
            });
        }
        else if (next_token->type == TokenType::STRING_LITERAL) {
            (void)iter_append(nodes_block_iter, ASTNode {
                .type = ASTNodeType::STRING_LITERAL,
                .parent = proc_call_node,
                .string_literal = {
                    .value = next_token->string_literal.content,
                },
            });
        }
        else {
            append(errors, ParseError {
                .code = ParseErrorCode::UNEXPECTED_TOKEN,
                .position = next_token->position,
                .src_code_line = __LINE__,
            });
            return false;
        }
    }

    // Set the arguments array for the procedure call node to include all parsed arguments
    proc_call_node->proc_call.arguments = to_array(nodes_block_iter);
    proc_call_node->proc_call.arguments = slice_by_offset(
        &proc_call_node->proc_call.arguments,
        proc_call_nodes_begin_index,
        nodes_block_iter->current_index
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

    Token *current_token = iter_next(tokens_iter);
    if (current_token->type != TokenType::PARENTHESIS_OPEN) {
        append(errors, ParseError {
            .code = ParseErrorCode::UNEXPECTED_TOKEN,
            .position = current_token->position,
            .src_code_line = __LINE__,
        });
        return false;
    }
    while(true) {
        current_token = iter_next(tokens_iter);
        switch (current_token->type) {
            case TokenType::PARENTHESIS_CLOSE:
                return true;
            case TokenType::COMMA:
                // Just skip commas
                continue;
            case TokenType::IDENTIFIER: {
                (void)iter_append(proc_params_iter, ProcParameterASTNode {
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
                    });
                    return false;
                }

                // TODO: Skip the type token for now but deal with it later
                (void)iter_next(tokens_iter);
                break;
            }
            default:
                append(errors, ParseError {
                    .code = ParseErrorCode::UNEXPECTED_TOKEN,
                    .position = current_token->position,
                    .src_code_line = __LINE__,
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
    DynamicArray<ParseError> *errors
) -> bool;

#define PARSE_ERROR_CREATE(error_code, token) \
    ParseError { .code = ParseErrorCode::error_code, .position = token->position, .src_code_line = __LINE__ }

static auto parse_expression(
    Iterator<Token> *tokens_iter,
    Context *context,
    Iterator<ASTNode> *nodes_block_iter,
    AllocatedArrayBlock<ProcParameterASTNode> *proc_params_block,
    Iterator<ProcParameterASTNode> *proc_params_iter,
    Iterator<TypeASTNode> *types_iter,
    DynamicArray<ParseError> *errors
) -> Result<ASTNode, ParseError> {
    auto next_token = iter_next(tokens_iter);
    assert(next_token != nullptr &&
        "Next token should not be null when parsing an expression");
    switch(next_token->type) {
        case TokenType::INTEGER_LITERAL: {
            return ok<ASTNode, ParseError>(ASTNode {
                .type = ASTNodeType::INTEGER_LITERAL,
                .parent = nullptr,
                .integer_literal = {
                    .value = IntegerLiteralASTNode {
                        .value = next_token->integer_literal.value,
                    },
                },
            });
        }
        case TokenType::KEYWORD_PROC: {
            // Expect procedure definition

            // Parse procedure parameters
            if (!parse_proc_params(tokens_iter, proc_params_iter, errors)) {
                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, next_token));
            }

            // Parse procedure return type (if there is one)
            // - If the procedure params are followed by an arrow token immediately,
            //   then there is no return type.
            // - If the procedure params are followed by an identifier token before the
            //   arrow token, then that identifier token is the return type.
            Token *proc_return_type_token = iter_next(tokens_iter);
            TypeASTNode *return_type_node = nullptr;
            if (proc_return_type_token->type == TokenType::ARROW) {
                // Unneccessary, but for clarity
                // return_type_node = nullptr;
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
            if (
                auto next_token = iter_next(tokens_iter);
                next_token->type != TokenType::NEWLINE
            ) {
                return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, next_token));
            }

            auto proc_node = iter_append(nodes_block_iter, ASTNode {
                .type = ASTNodeType::PROC_DEF,
                .parent = nullptr,
                .proc_def = {
                    .name = context->current_identifier->identifier.content,
                    .parameters = Array<ProcParameterASTNode>(
                        proc_params_block->data,
                        proc_params_iter->current_index
                    ),
                    .return_type = return_type_node,
                    .body = Array<ASTNode>(
                        context->nodes_block->data + nodes_block_iter->current_index,
                        0 // Will be updated later
                    ),
                },
            });

            // Parse procedure body
            // - Expect each line to be indented and contain a single statement
            while(true) {
                // If the line doesn't begin with an indent token, the procedure body has ended
                if (iter_peek(tokens_iter)->type != TokenType::INDENT) {
                    break;
                }
                (void)iter_next(tokens_iter); // Consume the indent token
                
                if (!parse_statement(
                    tokens_iter,
                    context,
                    nodes_block_iter,
                    proc_node,
                    proc_params_block,
                    proc_params_iter,
                    types_iter,
                    errors
                )) {
                    return err<ASTNode, ParseError>(
                        PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, iter_peek_prev(tokens_iter))
                    );
                }
                
                print("Finished parsing procedure body statement, current token: %\n", to_string(iter_current(tokens_iter)->type));
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

            // Update the procedure body length so that it includes all parsed body statements
            proc_node->proc_def.body.length =
                nodes_block_iter->current_index
                - ptr_sub(
                    nodes_block_iter->elements.data,
                    proc_node->proc_def.body.data
                );

            assert(proc_node->proc_def.body.length > 0 &&
                "Procedure body should contain at least one statement");
            assert(
                nodes_block_iter->elements[proc_node->proc_def.body.length].type == ASTNodeType::UNKNOWN &&
                "Next node after procedure body should be of UNKNOWN type");
            return ok<ASTNode, ParseError>(*proc_node);
        }
        default:
            return err<ASTNode, ParseError>(PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, next_token));
    }
}

static auto parse_statement(
    Iterator<Token> *tokens_iter,
    Context *context,
    Iterator<ASTNode> *nodes_block_iter,
    ASTNode *parent_node,
    AllocatedArrayBlock<ProcParameterASTNode> *proc_params_block,
    Iterator<ProcParameterASTNode> *proc_params_iter,
    Iterator<TypeASTNode> *types_iter,
    DynamicArray<ParseError> *errors
) -> bool {
    if (auto *next_token = iter_next(tokens_iter); next_token->type == TokenType::IDENTIFIER) {
        switch (auto peeked_token = iter_peek(tokens_iter); peeked_token->type) {
            case TokenType::PARENTHESIS_OPEN: {
                // Expect a procedure call

                int64_t proc_call_end_token_index = iter_get_index_at_if<Token>(
                    tokens_iter, [](auto *token) {
                        return token->type == TokenType::PARENTHESIS_CLOSE;
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
                    errors
                );
                if (!proc_call_args_parsed_ok) {
                    // TODO: Append a better error
                    append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, iter_current(tokens_iter)));
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
            case TokenType::VAR_DEF: {
                (void)iter_next(tokens_iter); // Consume VAR_DEF token
    
                // Expect a variable definition
    
                // Find the end of the statement to create a sub-iterator for the expression
                // (do not consume tokens in the main iterator yet)
                int64_t expr_end_token_index = iter_get_index_at_if<Token>(
                    tokens_iter, [](auto *token) {
                        return token->type == TokenType::NEWLINE || token->type == TokenType::END;
                    }
                );
                if (expr_end_token_index == -1) {
                    append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, iter_current(tokens_iter)));
                    return false;
                }
                auto expr_tokens_iter = iter_slice_by_offset(
                    tokens_iter,
                    tokens_iter->current_index,
                    expr_end_token_index
                );
                if (expr_tokens_iter.elements.length == 0) {
                    append(errors, PARSE_ERROR_CREATE(UNEXPECTED_TOKEN, iter_current(tokens_iter)));
                    return false;
                }
    
                // Parse the expression for the variable definition
                auto expr_parse_result = parse_expression(
                    &expr_tokens_iter,
                    context,
                    nodes_block_iter,
                    proc_params_block,
                    proc_params_iter,
                    types_iter,
                    errors
                );
                if (!is_ok(expr_parse_result)) {
                    append(errors, expr_parse_result.err);
                    return false;
                }
                auto expr_node = expr_parse_result.ok;
                (void)iter_append(nodes_block_iter, ASTNode {
                    .type = ASTNodeType::VARIABLE_DEFINITION,
                    .parent = parent_node,
                    .variable_definition = {
                        .name = next_token->identifier.content,
                        .value = expr_node.integer_literal.value,
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
        }
    }
    return true;
}

#undef PARSE_ERROR_CREATE

auto parse(Array<Token> *tokens, ArenaAllocator *allocator) -> Array<ASTNode> {
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
    auto proc_params_iter = to_iterator(&proc_params_block);
    assert (proc_params_iter.current_index == 0 &&
        "Procedure parameters iterator current index should be 0 at the start");

    // Parse the tokens into AST nodes
    auto context = Context{};
    assert(context.current_identifier == nullptr &&
        "Current identifier in context should be null at the start");
    context.nodes_block = &nodes_block;
    auto errors = DynamicArray<ParseError>(&errors_block);

    // Parse tokens
    auto tokens_iter = to_iterator(tokens);
    while (true) {
        auto *current_token = iter_next(&tokens_iter);
        assert(current_token != nullptr &&
            "Current token should not be null during parsing");

        if (current_token->type == TokenType::END) {
            break;
        }

        // Parse top-level nodes
        parse_top_level_node:
            if (current_token->type == TokenType::IDENTIFIER) {
                auto peeked_token = iter_next(&tokens_iter);
                if (peeked_token->type == TokenType::CONST_DEF) {
                    context.current_identifier = current_token;
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
                &errors
            );
            if (!is_ok(expr_result)) {
                append(&errors, expr_result.err);
                goto after_parsing;
            }
            continue;
    }

    after_parsing:
        print("Error count: %\n", errors.length);
        for (auto &error : to_array(&errors)) {
            print("\tParse error at line %, column %, source line %: %\n",
                error.position.line,
                error.position.col,
                error.src_code_line,
                static_cast<int>(error.code)
            );
        }

        // Allocate new blocks with the exact sizes and copy the data over to them
        auto nodes_block_arr = to_array(&nodes_block);
        nodes_block_arr = slice_by_offset(&nodes_block_arr, 0, nodes_block_iter.current_index);
        auto new_nodes_block = allocate_array_from_copy<ASTNode>(allocator, &nodes_block_arr);

        auto old_proc_params_arr = to_array(&proc_params_block);
        old_proc_params_arr = slice_by_offset(&old_proc_params_arr, 0, proc_params_iter.current_index);
        auto old_proc_params_block = allocate_array_from_copy<ProcParameterASTNode>(allocator, &old_proc_params_arr);

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

        // Update the proc parameters pointers in the AST nodes to point to the new tightly packed block
        for (auto &node : new_nodes_block) {
            if (node.type == ASTNodeType::PROC_DEF) {
                node.proc_def.parameters.data =
                    ptr_sub(node.proc_def.parameters.data,
                        ptr_sub(node.proc_def.parameters.data, new_proc_params_block.data));
            }
        }

        return to_array(&new_nodes_block);
}
