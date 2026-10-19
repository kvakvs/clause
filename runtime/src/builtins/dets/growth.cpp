#include "table.hpp"
#include <algorithm>
#include <iterator>

// The split of a slot's collections when a dets table grows by a segment (plan 21 step LIB53): dets_v9:re_hash/2.
// The slots of one segment are read in batches of up to MAXCOLL bytes; each collection's keys stay in the old slot
// or move to the slot m higher, as their hash modulo 2m says.
namespace clause::runtime::builtins::dets {
namespace {
// The segment being split and the positions of its first slot and of the slot m higher.
struct Split {
    Table &table;
    const Terms &terms;
    std::span<const std::byte> segment;
    std::uint32_t from;
    std::uint32_t to;
};

// The slots [begin, end) of a batch with the pointer bytes they get in both segments and the collection writes.
struct Batch {
    std::size_t begin;
    std::vector<Bytes> from;
    std::vector<Bytes> to;
    Writes trail;
};

Bytes slot_bytes(std::span<const std::byte> segment, std::size_t index) {
    const auto bytes = segment.subspan(SLOT_BYTES * index, SLOT_BYTES);
    return {bytes.begin(), bytes.end()};
}

Bytes concatenated(const std::vector<Bytes> &parts) {
    Bytes out;
    for (const auto &part : parts) {
        append(out, part);
    }
    return out;
}

void append_reversed(Writes &out, Writes writes) {
    std::ranges::move(writes.rbegin(), writes.rend(), std::back_inserter(out));
}

// The bytes of the keys of a collection that stay and of those that move.
std::pair<Bytes, Bytes> split_keys(const Split &split, const Bytes &collection) {
    std::pair<Bytes, Bytes> sides;
    const auto &head = split.table.head();
    try {
        for (const auto &key : split.table.per_key(split.terms, collection)) {
            const auto moves = split.terms.hash(key.key, head.hash) % head.m2 >= head.m;
            append(moves ? sides.second : sides.first, key.bytes);
        }
    } catch (const BadObject &) {
        bad_object("re_hash_slots", split.table.name());
    }
    return sides;
}

// Split one slot: its pointers in both segments, and the writes of the collections a split rewrites.
void split_slot(const Split &split, std::size_t index, const Bytes &collection, Batch &batch) {
    const auto raw = slot_bytes(split.segment, index);
    auto &from = batch.from[index - batch.begin];
    auto &to = batch.to[index - batch.begin];
    auto [stay, move] = split_keys(split, collection);
    if (stay.empty() || move.empty()) {
        from = move.empty() ? raw : Bytes(SLOT_BYTES);
        to = move.empty() ? Bytes(SLOT_BYTES) : raw;
        return;
    }
    const auto position1 = static_cast<std::uint32_t>(split.from + SLOT_BYTES * index);
    const auto position2 = static_cast<std::uint32_t>(split.to + SLOT_BYTES * index);
    auto kept = split.table.updated({.collection = read_u32(raw, 4), .size = read_u32(raw, 0), .slot = position1}, stay,
                                    true, true);
    auto moved = split.table.updated({.collection = 0, .size = 0, .slot = position2}, move, true, true);
    from = kept.pointers.empty() ? raw : kept.pointers.front().second;
    to = moved.pointers.front().second;
    append_reversed(batch.trail, std::move(kept.collections));
    append_reversed(batch.trail, std::move(moved.collections));
}

// Split the slots [begin, end), last first as OTP reads them, and write the batch.
void split_batch(const Split &split, std::size_t begin, std::size_t end) {
    Batch batch{
        .begin = begin, .from = std::vector<Bytes>(end - begin), .to = std::vector<Bytes>(end - begin), .trail = {}};
    std::vector<std::pair<std::uint64_t, std::uint32_t>> reads;
    for (auto index = end; index-- > begin;) {
        if (const auto size = read_u32(split.segment, SLOT_BYTES * index); size != 0) {
            reads.emplace_back(read_u32(split.segment, SLOT_BYTES * index + 4), size);
        }
    }
    const auto collections = split.table.pread_all(reads);
    auto next = collections.begin();
    for (auto index = end; index-- > begin;) {
        if (read_u32(split.segment, SLOT_BYTES * index) == 0) {
            batch.from[index - begin] = slot_bytes(split.segment, index);
            batch.to[index - begin] = Bytes(SLOT_BYTES);
        } else {
            split_slot(split, index, *next++, batch);
        }
    }
    Writes writes{{split.from + SLOT_BYTES * begin, concatenated(batch.from)},
                  {split.to + SLOT_BYTES * begin, concatenated(batch.to)}};
    std::ranges::move(batch.trail, std::back_inserter(writes));
    split.table.apply(writes);
}
} // namespace

void Table::re_hash(const Terms &terms, std::uint32_t slot_start) {
    const auto from = slot_position(slot_start);
    const auto to = slot_position(slot_start + head_.m);
    const auto segment = pread_all({{from, SEGMENT_BYTES}}).front();
    if (segment.size() < SEGMENT_BYTES) {
        fail(Failure::Shape::named, "premature_eof", name());
    }
    const Split split{.table = *this, .terms = terms, .segment = segment, .from = from, .to = to};
    std::size_t begin = 0;
    std::uint64_t so_far = 0;
    bool reading = false;
    for (std::size_t index = 0; index < SEGMENT_SLOTS;) {
        const auto size = read_u32(segment, SLOT_BYTES * index);
        if (so_far + size > MAX_COLLECTION && reading) {
            split_batch(split, begin, index);
            begin = index;
            so_far = 0;
            reading = false;
            continue;
        }
        so_far += size;
        reading = reading || size != 0;
        ++index;
    }
    if (so_far > 0) {
        split_batch(split, begin, SEGMENT_SLOTS);
    }
}
} // namespace clause::runtime::builtins::dets
