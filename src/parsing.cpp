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

enum ParseError {
    UNEXPECTED_TOKEN,
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
static auto to_array(Iterator<ElementType> *iter) -> Array<ElementType> {
    return Array<ElementType>(
        iter->elements.data,
        iter->current_index
    );
}

template<typename ElementType>
static auto to_iterator(AllocatedArrayBlock<ElementType> *block) -> Iterator<ElementType> {
    return {
        .elements = to_array(block),
        .current_index = 0,
    };
}
static auto to_iterator(Array<Token> *tokens) -> Iterator<Token> {
    return {
        .elements = *tokens,
        .current_index = 0,
    };
}

/**
 * Advances the iterator and returns the next element.
 */
template<typename ElementType>
static auto iter_next(Iterator<ElementType> *iter) -> ElementType* {
    assert (iter->elements.length > 0 &&
        "Cannot advance iterator because the iterator length is 0.");
    assert(iter->current_index < iter->elements.length &&
        "Iterator out of bounds");
    return &iter->elements.data[iter->current_index++];
}

/**
 * Advances the iterator and sets the next element to the given value, and returns it.
 */
template<typename ElementType>
static auto iter_next_and_set(
    Iterator<ElementType> *iter,
    ElementType &&value
) -> ElementType* {
    auto next_elem = iter_next(iter);
    *next_elem = value;
    return next_elem;
}

/**
 * Peeks at the current element without advancing the iterator.
 */
template<typename ElementType>
static auto iter_peek(Iterator<ElementType> *iter) -> ElementType* {
    assert(iter->current_index < iter->elements.length &&
        "Iterator out of bounds");
    return &iter->elements.data[iter->current_index];
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
auto append(DynamicArray<ElementType> *array, ElementType &&value) -> void {
    assert(array->length < array->max_length &&
        "DynamicArray out of capacity");
    array->data[array->length++] = value;
}

struct Context {
    Token *current_identifier = nullptr;
    ASTNode *current_proc_node = nullptr;
    bool in_proc_definition = false;
    Iterator<ASTNode> *nodes_block_iter = nullptr;
    Iterator<ProcParameterASTNode> *proc_params_iter = nullptr;
    Iterator<TypeASTNode> *types_iter = nullptr;
};

// For debugging purposes
auto print_value(FILE *file, Context *context) -> void {
    auto const *current_identifier = static_cast<void*>(context->current_identifier);
    auto const *current_proc_node = static_cast<void*>(context->current_proc_node);
    char const *in_proc_definition = context->in_proc_definition ? "true" : "false";
    fprintf(
        _bloom_test_get_file(file),
        "{current_identifier=%p, current_proc_node=%p, in_proc_definition=%s}",
        current_identifier, current_proc_node, in_proc_definition
    );
}

/**
 * Parses procedure call parameters and appends them to the given procedure call AST node.
 * @return true on success, false on failure.
 */
auto parse_proc_arguments(
    Iterator<Token> *tokens_iter,
    ASTNode *proc_call_node,
    Context *context,
    DynamicArray<ParseError> *errors
) -> bool {
    while(true) {
        auto next_token = iter_next(tokens_iter);
        if (next_token->type == TokenType::PARENTHESIS_CLOSE) {
            break;
        }
        else if (next_token->type == TokenType::COMMA) {
            continue;
        }
        else if(next_token->type == TokenType::END) {
            append(errors, ParseError::UNEXPECTED_TOKEN);
            return false;
        }
        else {
            // TODO: Hardcoded argument parsing for now, fix later
            if (next_token->type == TokenType::IDENTIFIER) {
                (void)iter_next_and_set(context->nodes_block_iter, ASTNode {
                    .type = ASTNodeType::IDENTIFIER,
                    .parent = proc_call_node,
                    .identifier = next_token->identifier.content,
                });
            }
            else if (next_token->type == TokenType::STRING_LITERAL) {
                (void)iter_next_and_set(context->nodes_block_iter, ASTNode {
                    .type = ASTNodeType::STRING_LITERAL,
                    .parent = proc_call_node,
                    .string_literal = {
                        .value = next_token->string_literal.content,
                    },
                });
            }
            else {
                append(errors, ParseError::UNEXPECTED_TOKEN);
                return false;
            }
        }
    }
    return true;
}

auto parse_statement(
    Iterator<Token> *tokens_iter,
    Context *context,
    DynamicArray<ParseError> *errors
) -> bool {
    auto next_token = iter_next(tokens_iter);
    assert(next_token != nullptr &&
        "Unexpected end of tokens while parsing");
    /*print("PARSING STATEMENT: %\n", to_string(next_token->type));
    print("CONTEXT: %\n", context);*/
    switch(next_token->type) {
        case TokenType::IDENTIFIER: {
            switch (auto next_token2 = iter_next(tokens_iter); next_token2->type) {
                case TokenType::ADD: {
                    if (context->current_proc_node == nullptr) {
                        append(errors, ParseError::UNEXPECTED_TOKEN);
                        return false;
                    }
                    assert(context->nodes_block_iter != nullptr &&
                        "Nodes block iterator should not be null in parse statement context");

                    auto identifier_left = next_token->identifier.content;
                    auto identifier_right_token = iter_next(tokens_iter);
                    if (identifier_right_token->type != TokenType::IDENTIFIER) {
                        append(errors, ParseError::UNEXPECTED_TOKEN);
                        return false;
                    }
                    auto identifier_right = identifier_right_token->identifier.content;

                    (void)iter_next_and_set(context->nodes_block_iter, ASTNode {
                        .type = ASTNodeType::BINARY_ADD,
                        .parent = context->current_proc_node,
                        .binary_operation = {
                            .oprt = BinaryOperatorType::ADD,
                            .identifier_left = identifier_left,
                            .identifier_right = identifier_right,
                        }
                    });
                    return true;
                }
                case TokenType::CONST_DEF: {
                    if (iter_peek(tokens_iter)->type != TokenType::KEYWORD_PROC) {
                        return false;
                    }
                    auto new_context = *context;
                    new_context.current_identifier = next_token;
                    parse_statement(tokens_iter, &new_context, errors);
                    return true;
                }
                case TokenType::VAR_DEF: {
                    if (context->current_proc_node == nullptr) {
                        append(errors, ParseError::UNEXPECTED_TOKEN);
                        return false;
                    }
                    // TODO: Handle expressions in general later. Now, deal with
                    // an integer literal as variable value only for simplicity
                    auto *integer_literal_token = iter_next(tokens_iter);
                    if (integer_literal_token->type != TokenType::INTEGER_LITERAL) {
                        append(errors, ParseError::UNEXPECTED_TOKEN);
                        return false;
                    }
                    (void)iter_next_and_set(context->nodes_block_iter, ASTNode {
                        .type = ASTNodeType::VARIABLE_DEFINITION,
                        .parent = context->current_proc_node,
                        .variable_definition = {
                            .name = next_token->identifier.content,
                            .value = IntegerLiteralASTNode {
                                .value = integer_literal_token->integer_literal.value,
                            },
                        },
                    });

                    // Expect newline after variable definition
                    if (iter_next(tokens_iter)->type != TokenType::NEWLINE) {
                        append(errors, ParseError::UNEXPECTED_TOKEN);
                        return false;
                    }
                    return true;
                }
                case TokenType::PARENTHESIS_OPEN: {
                    // Expect a procedure call
                    
                    auto *proc_call_node = iter_next_and_set(context->nodes_block_iter, ASTNode {
                        .type = ASTNodeType::PROC_CALL,
                        .parent = context->current_proc_node,
                        .proc_call = {
                            // .arguments = ... missing, it will be set later,
                            // after parsing arguments
                            .caller_identifier = next_token->identifier.content,
                        },
                    });
                    
                    // Parse procedure arguments
                    auto proc_args_begin_index =
                        context->nodes_block_iter->current_index;

                    if (!parse_proc_arguments(tokens_iter, proc_call_node, context, errors)) {
                        return false;
                    }

                    auto proc_arguments = to_array(context->nodes_block_iter);
                    proc_arguments = slice_by_offset(
                        &proc_arguments,
                        proc_args_begin_index,
                        context->nodes_block_iter->current_index
                    );

                    proc_call_node->proc_call.arguments = proc_arguments;
                    return true;
                }
                default:
                    append(errors, ParseError::UNEXPECTED_TOKEN);
                    return false;
            }
        }
        case TokenType::KEYWORD_PROC: {
            // Expect a procedure definition after a constant identifier
            // with a procedure name stored in the current identifier
            if (context->current_identifier == nullptr) {
                append(errors, ParseError::UNEXPECTED_TOKEN);
                return false;
            }
            assert(context->current_identifier->type == TokenType::IDENTIFIER &&
                "Current identifier in context should be of IDENTIFIER token type");

            // Expect to parse the procedure parameters
            auto new_context = *context;
            new_context.in_proc_definition = true;
            size_t const last_proc_param_begin_index =
                context->proc_params_iter->current_index;
            if (!parse_statement(tokens_iter, &new_context, errors)) {
                return false;
            }

            // Expect to parse procedure return type
            TypeASTNode *return_type_node;
            switch (auto next_token = iter_next(tokens_iter); next_token->type) {
                case TokenType::IDENTIFIER:
                    return_type_node = iter_next_and_set(context->types_iter, TypeASTNode {
                        .name = next_token->identifier.content,
                    });

                    if (iter_next(tokens_iter)->type != TokenType::ARROW) {
                        append(errors, ParseError::UNEXPECTED_TOKEN);
                        return false;
                    }
                    break;
                case TokenType::ARROW:
                    return_type_node = nullptr;
                    break;
                default:
                    append(errors, ParseError::UNEXPECTED_TOKEN);
                    return false;
            }

            // Construct the procedure definition AST node
            // and insert procedure body into it later
            auto proc_params_arr = to_array(context->proc_params_iter);
            proc_params_arr = slice_by_offset(
                &proc_params_arr,
                last_proc_param_begin_index,
                context->proc_params_iter->current_index
            );
            auto proc_name = context->current_identifier->identifier.content;
            auto *proc_node = iter_next_and_set(context->nodes_block_iter, ASTNode { 
                .type = ASTNodeType::PROC_DEF,
                .parent = nullptr,
                .proc_def = {
                    .name = proc_name,
                    .parameters = proc_params_arr,
                    .return_type = return_type_node,
                },
            });

            // Parse procedure body
            auto proc_body_nodes_begin_index =
                context->nodes_block_iter->current_index;

            if (iter_next(tokens_iter)->type != TokenType::NEWLINE) {
                append(errors, ParseError::UNEXPECTED_TOKEN);
                return false;
            }

            while (true) {
                if (iter_next(tokens_iter)->type == TokenType::INDENT) {
                    auto new_context = *context;
                    new_context.current_proc_node = proc_node;
                    if (!parse_statement(tokens_iter, &new_context, errors)) {
                        return false;
                    }
                }
                else {
                    break;
                }
            }

            // TODO Initialize proc body nodes after implicit return handling
            // so that there would be no need to modify the proc body
            // nodes length later manually
            auto proc_body_nodes = to_array(context->nodes_block_iter);
            proc_body_nodes = slice_by_offset(
                &proc_body_nodes,
                proc_body_nodes_begin_index,
                context->nodes_block_iter->current_index
            );

            // Since Bloom supports implicit return type with the last expression
            // like Rust, check if the last node in the procedure body is an expression
            // and if so, set it as the return value of the procedure
            if (proc_body_nodes.length > 0) {
                size_t last_node_index = proc_body_nodes.length - 1;
                auto &last_node = proc_body_nodes.data[last_node_index];
                if (last_node.type == ASTNodeType::BINARY_ADD) {
                    // "Append" a return node after the last node and set the last node's
                    // parent to be the return node in order to avoid listing the last
                    // expression node amongst procedure body statements
                    auto *return_node = iter_next_and_set(context->nodes_block_iter, ASTNode {
                        .type = ASTNodeType::RETURN,
                        .parent = proc_node,
                        .return_value = &last_node,
                    });
                    last_node.parent = return_node;
                    // FEELS LIKE A HACK Modify the procedure body nodes length
                    // so that the last node for return statement is included
                    proc_body_nodes.length += 1;
                }
            }

            proc_node->proc_def.body = proc_body_nodes;

            return true;
        }
        case TokenType::NEWLINE:
            // Skip newlines
            return true;
        case TokenType::PARENTHESIS_OPEN: {
            if (!context->in_proc_definition) {
                append(errors, ParseError::UNEXPECTED_TOKEN);
                return false;
            }

            // Parse procedure parameters
            assert(context->proc_params_iter != nullptr &&
                "Procedure parameters iterator should not be null in proc definition context");
            Token *current_token;
            while(true) {
                current_token = iter_next(tokens_iter);
                switch (current_token->type) {
                    case TokenType::PARENTHESIS_CLOSE:
                        return true;
                    case TokenType::COMMA:
                        // Just skip commas
                        continue;
                    case TokenType::IDENTIFIER: {
                        (void)iter_next_and_set(context->proc_params_iter, ProcParameterASTNode {
                            .name = current_token->identifier.content
                        });

                        if (iter_next(tokens_iter)->type != TokenType::TYPE_SEPARATOR) {
                            append(errors, ParseError::UNEXPECTED_TOKEN);
                            return false;
                        }

                        // TODO: Skip the type token for now but deal with it later
                        (void)iter_next(tokens_iter);
                        break;
                    }
                    default:
                        append(errors, ParseError::UNEXPECTED_TOKEN);
                        return false;
                }
            }

            assert(current_token->type == TokenType::PARENTHESIS_CLOSE &&
                "Expected closing parenthesis after procedure parameters");
            return false;
        }
        case TokenType::KEYWORD_PASS:
            if (context->current_proc_node == nullptr) {
                append(errors, ParseError::UNEXPECTED_TOKEN);
                return false;
            }

            (void)iter_next_and_set(context->nodes_block_iter, ASTNode {
                .type = ASTNodeType::PASS,
                .parent = context->current_proc_node,
            });
            return true;
        case TokenType::END:
            return false;
        default:
            return false;
    }
}

auto parse(Array<Token> *tokens, ArenaAllocator *allocator) -> Array<ASTNode> {
    // Allocate all necessary blocks upfront
    constexpr size_t MAX_ERROR_COUNT = 16;
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
    context.nodes_block_iter = &nodes_block_iter;
    context.proc_params_iter = &proc_params_iter;
    context.types_iter = &types_iter;
    assert(context.current_identifier == nullptr &&
        "Current identifier in context should be null at the start");
    auto errors = DynamicArray<ParseError>(&errors_block);
    auto tokens_iter = to_iterator(tokens);

    // TODO: Refactor, simplify but keep this is as (for now) for debugging purposes
    while (true) {
        /*printf("Parsing next statement...\n");*/
        auto success = parse_statement(&tokens_iter, &context, &errors);
        if (!success) {
            break;
        }
    }

    print("Error count: %\n", errors.length);

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
