#pragma once
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace clause::runtime {
// The standard allocator, except that value-initialization leaves an element default-initialized: a byte buffer
// grows without zeroing bytes that a read or copy fills right after (OPT3.4 file I/O).
template <class T> class FillFreeAllocator : public std::allocator<T> {
  public:
    FillFreeAllocator() = default;

    // Containers rebind their allocator to node types by conversion.
    template <class U> FillFreeAllocator(const FillFreeAllocator<U> &) noexcept {}

    template <class U> void construct(U *at) noexcept(std::is_nothrow_default_constructible_v<U>) {
        ::new (static_cast<void *>(at)) U;
    }

    template <class U, class... Arguments> void construct(U *at, Arguments &&...arguments) {
        std::construct_at(at, std::forward<Arguments>(arguments)...);
    }
};

// Bytes whose growth leaves the new bytes unset: off-heap binary buffers and port driver replies, filled right after
// they grow and taken over by a binary without a copy (OPT3.4 file I/O).
using ByteBuffer = std::vector<std::byte, FillFreeAllocator<std::byte>>;
} // namespace clause::runtime
