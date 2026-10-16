#include "heap_blocks.hpp"
#include <algorithm>
#include <new>
#include <optional>
#include <vector>

namespace clause::runtime::detail {
namespace {
// Blocks below this many words come from the C++ heap's own free lists cheaply and are not kept.
constexpr std::size_t CACHED_MIN_WORDS = 4096;
// Blocks and words one thread keeps at most: 32 MB on 64-bit targets.
constexpr std::size_t CACHED_BLOCKS = 8;
constexpr std::size_t CACHED_WORDS = std::size_t{4} << 20;

struct Cached {
    // A released block and its words.
    Word *block;
    std::size_t words;
};

// The blocks one thread released, freed when the thread ends.
class BlockCache final {
  public:
    BlockCache() = default;
    BlockCache(const BlockCache &) = delete;
    BlockCache &operator=(const BlockCache &) = delete;
    BlockCache(BlockCache &&) = delete;
    BlockCache &operator=(BlockCache &&) = delete;

    ~BlockCache() {
        for (const auto &cached : blocks_) {
            delete[] cached.block;
        }
    }

    // The smallest cached block of at least `words` and at most twice that, taken out of the cache.
    std::optional<Cached> take(std::size_t words) noexcept {
        auto best = blocks_.end();
        for (auto at = blocks_.begin(); at != blocks_.end(); ++at) {
            if (at->words >= words && at->words / 2 <= words && (best == blocks_.end() || at->words < best->words)) {
                best = at;
            }
        }
        if (best == blocks_.end()) {
            return std::nullopt;
        }
        const auto found = *best;
        held_ -= found.words;
        blocks_.erase(best);
        return found;
    }

    // Keep a released block while the cache has room; false leaves it to the caller to free.
    bool keep(Word *block, std::size_t words) noexcept {
        if (words < CACHED_MIN_WORDS || blocks_.size() == CACHED_BLOCKS || held_ + words > CACHED_WORDS) {
            return false;
        }
        if (blocks_.capacity() == 0) {
            try {
                blocks_.reserve(CACHED_BLOCKS);
            } catch (const std::bad_alloc &) {
                return false;
            }
        }
        blocks_.push_back({block, words});
        held_ += words;
        return true;
    }

  private:
    std::vector<Cached> blocks_;
    std::size_t held_ = 0;
};

BlockCache &cache() noexcept {
    thread_local BlockCache blocks;
    return blocks;
}
} // namespace

void BlockRelease::operator()(Word *block) const noexcept {
    if (!cache().keep(block, words)) {
        delete[] block;
    }
}

BlockWords allocate_block(std::size_t words) {
    if (words >= CACHED_MIN_WORDS) {
        if (const auto cached = cache().take(words)) {
            return BlockWords(cached->block, BlockRelease{cached->words});
        }
    }
    return BlockWords(new Word[words], BlockRelease{words});
}
} // namespace clause::runtime::detail
