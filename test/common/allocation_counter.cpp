#include "common/allocation_counter.hpp"

#include <cstdlib>
#include <new>

namespace {

thread_local std::size_t t_allocations = 0;

void *countedAllocate(std::size_t size) {
    ++t_allocations;
    if (size == 0) {
        size = 1;
    }
    while (true) {
        if (void *memory = std::malloc(size)) {
            return memory;
        }
        std::new_handler handler = std::get_new_handler();
        if (handler == nullptr) {
            throw std::bad_alloc();
        }
        handler();
    }
}

} // namespace

// The default nothrow forms route through these. The over-aligned forms do
// not, and stay uncounted.
void *operator new(std::size_t size) {
    return countedAllocate(size);
}

void *operator new[](std::size_t size) {
    return countedAllocate(size);
}

void operator delete(void *memory) noexcept {
    std::free(memory);
}

void operator delete[](void *memory) noexcept {
    std::free(memory);
}

void operator delete(void *memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void *memory, std::size_t) noexcept {
    std::free(memory);
}

namespace IRTest {

AllocationCounter::AllocationCounter()
    : m_start(t_allocations) {}

std::size_t AllocationCounter::allocations() const {
    return t_allocations - m_start;
}

} // namespace IRTest
