#ifndef __BLOOM_H_ARRAY__
#define __BLOOM_H_ARRAY__
#include <cassert>

template<typename T>
struct Array {
    T *data;
    size_t length;

    inline T& operator[](size_t index) {
        assert(index < length && "ArrayPtr index out of bounds");
        return data[index];
    }

    // To support range-based for loops
    inline T* begin() { return data; }
    inline T* end() { return data + length; }
};

#endif // __BLOOM_H_ARRAY__