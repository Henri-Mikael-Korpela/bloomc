#pragma once

#include <functional>
#include <bloom/assert.h>
#include <bloom/error-reporting.h>
#include <bloom/print.h>
#include <bloom/ptr.h>
#include <bloom/result.h>
#include <bloom/parsing.h>

/**
 * Converts an AllocatedArrayBlock to an Array.
 */
template<typename PointerT>
static inline auto to_array(AllocatedArrayBlock<PointerT> *block) -> Array<PointerT> {
    return Array<PointerT>(block->data, block->length);
}

template<typename ElementType>
struct Iterator {
    Array<ElementType> elements;
    size_t current_index;
};

template<typename ElementType>
static inline auto to_array(Iterator<ElementType> *iter) -> Array<ElementType> {
    return Array<ElementType>(
        iter->elements.data,
        iter->current_index
    );
}

template<typename ElementType>
static inline auto to_iterator(AllocatedArrayBlock<ElementType> *block) -> Iterator<ElementType> {
    return {
        .elements = to_array(block),
        .current_index = 0,
    };
}
static inline auto to_iterator(Array<Token> *tokens) -> Iterator<Token> {
    return {
        .elements = *tokens,
        .current_index = 0,
    };
}

/**
 * Advances the iterator and sets the next element to the given value, and returns it.
 */
template<typename ElementType>
static auto iter_append(
    Iterator<ElementType> *iter,
    ElementType &&value
) -> ElementType* {
    auto next_elem = iter_next(iter);
    *next_elem = value;
    return next_elem;
}

template<typename ElementType>
static auto iter_current(Iterator<ElementType> *iter) -> ElementType* {
    return &iter->elements.data[iter->current_index];
}

/**
 * Peeks until the given condition is met, returning the index of the element that meets the condition.
 * If no such element is found, returns -1.
 */
template<typename ElementType>
static auto iter_get_index_at_if(
    Iterator<ElementType> *iter,
    std::function<bool(ElementType const*)> &&condition_fn
) -> int64_t {
    size_t start_index = iter->current_index;
    for (size_t i = start_index; i < iter->elements.length; i++) {
        if (condition_fn(&iter->elements.data[i])) {
            return static_cast<int64_t>(i);
        }
    }
    return -1;
}

/**
 * Advances the iterator and returns the next element.
 */
template<typename ElementType>
static auto iter_next(Iterator<ElementType> *iter) -> ElementType* {
    assert (iter->elements.length > 0 &&
        "Cannot advance iterator because the iterator length is 0.");
    assertf(
        iter->current_index < iter->elements.length,
        "Iterator out of bounds while advancing to the next element:\n"
        "\tElement type size: %\n"
        "\tCurrent index: %\n"
        "\tMax length: %",
        sizeof(ElementType), iter->current_index, iter->elements.length
    );
    return &iter->elements.data[iter->current_index++];
}

/**
 * Peeks at the current element without advancing the iterator.
 */
template<typename ElementType>
static inline auto iter_peek(Iterator<ElementType> *iter) -> ElementType* {
    assert(iter->current_index < iter->elements.length &&
        "Iterator out of bounds while peeking");
    return &iter->elements.data[iter->current_index];
}

/**
 * Peeks at the previous element without changing the iterator.
 */
template<typename ElementType>
static inline auto iter_peek_prev(Iterator<ElementType> *iter) -> ElementType* {
    assert(iter->current_index > 0 &&
        "Iterator out of bounds while peeking prev");
    return &iter->elements.data[iter->current_index - 1];
}

/**
 * Creates a new iterator that is a slice of the given iterator from begin to end offsets.
 */
template<typename ElementType>
static auto iter_slice_by_offset(
    Iterator<ElementType> *iter,
    size_t begin,
    int64_t end
) -> Iterator<ElementType> {
    // Copy the iterator
    auto new_iter = *iter;
    assert(end >= 0 && begin <= end && end <= iter->elements.length &&
        "Slice end out of bounds");
    new_iter.elements = slice_by_offset(&new_iter.elements, begin, static_cast<size_t>(end));
    new_iter.current_index = 0;
    return new_iter;
}

/**
 * Tries to advance the iterator and returns the next element.
 * If the next element is not found, null is returned.
 */
template<typename ElementType>
static auto iter_try_next(Iterator<ElementType> *iter) -> ElementType* {
    if (iter->current_index >= iter->elements.length) {
        return nullptr;
    }
    return &iter->elements.data[iter->current_index++];
}

template<typename ElementType>
struct DynamicArray {
    ElementType *data;
    size_t length;
    size_t max_length;

    DynamicArray(AllocatedArrayBlock<ElementType> *target_block):
        data(target_block->data),
        length(0),
        max_length(target_block->length)
    {}
};

