# Bloom Error Reporting

## Array length mismatch

The following code in `array-length-mismatch.blm`:

```
main :: proc() ->
    a := [2]Int { 6, 12, 18 }
    printf("A: #1: %i, #2: %i\n", a[0], a[1])
```

results in the following error:

```
Compilation failed:

Error: Array length mismatch:
    An explicit length of 2 was given for an array initialization but there's a total of 3 elements in the initialization.

Location:
    In file array-length-mismatch.blm, line 2, column 10:
  |
2 |    a := [2]Int { 6, 12, 18 }
  |          ^     ^^^^^^^^^^^^^

How to fix:
    - If the list of elements is correct, change the array length of 2 to 3 to match the number of elements.
    - If the array length of 2 is correct, change the number of values to match the length of 2.
```