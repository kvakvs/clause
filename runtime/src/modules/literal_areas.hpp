#pragma once
#include "../memory/persistent_region.hpp"
#include <atomic>
#include <clause/runtime/base_types.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace clause::runtime::detail {
// The constant literal areas of the modules a runtime registered (OPT0.4): compound literals the compiler laid out
// as heap cells in read-only memory. Terms may point into them from any process of the runtime; the collector never
// moves them and copies share them. Areas are only added, and live as long as the program's code.
class LiteralAreas final {
  public:
    LiteralAreas() = default;
    ~LiteralAreas();
    LiteralAreas(const LiteralAreas &) = delete;
    LiteralAreas &operator=(const LiteralAreas &) = delete;

    // Add the words of one module's literal area; an empty area adds nothing. May throw std::bad_alloc.
    void add(std::span<const Word> area);
    // Add an area of the literal heap in the persistent term range and keep its owner, which frees it, as long as the
    // runtime (literal heap). May throw std::bad_alloc.
    void add(std::span<const Word> area, std::shared_ptr<const void> owner);
    // The words from a word-aligned address inside a module area to that area's end, or inside the persistent term
    // range to its committed end; empty for any other address. Lock-free: readers see an immutable snapshot.
    std::span<const Word> find(std::uintptr_t address) const noexcept;

    // Whether a copy may keep a word naming this address instead of copying its object: a module literal, or a
    // persistent term that is not retiring (persistent term reclamation).
    bool shared(std::uintptr_t address) const noexcept {
        return persistent_.contains(address) ? !persistent_.retiring(address) : module(address);
    }

    // Whether an address lies in a module's literal area; persistent terms are not module literals.
    bool module(std::uintptr_t address) const noexcept;
    // The first and one-past-last address spanned by all module areas now; equal for none. Areas are only added, so
    // an address outside it is no module literal of any term already reachable (cheap literal tests).
    std::pair<std::uintptr_t, std::uintptr_t> module_span() const noexcept;

    // The address range persistent terms are copied into (persistent term reclamation).
    PersistentRegion &persistent() noexcept { return persistent_; }

    const PersistentRegion &persistent() const noexcept { return persistent_; }

  private:
    // The words from an address inside a module area to that area's end; empty for any other address.
    std::span<const Word> module_words(std::uintptr_t address) const noexcept;

    struct Range {
        // First and one-past-last word addresses of an area, and its words.
        std::uintptr_t begin;
        std::uintptr_t end;
        const Word *words;
    };

    // The published ranges, sorted by address; replaced as a whole when an area is added.
    std::atomic<const std::vector<Range> *> current_{nullptr};
    // Every snapshot published so far, freed with the registry (readers may still hold an older one).
    std::vector<std::unique_ptr<const std::vector<Range>>> snapshots_;
    // Serializes writers.
    std::mutex mutex_;
    // The persistent term range; its areas come and go, so they are not entered in the snapshots.
    PersistentRegion persistent_;
    // The owners of the literal heap areas, freed before the range (literal heap).
    std::vector<std::shared_ptr<const void>> owners_;
};
} // namespace clause::runtime::detail
