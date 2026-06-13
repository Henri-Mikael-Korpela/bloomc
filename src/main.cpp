#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <bloom/defer.h>
#include <bloom/print.h>
#include <bloom/transpilation.h>

constexpr auto kb(size_t n) -> size_t { return n * 1024; }
constexpr auto mb(size_t n) -> size_t { return n * 1024 * 1024; }

constexpr size_t MAIN_MEMORY_SIZE = kb(256);

/**
 * Allocates a null-terminated C string.
 */
static auto allocate_null_terminated_str_from_str(ArenaAllocator *allocator, Str *str) -> char* {
    // +1 for null-terminator
    size_t required_size = (str->length + 1) * sizeof(char);
    assert(allocator->offset + required_size <= allocator->length &&
        "Failed to allocate C string from ArenaAllocator");
    char *c_str = reinterpret_cast<char*>(allocator->data + allocator->offset);
    allocator->offset += required_size;
    memcpy(c_str, str->data, str->length * sizeof(char));
    // Null-terminate the string
    c_str[str->length] = '\0';
    return c_str;
}

static auto shell_quote(std::string const &arg) -> std::string {
    std::string result = "'";
    for (char c : arg) {
        if (c == '\'') {
            result += "'\\''";
        }
        else {
            result += c;
        }
    }
    result += "'";
    return result;
}

auto run(int argc, char* argv[]) -> int {
    char const *input_file = nullptr;
    int pass_args_start = argc;

    for (int i = 2; i < argc; i++) {
        if (strncmp(argv[i], "--input-file", 12) == 0 && i + 1 < argc) {
            input_file = argv[++i];
        }
        else {
            pass_args_start = i;
            break;
        }
    }

    if (input_file == nullptr) {
        eprint("Error: --input-file is required\n");
        return 1;
    }

    auto input_file_path = std::filesystem::path(input_file);

    if (!std::filesystem::exists(input_file_path)) {
        eprint("Error: Input file does not exist\n");
        return 1;
    }

    input_file_path = std::filesystem::absolute(input_file_path);

    int src_fd = open(input_file_path.c_str(), O_RDONLY);
    if (src_fd == -1) {
        eprint("Error opening the input source file\n");
        return 1;
    }

    struct stat file_stat;
    if (fstat(src_fd, &file_stat) == -1) {
        eprint("Error getting the input source file status\n");
        close(src_fd);
        return 1;
    }

    byte *mapped_memory = static_cast<byte*>(mmap(nullptr, file_stat.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0));
    defer(munmap(mapped_memory, file_stat.st_size));
    close(src_fd);

    if (mapped_memory == MAP_FAILED) {
        eprint("Error mapping the input source file\n");
        return 1;
    }

    auto main_allocator = ArenaAllocator(MAIN_MEMORY_SIZE);

    auto input_file_content = cstr_to_str(reinterpret_cast<char*>(mapped_memory));
    Array<Token> tokens = tokenize(&input_file_content, &main_allocator);
    bool had_parse_errors = false;
    auto ast_nodes = parse(&tokens, &main_allocator, input_file_content, cstr_to_str(input_file), &had_parse_errors);
    if (had_parse_errors) {
        delete_allocator(&main_allocator);
        return 1;
    }

    std::filesystem::create_directories("build/tmp");
    char temp_c_file[] = "build/tmp/transpiled_XXXXXX.c";
    int temp_fd = mkstemps(temp_c_file, 2);

    if (temp_fd == -1) {
        eprint("Error creating temporary file\n");
        delete_allocator(&main_allocator);
        return 1;
    }

    auto c_code = transpile_to_c(&ast_nodes, &main_allocator);
    write(temp_fd, c_code.data, c_code.length);
    close(temp_fd);

    std::string temp_binary(temp_c_file, strlen(temp_c_file) - 2);
    std::string gcc_cmd = "gcc -o " + temp_binary + " " + temp_c_file;

    if (system(gcc_cmd.c_str()) != 0) {
        eprint("Error compiling generated C code\n");
        std::filesystem::remove(temp_c_file);
        std::filesystem::remove(temp_binary);
        delete_allocator(&main_allocator);
        return 1;
    }

    std::string run_cmd = shell_quote(temp_binary);
    for (int i = pass_args_start; i < argc; i++) {
        run_cmd += ' ';
        run_cmd += shell_quote(argv[i]);
    }

    int exit_code = system(run_cmd.c_str());

    std::filesystem::remove(temp_c_file);
    std::filesystem::remove(temp_binary);

    delete_allocator(&main_allocator);
    return exit_code;
}

