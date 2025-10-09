from dataclasses import dataclass
from enum import Enum, auto

class TestLineType(Enum):
    CODE = auto()
    INCLUDE = auto()
    STDOUT = auto()

@dataclass
class TestLine:
    type: TestLineType

COMMENT_LINE_BEGIN_STR = " * "
CODE_LINE_PREFIX = ">>> "
HEADER_FILE_PATH = "/home/henri/Personal/bloomc2/include/bloom/allocation.h"

with open(HEADER_FILE_PATH, "r") as file:
    header_content = file.read()

examples: list[list[TestLine]] = []

line_iter = iter(header_content.split("\n"))
try:
    while True:
        line = next(line_iter)
        if not "@example" in line:
            continue
        example_lines = []
        while line := next(line_iter):
            offset = line.find(COMMENT_LINE_BEGIN_STR)
            if offset == 0:
                # If the line contains an include directive
                if line.find("#include") == len(COMMENT_LINE_BEGIN_STR):
                    example_lines.append((TestLineType.INCLUDE, line[offset + len(COMMENT_LINE_BEGIN_STR):].strip()))
                # If the line starts with code
                elif line.find(CODE_LINE_PREFIX) == len(COMMENT_LINE_BEGIN_STR):
                    example_lines.append((TestLineType.CODE, line[offset + 3 + len(CODE_LINE_PREFIX):].strip()))
                # If the line contains stdout
                else:
                    example_lines.append((TestLineType.STDOUT, line[offset + len(COMMENT_LINE_BEGIN_STR):].strip()))
            else:
                break
        examples.append(example_lines)
except StopIteration:
    pass

full_src: str = ""

includes: list[str] = []
includes.append("#include <cassert>")
includes.append("#include <cstdlib>")
includes.append("#include <bloom/defer.h>")
includes.append("#include <bloom/print.h>")
main_func_test_calls: list[str] = []
tests: list[str] = []

for i, example in enumerate(examples):
    test_body = ""
    for line_type, content in example:
        match line_type:
            case TestLineType.CODE:
                test_body += "\t" + content + "\n"
            case TestLineType.INCLUDE:
                includes.append(content)
    test_body += "\treturn true;\n"
    cpp_src_content: str = f"""bool test_{i}(){{\n{test_body}}}"""
    tests.append(cpp_src_content)
    main_func_test_calls.append(f"\tassert(test_{i}());")

tests_src: str = ""
tests_src += "\n".join(includes)
tests_src += "\n\n"
tests_src += "\n\n".join(tests)
full_src += tests_src

main_func: str = \
f"""int main() {{
    printf("%s\\n", "Running tests...");

    assert(_bloom_test_output == nullptr && "Test file should be initially null");

    {{
        _bloom_test_output = fopen("tmp/test_output.txt", "w");
        assert(_bloom_test_output != nullptr && "Failed to open test output file");
        defer(fclose(_bloom_test_output));

    {chr(10).join(main_func_test_calls)}
    }}

    _bloom_test_output = fopen("tmp/test_output.txt", "r");
    assert(_bloom_test_output != nullptr && "Failed to open test output file for reading");
    defer(fclose(_bloom_test_output));

    fseek(_bloom_test_output, 0, SEEK_END);
    size_t length = ftell(_bloom_test_output);
    if (length > 0) {{
        // +1 for null terminator
        char *buffer = static_cast<char*>(malloc(length + 1));
        assert(buffer != nullptr && "Failed to allocate buffer for test output");
        defer(free(buffer));

        assert(buffer != nullptr);
        fread(buffer, 1, length, _bloom_test_output);
        buffer[length] = '\\0';
        printf("%s:\\n%s\\n", "Test output", buffer);
    }}
    else {{
        printf("%s\\n", "No test output");
    }}
    printf("%s\\n", "All tests passed");
    return 0;
}}
"""
full_src += "\n\n" + main_func
with open("tmp/tests.cpp", "w") as test_file:
    test_file.write(full_src)

print("Wrote tests source file")