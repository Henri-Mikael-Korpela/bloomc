# Bloom Programming Language

## Prerequisites

Before you begin, ensure you have the following installed:

- CMake (version 3.15 or higher)
- A C++ compiler (GCC, Clang, or MSVC)

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