auto transpile(char const *input_file_path_cstr, char const *output_file_path_cstr) -> int {
    auto input_file_path = std::filesystem::path(input_file_path_cstr);
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
    auto input_file_content = cstr_to_str(reinterpret_cast<char*>(mapped_memory));
    Array<Token> tokens = tokenize(&input_file_content, &main_allocator);
    print("Tokenized % tokens\n", tokens.length);

    for (size_t i = 0; i < tokens.length; i++) {
        auto const *token = &tokens.data[i];
        auto token_type = to_string(token->type);
        print("Token %:% %", token->position.line, token->position.col, to_string(token->type));
        switch (token->type) {
            case TokenType::IDENTIFIER:
                print(" | % (% chars)",
                    token->identifier.content,
                    token->identifier.content.length
                );
                break;
            case TokenType::INDENT:
                print(" | level: %",
                    token->indent.level
                );
                break;
            case TokenType::INTEGER_LITERAL:
                print(" | value: %",
                    token->integer_literal.value
                );
                break;
            case TokenType::KEYWORD_PROC:
                print(" | keyword: %", TOKEN_KEYWORD_PROC);
                break;
            case TokenType::STRING_LITERAL:
                print(" | content: %",
                    token->string_literal.content
                );
                break;
        }
        printf("\n");
    }
    printf("\n");

    // Parse the tokens into an AST
    bool had_parse_errors = false;
    auto ast_nodes = parse(&tokens, &main_allocator, input_file_content, cstr_to_str(input_file_path_cstr), &had_parse_errors);
    if (had_parse_errors) {
        delete_allocator(&main_allocator);
        return 1;
    }

    auto MISSING_TYPE = cstr_to_str("(none)");

    for (size_t ni = 0; ni < ast_nodes.length; ni++) {
        auto const *node = &ast_nodes.data[ni];
        if (node->parent != nullptr) {
            continue;
        }
        print("AST Node type: %\n", to_string(node->type));
        switch (node->type) {
            case ASTNodeType::BINARY_ADD: {
                auto const *operands = &node->binary_operation.operands;
                print("\tBinary operation (% operands):", operands->length);
                for (size_t i = 0; i < operands->length; i++) {
                    if (i != 0) {
                        printf(" +");
                    }
                    auto const *op = &operands->data[i];
                    if (op->type == BinaryOperandType::IDENTIFIER) {
                        print(" %", op->identifier);
                    }
                    else if (op->type == BinaryOperandType::PROC_CALL) {
                        print(" %(...)", op->proc_call.caller_identifier);
                    }
                    else {
                        printf(" <literal>");
                    }
                }
                printf("\n");
                break;
            }
            case ASTNodeType::PROC_DEF:
                print("\tProcedure name: % (% chars)\n",
                    node->proc_def.name,
                    node->proc_def.name.length
                );
                print("\tProcedure parameters (%):\n", node->proc_def.parameters.length);
                for (size_t i = 0; i < node->proc_def.parameters.length; i++) {
                    auto const *param = &node->proc_def.parameters.data[i];
                    print("\t\t%: % (% chars)\n", i, param->name, param->name.length);
                }
                Str return_type_name = MISSING_TYPE;
                if (node->proc_def.return_type) {
                    return_type_name = node->proc_def.return_type->name;
                }
                print("\tProcedure return type: %\n", return_type_name);
                print("\tProcedure body (length %):\n", node->proc_def.body.length);
                for (size_t si = 0; si < node->proc_def.body.length; si++) {
                    auto const *statement = &node->proc_def.body.data[si];
                    if (statement->parent != node) {
                        continue;
                    }
                    print("\t\tStatement: %\n", to_string(statement->type));
                    if (statement->type == ASTNodeType::PROC_CALL) {
                        print("\t\t\tArgument count: %\n",
                            statement->proc_call.arguments.length);
                    }
                    else if (statement->type == ASTNodeType::RETURN) {
                        print("\t\t\tReturn value node type: %\n",
                            to_string(statement->return_value->type));
                    }
                }
                break;
        }
    }

    // Transpile AST nodes into C source code
    auto target_file_path = cstr_to_str(output_file_path_cstr);

    // TODO Instead of setting the marker here and reclaiming the memory after generating the C code,
    // figure out a better lifetime management strategy.
    auto marker = allocator_marker_from_current_offset(&main_allocator);

    auto str_buffer = transpile_to_c(&ast_nodes, &main_allocator);

    char *target_file_path_c_str = allocate_null_terminated_str_from_str(&main_allocator, &target_file_path);

    FILE *file = fopen(target_file_path_c_str, "w");
    fwrite(str_buffer.data, 1, str_buffer.length, file);
    fclose(file);

    reclaim_to_marker(&main_allocator, &marker);

    print(
        "Main memory total: %, left: %, used: %\n",
        MAIN_MEMORY_SIZE,
        memory_left(&main_allocator),
        main_allocator.length - memory_left(&main_allocator)
    );

    delete_allocator(&main_allocator);
    return 0;
}

