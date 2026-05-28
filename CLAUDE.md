# C++ Code Style

## Identifiers
- For enumeration keys, use SCREAMING_SNAKE_CASE
- For functions, use snake_case

## Types
- Put `const` after type (e.g. `char const`) instead of before it (e.g. `const char`).
- Do not use RTTI

## Control structures
- If statements should be followed brackets. Do not use single line if bodies.
- `else` should start on its own line

## Expressions
- Do not use the ternary operator.

## Functions
- Use trailing return type for all functions

## Casting
- Do not use `dynamic_cast`

## C and C++ Standard Library
- Do not use `iostream`. Prefer C `stdio` instead.