#ifndef __BLOOM_H_ALLOCATION__
#define __BLOOM_H_ALLOCATION__
#include <cassert>
#include <cstdlib>
#include "types.h"

template<typename T>
struct AllocatedArrayBlock {
    T *data;
    size_t length;
    /**
     * The offset in the allocator when this block was allocated.
     */
    size_t allocation_offset;
};

struct ArenaAllocator {
    byte *data;
    size_t length;
    size_t offset;

    ArenaAllocator(size_t size);
    ~ArenaAllocator();
};

template<typename T>
AllocatedArrayBlock<T> allocate_object_array(ArenaAllocator *allocator, size_t length) {
    size_t required_size = length * sizeof(T);
    assert(allocator->offset + required_size <= allocator->length &&
        "Failed to allocate object array from ArenaAllocator");
    T* array = reinterpret_cast<T*>(allocator->data + allocator->offset);
    allocator->offset += required_size;
    return { array, length };
}

template<typename T>
inline size_t allocation_size(AllocatedArrayBlock<T> *block) {
    return block->length * sizeof(T);
}

inline size_t memory_left(ArenaAllocator *allocator) {
    return allocator->length - allocator->offset;
}

/**
 * Shrink the most recent array allocation by changing the length.
 */
template<typename T>
AllocatedArrayBlock<T> shrink_last_allocation(
    ArenaAllocator *allocator,
    AllocatedArrayBlock<T> *block,
    size_t new_length
) {
    assert(new_length <= block->length &&
        "New length must be less than or equal to the current length");
    assert(block->allocation_offset + block->length * sizeof(T) == allocator->offset &&
        "Can only shrink the most recent allocation");
    // Update the allocator's offset
    allocator->offset -= (block->length - new_length) * sizeof(T);
    // Update the block's length
    block->length = new_length;
    return *block;
}

#endif // __BLOOM_H_ALLOCATION__
