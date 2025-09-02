#include <cstdio>
#include <fstream>
#include <filesystem>

int main(int argc, char* argv[]) {
    if(argc < 3) {
        printf("Usage: %s run <input_file_path>\n", argv[0]);
        return 1;
    }

    auto current_path = std::filesystem::current_path();
    printf("Current path: %s\n", current_path.string().c_str());

    if (std::string(argv[1]) != "run") {
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

    // Print the input file content
    std::ifstream input_file(input_file_path);
    if (!input_file) {
        printf("Error: Failed to open input file\n");
        return 1;
    }

    std::string line;
    printf("\n");
    while (std::getline(input_file, line)) {
        printf("%s\n", line.c_str());
    }

    return 0;
}