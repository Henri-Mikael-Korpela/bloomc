# Bloom Programming Language Reference

## Source code representation

Bloom source code is stored in UTF-8 encoded text files.

## Statements

In Bloom like in Python, statements are grouped by indentation. Indentation uses 4 spaces.

## Identifiers

Identifiers are used for names for types and variables.

## Keywords

```
and
break
context
const
defer
else
enum
for
foreign
if
in
proc
return
struct
type_info_of
```

## Data types

### Boolean

Bloom support a boolean type whose values are `true` and `false`:

```
my_var := false
my_var := true
```

## Operators

Bloom has the following operators:

|Operator|Name            |
|--------|----------------|
|`+`     |Addition        |
|`/`     |Division        |
|`*`     |Multiplication  |
|`-`     |Substraction    |
|`<`     |Less than       |
|`==`    |Equality        |
|`..<`   |Exclusive range |
|`..=`   |Inclusive range |
|`..+`   |Counted range   |
|`and`   |Logical and     |

And here are the operators by precendence (from highest to lowest):

|Level|Operators           |Notes                          |
|-----|--------------------|-------------------------------|
|1    |`/`, `*`            |                               |
|2    |`+`, `-`            |                               |
|3    |`..<`, `..=`, `..+` |                               |
|4    |`==`, `<`           |Comparison, all non-associative|
|5    |`and`               |                               |

## Loops

There's only one loop statement in Bloom: `for`. It is used for a variety of loops already familiar from languages like C and Python.

Loops can iterate over a range of integers. Two range types are supported:

| Syntax  | Name            | Description                                                  |
|---------|-----------------|--------------------------------------------------------------|
| `a..<b` | Exclusive range | Iterates from `a` up to, but not including, `b`              |
| `a..=b` | Inclusive range | Iterates from `a` through `b` inclusive                      |
| `a..+n` | Counted range   | Iterates from `a` through `a + n` inclusive (`n + 1` values) |

```
for i in 0..<10 ->
    ...  // i = 0, 1, 2, ..., 9

for i in 0..=10 ->
    ...  // i = 0, 1, 2, ..., 10

for i in 2..+5 ->
    ...  // i = 2, 3, 4, 5, 6, 7
```

C-style `while` can be repreented with `for if` statement:

```
i := 0
for if i < 10 ->
    i += 1
```

You can create an infinite loop this way:

```
for ->
    ...
```

## Defer

The `defer` statement schedules a procedure call to run at the end of the current scope, regardless of how the scope exits. Multiple deferred calls in the same scope run in last-in, first-out order.

```
f := file_open_for_write("output.txt")
defer file_close(%f)

file_write(%f, "hello")  // file_close runs after this, at end of scope
```

## Arrays

Compile-time arrays can be initialized as follows

```
a := [3]Int{ 2, 5, 9 }
```

where 3 indicates the array length, Int represents the element type and values of that type are given inside brackets.

It is possible to omit the length and let the compiler deduce the length based on specified values. In order to do that, the length is replaced by keyword `const`. Example:

```
a := [const]Int{ 2, 5, 9 }
```

## Structs

C allows for multiple same fields in a designed initializer list (e.g. `MyStruct { .field1 = 3, .field1 = 7 }`) for a struct. In Bloom, this is not possible and results in a compilation error because it is wasteful to initialize the same field twice or more times.

## Procedures

A procedure is a named, reusable block of code. Procedures are defined with the `proc` keyword using the constant definition syntax (`name :: proc`).

### Definition

```
print_greeting :: proc() ->
    printf("Hello!\n")
```

The body is indented one level below the definition line and terminated by dedenting back.

### Parameters

Parameters are listed inside the parentheses as `name: Type` pairs separated by commas:

```
greet :: proc(name: Str, times: Int) ->
    for i in 0..<times ->
        printf("Hello, ")
        printf("%.*s!\n", name.length, name.data)
```

### Return type

The return type is written between the closing `)` and the `->` arrow:

```
sum :: proc(a: Int, b: Int) Int ->
    a + b
```

Procedures with no return type omit it entirely:

```
print_sum :: proc(a: Int, b: Int) ->
    printf("Sum: %i\n", a + b)
```

### Returning values

The last expression in a procedure body is implicitly returned:

```
double :: proc(n: Int) Int ->
    n * 2
```

For early returns, use the `return` keyword followed by the value:

```
safe_divide :: proc(a: Int, b: Int) Int ->
    if b == 0 ->
        return 0
    a / b
```

### Array parameters and return types

Fixed-size arrays are passed by pointer using the `[N]Type` syntax in parameter position:

```
sum_array :: proc(a: [3]Int) Int ->
    a[0] + a[1] + a[2]
```

A procedure can return a fixed-size array using `[N]Type` as the return type. The last expression must be a variable of that array type:

```
make_array :: proc() [3]Int ->
    a := [3]Int{ 2, 8, 12 }
    a[0] = 4
    a
```

