#pragma once
#include <atomic>
#include <clause/runtime/base_types.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
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
    // The words from a word-aligned address inside an area to that area's end; empty for any other address.
    // Lock-free: readers see an immutable snapshot.
    std::span<const Word> find(std::uintptr_t address) const noexcept;

  private:
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
};
} // namespace clause::runtime::detail
