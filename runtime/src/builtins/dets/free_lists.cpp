#include "free_lists.hpp"
#include <algorithm>

// The buddy system of dets files (plan 21 step LIB53), dets_utils.erl's alloc/free/find functions over ordered sets.
namespace clause::runtime::builtins::dets {
namespace {
// The address above which a block would reach the area reserved for the free lists.
constexpr std::uint64_t SPACE_LIMIT = (std::uint64_t{1} << 31U) - MAX_FREE_LISTS;
// Addresses per free list record: (MAXFREEOBJ - 4 - OHDSZ) div 4.
constexpr std::size_t RECORD_ADDRESSES = (MAX_FREE_OBJECT - 4 - OBJECT_HEADER) / 4;
constexpr std::uint32_t RECORD_HEADER = 4 + OBJECT_HEADER;

std::uint64_t block_size(int pos) { return power(pos - 1); }

// The buddy of a block, and the block the pair makes: dets_utils:my_buddy/3.
std::pair<std::uint32_t, FreeBlock> buddy(const FreeBlock &block, std::uint32_t base) {
    const auto size = block_size(block.pos);
    const auto offset = static_cast<std::uint64_t>(static_cast<std::int64_t>(block.address) - base);
    if ((offset & size) == 0) {
        return {static_cast<std::uint32_t>(block.address + size), {.address = block.address, .pos = block.pos + 1}};
    }
    const auto low = static_cast<std::uint32_t>(block.address - size);
    return {low, {.address = low, .pos = block.pos + 1}};
}

// dets_utils:adjust_addr/3: an address aligned down to the class of `where` relative to the base, unless below one
// block.
std::uint32_t aligned(const FreeBlock &where, std::uint32_t base) {
    const auto pow = static_cast<std::int64_t>(block_size(where.pos));
    const std::int64_t address = where.address;
    const auto rest = (address - static_cast<std::int64_t>(base)) % pow;
    return static_cast<std::uint32_t>(std::max<std::int64_t>(rest == 0 || address < pow ? address : address - rest, 0));
}

void append_record(Bytes &out, int pos, std::span<const std::uint32_t> addresses) {
    append_u32(out, static_cast<std::uint32_t>(addresses.size() * 4) + RECORD_HEADER);
    append_u32(out, FREE);
    append_u32(out, static_cast<std::uint32_t>(pos));
    for (const auto address : addresses) {
        append_u32(out, address);
    }
}

// Whether the record at the start of `rest` is well formed: a class, and whole addresses within the bytes.
bool record_fits(std::span<const std::byte> rest) {
    const auto pos = read_u32(rest, 8);
    if (pos < 1 || pos > MAX_BUDDY) {
        return false;
    }
    const auto size = read_u32(rest, 0);
    return size >= RECORD_HEADER && (size - RECORD_HEADER) % 4 == 0 && size <= rest.size();
}

// The addresses of one record when they rise above `last`; none otherwise.
std::optional<std::vector<std::uint32_t>> record_addresses(std::span<const std::byte> bytes, std::int64_t last) {
    std::vector<std::uint32_t> addresses;
    addresses.reserve(bytes.size() / 4);
    for (std::size_t at = 0; at < bytes.size(); at += 4) {
        const auto address = read_u32(bytes, at);
        if (static_cast<std::int64_t>(address) <= last) {
            return std::nullopt;
        }
        last = address;
        addresses.push_back(address);
    }
    return addresses;
}

// Reads free list records in order: the addresses of each class must rise across its consecutive records.
class RecordReader final {
  public:
    // Take the record at the start of `rest`; false when it is malformed.
    bool take(std::span<const std::byte> rest, FreeLists &lists) {
        if (!record_fits(rest)) {
            return false;
        }
        const auto pos = static_cast<int>(read_u32(rest, 8));
        last_ = pos == pos_ ? last_ : -1;
        const auto addresses = record_addresses(rest.subspan(RECORD_HEADER, read_u32(rest, 0) - RECORD_HEADER), last_);
        if (!addresses) {
            return false;
        }
        pos_ = pos;
        last_ = addresses->empty() ? last_ : addresses->back();
        lists.at(pos).insert(addresses->begin(), addresses->end());
        return true;
    }

