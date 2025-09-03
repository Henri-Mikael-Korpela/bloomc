#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef unsigned char byte;

template<typename T>
struct Array {
    T *data;
    size_t length;

    inline T& operator[](size_t index) {
        assert(index < length && "ArrayPtr index out of bounds");
        return data[index];
    }

    // To support range-based for loops
    inline T* begin() { return data; }
    inline T* end() { return data + length; }
};

template<typename T>
struct AllocatedArrayBlock {
    T *data;
    size_t length;
    /**
     * The offset in the allocator when this block was allocated.
     */
    size_t allocation_offset;
};

struct ArenaAllocator {
    byte *data;
    size_t length;
    size_t offset;

    ArenaAllocator(size_t size) : offset(0), length(size) {
        data = static_cast<byte*>(malloc(size));
        assert(data != nullptr && "Failed to allocate memory for ArenaAllocator");
    }
    ~ArenaAllocator() {
        free(data);
    }
};

template<typename T>
AllocatedArrayBlock<T> allocate_object_array(ArenaAllocator *allocator, size_t length) {
    size_t required_size = length * sizeof(T);
    assert(allocator->offset + required_size <= allocator->length &&
        "Failed to allocate object array from ArenaAllocator");
    T* array = reinterpret_cast<T*>(allocator->data + allocator->offset);
    allocator->offset += required_size;
    return { array, length };
}

template<typename T>
size_t allocation_size(AllocatedArrayBlock<T> *block) {
    return block->length * sizeof(T);
}

inline size_t memory_left(ArenaAllocator *allocator) {
    return allocator->length - allocator->offset;
}

/**
 * Shrink the most recent array allocation by changing the length.
 */
template<typename T>
AllocatedArrayBlock<T> shrink_last_allocation(
    ArenaAllocator *allocator,
    AllocatedArrayBlock<T> *block,
    size_t new_length
) {
    assert(new_length <= block->length &&
        "New length must be less than or equal to the current length");
    assert(block->allocation_offset + block->length * sizeof(T) == allocator->offset &&
        "Can only shrink the most recent allocation");
    // Update the allocator's offset
    allocator->offset -= (block->length - new_length) * sizeof(T);
    // Update the block's length
    block->length = new_length;
    return *block;
}

/**
 * Represents a string, whose length is determined
 * by a length field instead of null-termination.
 */
struct String {
    char *data;
    size_t length;

    static inline String from_data_and_length(char *data, size_t length) {
        return String {
            .data = data,
            .length = length,
        };
    }
    static String from_null_terminated_str(char *value) {
        String str;
        str.length = strlen(value);
        str.data = value;
        return str;
    }
};
static_assert(sizeof(String) == 16, "String size is not 16 bytes");

inline char char_at(String *str, size_t index) {
    assert(index < str->length && "String index out of bounds");
    return str->data[index];
}

char next_char_or_null_char(String *str, size_t current_index) {
    if (current_index + 1 < str->length) {
        return char_at(str, current_index + 1);
    } else {
        return '\0';
    }
}

enum class TokenType : uint8_t {
    UNKNOWN = 0,
    ARROW,
    CONST_DEF,
    IDENTIFIER,
    INDENT,
    ADD = '+',
    BRACE_CLOSE = '}',
    BRACE_OPEN = '{',
    COMMA = ',',
    NEWLINE = '\n',
    PARENTHESIS_CLOSE = ')',
    PARENTHESIS_OPEN = '(',
    TYPE_SEPARATOR = ':',
};

struct Token {
    TokenType type;
    union {
        struct {
            String content;
        } identifier;
        struct {
            size_t level;
        } indent;
    };
};
static_assert(sizeof(Token) == 24, "Token size is not 24 bytes");

constexpr String to_string(TokenType type) {
    #define STR(x) String::from_null_terminated_str(const_cast<char*>(x))
    switch (type) {
        case TokenType::ADD:               return STR("+");
        case TokenType::ARROW:             return STR("->");
        case TokenType::BRACE_CLOSE:       return STR("}");
        case TokenType::BRACE_OPEN:        return STR("{");
        case TokenType::COMMA:             return STR(",");
        case TokenType::CONST_DEF:         return STR("const_def");
        case TokenType::IDENTIFIER:        return STR("identifier");
        case TokenType::INDENT:            return STR("indent");
        case TokenType::NEWLINE:           return STR("newline");
        case TokenType::PARENTHESIS_CLOSE: return STR(")");
        case TokenType::PARENTHESIS_OPEN:  return STR("(");
        case TokenType::TYPE_SEPARATOR:    return STR(":");
        default:                           return STR("undefined");
    }
    #undef STR
}

/**
 * Tokenizes the input string into an array of tokens.
 *
 * The tokens are stored in an ArenaAllocator for efficient memory management.
 */
