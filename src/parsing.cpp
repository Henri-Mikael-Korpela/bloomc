#include <bloom/print.h>
#include <bloom/ptr.h>
#include <bloom/parsing.h>

/**
 * Converts an AllocatedArrayBlock to an Array.
 */
template<typename PointerT>
inline Array<PointerT> to_array(AllocatedArrayBlock<PointerT> *block) {
    return Array<PointerT> {
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
    auto types_block = allocate_array<TypeASTNode>(allocator, tokens->length);
    size_t current_type_index = 0;

    // Store the initial allocation offset in order to resize
    // both the nodes array and the proc params array later
    auto initial_marker = allocator_marker_from_current_offset(allocator);

    auto nodes_block = allocate_array<ASTNode>(allocator, tokens->length);
    size_t current_node_index = 0;

    auto proc_params_block = allocate_array<ProcParameterASTNode>(allocator, tokens->length);
    size_t current_proc_param_index = 0;

    /**
     * Appends node to the nodes block and returns a pointer to the appended node.
     */
    auto append_node = [&](ASTNode &&node) -> ASTNode* {
        nodes_block.data[current_node_index] = node;
        assert(current_node_index + 1 < nodes_block.length &&
            "Cannot append nodes because there is not enough space to append nodes");
        return &nodes_block.data[current_node_index++];
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
                        proc_params_block.data[current_proc_param_index] = ProcParameterASTNode {
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

                // Parse procedure return type if it exists before the arrow operator
                TypeASTNode *return_type_node;
                if (
                    auto next_node = tokens_next(&tokens_iter);
                    token_is_of_type(next_node, TokenType::IDENTIFIER)
                ) {
                    return_type_node = &types_block.data[current_type_index];
                    current_type_index++;
                    *return_type_node = TypeASTNode {
                        .name = next_node->identifier.content,
                    };

                    next_node = tokens_next(&tokens_iter);
                    if (!token_is_of_type(next_node, TokenType::ARROW)) {
                        eprint("Error: Expected arrow operator after the procedure return type.\n");
                        goto return_result;
                    }
                }
                // If the next token is an arrow operator, meaning there is no return type
                else if (token_is_of_type(next_node, TokenType::ARROW)) {
                    return_type_node = nullptr;
                }
                else {
                    eprint("Error: Unexpected token after the procedure parameter list.\n");
                    goto return_result;
                }

                // Expect newline to begin the procedure body
                if (
                    auto next_node = tokens_next(&tokens_iter);
                    !token_is_of_type(next_node, TokenType::NEWLINE)
                ) {
                    eprint("Error: Expected a newline character to begin a procedure body.\n");
                    goto return_result;
                }

                // Parse procedure body where each statement begins with an indentation
                if (
                    auto next_node = tokens_next(&tokens_iter);
                    !(token_is_of_type(next_node, TokenType::INDENT) && next_node->indent.level == 1)
                ) {
                    eprint("Error: Expected a valid indentation for a statement in a procedure body");
                    goto return_result;
                }

                // Parse procedure statements
                // TODO Hardcoded addition operation with two identifiers, rework to make it more general-purpose!
                auto next_node = tokens_next(&tokens_iter);
                if (!token_is_of_type(next_node, TokenType::IDENTIFIER)) {
                    eprint("Error: Expected an identifier.\n");
                    goto return_result;
                }
                auto identifier_left = &next_node->identifier;

                if (
                    auto next_node = tokens_next(&tokens_iter);
                    !token_is_of_type(next_node, TokenType::ADD)
                ) {
                    eprint("Error: Expected operator '+'.\n");
                    goto return_result;
                }

                next_node = tokens_next(&tokens_iter);
                if (!token_is_of_type(next_node, TokenType::IDENTIFIER)) {
                    eprint("Error: Expected an identifier.\n");
                    goto return_result;
                }
                auto identifier_right = &next_node->identifier;

                auto proc_node = append_node(ASTNode { 
                    .type = ASTNodeType::PROC_DEF,
                    .parent = nullptr,
                    .proc_def = {
                        .name = token->identifier.content,
                        .parameters = slice_by_offset(
                            to_array(&proc_params_block),
                            proc_param_begin_index,
                            current_proc_param_index - proc_param_begin_index
                        ),
                        .return_type = return_type_node,
                    }
                });

                append_node(ASTNode {
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
                proc_node->proc_def.body = slice_by_offset(to_array(&nodes_block), 1, current_node_index);
            }
        }
    }

    return_result:
        // Allocate new blocks with the exact sizes and copy the data over to them
        auto nodes_block_arr = slice_by_offset(to_array(&nodes_block), 0, current_node_index);
        auto new_nodes_block = allocate_array_from_copy<ASTNode>(allocator, &nodes_block_arr);

        auto old_proc_params_arr = slice_by_offset(to_array(&proc_params_block), 0, current_proc_param_index);
        auto old_proc_params_block = allocate_array_from_copy<ProcParameterASTNode>(allocator, &old_proc_params_arr);

        // Reset the allocator offset to the initial value and re-allocate
        // the nodes and proc params blocks to be tightly packed
        allocator->offset = initial_marker.offset;

        auto new_nodes_block_arr = to_array(&new_nodes_block);
        new_nodes_block = allocate_array_from_copy<ASTNode>(allocator, &new_nodes_block_arr);
        assert(new_nodes_block.length == current_node_index &&
            "Node count mismatch after re-allocation");

        auto new_proc_params_arr = to_array(&old_proc_params_block);
        auto new_proc_params_block = allocate_array_from_copy<ProcParameterASTNode>(allocator, &new_proc_params_arr);
        assert(new_proc_params_block.length == current_proc_param_index &&
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