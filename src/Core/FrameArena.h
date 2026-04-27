// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_FRAME_ARENA_H
#define NDEVC_FRAME_ARENA_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

// Linear scratch allocator. One instance per frame pipeline stage.
// Alloc() is O(1) — a pointer bump with alignment rounding.
// Reset() is O(1) — moves the offset back to zero.
// Destructors are never called; only trivially-destructible types may be allocated.
class FrameArena {
public:
    static constexpr size_t kDefaultCapacity = 32 * 1024 * 1024; // 32 MB

    explicit FrameArena(size_t capacity = kDefaultCapacity)
        : base_(new uint8_t[capacity]), offset_(0), capacity_(capacity) {}

    ~FrameArena() { delete[] base_; }

    FrameArena(const FrameArena&)            = delete;
    FrameArena& operator=(const FrameArena&) = delete;
    FrameArena(FrameArena&&)                 = delete;
    FrameArena& operator=(FrameArena&&)      = delete;

    // Allocate `count` objects of type T, zero-initialized, aligned to alignof(T).
    // Returns nullptr on overflow (assert fires in debug).
    template<typename T>
    T* Alloc(size_t count = 1) {
        static_assert(std::is_trivially_destructible_v<T>,
            "FrameArena: only trivially destructible types may be allocated");
        const size_t alignment = alignof(T);
        const size_t aligned   = (offset_ + alignment - 1) & ~(alignment - 1);
        const size_t bytes     = sizeof(T) * count;
        const size_t end       = aligned + bytes;
        assert(end <= capacity_ && "FrameArena overflow — increase kDefaultCapacity");
        if (end > capacity_) return nullptr;
        T* ptr = reinterpret_cast<T*>(base_ + aligned);
        std::memset(ptr, 0, bytes);
        offset_ = end;
        return ptr;
    }

    // Reset the write cursor to zero. O(1). Call once at the top of each frame.
    void Reset() noexcept { offset_ = 0; }

    size_t Used()     const noexcept { return offset_; }
    size_t Capacity() const noexcept { return capacity_; }

private:
    uint8_t* base_     = nullptr;
    size_t   offset_   = 0;
    size_t   capacity_ = 0;
};

#endif
