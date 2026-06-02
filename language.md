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
const
else
for
if
in
proc
```

## Operators

Bloom has the following operators:

|Operator|Name             |
|--------|-----------------|
|`+`     |Addition         |
|`==`    |Equality         |
|`..<`   |Range (exclusive)|

And here are the operators by precendence (from highest to lowest):

|Level|Operators|Notes                          |
|-----|---------|-------------------------------|
|1    |`+`      |                               |
|2    |`..<`    |                               |
|3    |`==`, `<`|Comparison, all non-associative|

## Loops

There's only one loop statement in Bloom: `for`. It is used for a variety of loops already familiar from languages like C and Python.

Loops can iterate over a given range like this:

```
for i in 0..<10 ->
    ...
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
