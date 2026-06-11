# Bloom Programming Language Reference

## Source code representation

Bloom source code is stored in UTF-8 encoded text files.

## Statements

In Bloom like in Python, statements are grouped by indentation. Indentation uses 4 spaces.

## Identifiers

Identifiers are used for names for types and variables.

## Keywords

```
break
context
const
defer
else
for
foreign
if
in
proc
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

And here are the operators by precendence (from highest to lowest):

|Level|Operators           |Notes                          |
|-----|--------------------|-------------------------------|
|1    |`/`, `*`            |                               |
|2    |`+`, `-`            |                               |
|3    |`..<`, `..=`, `..+` |                               |
|4    |`==`, `<`           |Comparison, all non-associative|

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

## Reflection

`type_info_of` is a built-in operator that returns compile-time type information about an expression or a type name. The expression passed to it is **not evaluated** — only its type is inspected.

```
type_info_of(<expression or type name>)
```

The returned value is a compile-time structure with the following field:

| Field          | Type | Description                        |
|----------------|------|------------------------------------|
| `size_in_bytes` | Int  | Size of the type in bytes          |

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

### Transpilation

Access to `size_in_bytes` is transpiled to a C `sizeof` expression:

```
type_info_of(Int).size_in_bytes   →   sizeof(int)
type_info_of(U8).size_in_bytes    →   sizeof(uint8_t)
type_info_of(Bool).size_in_bytes  →   sizeof(bool)
type_info_of(Str).size_in_bytes   →   sizeof(BloomStr)
```
