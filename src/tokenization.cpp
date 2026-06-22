#include <cctype>
#include <cstdio>
#include <bloom/defer.h>
#include <bloom/print.h>
#include <bloom/tokenization.h>

static auto next_char_or_null_char(Str *str, size_t current_index) -> char {
    if (current_index + 1 < str->length) {
        return str_char_at(str, current_index + 1);
    }
    else {
        return '\0';
    }
}

static inline auto to_array(AllocatedArrayBlock<Token> *tokens_block) -> Array<Token> {
    return Array<Token>(tokens_block->data, tokens_block->length);
}

/**
 * Tokenizes the input string into an array of tokens.
 *
 * The tokens are stored in an ArenaAllocator for efficient memory management.
 */
auto tokenize(Str *input, ArenaAllocator *allocator) -> Array<Token> {
    // Pre-allocate a string content buffer for multiline strings.
    // Must be allocated BEFORE the tokens array so that shrink_last_allocation
    // on tokens does not reclaim this memory.
    char *string_content_buf = reinterpret_cast<char*>(allocator->data + allocator->offset);
    // Round up to 8-byte alignment so the following Token array is properly aligned.
    size_t const str_buf_size = (input->length + 7) & ~(size_t)7;
    allocator->offset += str_buf_size;
    size_t string_content_offset = 0;

    // Allocate initially based on the input string length and
    // shrink the allocation later once the final token count is known
    auto tokens_block = allocate_array<Token>(allocator, input->length);
    auto result = to_array(&tokens_block);

    constexpr size_t COL_BEGIN = 1;

    Token::Position current_position = {
        .col = COL_BEGIN,
        .line = 1,
    };
    size_t current_token_index = 0;
    size_t first_indentation_space_count = 0;

    auto append_token = [&](Token &&token) {
        token.position = current_position;
        result[current_token_index] = token;
        current_token_index++;
    };
    auto append_token_of_type = [&](TokenType token_type) {
        result[current_token_index] = {
            .type = token_type,
            .position = current_position
        };
        current_token_index++;
    };

    for (size_t i = 0; i < input->length; i++) {
        char c = str_char_at(input, i);
        if (isalpha(c) || c == '_') {
            // Expect an identifier
            auto begin = i;
            while (i + 1 < input->length) {
                size_t next_i = i + 1;
                // Keep going if the next character is an alphabet
                if (isalnum(str_char_at(input, next_i)) || str_char_at(input, next_i) == '_') {
                    i++;
                }
                else {
                    break;
                }
            }
            auto end = i;
            auto identifier_len = end - begin + 1;

            defer(current_position.col += identifier_len);

            // If the text is a keyword
            auto word = str_slice(input, begin, identifier_len);
            if (word == TOKEN_KEYWORD_AND) {
                append_token_of_type(TokenType::KEYWORD_AND);
                continue;
            }
            if (word == TOKEN_KEYWORD_OR) {
                append_token_of_type(TokenType::KEYWORD_OR);
                continue;
            }
            if (word == TOKEN_KEYWORD_CVOID) {
                append_token_of_type(TokenType::KEYWORD_CVOID);
                continue;
            }
            if (word == TOKEN_KEYWORD_BREAK) {
                append_token_of_type(TokenType::KEYWORD_BREAK);
                continue;
            }
            if (word == TOKEN_KEYWORD_DEFER) {
                append_token_of_type(TokenType::KEYWORD_DEFER);
                continue;
            }
            if (word == TOKEN_KEYWORD_DYNAMIC) {
                append_token_of_type(TokenType::KEYWORD_DYNAMIC);
                continue;
            }
            if (word == TOKEN_KEYWORD_NIL) {
                append_token_of_type(TokenType::KEYWORD_NIL);
                continue;
            }
            if (word == TOKEN_KEYWORD_NOT) {
                if (i + 1 < input->length && str_char_at(input, i + 1) == '=') {
                    i++;
                    current_position.col += 1;
                    append_token_of_type(TokenType::NOT_EQUAL);
                }
                else {
                    append_token_of_type(TokenType::UNKNOWN);
                }
                continue;
            }
            if (word == TOKEN_KEYWORD_CONST) {
                append_token_of_type(TokenType::KEYWORD_CONST);
                continue;
            }
            if (word == TOKEN_KEYWORD_ELSE) {
                append_token_of_type(TokenType::KEYWORD_ELSE);
                continue;
            }
            if (word == TOKEN_KEYWORD_ENUM) {
                append_token_of_type(TokenType::KEYWORD_ENUM);
                continue;
            }
            if (word == TOKEN_KEYWORD_FALSE) {
                append_token_of_type(TokenType::KEYWORD_FALSE);
                continue;
            }
            if (word == TOKEN_KEYWORD_FOR) {
                append_token_of_type(TokenType::KEYWORD_FOR);
                continue;
            }
            if (word == TOKEN_KEYWORD_FOREIGN) {
                append_token_of_type(TokenType::KEYWORD_FOREIGN);
                continue;
            }
            if (word == TOKEN_KEYWORD_IF) {
                append_token_of_type(TokenType::KEYWORD_IF);
                continue;
            }
            if (word == TOKEN_KEYWORD_IN) {
                append_token_of_type(TokenType::KEYWORD_IN);
                continue;
            }
            if (word == TOKEN_KEYWORD_PACKAGE) {
                append_token_of_type(TokenType::KEYWORD_PACKAGE);
                continue;
            }
            if (word == TOKEN_KEYWORD_PASS) {
                append_token_of_type(TokenType::KEYWORD_PASS);
                continue;
            }
            if (word == TOKEN_KEYWORD_PROC) {
                append_token_of_type(TokenType::KEYWORD_PROC);
                continue;
            }
            if (word == TOKEN_KEYWORD_RETURN) {
                append_token_of_type(TokenType::KEYWORD_RETURN);
                continue;
            }
            if (word == TOKEN_KEYWORD_STRUCT) {
                append_token_of_type(TokenType::KEYWORD_STRUCT);
                continue;
            }
            if (word == TOKEN_KEYWORD_TRUE) {
                append_token_of_type(TokenType::KEYWORD_TRUE);
                continue;
            }

            // If the text wasn't a keyword, treat it as a regular identifier
            append_token({
                .type = TokenType::IDENTIFIER,
                .position = current_position,
                .identifier = {
                    .content = str_slice(input, begin, identifier_len)
                }
            });
        }
        else if (c == static_cast<char>(TokenType::COMMA)) {
            append_token_of_type(TokenType::COMMA);
            current_position.col += 1;
        }
        else if (c == static_cast<char>(TokenType::NEWLINE)) {
            append_token_of_type(TokenType::NEWLINE);
            current_position.line++;
            current_position.col = COL_BEGIN;
        }
        else if (c == ' ') {
            bool const at_line_start = (current_token_index == 0 ||
                result[current_token_index - 1].type == TokenType::NEWLINE);
            if (at_line_start) {
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
                    size_t level = indentation / first_indentation_space_count;

                    // Ensure the indentation is not inconsistent. If it is, create an error
                    if ((indentation % first_indentation_space_count) != 0) {
                        eprint("Inconsistent indentation\n");
                        exit(1);
                    }

                    append_token({
                        .type = TokenType::INDENT,
                        .indent = {
                            .level = level,
                        }
                    });
                    current_position.col += indentation;
                }
                else {
                    current_position.col += 1;
                }
            }
            else {
                current_position.col += 1;
            }
        }
        else if (isdigit(c)) {
            // Expect an integer literal
            auto begin = i;
            while (i + 1 < input->length) {
                // Keep going if the next character is a digit
                if (isdigit(str_char_at(input, i + 1))) {
                    i++;
                }
                else {
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
            current_position.col += (end - begin + 1);
        }
        else if (c == '-') {
            if (char next_char = next_char_or_null_char(input, i); next_char == '>') {
                i++;
                append_token_of_type(TokenType::ARROW);
                current_position.col += 2;
            }
            else {
                append_token_of_type(TokenType::SUBTRACT);
                current_position.col += 1;
            }
        }
        else if (c == static_cast<char>(TokenType::BRACKET_OPEN)) {
            append_token_of_type(TokenType::BRACKET_OPEN);
            current_position.col += 1;
        }
        else if (c == static_cast<char>(TokenType::BRACKET_CLOSE)) {
            append_token_of_type(TokenType::BRACKET_CLOSE);
            current_position.col += 1;
        }
        else if (c == static_cast<char>(TokenType::BRACE_CLOSE)) {
            append_token_of_type(TokenType::BRACE_CLOSE);
        }
        else if (c == static_cast<char>(TokenType::BRACE_OPEN)) {
            append_token_of_type(TokenType::BRACE_OPEN);
            current_position.col += 1;
        }
        else if (c == static_cast<char>(TokenType::PARENTHESIS_CLOSE)) {
            append_token_of_type(TokenType::PARENTHESIS_CLOSE);
            current_position.col += 1;
        }
        else if (c == static_cast<char>(TokenType::PARENTHESIS_OPEN)) {
            append_token_of_type(TokenType::PARENTHESIS_OPEN);
            current_position.col += 1;
        }
        else if (c == ':') {
            char next_char = next_char_or_null_char(input, i);
            switch (next_char) {
                case ':':
                    i++;
                    append_token_of_type(TokenType::CONST_DEF);
                    current_position.col += 2;
                    break;
                case '=':
                    i++;
                    append_token_of_type(TokenType::VAR_DEF);
                    current_position.col += 2;
                    break;
                default:
                    append_token_of_type(TokenType::TYPE_SEPARATOR);
                    current_position.col += 1;
                    break;
            }
        }
        else if (c == '\\') {
            // Line continuation: \ immediately followed by a newline
            if (i + 1 < input->length && str_char_at(input, i + 1) == '\n') {
                i++; // skip '\n'; for loop's i++ will advance past it
                current_position.line++;
                current_position.col = COL_BEGIN;
                // The next line's leading spaces are not at line start
                // (no NEWLINE token was emitted), so they won't generate
                // an INDENT token — they just advance col.
            }
        }
        else if(c == '"') {
            // Check for triple-quoted multiline string """
            if (i + 2 < input->length &&
                str_char_at(input, i + 1) == '"' &&
                str_char_at(input, i + 2) == '"')
            {
                // Base indentation: number of spaces before the opening """
                size_t const base_indent = current_position.col - 1;
                size_t j = i + 3; // first char after opening """

                if (j >= input->length || str_char_at(input, j) != '\n') {
                    eprint("Multiline string opening \"\"\" must be immediately followed by a newline\n");
                    exit(1);
                }
                j++; // skip the newline after """
                current_position.line++;
                current_position.col = COL_BEGIN;

                char *content_start = string_content_buf + string_content_offset;
                size_t content_len = 0;
                bool found_closing = false;

                while (j < input->length) {
                    // Strip exactly base_indent leading spaces from this content line
                    for (size_t si = 0; si < base_indent && j < input->length && str_char_at(input, j) == ' '; si++) {
                        j++;
                    }

                    // Check for closing """
                    if (j + 2 < input->length &&
                        str_char_at(input, j) == '"' &&
                        str_char_at(input, j + 1) == '"' &&
                        str_char_at(input, j + 2) == '"')
                    {
                        j += 3; // consume closing """
                        // Advance j to the newline ending the closing """ line
                        while (j < input->length && str_char_at(input, j) != '\n') {
                            j++;
                        }
                        // j now points to '\n' (or end of input)
                        found_closing = true;
                        break;
                    }

                    // Copy content of this line (up to the trailing newline)
                    while (j < input->length && str_char_at(input, j) != '\n') {
                        string_content_buf[string_content_offset + content_len] = str_char_at(input, j);
                        content_len++;
                        j++;
                    }
                    // Skip the newline (not part of string content)
                    if (j < input->length && str_char_at(input, j) == '\n') {
                        j++;
                        current_position.line++;
                        current_position.col = COL_BEGIN;
                    }
                }

                if (!found_closing) {
                    eprint("Multiline string not closed with \"\"\"\n");
                    exit(1);
                }

                string_content_offset += content_len;

                bool has_interp = false;
                for (size_t k = 0; k + 1 < content_len; k++) {
                    if (content_start[k] == '$' && content_start[k + 1] == '{') {
                        has_interp = true;
                        break;
                    }
                }
                append_token({
                    .type = has_interp ? TokenType::INTERP_STRING_LITERAL : TokenType::STRING_LITERAL,
                    .string_literal = {
                        .content = str_from_data_and_length(content_start, content_len)
                    }
                });

                // Leave i so that after the for loop's i++, it points to the '\n'
                // that follows the closing """ (or stays at end of input).
                // The '\n' will be processed as a NEWLINE token on the next iteration.
                i = (j > 0) ? j - 1 : 0;
            }
            else {
                // Regular single-quoted string literal
                auto begin = i + 1;
                while (i + 1 < input->length) {
                    i++;
                    if (str_char_at(input, i) == '"') {
                        break;
                    }
                }
                auto string_len = i - begin;
                bool has_interp = false;
                for (size_t k = begin; k + 1 < begin + string_len; k++) {
                    if (str_char_at(input, k) == '$' && str_char_at(input, k + 1) == '{') {
                        has_interp = true;
                        break;
                    }
                }
                append_token({
                    .type = has_interp ? TokenType::INTERP_STRING_LITERAL : TokenType::STRING_LITERAL,
                    .string_literal = {
                        .content = str_slice(input, begin, string_len)
                    },
                });
                current_position.col += (string_len + 2); // +2 for the quotes
            }
        }
        else if (c == '<') {
            append_token_of_type(TokenType::LESS_THAN);
            current_position.col += 1;
        }
        else if (c == '.') {
            if (i + 2 < input->length &&
                str_char_at(input, i + 1) == '.' &&
                str_char_at(input, i + 2) == '<')
            {
                i += 2;
                append_token_of_type(TokenType::RANGE_EXCLUSIVE);
                current_position.col += 3;
            }
            else if (i + 2 < input->length &&
                str_char_at(input, i + 1) == '.' &&
                str_char_at(input, i + 2) == '+')
            {
                i += 2;
                append_token_of_type(TokenType::RANGE_COUNTED);
                current_position.col += 3;
            }
            else if (i + 2 < input->length &&
                str_char_at(input, i + 1) == '.' &&
                str_char_at(input, i + 2) == '=')
            {
                i += 2;
                append_token_of_type(TokenType::RANGE_INCLUSIVE);
                current_position.col += 3;
            }
            else if (i + 1 < input->length && str_char_at(input, i + 1) == '.') {
                i++;
                append_token_of_type(TokenType::RANGE);
                current_position.col += 2;
            }
            else {
                append_token_of_type(TokenType::DOT);
                current_position.col += 1;
            }
        }
        else if (c == '=') {
            if (char next_char = next_char_or_null_char(input, i); next_char == '=') {
                i++;
                append_token_of_type(TokenType::EQUAL_EQUAL);
                current_position.col += 2;
            }
            else {
                append_token_of_type(TokenType::EQUALS);
                current_position.col += 1;
            }
        }
        else if (c == static_cast<char>(TokenType::MULTIPLY)) {
            append_token_of_type(TokenType::MULTIPLY);
            current_position.col += 1;
        }
        else if (c == static_cast<char>(TokenType::ADD)) {
            if (char next_char = next_char_or_null_char(input, i); next_char == '=') {
                i++;
                append_token_of_type(TokenType::ADD_ASSIGN);
                current_position.col += 2;
            }
            else {
                append_token_of_type(TokenType::ADD);
                current_position.col += 1;
            }
        }
        else if (c == '%') {
            append_token_of_type(TokenType::ADDRESS_OF);
            current_position.col += 1;
        }
        else if (c == '^') {
            append_token_of_type(TokenType::CARET);
            current_position.col += 1;
        }
        else if (c == '/') {
            if (char next_char = next_char_or_null_char(input, i); next_char == '/') {
                while (i + 1 < input->length && str_char_at(input, i + 1) != '\n') {
                    i++;
                }
            }
            else {
                append_token_of_type(TokenType::DIVIDE);
                current_position.col += 1;
            }
        }
    }

    append_token_of_type(TokenType::END);

    // The final token count is known now, shrink the allocation
    tokens_block = shrink_last_allocation(allocator, &tokens_block, current_token_index);
    result.length = tokens_block.length;
    return result;
}
