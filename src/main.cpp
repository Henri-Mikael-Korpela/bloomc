#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "types.h"
#include "allocation.h"
#include "string.h"
#include "tokenization.h"

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
    close(fd);
    if (mapped_memory == MAP_FAILED) {
        perror("Error mapping the input source file");
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
            case TokenType::INTEGER_LITERAL:
                printf("\tInteger literal: %li\n",
                    token.integer_literal.value
                );
                break;
        }
    }

    munmap(mapped_memory, file_stat.st_size);

    printf("Main memory left: %zu\n", memory_left(&main_allocator));
    return 0;
}