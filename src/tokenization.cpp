#include <cctype>
#include <cstdio>
#include <bloom/print.h>
#include <bloom/tokenization.h>
#include <cstring>

static auto next_char_or_null_char(String *str, size_t current_index) -> char {
    if (current_index + 1 < str->length) {
        return char_at(str, current_index + 1);
    } else {
        return '\0';
    }
}

/**
 * Tokenizes the input string into an array of tokens.
 *
 * The tokens are stored in an ArenaAllocator for efficient memory management.
 */
auto tokenize(String *input, ArenaAllocator *allocator) -> Array<Token> {
    // Allocate initially based on the input string length and
    // shrink the allocation later once the final token count is known
    auto tokens_block = allocate_array<Token>(allocator, input->length);
    auto result = Array<Token> {
        .data = tokens_block.data,
        .length = tokens_block.length
    };
    size_t current_token_index = 0;

    auto append_token = [&](Token &&token) {
        result[current_token_index] = token;
        current_token_index++;
    };
    auto append_token_of_type = [&](TokenType token_type) {
        result[current_token_index] = { .type = token_type };
        current_token_index++;
    };

    size_t first_indentation_space_count = 0;

    for (size_t i = 0; i < input->length; i++) {
        char c = char_at(input, i);
        if (isalpha(c)) {
            // Expect an identifier
            auto begin = i;
            while (i + 1 < input->length) {
                // Keep going if the next character is an alphabet
                if (isalnum(char_at(input, i + 1))) {
                    i++;
                } else {
                    break;
                }
            }
            auto end = i;
            auto identifier_len = end - begin + 1;
            if (identifier_len == 4) {
                // Check for the 'proc' keyword
                if (strncmp(input->data + begin, TOKEN_KEYWORD_PROC, identifier_len) == 0) {
                    append_token_of_type(TokenType::KEYWORD_PROC);
                    continue;
                }
            }
            // Otherwise, it's a regular identifier
            append_token({
                .type = TokenType::IDENTIFIER,
                .identifier = {
                    .content = String::from_data_and_length(
                        input->data + begin,
                        identifier_len
                    )
                }
            });
        }
        else if (c == static_cast<char>(TokenType::COMMA)) {
            append_token_of_type(TokenType::COMMA);
        }
        else if (c == static_cast<char>(TokenType::NEWLINE)) {
            append_token_of_type(TokenType::NEWLINE);
        }
        else if (c == ' ') {
            if (char next_char = next_char_or_null_char(input, i); next_char == ' ') {
                i++;
                // 2 for the number of spaces that were consumed already
                size_t space_begin = i - 2;
                do {
                    i++;
                    next_char = next_char_or_null_char(input, i);
                } while (next_char == ' ');

                size_t indentation = i - space_begin;
                if (first_indentation_space_count == 0) {
                    first_indentation_space_count = indentation;
                }
                size_t level = first_indentation_space_count / indentation;

                // Ensure the indentation is not inconsistent. If it is, create an error
                if ((first_indentation_space_count % indentation) != 0) {
                    eprint("Inconsistent indentation\n");
                    exit(1);
                }

                append_token({
                    .type = TokenType::INDENT,
                    .indent = {
                        .level = level,
                    }
                });
            }
        }
        else if (isdigit(c)) {
            // Expect an integer literal
            auto begin = i;
            while (i + 1 < input->length) {
                // Keep going if the next character is a digit
                if (isdigit(char_at(input, i + 1))) {
                    i++;
                } else {
                    break;
                }
            }
            auto end = i;
            append_token({
                .type = TokenType::INTEGER_LITERAL,
                .integer_literal = {
                    .value = strtol(input->data + begin, nullptr, 10)
                }
            });
        }
        else if (c == '-') {
            if (char next_char = next_char_or_null_char(input, i); next_char == '>') {
                i++;
                append_token_of_type(TokenType::ARROW);
            }
        }
        else if (c == static_cast<char>(TokenType::BRACE_CLOSE)) {
            append_token_of_type(TokenType::BRACE_CLOSE);
        }
        else if (c == static_cast<char>(TokenType::BRACE_OPEN)) {
            append_token_of_type(TokenType::BRACE_OPEN);
        }
        else if (c == static_cast<char>(TokenType::PARENTHESIS_CLOSE)) {
            append_token_of_type(TokenType::PARENTHESIS_CLOSE);
        }
        else if (c == static_cast<char>(TokenType::PARENTHESIS_OPEN)) {
            append_token_of_type(TokenType::PARENTHESIS_OPEN);
        }
        else if (c == ':') {
            if (
                char next_char = next_char_or_null_char(input, i);
                next_char == ':'
            ) {
                i++;
                append_token_of_type(TokenType::CONST_DEF);
            }
            else if(next_char == '=') {
                i++;
                append_token_of_type(TokenType::VAR_DEF);
            }
            else {
                append_token_of_type(TokenType::TYPE_SEPARATOR);
            }
        }
        else if (c == static_cast<char>(TokenType::ADD)) {
            append_token_of_type(TokenType::ADD);
        }
    }

    append_token_of_type(TokenType::END);

    // The final token count is known now, shrink the allocation
    tokens_block = shrink_last_allocation(allocator, &tokens_block, current_token_index);
    result.length = tokens_block.length;
    print("Tokens block allocation size: % (% tokens)\n", allocation_size(&tokens_block), tokens_block.length);
    return result;
}
