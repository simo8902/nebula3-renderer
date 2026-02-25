// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_RINGBUFFER_H
#define NDEVC_RINGBUFFER_H

#include <vector>
#include <cstddef>
#include <cassert>

namespace Particles {

template <class T>
class RingBuffer {
public:
    RingBuffer() = default;
    explicit RingBuffer(size_t capacity) { SetCapacity(capacity); }

    void SetCapacity(size_t newCapacity) {
        capacity = newCapacity;
        size = 0;
        baseIndex = 0;
        headIndex = 0;
        elements.clear();
        if (newCapacity > 0) {
            elements.resize(newCapacity);
        }
    }

    void Clear() { SetCapacity(0); }

    void Reset() {
        size = 0;
        baseIndex = 0;
        headIndex = 0;
    }

    size_t Capacity() const { return capacity; }
    size_t Size() const { return size; }
    bool IsEmpty() const { return size == 0; }

    void Add(const T& value) {
        assert(capacity > 0);
        elements[headIndex++] = value;
        if (headIndex >= capacity) headIndex = 0;
        if (size == capacity) {
            baseIndex++;
            if (baseIndex >= capacity) baseIndex = 0;
        } else {
            size++;
        }
    }

    T& operator[](size_t index) {
        assert(index < size);
        size_t absIndex = index + baseIndex;
        if (absIndex >= capacity) absIndex -= capacity;
        return elements[absIndex];
    }

    const T& operator[](size_t index) const {
        assert(index < size);
        size_t absIndex = index + baseIndex;
        if (absIndex >= capacity) absIndex -= capacity;
        return elements[absIndex];
    }

    T* GetBuffer() { return elements.data(); }
    const T* GetBuffer() const { return elements.data(); }

private:
    size_t capacity = 0;
    size_t size = 0;
    size_t baseIndex = 0;
    size_t headIndex = 0;
    std::vector<T> elements;
};

} // namespace Particles

#endif // NDEVC_RINGBUFFER_H
