#ifndef __BLOOM_H_ARRAY__
#define __BLOOM_H_ARRAY__
#include <cassert>
#include <cstddef>

template<typename ElementType>
struct Array {
    ElementType *data;
    size_t length;

    inline ElementType& operator[](size_t index) {
        assert(index < length && "ArrayPtr index out of bounds");
        return data[index];
    }

    // To support range-based for loops
    inline auto begin() -> ElementType* { return data; }
    inline auto end()   -> ElementType* { return data + length; }
};

/**
 * Returns a slice of the array from begin (inclusive) to end (exclusive).
 * @param begin The starting index of the slice (inclusive).
 * @param end The end index of the slice (exclusive). This is NOT the slice length.
 */
template<typename ElementType>
auto slice_by_offset(Array<ElementType> *array, size_t begin, size_t end) -> Array<ElementType> {
    assert(begin <= end && end <= array->length && "Slice end out of bounds");
    return Array<ElementType> {
        .data = array->data + begin,
        .length = end - begin
    };
}

template<typename ElementType>
auto slice_by_length(Array<ElementType> *array, size_t begin, size_t length) -> Array<ElementType> {
    assert(begin + length <= array->length && "Slice end out of bounds");
    return Array<ElementType> {
        .data = array->data + begin,
        .length = length
    };
}

#endif // __BLOOM_H_ARRAY__