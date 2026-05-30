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
else
for
if
proc
```

## Loops

There's only one loop statement in Bloom: `for`.

You can create an infinite loop this way:

```
for ->
    ...
```

## Arrays

Compile-time arrays are initialized as follows

```
a := [const]Int{ 2, 5, 9 }
```

where const indicates the size of the array is determined at compile-time, Int represents the element type and values of that type are given inside brackets.