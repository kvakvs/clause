#include "detached_term.hpp"
#include "../terms/term_layout.hpp"
#include "heap_object.hpp"
#include "runtime_memory.hpp"
#include <memory>
#include <new>
#include <utility>

// Detached terms (plan 21 step LIB10A): the block's life; the copy itself is in copy.cpp, beside the graph copy.
namespace clause::runtime::detail {
static_assert(sizeof(std::size_t) == sizeof(Word), "offsets keep the words after them aligned");

DetachedTerm::Block *DetachedTerm::allocate(RuntimeMemory &memory, std::size_t words, std::size_t cells) {
    void *raw = ::operator new(sizeof(Block) + ((cells + words) * sizeof(Word)));
    return new (raw) Block{.memory = &memory, .words = words, .capacity = cells};
}

DetachedTerm::DetachedTerm(DetachedTerm &&other) noexcept
    : root_(std::exchange(other.root_, 0)), block_(std::exchange(other.block_, nullptr)) {}

DetachedTerm &DetachedTerm::operator=(DetachedTerm &&other) noexcept {
    if (this != &other) {
        release();
        root_ = std::exchange(other.root_, 0);
        block_ = std::exchange(other.block_, nullptr);
    }
    return *this;
}

DetachedTerm::~DetachedTerm() { release(); }

Term DetachedTerm::term() const noexcept { return TermAccess::make(root_); }

std::size_t DetachedTerm::words() const noexcept { return block_ == nullptr ? 0 : block_->words; }

void DetachedTerm::release() noexcept {
    if (block_ == nullptr) {
        return;
    }
    auto *words = block_->data();
    for (std::size_t index = 0; index < block_->cells; ++index) {
        std::destroy_at(reinterpret_cast<layout::RefcBinaryCell *>(words + block_->offsets()[index]));
    }
    block_->memory->release(block_->words);
    std::destroy_at(block_);
    ::operator delete(static_cast<void *>(block_));
    block_ = nullptr;
}
} // namespace clause::runtime::detail