template<typename ElementType>
static auto append(DynamicArray<ElementType> *array, ElementType value) -> void {
    assert(array->length < array->max_length &&
        "DynamicArray out of capacity");
    array->data[array->length++] = value;
}

/**
 * Converts a DynamicArray to an Array.
 */
template<typename T>
static inline auto to_array(DynamicArray<T> *value) -> Array<T> {
    return Array<T>(value->data, value->length);
}

struct Context {
    Token *current_identifier = nullptr;
    ASTNode *current_proc_node = nullptr;
    bool in_proc_definition = false;
    AllocatedArrayBlock<ASTNode> *nodes_block;
    struct ConstEntry { Str name; int64_t value; };
    ConstEntry constants[64] = {};
    size_t constant_count = 0;
    struct VarTypeEntry { Str name; bool is_bool; };
    VarTypeEntry var_types[128] = {};
    size_t var_type_count = 0;
    struct ArraySizeEntry { Str name; int64_t size; };
    ArraySizeEntry array_sizes[64] = {};
    size_t array_size_count = 0;
};

inline auto str_equal(Str a, Str b) -> bool {
    return a.length == b.length && strncmp(a.data, b.data, a.length) == 0;
}

#define PARSE_ERROR_CREATE(error_code, token) \
    ParseError { .code = ParseErrorCode::error_code, .position = token->position, .src_code_line = __LINE__, .token_type = token->type }

// Forward declarations for functions that cross file boundaries

auto print_value(FILE *file, Context *context) -> void;
auto expect_token_or_append_error(Iterator<Token> *tokens_iter, TokenType expected, DynamicArray<ParseError> *errors) -> Token *;
auto expect_arrow_newline(Iterator<Token> *tokens_iter, DynamicArray<ParseError> *errors) -> bool;
auto slice_expression_tokens(Iterator<Token> *tokens_iter, DynamicArray<ParseError> *errors, Iterator<Token> *out_iter) -> bool;
auto find_proc_def_node(Iterator<ASTNode> const *nodes_block_iter, Str const *name) -> ASTNode const *;
auto infer_arg_type_name(ASTNode const *arg, Context const *context) -> Str;
auto parse_proc_call_arguments(Iterator<Token> *tokens_iter, ASTNode *proc_call_node, Iterator<ASTNode> *nodes_block_iter, Context *context, Iterator<BinaryOperand> *operands_iter, DynamicArray<ParseError> *errors, Token::Position call_pos) -> bool;
auto parse_proc_params(Iterator<Token> *tokens_iter, Iterator<ProcParameterASTNode> *proc_params_iter, DynamicArray<ParseError> *errors) -> bool;
auto parse_statement(Iterator<Token> *tokens_iter, Context *context, Iterator<ASTNode> *nodes_block_iter, ASTNode *parent_node, AllocatedArrayBlock<ProcParameterASTNode> *proc_params_block, Iterator<ProcParameterASTNode> *proc_params_iter, Iterator<TypeASTNode> *types_iter, Iterator<BinaryOperand> *operands_iter, Iterator<int64_t> *array_elements_iter, DynamicArray<ParseError> *errors, size_t current_indent_level) -> bool;
auto parse_indented_body(Iterator<Token> *tokens_iter, Context *context, Iterator<ASTNode> *nodes_block_iter, ASTNode *parent_node, AllocatedArrayBlock<ProcParameterASTNode> *proc_params_block, Iterator<ProcParameterASTNode> *proc_params_iter, Iterator<TypeASTNode> *types_iter, Iterator<BinaryOperand> *operands_iter, Iterator<int64_t> *array_elements_iter, DynamicArray<ParseError> *errors, size_t current_indent_level, Array<ASTNode> *body) -> bool;
auto parse_expression(Iterator<Token> *tokens_iter, Context *context, Iterator<ASTNode> *nodes_block_iter, AllocatedArrayBlock<ProcParameterASTNode> *proc_params_block, Iterator<ProcParameterASTNode> *proc_params_iter, Iterator<TypeASTNode> *types_iter, Iterator<BinaryOperand> *operands_iter, Iterator<int64_t> *array_elements_iter, DynamicArray<ParseError> *errors) -> Result<ASTNode, ParseError>;
auto infer_bloom_type_from_tokens(Iterator<Token> *expr_iter, Context *context, Iterator<ASTNode> const *nodes_block_iter) -> Str;
auto eval_const_additive(Iterator<Token> *tokens_iter, Context *context, Iterator<ASTNode> const *nodes_block_iter, DynamicArray<ParseError> *errors, int64_t *out_value) -> bool;
auto bloom_type_size_in_bytes(Str type_name) -> int64_t;
auto bloom_type_to_c_type(Str type_name) -> char const *;