### Slice parameters and return types

Array slices use the `[]Type` syntax. A slice holds a pointer to a contiguous region of elements and its length:

```
first_two :: proc(a: [4]Int) []Int ->
    a[0..<2]
```

Slices can also be passed as parameters:

```
print_bytes :: proc(data: []U8) ->
    ...
```

### The main procedure

The program entry point is a procedure named `main`. When `main` takes command-line arguments, declare a single `[]Str` parameter:

```
main :: proc(args: []Str) ->
    if length(args) < 2 ->
        print("Too few arguments given\n")
        return 1
    print("First argument: {}\n", args[1])
```

The `[]Str` parameter is automatically populated from the operating system's argument vector. `length(args)` includes the program name as the first element, so the first user-supplied argument is at index 1.

## Reflection

`type_info_of` is a built-in operator that returns compile-time type information about an expression or a type name. The expression passed to it is **not evaluated** — only its type is inspected.

```
type_info_of(<expression or type name>)
```

The returned value is a compile-time structure with the following fields:

| Field           | Type | Description                                   |
|-----------------|------|-----------------------------------------------|
| `size_in_bytes` | Int  | Size of the type in bytes                     |
| `name`          | Str  | Name of the type as a string (struct types only) |

### Example: querying the size of a built-in type

```
n := type_info_of(Int).size_in_bytes   // 4
```

### Example: querying the size of an expression's type

The type is deduced from the expression without evaluating it:

```
Event :: struct ->
    id: Int
    name: Str

events := [const]Event { .{ id = 1, name = "first" } }
id_size := type_info_of(events[0].id).size_in_bytes   // 4, same as sizeof(int)
```

### Example: using the result as a compile-time constant

`type_info_of(...).size_in_bytes` can appear on the right-hand side of a constant definition (`::`) and the result is folded to an integer at compile time:

```
ID_SIZE :: type_info_of(Int).size_in_bytes   // ID_SIZE = 4

buffer := [ID_SIZE + 1]U8{}
```

### Example: querying the name of a struct type

`type_info_of(...).name` returns the Bloom type name as a `Str`:

```
Event :: struct ->
    id: Int
    name: Str

events := [const]Event { .{ id = 1, name = "first" } }
print("Type: {}", type_info_of(events[0]).name)   // prints: Type: Event
```

### Transpilation

Access to `size_in_bytes` is transpiled to a C `sizeof` expression:

```
type_info_of(Int).size_in_bytes   →   sizeof(int)
type_info_of(U8).size_in_bytes    →   sizeof(uint8_t)
type_info_of(Bool).size_in_bytes  →   sizeof(bool)
type_info_of(Str).size_in_bytes   →   sizeof(BloomStr)
```

Access to `name` is transpiled to a C `BloomStr` string literal:

```
type_info_of(events[0]).name   →   (BloomStr){.data = "Event", .length = 5}
```

## Enumerations

An enumeration (enum) defines a named integer type whose values are restricted to a fixed set of named members. Members are ordered starting from 0.

### Definition

```
Color :: enum ->
    RED
    GREEN
    BLUE
```

This defines `Color` as an enum type with members `RED = 0`, `GREEN = 1`, and `BLUE = 2`.

### Member access

Enum members are accessed with the dot operator on the type name:

```
color := Color.RED
```

### Dot-prefix shorthand

When the enum type can be inferred from context, members can be written with a dot prefix (`.MEMBER`) instead of the full `Type.MEMBER` form.

In comparisons, the type is inferred from the left operand:

```
if color == .RED ->
    print("red\n")
```

In procedure calls, the type is inferred from the declared parameter type:

```
print_color_in_finnish :: proc(color: Color) -> ...

print_color_in_finnish(.GREEN)
```

### Reflection via type_info_of

`type_info_of(value).members` returns an array of all enum members for the enum type of `value`. Each member has a `.name` field (`Str`) and a `.value` field (`Int`):

```
color := Color.RED

// Access the name of the current member
print("Key: {}\n", type_info_of(color).members[color].key)

// Iterate over all members
for member in type_info_of(color).members ->
    print("Name: {}, Value: {}\n", member.name, member.value)
```

### Transpilation

Enum definitions are transpiled to a C `typedef int` and per-member `static const int` constants:

```
Color :: enum ->    →   typedef int Color;
    RED                 static int const __bloom_Color_RED = 0;
    GREEN               static int const __bloom_Color_GREEN = 1;
    BLUE                static int const __bloom_Color_BLUE = 2;
```

A member array is also generated for reflection:

```
static BloomEnumMember const __bloom_Color_members[] = {
    {.name = {.data = "RED",   .length = 3}, .value = 0},
    {.name = {.data = "GREEN", .length = 5}, .value = 1},
    {.name = {.data = "BLUE",  .length = 4}, .value = 2},
};
```
