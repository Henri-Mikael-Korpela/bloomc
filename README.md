# Bloom Programming Language

Bloom is a statically-typed, compiled programming language designed for simplicity and performance. It is C-like, influenced by many languages like Go, JavaScript, Odin and Rust, readable like Python and has a batteries included philosophy, aimed for making low-level programming for web more accessible.

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

There is an a rough design document in [https://henrijahanna.fi/blog/bloom_overview.php](https://henrijahanna.fi/blog/bloom_overview.php), you may want to take a look at that.

## Prerequisites

Before you begin, ensure you have the following installed:

- CMake (version 3.15 or higher)
- A C++17 compiler (GCC, Clang, or MSVC)

Also, all documentation and testing has been done on Ubuntu Linux.

## Running the Compiler on Linux

In order to run the compiler, you must build the Bloom compiler first. Use the following commands from the repository root:

```bash
cmake -B build && cmake --build build
```

This will generate the `bloomc` executable in the `build` directory.

You can run the compiler using a source code file on Linux like this:

```bash
./build/bloomc run <input_file_path>
```

There's an example Bloom source code in `docs/examples/sum.blm`, which you can run as follows in the repository root:

```bash
./build/bloomc run docs/examples/sum.blm
```

## Language Reference

### Ranges

Ranges are used in `for` loops to iterate over a sequence of integers. Two range types are supported:

| Syntax      | Name            | Description                                              | Example values for `2..<8` / `2..+5` |
|-------------|-----------------|----------------------------------------------------------|---------------------------------------|
| `a..<b`     | Exclusive range | Iterates from `a` up to, but not including, `b`          | `2, 3, 4, 5, 6, 7`                   |
| `a..+n`     | Counted range   | Iterates from `a` through `a + n` (inclusive, `n+1` values) | `2, 3, 4, 5, 6, 7`                   |

```
for i in 0..<10 ->
    printf("Index: %i\n", i)  // prints 0 through 9

for i in 2..+5 ->
    printf("Index: %i\n", i)  // prints 2 through 7
```