  private:
    int pos_ = 0;
    std::int64_t last_ = -1;
};
} // namespace

std::vector<Area> gaps(const std::vector<Area> &free, std::uint32_t start, std::optional<std::uint32_t> end) {
    std::vector<Area> areas;
    auto at = start;
    for (const auto &block : free) {
        if (at < block.from) {
            areas.push_back({.from = at, .to = block.from});
        }
        at = block.to;
    }
    if (end && at < *end) {
        areas.push_back({.from = at, .to = *end});
    }
    return areas;
}

FreeLists::FreeLists(std::uint32_t base) { at(MAX_BUDDY).insert(base); }

FreeBlock FreeLists::first_free(int wanted, const std::string &file) const {
    for (auto pos = wanted; pos <= MAX_BUDDY; ++pos) {
        const auto &set = at(pos);
        if (set.empty()) {
            continue;
        }
        if (*set.begin() + block_size(wanted) > SPACE_LIMIT) {
            break;
        }
        return {.address = *set.begin(), .pos = pos};
    }
    fail(Failure::Shape::named, "no_more_space_on_file", file);
}

void FreeLists::reserve(const FreeBlock &block, int wanted) {
    at(block.pos).erase(block.address);
    move_down(block, wanted);
}

void FreeLists::move_down(const FreeBlock &block, int wanted) {
    for (auto pos = block.pos; pos > wanted; --pos) {
        at(pos - 1).insert(static_cast<std::uint32_t>(block.address + (block_size(pos) >> 1U)));
    }
}

void FreeLists::undo_free(FreeBlock block, std::uint32_t base) {
    while (block.pos <= MAX_BUDDY && at(block.pos).erase(block.address) == 0) {
        const auto [pair, up] = buddy(block, base);
        at(block.pos).insert(pair);
        block = up;
    }
}

void FreeLists::free_in(FreeBlock block, std::uint32_t base) {
    for (; block.pos <= MAX_BUDDY;) {
        const auto [pair, up] = buddy(block, base);
        if (!at(block.pos).contains(pair)) {
            at(block.pos).insert(block.address);
            return;
        }
        at(block.pos).erase(block.address);
        at(block.pos).erase(pair);
        block = up;
    }
}

std::vector<Area> FreeLists::all() const {
    std::vector<Area> blocks;
    for (int pos = 1; pos <= MAX_BUDDY; ++pos) {
        for (const auto address : at(pos)) {
            blocks.push_back({.from = address, .to = static_cast<std::uint32_t>(address + block_size(pos))});
        }
    }
    std::ranges::sort(blocks, {}, [](const Area &area) { return std::pair{area.from, area.to}; });
    return blocks;
}

std::vector<Area> FreeLists::interval(const Area &area, std::uint32_t base) const {
    std::vector<Area> blocks;
    for (int pos = 1; pos <= MAX_BUDDY; ++pos) {
        const auto &set = at(pos);
        for (auto it = set.lower_bound(aligned({.address = area.from, .pos = pos}, base));
             it != set.end() && *it <= area.to; ++it) {
            blocks.push_back({.from = *it, .to = static_cast<std::uint32_t>(*it + block_size(pos))});
        }
    }
    std::ranges::sort(blocks, {}, [](const Area &block) { return std::pair{block.from, block.to}; });
    return blocks;
}

std::optional<FreeBlock> FreeLists::next_free(std::uint32_t address, std::uint32_t base) const {
    std::optional<FreeBlock> best;
    for (int pos = 1; pos <= MAX_BUDDY; ++pos) {
        const auto &set = at(pos);
        const auto found = set.lower_bound(aligned({.address = address, .pos = pos}, base));
        if (found != set.end() && (!best || *found < best->address)) {
            best = FreeBlock{.address = *found, .pos = pos};
        }
    }
    return best;
}

Bytes FreeLists::to_file() const {
    Bytes out;
    for (int pos = 1; pos <= MAX_BUDDY; ++pos) {
        const std::vector<std::uint32_t> addresses(at(pos).begin(), at(pos).end());
        for (std::size_t from = 0; from < addresses.size(); from += RECORD_ADDRESSES) {
            const auto count = std::min(RECORD_ADDRESSES, addresses.size() - from);
            append_record(out, pos, std::span(addresses).subspan(from, count));
        }
    }
    append_u32(out, RECORD_HEADER);
    append_u32(out, FREE);
    append_u32(out, END_FREE);
    return out;
}

std::optional<FreeLists> FreeLists::from_file(std::span<const std::byte> bytes) {
    FreeLists lists;
    RecordReader reader;
    for (std::size_t at = 0; at + RECORD_HEADER <= bytes.size(); at += read_u32(bytes, at)) {
        if (read_u32(bytes, at + 4) != FREE) {
            return std::nullopt;
        }
        if (read_u32(bytes, at + 8) == END_FREE) {
            return lists;
        }
        if (!reader.take(bytes.subspan(at), lists)) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

FreeBlock Allocator::alloc(std::uint64_t size, const std::string &file) {
    const auto pos = 1 + log2(size + 1);
    if (!frozen_) {
        const auto block = current_.first_free(pos, file);
        current_.reserve(block, pos);
        return {.address = block.address, .pos = pos};
    }
    const auto block = frozen_->first_free(pos, file);
    frozen_->reserve(block, pos);
    current_.undo_free(block, base_);
    current_.move_down(block, pos);
    return {.address = block.address, .pos = pos};
}

void Allocator::free(const FreeBlock &block) { current_.free_in(block, base_); }

void Allocator::alloc_many(std::uint64_t total, const std::string &file) {
    int pos = 1;
    while (total > 0) {
        const auto block = current_.first_free(pos, file);
        if (block_size(block.pos) >= total) {
            alloc_many_rest(total, file);
            return;
        }
        current_.reserve(block, block.pos);
        total -= block_size(block.pos);
        pos = block.pos;
    }
}

// dets_utils:alloc_many2/5: the rest of a run, one block per bit of its size from the highest down.
void Allocator::alloc_many_rest(std::uint64_t total, const std::string &file) {
    for (auto pos = 1 + log2(total + 1); total > 0 && pos >= 1; --pos) {
        if ((total & block_size(pos)) == 0) {
            continue;
        }
        current_.reserve(current_.first_free(pos, file), pos);
        total -= block_size(pos);
    }
}

void Allocator::fix() { frozen_ = current_; }

void Allocator::unfix() { frozen_.reset(); }

std::optional<Area> Allocator::next_allocated(std::uint32_t address) const {
    for (;;) {
        const auto free = current_.next_free(address, base_);
        if (!free) {
            return std::nullopt;
        }
        if (free->address > address) {
            return Area{.from = address, .to = free->address};
        }
        address = static_cast<std::uint32_t>(free->address + block_size(free->pos));
    }
}

std::vector<Area> Allocator::allocated(std::uint32_t address, std::uint32_t length) const {
    for (;;) {
        const auto high = address + length;
        auto areas = gaps(current_.interval({.from = address, .to = high}, base_), address, high);
        if (!areas.empty()) {
            return areas;
        }
        const auto next = next_allocated(address);
        if (!next) {
            return areas;
        }
        address = next->from;
        length = CHUNK_SIZE;
    }
}

std::vector<Area> Allocator::all_allocated() const { return gaps(current_.all(), base_, std::nullopt); }
} // namespace clause::runtime::builtins::dets