auto build(int argc, char* argv[]) -> int {
    char const *input_file = nullptr;
    char const *output_file = nullptr;

    for (int i = 2; i < argc; i++) {
        if (strncmp(argv[i], "--input-file", 12) == 0 && i + 1 < argc) {
            input_file = argv[++i];
        }
        else if (strncmp(argv[i], "--output-file", 13) == 0 && i + 1 < argc) {
            output_file = argv[++i];
        }
    }

    if (input_file == nullptr) {
        eprint("Error: --input-file is required\n");
        return 1;
    }
    if (output_file == nullptr) {
        eprint("Error: --output-file is required\n");
        return 1;
    }

    auto input_file_path = std::filesystem::path(input_file);
    if (!std::filesystem::exists(input_file_path)) {
        eprint("Error: Input file does not exist\n");
        return 1;
    }
    input_file_path = std::filesystem::absolute(input_file_path);

    int src_fd = open(input_file_path.c_str(), O_RDONLY);
    if (src_fd == -1) {
        eprint("Error opening the input source file\n");
        return 1;
    }

    struct stat file_stat;
    if (fstat(src_fd, &file_stat) == -1) {
        eprint("Error getting the input source file status\n");
        close(src_fd);
        return 1;
    }

    byte *mapped_memory = static_cast<byte*>(mmap(nullptr, file_stat.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0));
    defer(munmap(mapped_memory, file_stat.st_size));
    close(src_fd);

    if (mapped_memory == MAP_FAILED) {
        eprint("Error mapping the input source file\n");
        return 1;
    }

    auto main_allocator = ArenaAllocator(MAIN_MEMORY_SIZE);

    auto input_file_content = cstr_to_str(reinterpret_cast<char*>(mapped_memory));
    Array<Token> tokens = tokenize(&input_file_content, &main_allocator);
    bool had_parse_errors = false;
    auto ast_nodes = parse(&tokens, &main_allocator, input_file_content, cstr_to_str(input_file), &had_parse_errors);
    if (had_parse_errors) {
        delete_allocator(&main_allocator);
        return 1;
    }

    std::filesystem::create_directories("build/tmp");
    char temp_c_file[] = "build/tmp/transpiled_XXXXXX.c";
    int temp_fd = mkstemps(temp_c_file, 2);
    if (temp_fd == -1) {
        eprint("Error creating temporary file\n");
        delete_allocator(&main_allocator);
        return 1;
    }

    auto c_code = transpile_to_c(&ast_nodes, &main_allocator);
    write(temp_fd, c_code.data, c_code.length);
    close(temp_fd);

    std::string gcc_cmd = std::string("gcc -o ") + output_file + " " + temp_c_file;
    int gcc_result = system(gcc_cmd.c_str());

    std::filesystem::remove(temp_c_file);
    delete_allocator(&main_allocator);

    if (gcc_result != 0) {
        eprint("Error compiling generated C code\n");
        return 1;
    }

    return 0;
}

auto main(int argc, char* argv[]) -> int {
    if (argc < 2) {
        eprint("Usage: % run|transpile|build <args...>\n", argv[0]);
        return 1;
    }

    if (strncmp(argv[1], "run", 3) == 0) {
        return run(argc, argv);
    }
    else if (strncmp(argv[1], "transpile", 9) == 0) {
        if (argc < 3) {
            eprint("Usage: % transpile <input_file_path> <output_file_path>\n", argv[0]);
            return 1;
        }
        return transpile(argv[2], argv[3]);
    }
    else if (strncmp(argv[1], "build", 5) == 0) {
        return build(argc, argv);
    }
    else {
        eprint("Error: First argument must be 'run', 'transpile', or 'build'\n");
        return 1;
    }
}