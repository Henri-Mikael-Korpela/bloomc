#include <filesystem>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <bloom/defer.h>
#include <bloom/print.h>
#include <bloom/transpilation.h>

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

    auto MISSING_TYPE = String::from_null_terminated_str("(none)");

    for (auto &node : ast_nodes) {
        if (node.parent != nullptr) {
            continue;
        }
        print("AST Node type: %\n", to_string(node.type));
        switch (node.type) {
            case ASTNodeType::BINARY_ADD:
                print("\tBinary operation: % + %\n",
                    node.binary_operation.identifier_left,
                    node.binary_operation.identifier_right
                );
                break;
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
                String return_type_name = node.proc_def.return_type
                    ? node.proc_def.return_type->name
                    : MISSING_TYPE;
                print("\tProcedure return type: %\n", return_type_name);
                printf("\tProcedure body:\n");
                for (auto &statement : node.proc_def.body) {
                    printf("\t\tStatement\n");
                }
                break;
        }
    }

    // Transpile AST nodes into C source code
    auto target_file_path = String::from_null_terminated_str("/home/henri/Personal/bloomc2/sum.c");
    transpile_to_c(&target_file_path, &ast_nodes, &main_allocator);

    print(
        "Main memory left: %, used: %\n",
        memory_left(&main_allocator),
        main_allocator.length - memory_left(&main_allocator)
    );

    delete_allocator(&main_allocator);
    return 0;
}