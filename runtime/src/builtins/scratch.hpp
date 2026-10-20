#pragma once
#include <cstddef>
#include <utility>

// Codec scratch reuse: builtins that build large temporaries (the external term codec's stacks and output) borrow a
// per-thread buffer for one call and return it with its capacity, so each call does not take fresh pages from the
// C++ heap, which returns large freed blocks to the operating system.
namespace clause::runtime::builtins {
// Buffers whose capacity is above this many bytes are freed instead of kept, bounding what each thread holds.
inline constexpr std::size_t SCRATCH_KEPT_BYTES = std::size_t{1} << 20;

// One borrowed, cleared Buffer (a std::vector or std::string); Slot names which per-thread buffer, so one builtin can
// borrow several of the same type. A nested borrow of a slot in use gets an empty buffer.
template <typename Buffer, typename Slot> class Scratch final {
  public:
    Scratch() : buffer_(std::exchange(kept(), Buffer{})) { buffer_.clear(); }

    ~Scratch() {
        if (buffer_.capacity() * sizeof(typename Buffer::value_type) <= SCRATCH_KEPT_BYTES) {
            kept() = std::move(buffer_);
        }
    }

    Scratch(const Scratch &) = delete;
    Scratch &operator=(const Scratch &) = delete;
    Scratch(Scratch &&) = delete;
    Scratch &operator=(Scratch &&) = delete;

    Buffer &operator*() noexcept { return buffer_; }

    Buffer *operator->() noexcept { return &buffer_; }

  private:
    // The buffer this thread keeps between borrows.
    static Buffer &kept() noexcept {
        thread_local Buffer buffer;
        return buffer;
    }

    // The buffer in use for this borrow.
    Buffer buffer_;
};
} // namespace clause::runtime::builtins
