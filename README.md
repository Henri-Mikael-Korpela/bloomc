# Bloom Programming Language

Bloom is a statically-typed, compiled programming language designed for simplicity and performance. It is C-like, influenced by many languages like Odin and Rust, readable like Python and has a batteries included philosophy, aimed for making low-level programming for web more accessible.

The language is currently under active development. Here's an example of what is to come:

```odin
csv from import "@bloom/csv"

main :: proc() ->
    csv_file, err := csv.open_file("data.csv")
    if err.code != .NONE ->
        eprintln("Error reading CSV file: {}", err.message)
        return
    defer csv.close_file(csv_file)

    row_reader := csv.get_row_reader(csv_file)
    for ->
        row, err := csv.read_row(row_reader)
        if case err.code == $ ->
            .EOF -> break
            .NONE ->
                eprintln("Error reading row: {}", err.message)
                continue

        println("Row:")
        for key, value in row ->
            println("\t{}: {}", key, value)
```

## Prerequisites

Before you begin, ensure you have the following installed:

- CMake (version 3.15 or higher)
- A C++17 compiler (GCC, Clang, or MSVC)

Also, all documentation and testing has been done on Ubuntu Linux.

## Running the Compiler on Linux

In order to run the compiler, you must build the Bloom compiler first. Use the following commands from the repository root:

```bash
# Create a build directory
mkdir build
cd build

# Configure the project
cmake ..

# Build the project
cmake --build .
```

This will generate the `bloomc` executable in the `build` directory.

While you are still inside the build directory, you can run the compiler using a source code file on Linux like this:

```bash
./bloomc run <input_file_path>
```

There's an example Bloom source code in `docs/examples/sum.blm`, which you can run as follows in the repository root:

```bash
./build/bloomc run docs/examples/sum.blm
```
