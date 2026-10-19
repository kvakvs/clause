#include "table.hpp"
#include <algorithm>
#include <iterator>

// The slots, segments, growth and saving of dets files (plan 21 step LIB53): dets_v9.erl's init_file, cache_segps,
// slot_objects, may_grow, grow, re_hash, updated, mark_dirty and do_perform_save.
namespace clause::runtime::builtins::dets {
namespace {
// The power of two of at least `slots` slots (at least one segment); dets_v9:slots2/1.
std::uint32_t slots_of(std::uint32_t slots) {
    return static_cast<std::uint32_t>(power(log2(std::max(slots, SEGMENT_SLOTS))));
}

Bytes zeros(std::size_t count) { return Bytes(count); }

Bytes word(std::uint32_t value) {
    Bytes out;
    append_u32(out, value);
    return out;
}

Bytes pair_of(std::uint32_t first, std::uint32_t second) {
    Bytes out;
    append_u32(out, first);
    append_u32(out, second);
    return out;
}

// A collection: its size and ACTIVE status, then its keys' bytes.
Bytes collection_bytes(std::uint32_t size, const Bytes &keys) {
    Bytes out;
    append_u32(out, size);
    append_u32(out, ACTIVE);
    append(out, keys);
    return out;
}

// The objects of a set collection's keys: <<Size:32, Object>> each.
void set_objects(const Terms &terms, std::span<const std::byte> keys, std::vector<Term> &out) {
    for (std::size_t at = 0; at < keys.size();) {
        const auto size = read_u32(keys, at);
        if (size < 4 || at + size > keys.size()) {
            throw BadObject{};
        }
        out.push_back(terms.decode(keys.subspan(at + 4, size - 4)));
        at += size;
    }
}

// The objects of a bag collection's keys: <<KeySize:32, <<Size:32, Object>>...>> each.
void bag_objects(const Terms &terms, std::span<const std::byte> keys, std::vector<Term> &out) {
    for (std::size_t at = 0; at < keys.size();) {
        const auto size = read_u32(keys, at);
        if (size < 4 || at + size > keys.size()) {
            throw BadObject{};
        }
        set_objects(terms, keys.subspan(at + 4, size - 4), out);
        at += size;
    }
}

// The key at the start of `keys` of a set collection.
StoredKey set_key(const Terms &terms, std::span<const std::byte> keys, std::uint32_t keypos) {
    const auto size = read_u32(keys, 0);
    if (size < 4 || size > keys.size()) {
        throw BadObject{};
    }
    const auto bytes = keys.first(size);
    const auto term = terms.decode(bytes.subspan(4));
    return {.key = Terms::element(term, keypos),
            .bytes = bytes,
            .first = {.term = term, .seq = SEQ_START, .source = Item::Source::stored, .bytes = bytes},
            .rest = {}};
}

// The key at the start of `keys` of a bag collection.
StoredKey bag_key(const Terms &terms, std::span<const std::byte> keys, std::uint32_t keypos) {
    const auto size = read_u32(keys, 0);
    if (size < 8 || size > keys.size()) {
        throw BadObject{};
    }
    const auto bytes = keys.first(size);
    const auto object_size = read_u32(bytes, 4);
    if (object_size < 4 || 4 + object_size > size) {
        throw BadObject{};
    }
    const auto first = bytes.subspan(4, object_size);
    const auto term = terms.decode(first.subspan(4));
    return {.key = Terms::element(term, keypos),
            .bytes = bytes,
            .first = {.term = term, .seq = SEQ_START, .source = Item::Source::stored, .bytes = first},
            .rest = bytes.subspan(4 + object_size)};
}
} // namespace

void bad_object(const std::string &where, const std::string &file) { fail(Failure::Shape::bad_object, where, file); }

std::uint32_t no_parts(std::uint32_t slots) { return ((slots - 1) / (SEGMENT_SLOTS * PART_SEGMENTS)) + 1; }

std::int64_t no_segments(std::int64_t slots) { return ((slots - 1) / SEGMENT_SLOTS) + 1; }

Table::Table(File file, Head head, Allocator allocator, bool read_only)
    : file_(std::move(file)), head_(std::move(head)), allocator_(std::move(allocator)), read_only_(read_only) {}

Table Table::initiate(File file, const TableShape &shape, bool init_segments) {
    const auto max = std::min(shape.max_slots, MAX_SLOTS);
    Head head;
    head.min_slots = slots_of(std::min(shape.min_slots, max));
    head.max_slots = slots_of(max);
    head.m = head.next = head.min_slots;
    head.m2 = 2 * head.m;
    head.type = shape.type;
    head.keypos = shape.keypos;
    return started(std::move(file), head, init_segments);
}

Table Table::copy_of(File file, const Head &source) {
    Head head;
    head.m = source.m;
    head.m2 = source.m2;
    head.next = source.next;
    head.n = source.n;
    head.no_objects = source.no_objects;
    head.no_keys = source.no_keys;
    head.type = source.type;
    head.keypos = source.keypos;
    head.min_slots = source.min_slots;
    head.max_slots = source.max_slots;
    head.hash = source.hash;
    return started(std::move(file), head, false);
}

Table Table::started(File file, const Head &head, bool init_segments) {
    Table table(std::move(file), head, Allocator(BASE), false);
    auto header = initial_file_header(table.head_);
    header.resize(BASE);
    Writes writes{{0, std::move(header)}};
    for (std::uint32_t part = 0; part < no_parts(table.head_.next); ++part) {
        auto made = table.allocate_part(HEAD_END + 4 * part);
        std::ranges::move(made, std::back_inserter(writes));
    }
    for (std::uint32_t segment = 0; segment < no_segments(table.head_.next); ++segment) {
        auto made = table.allocate_segment(segment);
        // Without init_segments the segments' zeros are left to the caller (init_table writes them).
        std::erase_if(made, [&](const auto &write) { return !init_segments && write.second.size() == SEGMENT_BYTES; });
        std::ranges::move(made, std::back_inserter(writes));
    }
    table.apply(writes);
    auto probe = table.allocator_;
    const auto where = probe.alloc(BUMP, table.name()).address;
    table.allocator_ = Allocator(where);
    table.head_.base = where;
    return table;
}

std::optional<FreeLists> read_free_lists(const File &file, const Head &head) {
    const auto at = std::min<std::uint64_t>(head.freelists_p, file.size());
    return FreeLists::from_file(file.pread(at, static_cast<std::size_t>(file.size() - at)));
}

Table Table::opened(File file, Head head, FreeLists lists, bool read_only) {
    // OTP reads the free lists with the base of an R14 file.
    head.base = BASE;
    Table table(std::move(file), std::move(head), Allocator(std::move(lists), BASE), read_only);
    table.cache_segments();
    return table;
}

void Table::cache_segments() {
    const auto count = no_parts(head_.next);
    const auto pointers = file_.pread(HEAD_END, 4 * static_cast<std::size_t>(count));
    if (pointers.size() < 4 * static_cast<std::size_t>(count)) {
        fail(Failure::Shape::named, "tooshort", name());
    }
    for (std::uint32_t part = 0; part < count; ++part) {
        const auto address = read_u32(pointers, 4 * static_cast<std::size_t>(part));
        parts_[HEAD_END + 4 * part] = address;
        const auto segments = file_.pread(address, PART_BYTES);
        for (std::size_t at = 0; at + 4 <= segments.size() && read_u32(segments, at) != 0; at += 4) {
            segments_[static_cast<std::uint32_t>(address + at)] = read_u32(segments, at);
        }
    }
}

Writes Table::allocate_part(std::uint32_t part_position) {
    const auto part = allocator_.alloc(PART_BYTES - 1, name()).address;
    parts_[part_position] = part;
    return {{part, zeros(PART_BYTES)}, {part_position, word(part)}};
}

Writes Table::allocate_segment(std::uint32_t segment) {
    const auto part_position = HEAD_END + 4 * (segment >> PART_SEGMENTS_LOG2);
    Writes part_writes;
    if (!parts_.contains(part_position)) {
        part_writes = allocate_part(part_position);
    }
    const auto address = allocator_.alloc(SEGMENT_BYTES - 1, name()).address;
    const auto pointer = parts_.at(part_position) + 4 * (segment & (PART_SEGMENTS - 1));
    segments_[pointer] = address;
    Writes writes{{address, zeros(SEGMENT_BYTES)}};
    if (!part_writes.empty()) {
        writes.push_back(std::move(part_writes[0]));
    }
    writes.emplace_back(pointer, word(address));
    if (!part_writes.empty()) {
        writes.push_back(std::move(part_writes[1]));
    }
    return writes;
}

std::uint32_t Table::slot_of(const Terms &terms, const Term &key) const {
    const auto hash = terms.hash(key, head_.hash);
    if (head_.hash == HashMethod::phash2) {
        const auto low = hash & (head_.m - 1);
        return low < head_.n ? hash & (head_.m2 - 1) : low;
    }
    const auto low = hash % head_.m;
    return low < head_.n ? hash % head_.m2 : low;
}

std::uint32_t Table::slot_position(std::uint32_t slot) const {
    const auto segment = slot >> SEGMENT_SLOTS_LOG2;
    const auto part = parts_.find(HEAD_END + 4 * (segment >> PART_SEGMENTS_LOG2));
    const auto found =
        part == parts_.end() ? segments_.end() : segments_.find(part->second + 4 * (segment & (PART_SEGMENTS - 1)));
    if (found == segments_.end()) {
        bad_object("slot_position", name());
    }
    return found->second + SLOT_BYTES * (slot & (SEGMENT_SLOTS - 1));
}

std::uint64_t Table::max_collection() const {
    return head_.max_object_size ? power(*head_.max_object_size) : power(32);
}

Table::Collection Table::read_slot(std::uint32_t slot_position, const char *where) const {
    const auto pointer = file_.pread(slot_position, SLOT_BYTES);
    if (pointer.size() < SLOT_BYTES) {
        bad_object(where, name());
    }
    const auto size = read_u32(pointer, 0);
    const auto address = read_u32(pointer, 4);
    if (size == 0 && address == 0) {
        return {};
    }
    if (size > max_collection()) {
        bad_object(where, name());
    }
    auto bytes = file_.pread(address, size);
    if (bytes.size() < OBJECT_HEADER || read_u32(bytes, 0) != size) {
        bad_object(where, name());
    }
    return {.pointer = address, .bytes = std::move(bytes)};
}

std::vector<StoredKey> Table::per_key(const Terms &terms, std::span<const std::byte> collection) const {
    if (collection.size() < OBJECT_HEADER || read_u32(collection, 0) != collection.size() ||
        read_u32(collection, STATUS_POS) != ACTIVE) {
        throw BadObject{};
    }
    const auto keys = collection.subspan(OBJECT_HEADER);
    std::vector<StoredKey> stored;
    for (std::size_t at = 0; at < keys.size(); at += stored.back().bytes.size()) {
        stored.push_back(head_.type == TableType::set ? set_key(terms, keys.subspan(at), head_.keypos)
                                                      : bag_key(terms, keys.subspan(at), head_.keypos));
    }
    return stored;
}

std::vector<Item> Table::other_objects(const Terms &terms, std::span<const std::byte> bytes) {
    std::vector<Item> items;
    auto seq = SEQ_START + 1;
    for (std::size_t at = 0; at < bytes.size(); ++seq) {
        const auto size = read_u32(bytes, at);
        if (size < 4 || at + size > bytes.size()) {
            throw BadObject{};
        }
        const auto object = bytes.subspan(at, size);
        items.push_back(
            {.term = terms.decode(object.subspan(4)), .seq = seq, .source = Item::Source::stored, .bytes = object});
        at += size;
    }
    return items;
}

std::optional<std::vector<Term>> Table::slot_objects(const Terms &terms, std::uint32_t slot) const {
    if (slot >= head_.next) {
        return std::nullopt;
    }
    const auto collection = read_slot(slot_position(slot), "slot_objects");
    std::vector<Term> objects;
    if (collection.bytes.empty()) {
        return objects;
    }
    try {
        const auto keys = std::span(collection.bytes).subspan(OBJECT_HEADER);
        head_.type == TableType::set ? set_objects(terms, keys, objects) : bag_objects(terms, keys, objects);
    } catch (const BadObject &) {
        bad_object("slot_objects", name());
    }
    return objects;
}

void Table::apply(const Writes &writes) {
    for (const auto &[at, bytes] : writes) {
        file_.pwrite(at, bytes);
    }
}

Bytes Table::pread(std::uint64_t at, std::size_t size) const { return file_.pread(at, size); }

std::vector<Bytes> Table::pread_all(const std::vector<std::pair<std::uint64_t, std::uint32_t>> &positions) const {
    std::vector<Bytes> read;
    for (const auto &[at, size] : positions) {
        auto bytes = file_.pread(at, size);
        if (bytes.empty()) {
            fail(Failure::Shape::named, "premature_eof", name());
        }
        read.push_back(std::move(bytes));
    }
    return read;
}

bool Table::may_grow(const Terms &terms, std::uint64_t new_keys, bool many_times) {
    if ((new_keys == 0 && !many_times) || fixed() || read_only_ || head_.next >= head_.max_slots) {
        return false;
    }
    const auto extra =
        std::min<std::int64_t>(std::int64_t{2} * SEGMENT_SLOTS, static_cast<std::int64_t>(head_.no_keys + new_keys) -
                                                                    static_cast<std::int64_t>(head_.next));
    if (many_times && extra > SEGMENT_SLOTS) {
        grow(terms, 1);
        return true;
    }
    if (extra > 0) {
        grow(terms, static_cast<std::uint64_t>(extra));
    }
    return false;
}

void Table::grow(const Terms &terms, std::uint64_t extra) {
    for (auto left = static_cast<std::int64_t>(extra); left > 0 && head_.next < head_.max_slots;
         left -= SEGMENT_SLOTS) {
        apply(allocate_segment(head_.next / SEGMENT_SLOTS));
        re_hash(terms, head_.n);
        if (head_.n + SEGMENT_SLOTS == head_.m) {
            head_.n = 0;
            head_.next += SEGMENT_SLOTS;
            head_.m2 = 4 * head_.m;
            head_.m = 2 * head_.m;
        } else {
            head_.n += SEGMENT_SLOTS;
            head_.next += SEGMENT_SLOTS;
        }
    }
}

void Table::collection_added(int log2_size) {
    if (!head_.collections) {
        return;
    }
    ++(*head_.collections)[log2_size];
    if (head_.max_object_size.value_or(0) < log2_size) {
        head_.max_object_size = log2_size;
    }
}

void Table::collection_removed(int log2_size) {
    if (!head_.collections) {
        return;
    }
    --(*head_.collections)[log2_size];
    if (head_.max_object_size.value_or(0) == log2_size) {
        head_.max_object_size = max_object_size(*head_.collections);
    }
}

Table::Updated Table::updated(const Place &place, const Bytes &bytes, bool changed, bool same_counts) {
    const auto size = static_cast<std::uint32_t>(bytes.size()) + OBJECT_HEADER;
    if (bytes.empty() && place.collection == 0) {
        return {};
    }
    if (place.collection == 0) {
        const auto block = allocator_.alloc(size - 1, name());
        collection_added(block.pos - 1);
        return {.pointers = {{place.slot, pair_of(size, block.address)}},
                .collections = {{block.address, collection_bytes(size, bytes)}}};
    }
    if (bytes.empty()) {
        const FreeBlock freed{.address = place.collection, .pos = size_class(place.size)};
        allocator_.free(freed);
        collection_removed(freed.pos - 1);
        return {.pointers = {{place.slot, pair_of(0, 0)}},
                .collections = {{place.collection + STATUS_POS, word(FREE)}}};
    }
    if (!changed) {
        return {};
    }
    return rewritten(place, bytes, same_counts);
}

Table::Updated Table::rewritten(const Place &place, const Bytes &bytes, bool same_counts) {
    const auto size = static_cast<std::uint32_t>(bytes.size()) + OBJECT_HEADER;
    const bool same = place.size == size;
    // A fixed table overwrites a collection only when its objects and keys stay as they were (bchunk counts them).
    const bool overwrite = (same || size_class(place.size) == size_class(size)) && (!fixed() || same_counts);
    if (same && !fixed()) {
        return {.pointers = {}, .collections = {{place.collection + OBJECT_HEADER, bytes}}};
    }
    if (overwrite) {
        return {.pointers = {{place.slot, pair_of(size, place.collection)}},
                .collections = {{place.collection, collection_bytes(size, bytes)}}};
    }
    const FreeBlock freed{.address = place.collection, .pos = size_class(place.size)};
    allocator_.free(freed);
    const auto block = allocator_.alloc(size - 1, name());
    collection_added(block.pos - 1);
    collection_removed(freed.pos - 1);
    Updated result{.pointers = {{place.slot, pair_of(size, block.address)}},
                   .collections = {{block.address, collection_bytes(size, bytes)}}};
    if (block.address != place.collection) {
        result.collections.emplace_back(place.collection + STATUS_POS, word(FREE));
    }
    return result;
}

void Table::mark_dirty() {
    file_.pwrite(CLOSED_PROPERLY_POS, word(NOT_PROPERLY_CLOSED));
    file_.sync();
    file_.truncate(head_.freelists_p);
}

void Table::save(bool sync) {
    const auto at = static_cast<std::uint32_t>(file_.size());
    head_.freelists_p = at;
    auto bytes = allocator_.lists().to_file();
    const auto size = static_cast<std::uint32_t>(at + bytes.size() + 4);
    append_u32(bytes, head_.base == BASE ? size : size - head_.base);
    file_.pwrite(at, bytes);
    file_.pwrite(0, file_header(head_, at, CLOSED_PROPERLY));
    if (file_.in_memory()) {
        file_.write_out();
    } else if (sync) {
        file_.sync();
    }
}
} // namespace clause::runtime::builtins::dets
