#include <bloom/defer.h>
#include <bloom/log.h>
#include <bloom/print.h>
#include <bloom/transpilation.h>

static auto allocate_dynamic_str(ArenaAllocator *allocator) -> DynamicString {
    return {
        .data = reinterpret_cast<char*>(allocator->data + allocator->offset),
        .length = 0,
        .max_length = allocator->length - allocator->offset,
    };
}

/**
 * Allocates a null-terminated C string.
 */
static auto allocate_null_terminated_str_from_str(ArenaAllocator *allocator, String *str) -> char* {
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

auto transpile_to_c(
    String *target_file_path,
    Array<ASTNode> *ast_nodes,
    ArenaAllocator *allocator
) -> void {
    auto marker = allocator_marker_from_current_offset(allocator);
    defer({
        logf("DEFER Reclaimed\n");
        reclaim_to_marker(allocator, &marker);
    });
    
    auto str_buffer = allocate_dynamic_str(allocator);

    #define PUSH_STR(value) allocator->offset += push_str(&str_buffer, value)

    PUSH_STR("#include <stdio.h>\n\n");

    for (auto &node : *ast_nodes) {
        switch (node.type) {
            case ASTNodeType::PROC_DEF: {
                char const *return_type_name = nullptr;
                if (node.proc_def.return_type != nullptr) {
                    if (compare_str(&node.proc_def.return_type->name, "Int")) {
                        return_type_name = "int";
                    }
                }
                else {
                    return_type_name = "void";
                }
                assert(return_type_name != nullptr && "Unsupported return type in transpilation");
                PUSH_STR(return_type_name);
                PUSH_STR(' ');
                PUSH_STR(&node.proc_def.name);
                PUSH_STR('(');
                auto *params = &node.proc_def.parameters;
                for (size_t i = 0; i < params->length; i++) {
                    auto *param = &params->data[i];
                    if (i != 0) {
                        PUSH_STR(", ");
                    }
                    // For simplicity, assume all parameters are of type int
                    PUSH_STR("int ");
                    PUSH_STR(&param->name);
                }
                PUSH_STR(')');
                PUSH_STR("{\n");
                for (auto &statement : node.proc_def.body) {
                    PUSH_STR('\t');
                    switch (statement.type) {
                        case ASTNodeType::BINARY_ADD: {
                            PUSH_STR("return ");
                            PUSH_STR(&statement.binary_operation.identifier_left);
                            PUSH_STR(" + ");
                            PUSH_STR(&statement.binary_operation.identifier_right);
                            PUSH_STR(";\n");
                            break;
                        }
                        default:
                            break;
                    }
                }
                PUSH_STR("}\n\n");
                break;
            }
            default:
                break;
        }
    }

    //PUSH_STR("int main(){\n\tprintf(\"%i\\n\", sum(5, 10));\n\treturn 0;\n}");

    #undef PUSH_STR

    // Write the target file
    char *target_file_path_c_str = allocate_null_terminated_str_from_str(allocator, target_file_path);

    FILE *file = fopen(target_file_path_c_str, "w");
    defer({
        logf("DEFER Closing file\n");
        fclose(file);
    });

    fwrite(str_buffer.data, 1, str_buffer.length, file);

    logf("File written\n");
}