Array<Token> tokenize(String *input, ArenaAllocator *allocator) {
    // Allocate initially based on the input string length and
    // shrink the allocation later once the final token count is known
    auto tokens_block = allocate_object_array<Token>(allocator, input->length);
    auto result = Array<Token> {
        .data = tokens_block.data,
        .length = tokens_block.length
    };
    size_t current_token_index = 0;

    for (size_t i = 0; i < input->length; i++) {
        char c = char_at(input, i);
        if (isalpha(c)) {
            // Expect an identifier
            auto begin = i;
            while (i + 1 < input->length) {
                // Keep going if the next character is an alphabet
                if (isalpha(char_at(input, i + 1))) {
                    i++;
                } else {
                    break;
                }
            }
            auto end = i;
            auto token = Token {
                .type = TokenType::IDENTIFIER,
                .identifier = {
                    .content = String::from_data_and_length(
                        input->data + begin,
                        end - begin + 1
                    )
                }
            };
            result[current_token_index] = token;
            current_token_index++;
        }
        else if (c == static_cast<char>(TokenType::COMMA)) {
            auto token = Token {
                .type = TokenType::COMMA
            };
            result[current_token_index] = token;
            current_token_index++;
        }
        else if (c == static_cast<char>(TokenType::NEWLINE)) {
            auto token = Token {
                .type = TokenType::NEWLINE
            };
            result[current_token_index] = token;
            current_token_index++;
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
                auto token = Token {
                    .type = TokenType::INDENT,
                    .indent = {
                        .level = i - space_begin
                    }
                };
                result[current_token_index] = token;
                current_token_index++;
            }
        }
        else if (c == '-') {
            if (char next_char = next_char_or_null_char(input, i); next_char == '>') {
                i++;
                auto token = Token {
                    .type = TokenType::ARROW
                };
                result[current_token_index] = token;
                current_token_index++;
            }
        }
        else if (c == static_cast<char>(TokenType::BRACE_CLOSE)) {
            auto token = Token {
                .type = TokenType::BRACE_CLOSE
            };
            result[current_token_index] = token;
            current_token_index++;
        }
        else if (c == static_cast<char>(TokenType::BRACE_OPEN)) {
            auto token = Token {
                .type = TokenType::BRACE_OPEN
            };
            result[current_token_index] = token;
            current_token_index++;
        }
        else if (c == static_cast<char>(TokenType::PARENTHESIS_CLOSE)) {
            auto token = Token {
                .type = TokenType::PARENTHESIS_CLOSE
            };
            result[current_token_index] = token;
            current_token_index++;
        }
        else if (c == static_cast<char>(TokenType::PARENTHESIS_OPEN)) {
            auto token = Token {
                .type = TokenType::PARENTHESIS_OPEN
            };
            result[current_token_index] = token;
            current_token_index++;
        }
        else if (c == ':') {
            if (
                char next_char = next_char_or_null_char(input, i);
                next_char == ':'
            ) {
                i++;
                auto token = Token {
                    .type = TokenType::CONST_DEF,
                };
                result[current_token_index] = token;
                current_token_index++;
            }
            else {
                auto token = Token {
                    .type = TokenType::TYPE_SEPARATOR,
                };
                result[current_token_index] = token;
                current_token_index++;
            }
        }
        else if (c == static_cast<char>(TokenType::ADD)) {
            auto token = Token {
                .type = TokenType::ADD
            };
            result[current_token_index] = token;
            current_token_index++;
        }
    }

    // The final token count is known now, shrink the allocation
    tokens_block = shrink_last_allocation(allocator, &tokens_block, current_token_index);
    result.length = tokens_block.length;
    printf("Tokens block allocation size: %zu (%zu tokens)\n", allocation_size(&tokens_block), tokens_block.length);
    return result;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: %s run <input_file_path>\n", argv[0]);
        return 1;
    }

    auto current_path = std::filesystem::current_path();
    printf("Current path: %s\n", current_path.string().c_str());

    if (strncmp(argv[1], "run", 3) != 0) {
        printf("Error: First argument must be 'run'\n");
        return 1;
    }

    auto input_file_path = std::filesystem::path(argv[2]);
    printf("Input file path: %s\n", input_file_path.string().c_str());

    if (!std::filesystem::exists(input_file_path)) {
        printf("Error: Input file does not exist\n");
        return 1;
    }

    // Convert the input file path to an absolute path
    input_file_path = std::filesystem::absolute(input_file_path);

    int fd = open(input_file_path.c_str(), O_RDONLY);
    if (fd == -1) {
        perror("Error opening the input source file");
        return 1;
    }

    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1) {
        perror("Error getting the input source file status");
        close(fd);
        return 1;
    }

    byte *mapped_memory = static_cast<byte*>(mmap(nullptr, file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (mapped_memory == MAP_FAILED) {
        perror("Error mapping the input source file");
        close(fd);
        return 1;
    }

    auto main_allocator = ArenaAllocator(2048);

    // Print the file contents
    printf("File contents: %s\n", reinterpret_cast<char*>(mapped_memory));

    // Tokenize the input
    auto input_file_content = String::from_null_terminated_str(reinterpret_cast<char*>(mapped_memory));
    Array<Token> tokens = tokenize(&input_file_content, &main_allocator);
    printf("Tokenized %zu tokens\n", tokens.length);

    for (size_t i = 0; i < tokens.length; i++) {
        auto &token = tokens[i];
        auto token_type = to_string(token.type);
        printf("Token %zu %.*s\n",
            i,
            static_cast<int>(token_type.length), token_type.data
        );
        switch (token.type) {
            case TokenType::IDENTIFIER:
                printf("\t%.*s (%zu chars)\n",
                    static_cast<int>(token.identifier.content.length),
                    token.identifier.content.data,
                    token.identifier.content.length
                );
                break;
            case TokenType::INDENT:
                printf("\tIndentation level: %zu\n",
                    token.indent.level
                );
                break;
        }
    }

    munmap(mapped_memory, file_stat.st_size);
    close(fd);

    printf("Main memory left: %zu\n", memory_left(&main_allocator));
    return 0;
}