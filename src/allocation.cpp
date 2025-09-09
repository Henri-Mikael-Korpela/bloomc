#include "allocation.h"

ArenaAllocator::ArenaAllocator(size_t size) : offset(0), length(size) {
    data = static_cast<byte*>(malloc(size));
    assert(data != nullptr && "Failed to allocate memory for ArenaAllocator");
}
ArenaAllocator::~ArenaAllocator() {
    free(data);
}