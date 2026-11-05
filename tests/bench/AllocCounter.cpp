#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "Harness.hpp"

namespace {

std::atomic<uint64_t> allocationTotal{0};
std::atomic<uint64_t> byteTotal{0};

void* countedAllocate(std::size_t size) {
    allocationTotal.fetch_add(1, std::memory_order_relaxed);
    byteTotal.fetch_add(size, std::memory_order_relaxed);
    void* block = std::malloc(size ? size : 1);
    if (!block) throw std::bad_alloc();
    return block;
}

void* countedAlignedAllocate(std::size_t size, std::size_t alignment) {
    allocationTotal.fetch_add(1, std::memory_order_relaxed);
    byteTotal.fetch_add(size, std::memory_order_relaxed);
    const std::size_t rounded = (size + alignment - 1) / alignment * alignment;
    void* block = std::aligned_alloc(alignment, rounded ? rounded : alignment);
    if (!block) throw std::bad_alloc();
    return block;
}

}

namespace bench {
uint64_t allocationCount() { return allocationTotal.load(std::memory_order_relaxed); }
uint64_t allocatedBytes() { return byteTotal.load(std::memory_order_relaxed); }
}

void* operator new(std::size_t size) { return countedAllocate(size); }
void* operator new[](std::size_t size) { return countedAllocate(size); }
void* operator new(std::size_t size, std::align_val_t alignment) { return countedAlignedAllocate(size, static_cast<std::size_t>(alignment)); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return countedAlignedAllocate(size, static_cast<std::size_t>(alignment)); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return countedAllocate(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return countedAllocate(size);
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void* block) noexcept { std::free(block); }
void operator delete[](void* block) noexcept { std::free(block); }
void operator delete(void* block, std::size_t) noexcept { std::free(block); }
void operator delete[](void* block, std::size_t) noexcept { std::free(block); }
void operator delete(void* block, std::align_val_t) noexcept { std::free(block); }
void operator delete[](void* block, std::align_val_t) noexcept { std::free(block); }
void operator delete(void* block, std::size_t, std::align_val_t) noexcept { std::free(block); }
void operator delete[](void* block, std::size_t, std::align_val_t) noexcept { std::free(block); }
void operator delete(void* block, const std::nothrow_t&) noexcept { std::free(block); }
void operator delete[](void* block, const std::nothrow_t&) noexcept { std::free(block); }
