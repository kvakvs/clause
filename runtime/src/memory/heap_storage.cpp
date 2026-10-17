#include "heap_storage.hpp"
#include "off_heap.hpp"
#include <algorithm>
#include <limits>
#include <memory>
#include <new>

namespace clause::runtime::detail {
std::span<const Word> HeapArea::used() const noexcept { return {words_.get(), top_}; }

std::span<Word> HeapArea::from(std::uintptr_t address) noexcept {
    const auto begin = reinterpret_cast<std::uintptr_t>(words_.get());
    if (address < begin || (address - begin) % sizeof(Word) != 0 || (address - begin) / sizeof(Word) >= top_) {
        return {};
    }
    return std::span{words_.get(), top_}.subspan((address - begin) / sizeof(Word));
}

bool HeapArea::fits(std::size_t words) const noexcept { return words <= capacity_ - top_; }

std::span<Word> HeapArea::claim(std::size_t words) noexcept {
    const std::span<Word> claimed{words_.get() + top_, words};
    top_ += words;
    return claimed;
}

std::span<Word> HeapArea::bump(std::size_t words) noexcept {
    const std::span<Word> reserved{words_.get() + top_, words};
    std::ranges::fill(reserved, Word{0});
    top_ += words;
    return reserved;
}

HeapStorage::HeapStorage(HeapOptions options, std::weak_ptr<const ContextLifetime> lifetime, AtomStorage &atoms,
                         std::shared_ptr<RuntimeMemory> memory, const IdentityNumbers &identities)
    : options_(options), default_min_heap_words_(options.min_heap_words), lifetime_(std::move(lifetime)),
      token_(lifetime_.lock().get()), memory_(std::move(memory)), atoms_(&atoms), identities_(&identities) {}

HeapStorage::~HeapStorage() {
    release_off_heap(*this);
    memory_->release(capacity_words_);
}

HeapArea &HeapStorage::window() noexcept { return window_ == NO_FRAGMENT ? heap_ : fragments_[window_]; }

void HeapStorage::absorb() noexcept {
    if (inline_ && inline_->heap_top) {
        auto &area = window();
        const auto top = static_cast<std::size_t>(inline_->heap_top - area.words_.get());
        used_words_ += top - area.top_;
        area.top_ = top;
    }
}

void HeapStorage::expose() noexcept {
    if (!inline_) {
        return;
    }
    // A heap block nearly full shows this storage's own newest fragment instead, so allocation between safepoints (a
    // body recursion's returns) stays inline (fragment windows).
    window_ = !heap_.fits(WINDOW_WORDS) && own_fragment_ < fragments_.size() ? own_fragment_ : NO_FRAGMENT;
    const auto &area = window();
    const bool shown = area.capacity_ != 0 && options_.limit_bytes == UNLIMITED_HEAP_BYTES;
    inline_->heap_top = shown ? area.words_.get() + area.top_ : nullptr;
    inline_->heap_limit = shown ? area.words_.get() + area.capacity_ : nullptr;
}

bool HeapStorage::alive() const noexcept { return !lifetime_.expired() && token_->alive(); }

HeapMark HeapStorage::mark() const noexcept {
    return {heap_.capacity_, heap_.top_,     fragments_.size(), fragments_.empty() ? 0 : fragments_.back().top_,
            used_words_,     capacity_words_};
}

void HeapStorage::rollback(HeapMark mark) noexcept {
    while (fragments_.size() > mark.fragments) {
        fragments_.pop_back();
    }
    own_fragment_ = own_fragment_ < fragments_.size() ? own_fragment_ : NO_FRAGMENT;
    std::erase_if(ranges_, [&](const FragmentRange &range) { return range.fragment_ >= fragments_.size(); });
    if (!fragments_.empty()) {
        fragments_.back().top_ = mark.fragment_top;
    }
    if (mark.heap_capacity == 0) {
        heap_ = {};
    } else {
        heap_.top_ = mark.heap_top;
    }
    used_words_ = mark.used_words;
    memory_->release(capacity_words_ - mark.capacity_words);
    capacity_words_ = mark.capacity_words;
    pending_ = false;
    expose();
}

namespace {
// Validate arithmetic before reserving backing words or changing accounting.
std::expected<void, HeapError> validate(const HeapStorage &storage, std::size_t words) {
    if (words == 0 || words > std::numeric_limits<std::size_t>::max() / sizeof(Word)) {
        return std::unexpected(HeapError::invalid_size);
    }
    if (storage.pending_) {
        return std::unexpected(HeapError::unsafe_point);
    }
    if (words >
        words_left(storage.options_.limit_bytes / sizeof(Word), storage.used_words_ + storage.off_heap_words_)) {
        return std::unexpected(HeapError::limit_exceeded);
    }
    return {};
}
} // namespace

std::span<const Word> HeapStorage::readable(std::uintptr_t address) noexcept {
    if (const auto found = owned(address); !found.empty()) {
        return found;
    }
    return literals_ ? literals_->find(address) : std::span<const Word>{};
}

std::span<Word> HeapStorage::area_of(std::uintptr_t address) noexcept {
    absorb();
    for (auto *area : {&heap_, &old_}) {
        if (!area->from(address).empty()) {
            return {area->words_.get(), area->top_};
        }
    }
    const auto range = std::ranges::upper_bound(ranges_, address, {}, &FragmentRange::begin_);
    if (range == ranges_.begin()) {
        return {};
    }
    auto &fragment = fragments_[std::prev(range)->fragment_];
    return fragment.from(address).empty() ? std::span<Word>{} : std::span{fragment.words_.get(), fragment.top_};
}

std::span<Word> HeapStorage::owned(std::uintptr_t address) noexcept {
    absorb();
    if (const auto found = heap_.from(address); !found.empty()) {
        return found;
    }
    if (const auto found = old_.from(address); !found.empty()) {
        return found;
    }
    const auto range = std::ranges::upper_bound(ranges_, address, {}, &FragmentRange::begin_);
    return range == ranges_.begin() ? std::span<Word>{} : fragments_[std::prev(range)->fragment_].from(address);
}

std::expected<HeapArea, HeapError> HeapStorage::block(std::size_t words, std::size_t minimum) const {
    const auto remaining = room();
    if (words > remaining) {
        return std::unexpected(HeapError::limit_exceeded);
    }
    const auto capacity = std::min(remaining, std::max(words, minimum));
    return HeapArea{allocate_block(capacity), capacity};
}

std::expected<HeapArea *, HeapError> HeapStorage::add_fragment(std::size_t words) {
    // Grow the chain geometrically up front, so the insertions below cannot throw.
    if (fragments_.size() == fragments_.capacity()) {
        fragments_.reserve(2 * fragments_.size() + 1);
    }
    ranges_.reserve(fragments_.capacity());
    // Without a budget, at least as large as the fragments so far, so a long run of allocation between safepoints
    // needs few of them (fragment windows); a budget keeps the minimum.
    const auto minimum = std::min(options_.min_heap_words, DEFAULT_MIN_HEAP_WORDS);
    const auto grown =
        options_.limit_bytes == UNLIMITED_HEAP_BYTES ? capacity_words_ - heap_.capacity_ - old_.capacity_ : 0;
    auto fresh = block(words, std::max(minimum, grown));
    if (!fresh) {
        return std::unexpected(fresh.error());
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(fresh->words_.get());
    ranges_.insert(std::ranges::upper_bound(ranges_, begin, {}, &FragmentRange::begin_), {begin, fragments_.size()});
    capacity_words_ += fresh->capacity_;
    memory_->force(fresh->capacity_);
    signal_collection();
    own_fragment_ = fragments_.size();
    return &fragments_.emplace_back(std::move(*fresh));
}

std::expected<HeapArea *, HeapError> HeapStorage::area_for(std::size_t words) {
    if (heap_.capacity_ == 0) {
        auto fresh = block(words, options_.min_heap_words);
        if (!fresh) {
            return std::unexpected(fresh.error());
        }
        heap_ = std::move(*fresh);
        capacity_words_ += heap_.capacity_;
        memory_->force(heap_.capacity_);
    }
    if (heap_.fits(words)) {
        return &heap_;
    }
    if (!fragments_.empty() && fragments_.back().fits(words)) {
        return &fragments_.back();
    }
    return add_fragment(words);
}

std::expected<std::span<Word>, HeapError> HeapStorage::reserve(std::size_t words) {
    auto reserved = take(words, true);
    pending_ = reserved.has_value();
    return reserved;
}

std::expected<std::span<Word>, HeapError> HeapStorage::claim(std::size_t words) { return take(words, false); }

std::expected<std::span<Word>, HeapError> HeapStorage::take(std::size_t words, bool zeroed) {
    absorb();
    if (const auto valid = validate(*this, words); !valid) {
        return std::unexpected(valid.error());
    }
    // A failed or throwing area_for changed nothing: a new heap block always fits the request.
    const auto area = area_for(words);
    if (!area) {
        return std::unexpected(area.error());
    }
    used_words_ += words;
    const auto taken = zeroed ? (*area)->bump(words) : (*area)->claim(words);
    expose();
    return taken;
}

std::size_t HeapStorage::room() const noexcept {
    return std::min(words_left(options_.limit_bytes / sizeof(Word), capacity_words_ + off_heap_words_),
                    memory_->available());
}

std::size_t HeapStorage::budget() const noexcept {
    const auto owned = capacity_words_ + off_heap_words_;
    return std::min(options_.limit_bytes / sizeof(Word), owned + memory_->available());
}

bool HeapStorage::adopt(HeapStorage &message) {
    absorb();
    if (pending_ || heap_.capacity_ == 0 || message.pending_) {
        return false;
    }
    // Everything that may allocate comes first: the merged buffer counts and room for the new fragments.
    auto buffers = buffers_;
    std::size_t charged = 0;
    for (const auto &[buffer, cells] : message.buffers_) {
        const auto [entry, fresh] = buffers.try_emplace(buffer, 0);
        entry->second += cells;
        charged += fresh ? (buffer->size() + sizeof(Word) - 1) / sizeof(Word) : 0;
    }
    const auto added = message.fragments_.size() + (message.heap_.capacity_ != 0 ? 1 : 0);
    fragments_.reserve(fragments_.size() + added);
    ranges_.reserve(ranges_.size() + added);
    // Then nothing fails: the blocks keep their addresses, so every word of the message stays valid.
    const auto take = [&](HeapArea &area) {
        const auto begin = reinterpret_cast<std::uintptr_t>(area.words_.get());
        ranges_.insert(std::ranges::upper_bound(ranges_, begin, {}, &FragmentRange::begin_),
                       {begin, fragments_.size()});
        adopted_words_ += area.top_;
        fragments_.push_back(std::move(area));
    };
    if (message.heap_.capacity_ != 0) {
        take(message.heap_);
        message.heap_ = {};
    }
    std::ranges::for_each(message.fragments_, take);
    adopted_fragments_ += added;
    used_words_ += std::exchange(message.used_words_, 0);
    capacity_words_ += std::exchange(message.capacity_words_, 0);
    buffers_ = std::move(buffers);
    message.buffers_.clear();
    off_heap_words_ += charged;
    splice_off_heap(*this, message);
    message.off_heap_words_ = 0;
    message.fragments_.clear();
    message.ranges_.clear();
    if (adopted_words_ >= heap_.capacity_) {
        signal_collection();
    }
    return true;
}

void HeapStorage::replace(HeapArea heap, bool full) noexcept {
    adopted_fragments_ = 0;
    adopted_words_ = 0;
    memory_->release(capacity_words_);
    if (full) {
        old_ = {};
    }
    heap_ = std::move(heap);
    fragments_ = std::vector<HeapArea>{};
    ranges_ = std::vector<FragmentRange>{};
    own_fragment_ = NO_FRAGMENT;
    window_ = NO_FRAGMENT;
    // Every survivor has now lived through a collection: the next minor collection promotes them (generational).
    high_water_ = heap_.top_;
    used_words_ = heap_.top_ + old_.top_;
    capacity_words_ = heap_.capacity_ + old_.capacity_;
    // The old area's words stay charged: release took every area's charge, so the runtime account is forced back up.
    memory_->force(old_.capacity_);
    expose();
}
} // namespace clause::runtime::detail
