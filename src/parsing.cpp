#include <bloom/print.h>
#include <bloom/ptr.h>
#include <bloom/parsing.h>

/**
 * Converts an AllocatedArrayBlock to an Array.
 */
template<typename T>
inline Array<T> to_array(AllocatedArrayBlock<T> *block) {
    return Array<T> {
        .data = block->data,
        .length = block->length
    };
}

struct TokenIterator {
    Array<Token> *tokens;
    size_t current_index;
};

static inline auto token_is_of_type(Token *token, TokenType type) -> bool {
    return token != nullptr && token->type == type;
}
/**
 * Advances the iterator and returns the next token, or nullptr if at the end.
 */
static auto tokens_next(TokenIterator *iter) -> Token* {
    if (iter->current_index < iter->tokens->length) {
        iter->current_index++;
        return &iter->tokens->data[iter->current_index - 1];
    } else {
        return nullptr;
    }
}
/**
 * Peeks at the current token without advancing the iterator.
 */
static auto tokens_peek(TokenIterator *iter) -> Token* {
    if (iter->current_index < iter->tokens->length) {
        return &iter->tokens->data[iter->current_index];
    } else {
        return nullptr;
    }
}

auto parse(Array<Token> *tokens, ArenaAllocator *allocator) -> Array<ASTNode> {
    // Store the initial allocation offset in order to resize
    // both the nodes array and the proc params array later
    auto initial_marker = allocator_marker_from_current_offset(allocator);

    auto nodes_block = allocate_array<ASTNode>(allocator, tokens->length);
    size_t current_node_index = 0;

    auto proc_params_block = allocate_array<AstNodeProcParameter>(allocator, tokens->length);
    size_t current_proc_param_index = 0;

    auto append_node = [&](ASTNode &&node) {
        nodes_block.data[current_node_index] = node;
        current_node_index++;
    };

    auto tokens_iter = TokenIterator {
        .tokens = tokens,
        .current_index = 0
    };

    while (auto token = tokens_next(&tokens_iter)) {
        if (token->type == TokenType::IDENTIFIER) {
            if (token_is_of_type(tokens_peek(&tokens_iter), TokenType::CONST_DEF)) {
                (void)tokens_next(&tokens_iter);

                // Expect a procedure definition
                if (!token_is_of_type(tokens_next(&tokens_iter), TokenType::KEYWORD_PROC)) {
                    eprint("Error: Expected 'proc' keyword after 'const_def'\n");
                    break;
                }
                if (!token_is_of_type(tokens_next(&tokens_iter), TokenType::PARENTHESIS_OPEN)) {
                    eprint("Error: Expected '(' after procedure name\n");
                    break;
                }

                auto proc_param_begin_index = current_proc_param_index;

                // Parse procedure parameters (if any)
                do {
                    auto peeked_token = tokens_next(&tokens_iter);
                    if (token_is_of_type(peeked_token, TokenType::PARENTHESIS_CLOSE)) {
                        break;
                    }
                    else if(token_is_of_type(peeked_token, TokenType::COMMA)) {
                        // Just skip commas
                        continue;
                    }
                    else if(token_is_of_type(peeked_token, TokenType::IDENTIFIER)) {
                        String param_name = peeked_token->identifier.content;
                        proc_params_block.data[current_proc_param_index] = AstNodeProcParameter {
                            .name = param_name
                        };
                        current_proc_param_index++;

                        if (!token_is_of_type(tokens_next(&tokens_iter), TokenType::TYPE_SEPARATOR)) {
                            eprint("Error: Unexpected end of tokens while parsing procedure parameters\n");
                            goto return_result;
                        }

                        // TODO: Skip the type token for now but deal with it later
                        (void)tokens_next(&tokens_iter);
                    }
                    else {
                        eprint(
                            "Error: Unexpected token \"%\" while parsing procedure parameters\n",
                            to_string(peeked_token->type)
                        );
                        goto return_result;
                    }
                } while(true);

                auto proc_params = to_array(&proc_params_block);
                append_node(ASTNode{ 
                    .type = ASTNodeType::PROC_DEF,
                    .proc_def = {
                        .name = token->identifier.content,
                        .parameters = slice_by_offset(
                            &proc_params,
                            proc_param_begin_index,
                            current_proc_param_index - proc_param_begin_index
                        )
                    }
                });
            }
        }
    }

    return_result:
        auto extra_allocation_marker = allocator_marker_from_current_offset(allocator);
        size_t allocator_offset_after_extra_allocations = allocator->offset;

        // Allocate new blocks with the exact sizes and copy the data over to them
        auto nodes_block_arr = slice_by_offset(to_array(&nodes_block), 0, current_node_index);
        auto new_nodes_block = allocate_array_from_copy<ASTNode>(allocator, &nodes_block_arr);

        auto old_proc_params_arr = slice_by_offset(to_array(&proc_params_block), 0, current_proc_param_index);
        auto old_proc_params_block = allocate_array_from_copy<AstNodeProcParameter>(allocator, &old_proc_params_arr);

        // Reset the allocator offset to the initial value and re-allocate
        // the nodes and proc params blocks to be tightly packed
        allocator->offset = initial_marker.offset;

        auto new_nodes_block_arr = to_array(&new_nodes_block);
        new_nodes_block = allocate_array_from_copy<ASTNode>(allocator, &new_nodes_block_arr);
        assert(new_nodes_block.length == current_node_index &&
            "Node count mismatch after re-allocation");

        auto new_proc_params_arr = to_array(&old_proc_params_block);
        auto new_proc_params_block = allocate_array_from_copy<AstNodeProcParameter>(allocator, &new_proc_params_arr);
        assert(new_proc_params_block.length == current_proc_param_index &&
            "Proc parameter count mismatch after re-allocation");

        auto exact_allocation_marker = allocator_marker_from_current_offset(allocator);
        size_t allocator_offset_after_exact_allocations = allocator->offset;
        print("Allocator offset after exact allocations: %\n", allocator_offset_after_exact_allocations);

        // Update the proc parameters pointers in the AST nodes to point to the new tightly packed block
        for (auto &node : new_nodes_block) {
            if (node.type == ASTNodeType::PROC_DEF) {
                node.proc_def.parameters.data =
                    ptr_sub(node.proc_def.parameters.data,
                        ptr_sub(node.proc_def.parameters.data, new_proc_params_block.data));
            }
        }

        // Reclaim any unused memory in the allocator
        reclaim_memory_by_markers(allocator, &extra_allocation_marker, &exact_allocation_marker);

        return to_array(&new_nodes_block);
}