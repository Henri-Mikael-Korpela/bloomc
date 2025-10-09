#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <bloom/types.h>
#include <bloom/allocation.h>
#include <bloom/defer.h>
#include <bloom/print.h>
#include <bloom/ptr.h>
#include <bloom/string.h>
#include <bloom/tokenization.h>

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

enum class ASTNodeType : uint8_t {
    UNKNOWN = 0,
    PROC_DEF,
};

struct AstNodeProcParameter {
    String name;
};
static_assert(sizeof(AstNodeProcParameter) == 16, "AstNodeProcParameter size is not 16 bytes");

struct ASTNode {
    ASTNodeType type;
    struct {
        String name;
        Array<AstNodeProcParameter> parameters;
    } proc_def;
};
static_assert(sizeof(ASTNode) == 40, "ASTNode size is not 40 bytes");

struct TokenIterator {
    Array<Token> *tokens;
    size_t current_index;
};

inline auto token_is_of_type(Token *token, TokenType type) -> bool {
    return token != nullptr && token->type == type;
}
/**
 * Advances the iterator and returns the next token, or nullptr if at the end.
 */
auto tokens_next(TokenIterator *iter) -> Token* {
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
auto tokens_peek(TokenIterator *iter) -> Token* {
    if (iter->current_index < iter->tokens->length) {
        return &iter->tokens->data[iter->current_index];
    } else {
        return nullptr;
    }
}

Array<ASTNode> parse(Array<Token> *tokens, ArenaAllocator *allocator) {
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
        print("Allocator offset after extra allocations: %\n", allocator_offset_after_extra_allocations);

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

constexpr auto to_string(ASTNodeType type) -> String {
    #define STR(x) String::from_null_terminated_str(x)
    switch (type) {
        case ASTNodeType::PROC_DEF: return STR("procedure definition");
        default:                    return STR("undefined");
    }
    #undef STR
}

constexpr size_t kb(size_t n) { return n * 1024; }
constexpr size_t mb(size_t n) { return n * 1024 * 1024; }

const size_t MAIN_MEMORY_SIZE = kb(3);

int main(int argc, char* argv[]) {
    if (argc < 3) {
        eprint("Usage: % run <input_file_path>\n", argv[0]);
        return 1;
    }

    auto current_path = std::filesystem::current_path();

    if (strncmp(argv[1], "run", 3) != 0) {
        eprint("Error: First argument must be 'run'\n");
        return 1;
    }

    auto input_file_path = std::filesystem::path(argv[2]);
    print("Input file path: %\n", input_file_path.string().c_str());

    if (!std::filesystem::exists(input_file_path)) {
        eprint("Error: Input file does not exist\n");
        return 1;
    }

    // Convert the input file path to an absolute path
    input_file_path = std::filesystem::absolute(input_file_path);

    int fd = open(input_file_path.c_str(), O_RDONLY);
    if (fd == -1) {
        eprint("Error opening the input source file");
        return 1;
    }

    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1) {
        eprint("Error getting the input source file status");
        close(fd);
        return 1;
    }

    byte *mapped_memory = static_cast<byte*>(mmap(nullptr, file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
    defer(munmap(mapped_memory, file_stat.st_size));

    // Memory mapping done, the file descriptor no longer needed
    close(fd);

    if (mapped_memory == MAP_FAILED) {
        eprint("Error mapping the input source file");
        return 1;
    }

    auto main_allocator = ArenaAllocator(MAIN_MEMORY_SIZE);

    // Print the file contents
    print("File contents: %\n", reinterpret_cast<char*>(mapped_memory));

    // Tokenize the input
    auto input_file_content = String::from_null_terminated_str(reinterpret_cast<char*>(mapped_memory));
    Array<Token> tokens = tokenize(&input_file_content, &main_allocator);
    print("Tokenized % tokens\n", tokens.length);

    for (auto &token : tokens) {
        auto token_type = to_string(token.type);
        print("Token %\n", to_string(token.type));
        switch (token.type) {
            case TokenType::IDENTIFIER:
                print("\t% (% chars)\n",
                    token.identifier.content,
                    token.identifier.content.length
                );
                break;
            case TokenType::INDENT:
                print("\tIndentation level: %\n",
                    token.indent.level
                );
                break;
            case TokenType::INTEGER_LITERAL:
                print("\tInteger literal: %\n",
                    token.integer_literal.value
                );
                break;
            case TokenType::KEYWORD_PROC:
                print("\tKeyword: %\n", TOKEN_KEYWORD_PROC);
                break;
        }
    }

    // Parse the tokens into an AST
    auto ast_nodes = parse(&tokens, &main_allocator);

    for (auto &node : ast_nodes) {
        print("AST Node type: %\n", to_string(node.type));
        switch (node.type) {
            case ASTNodeType::PROC_DEF:
                print("\tProcedure name: % (% chars)\n",
                    node.proc_def.name,
                    node.proc_def.name.length
                );
                print("\tProcedure parameters (%):\n", node.proc_def.parameters.length);
                for (size_t i = 0; i < node.proc_def.parameters.length; i++) {
                    auto &param = node.proc_def.parameters[i];
                    print("\t\t%: % (% chars)\n", i, param.name, param.name.length);
                }
                break;
        }
    }

    print(
        "Main memory left: %, used: %\n",
        memory_left(&main_allocator),
        main_allocator.length - memory_left(&main_allocator)
    );

    delete_allocator(&main_allocator);
    return 0;
}