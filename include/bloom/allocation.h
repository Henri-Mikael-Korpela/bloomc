#ifndef __BLOOM_H_ALLOCATION__
#define __BLOOM_H_ALLOCATION__
#include <cassert>
#include <cstdlib>
#include <bloom/array.h>
#include <bloom/types.h>

template<typename ElementType>
struct AllocatedArrayBlock {
    ElementType *data;
    size_t length;
    /**
     * The offset in the allocator when this block was allocated.
     */
    size_t allocation_offset;

    // Add support for range-based for loops
    ElementType* begin() { return data; }
    ElementType* end() { return data + length; }
};

struct AllocatorMarker {
    size_t offset;
};

struct ArenaAllocator {
    byte *data;
    size_t length;
    size_t offset;

    ArenaAllocator(size_t size);
};

inline auto allocator_marker_from_current_offset(ArenaAllocator *allocator) -> AllocatorMarker {
    return AllocatorMarker { allocator->offset };
}

/**
 * Allocates an array of objects of type ElementType.
 * 
 * @example Allocation of an array of integers.
 * #include <cstdio>
 * #include <bloom/allocation.h>
 * >>> auto block = ArenaAllocator(256);
 * >>> auto arr = allocate_array<int32_t>(&block, 2); // 8 bytes
 * >>> arr.data[0] = 42;
 * >>> arr.data[1] = 43;
 * >>> print("Memory left: %\n", memory_left(&block));
 * Memory left: 248
 */
template<typename ElementType>
auto allocate_array(ArenaAllocator *allocator, size_t length) -> AllocatedArrayBlock<ElementType> {
    size_t required_size = length * sizeof(ElementType);
    assert(allocator->offset + required_size <= allocator->length &&
        "Failed to allocate object array from ArenaAllocator");
    ElementType* array = reinterpret_cast<ElementType*>(allocator->data + allocator->offset);
    allocator->offset += required_size;
    return { array, length };
}

/**
 * Allocates an array and copies the contents of the given array into it.
 */
template<typename ElementType>
auto allocate_array_from_copy(ArenaAllocator *allocator, Array<ElementType> *src_array) -> AllocatedArrayBlock<ElementType> {
    auto block = allocate_array<ElementType>(allocator, src_array->length);
    copy_array(&block, src_array);
    return block;
}

template<typename ElementType>
inline auto allocation_size(AllocatedArrayBlock<ElementType> *block) -> size_t {
    return block->length * sizeof(ElementType);
}

/**
 * Copies the contents of the given array into the allocated block.
 */
template<typename ElementType>
inline auto copy_array(AllocatedArrayBlock<ElementType> *dest_block, Array<ElementType> *src_array) -> void {
    memcpy(dest_block->data, src_array->data, src_array->length * sizeof(ElementType));
}

extern auto delete_allocator(ArenaAllocator *allocator) -> void;

inline auto memory_left(ArenaAllocator *allocator) -> size_t {
    return allocator->length - allocator->offset;
}

/**
 * Shrink the most recent array allocation by changing the length.
 */
template<typename ElementType>
auto shrink_last_allocation(
    ArenaAllocator *allocator,
    AllocatedArrayBlock<ElementType> *block,
    size_t new_length
) -> AllocatedArrayBlock<ElementType> {
    assert(new_length <= block->length &&
        "New length must be less than or equal to the current length");
    // Update the allocator's offset
    allocator->offset -= (block->length - new_length) * sizeof(ElementType);
    // Update the block's length
    block->length = new_length;
    return *block;
}

struct DebugByte {
    byte value;
};

enum DebugColor {
    DEBUG_COLOR_GREEN = 32,
    DEBUG_COLOR_RED = 31,
    DEBUG_COLOR_WHITE = 37,
    DEBUG_COLOR_YELLOW = 33,
};

extern auto debug_print_bytes(Array<DebugByte> bytes, DebugColor color) -> void;

template<typename ElementType>
inline auto get_debug_printable_bytes(AllocatedArrayBlock<ElementType> *block) -> Array<DebugByte> {
    return Array<DebugByte> {
        .data = reinterpret_cast<DebugByte*>(block->data),
        .length = allocation_size(block)
    };
}

inline auto get_debug_printable_bytes(Array<byte> *array) -> Array<DebugByte> {
    return Array<DebugByte> {
        .data = reinterpret_cast<DebugByte*>(array->data),
        .length = array->length
    };
}

/**
 * Reclaims memory in the allocator between two markers by zeroing it out.
 * The old marker offset must be greater than or equal to the new marker offset.
 */
extern auto reclaim_memory_by_markers(
    ArenaAllocator *allocator,
    AllocatorMarker *old_marker,
    AllocatorMarker *new_marker
) -> void;

extern auto to_array(ArenaAllocator *allocator) -> Array<byte>;

#endif // __BLOOM_H_ALLOCATION__
