#pragma once
#include <cassert>
#include <clause/compiler/ast/ids.hpp>
#include <limits>
#include <stdexcept>
#include <vector>

namespace clause::ast::detail {
// An owned identity token distinguishes independent modules even after one is destroyed.
struct Owner {};

// Flat node storage makes rollback and destruction iterative rather than recursive.
template <typename Value, typename Tag> class Arena {
  public:
    // All arenas within a module use the same retained owner identity.
    explicit Arena(std::shared_ptr<const Owner> owner) : owner_(std::move(owner)) {}

    // Allocate a fresh generation; rollback never rewinds the generation counter.
    Id<Tag> append(Value value) {
        if (next_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::length_error("AST handle generations exhausted");
        }
        const auto generation = next_++;
        entries_.push_back({generation, std::move(value)});
        return Id<Tag>(owner_, {entries_.size() - 1, generation});
    }

    // Validate both module identity and liveness before exposing a payload.
    const Value &get(const Id<Tag> &id) const {
        if (id.owner_ != owner_ || id.slot_.index >= entries_.size()) {
            throw std::invalid_argument("foreign or expired AST handle");
        }
        const auto &entry = entries_[id.slot_.index];
        if (entry.generation != id.slot_.generation) {
            throw std::invalid_argument("rolled-back AST handle");
        }
        return entry.value;
    }

    // Checkpoint sizes come only from the builder's single active transaction.
    std::size_t size() const { return entries_.size(); }

    // The value at an arena position, for references by position that outlive handles. Added for the inference cache
    // (58V).
    const Value &at(std::size_t index) const { return entries_.at(index).value; }

    void truncate(std::size_t size) noexcept {
        assert(size <= entries_.size());
        while (entries_.size() > size) {
            entries_.pop_back();
        }
    }

  private:
    struct Entry {
        // Store liveness separately from the typed value so recycled slots are detectable.
        std::uint64_t generation;
        Value value;
    };

    // A handle retains only the identity token, never the AST or recursive children.
    std::shared_ptr<const Owner> owner_;
    std::uint64_t next_ = 1;
    std::vector<Entry> entries_;
};
} // namespace clause::ast::detail
