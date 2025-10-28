#include <bloom/allocation.h>
#include <bloom/print.h>
#include <cstdarg>
#include <cstring>

ArenaAllocator::ArenaAllocator(size_t size) : offset(0), length(size) {
    data = static_cast<byte*>(calloc(size, 1));
    assert(data != nullptr && "Failed to allocate memory for ArenaAllocator");
}

auto delete_allocator(ArenaAllocator *allocator) -> void {
    free(allocator->data);
}

auto debug_print_bytes(Array<DebugByte> bytes, DebugColor color) -> void {
    // Set the color
    printf("\033[%dm", color);

    // Print 2 digit hex bytes and each line should contain 16 bytes
    for (size_t i = 0; i < bytes.length; i++) {
        if (i % 16 == 0) {
            if (i != 0) {
                printf("\n");
            }
            printf("%04X: ", static_cast<unsigned int>(i));
        }
        printf("%02X ", bytes.data[i].value);
    }
    printf("\n");

    // Unset the color
    printf("\033[0m");
}

auto reclaim_memory_by_markers(
    ArenaAllocator *allocator,
    AllocatorMarker *old_marker,
    AllocatorMarker *new_marker
) -> void {
    assert(old_marker->offset >= new_marker->offset &&
        "Old marker offset must be greater than or equal to new marker offset");
    size_t allocation_size_to_reclaim = old_marker->offset - new_marker->offset;
    if (allocation_size_to_reclaim == 0) {
        return;
    }
    memset(allocator->data + allocator->offset, 0, allocation_size_to_reclaim);
    print("Allocator offset: %\n", allocator->offset);
    assert (allocator->offset >= allocation_size_to_reclaim &&
        "Allocator offset underflow on reclaim");
    allocator->offset -= allocation_size_to_reclaim;
}

auto to_array(ArenaAllocator *allocator) -> Array<byte> {
    return Array<byte>(allocator->data, allocator->length);
}
