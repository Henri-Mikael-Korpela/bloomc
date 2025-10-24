#include <bloom/print.h>
#include <bloom/ptr.h>
#include <bloom/parsing.h>

/**
 * Converts an AllocatedArrayBlock to an Array.
 */
template<typename PointerT>
static inline Array<PointerT> to_array(AllocatedArrayBlock<PointerT> *block) {
    return Array<PointerT> {
        .data = block->data,
        .length = block->length
    };
}

template<typename ElementType>
struct Iterator {
    Array<ElementType> elements;
    size_t current_index;
};

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
static auto iter_next_and_set(Iterator<ElementType> *iter, ElementType &&value) -> ElementType* {
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

auto parse(Array<Token> *tokens, ArenaAllocator *allocator) -> Array<ASTNode> {
    auto types_block = allocate_array<TypeASTNode>(allocator, tokens->length);
    auto types_iter = to_iterator(&types_block);

    // Store the initial allocation offset in order to resize
    // both the nodes array and the proc params array later
    auto initial_marker = allocator_marker_from_current_offset(allocator);

    auto nodes_block = allocate_array<ASTNode>(allocator, tokens->length);
    auto nodes_block_iter = to_iterator(&nodes_block);

    auto proc_params_block = allocate_array<ProcParameterASTNode>(allocator, tokens->length);
    auto proc_params_iter = to_iterator(&proc_params_block);

    for (auto tokens_iter = to_iterator(tokens);;) {
        auto token = iter_next(&tokens_iter);
        if (token->type == TokenType::END) {
            break;
        }
        else if (token->type == TokenType::IDENTIFIER) {
            if (iter_peek(&tokens_iter)->type != TokenType::CONST_DEF) {
                continue;
            }

            (void)iter_next(&tokens_iter);

            // Expect a procedure definition
            if (iter_next(&tokens_iter)->type != TokenType::KEYWORD_PROC) {
                eprint("Error: Expected 'proc' keyword after 'const_def'\n");
                break;
            }
            if (iter_next(&tokens_iter)->type != TokenType::PARENTHESIS_OPEN) {
                eprint("Error: Expected '(' after procedure name\n");
                break;
            }

            // Parse procedure parameters (if any)
            do {
                auto current_token = iter_next(&tokens_iter);
                switch (current_token->type) {
                    case TokenType::PARENTHESIS_CLOSE:
                        goto parse_return_type;
                    case TokenType::COMMA:
                        // Just skip commas
                        continue;
                    case TokenType::IDENTIFIER:
                        (void)iter_next_and_set(&proc_params_iter, ProcParameterASTNode {
                            .name = current_token->identifier.content
                        });

                        if (iter_next(&tokens_iter)->type != TokenType::TYPE_SEPARATOR) {
                            eprint("Error: Unexpected end of tokens while parsing procedure parameters\n");
                            goto return_result;
                        }

                        // TODO: Skip the type token for now but deal with it later
                        (void)iter_next(&tokens_iter);
                        break;
                    default:
                        eprint(
                            "Error: Unexpected token \"%\" while parsing procedure parameters\n",
                            to_string(current_token->type)
                        );
                        goto return_result;
                }
            } while(true);

            parse_return_type:
            // Parse procedure return type if it exists before the arrow operator
            TypeASTNode *return_type_node;
            auto next_node = iter_next(&tokens_iter);
            if (next_node->type == TokenType::IDENTIFIER) {
                return_type_node = iter_next_and_set(&types_iter, TypeASTNode {
                    .name = next_node->identifier.content,
                });

                next_node = iter_next(&tokens_iter);
                if (next_node->type != TokenType::ARROW) {
                    eprint("Error: Expected arrow operator after the procedure return type.\n");
                    goto return_result;
                }
            }
            // If the next token is an arrow operator, meaning there is no return type
            else if (next_node->type == TokenType::ARROW) {
                return_type_node = nullptr;
            }
            else {
                eprint("Error: Unexpected token after the procedure parameter list.\n");
                goto return_result;
            }

            // Expect newline to begin the procedure body
            if (iter_next(&tokens_iter)->type != TokenType::NEWLINE) {
                eprint("Error: Expected a newline character to begin a procedure body.\n");
                goto return_result;
            }

            // Parse procedure body where each statement begins with an indentation
            next_node = iter_next(&tokens_iter);
            if (!(next_node->type == TokenType::INDENT && next_node->indent.level == 1)) {
                eprint("Error: Expected a valid indentation for a statement in a procedure body");
                goto return_result;
            }

            // Parse procedure statements
            // TODO Hardcoded addition operation with two identifiers, rework to make it more general-purpose!
            next_node = iter_next(&tokens_iter);
            if (next_node->type != TokenType::IDENTIFIER) {
                eprint("Error: Expected an identifier.\n");
                goto return_result;
            }
            auto identifier_left = &next_node->identifier;

            if (iter_next(&tokens_iter)->type != TokenType::ADD) {
                eprint("Error: Expected operator '+'.\n");
                goto return_result;
            }

            next_node = iter_next(&tokens_iter);
            if (next_node->type != TokenType::IDENTIFIER) {
                eprint("Error: Expected an identifier.\n");
                goto return_result;
            }
            auto identifier_right = &next_node->identifier;

            auto proc_node = iter_next_and_set(&nodes_block_iter, ASTNode { 
                .type = ASTNodeType::PROC_DEF,
                .parent = nullptr,
                .proc_def = {
                    .name = token->identifier.content,
                    .parameters = slice_by_offset(to_array(&proc_params_block), 0, proc_params_iter.current_index),
                    .return_type = return_type_node,
                }
            });

            (void)iter_next_and_set(&nodes_block_iter, ASTNode {
                .type = ASTNodeType::BINARY_ADD,
                .parent = proc_node,
                .binary_operation = {
                    .oprt = BinaryOperatorType::ADD,
                    .identifier_left = identifier_left->content,
                    .identifier_right = identifier_right->content,
                },
            });

            // Nodes appended after the procedure definition are stored in
            // the same nodes block so it is possible to take the nodes
            // appended after the definition
            proc_node->proc_def.body = slice_by_offset(to_array(&nodes_block), 1, nodes_block_iter.current_index);
        }
    }

    return_result:
        // Allocate new blocks with the exact sizes and copy the data over to them
        auto nodes_block_arr = slice_by_offset(to_array(&nodes_block), 0, nodes_block_iter.current_index);
        auto new_nodes_block = allocate_array_from_copy<ASTNode>(allocator, &nodes_block_arr);

        auto old_proc_params_arr = slice_by_offset(to_array(&proc_params_block), 0, proc_params_iter.current_index);
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
        for (auto node : new_nodes_block) {
            if (node.type == ASTNodeType::PROC_DEF) {
                node.proc_def.parameters.data =
                    ptr_sub(node.proc_def.parameters.data,
                        ptr_sub(node.proc_def.parameters.data, new_proc_params_block.data));
            }
        }

        return to_array(&new_nodes_block);
}
