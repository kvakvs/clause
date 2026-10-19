#include "bulk.hpp"
#include "../collections/collections.hpp"
#include "work_list.hpp"
#include <algorithm>
#include <map>

namespace clause::runtime::builtins::dets {
namespace {
// An object of one slot while its collection is built: its key, sequence number, term and bytes.
struct SlotObject {
    Term key;
    std::uint32_t seq;
    Term term;
    const Bytes *bytes;
};

bool bulk_less(const BulkObject &left, const BulkObject &right) {
    if (left.slot != right.slot) {
        return left.slot < right.slot;
    }
    if (left.seq != right.seq) {
        return left.seq < right.seq;
    }
    return left.bytes < right.bytes;
}

// The keys of a set slot: the objects in term order of {Key, Seq}, a key's last object kept (prep_set_slot/7).
Bytes set_keys(const std::vector<SlotObject> &objects, Counts &counts) {
    Bytes bytes;
    const auto emit = [&](const Bytes &object) {
        append_u32(bytes, static_cast<std::uint32_t>(object.size() + 4));
        append(bytes, object);
        ++counts.objects;
        ++counts.keys;
    };
    for (std::size_t at = 0; at < objects.size(); ++at) {
        if (at + 1 == objects.size() || !Terms::same(objects[at + 1].key, objects[at].key)) {
            emit(*objects[at].bytes);
        }
    }
    return bytes;
}

// The collection block of a slot's keys: size word, ACTIVE, the keys and zeros up to the buddy size.
Bytes block_of(const Bytes &keys) {
    const auto size = static_cast<std::uint32_t>(keys.size()) + OBJECT_HEADER;
    Bytes block;
    append_u32(block, size);
    append_u32(block, ACTIVE);
    append(block, keys);
    block.resize(power(size_class(size) - 1));
    return block;
}

// The objects of one slot, decoded and sorted as lists:sort/1 sorts {Slot, Key, Seq, Term, Bytes}.
std::vector<SlotObject> slot_objects(const Terms &terms, std::uint32_t keypos, std::span<const BulkObject> objects) {
    std::vector<SlotObject> decoded;
    for (const auto &object : objects) {
        const auto term = terms.decode(object.bytes);
        decoded.push_back(
            {.key = Terms::element(term, keypos), .seq = object.seq, .term = term, .bytes = &object.bytes});
    }
    std::ranges::stable_sort(decoded, [](const SlotObject &left, const SlotObject &right) {
        const auto order = collections::compare(left.key, right.key);
        return order != 0 ? order < 0 : left.seq < right.seq;
    });
    return decoded;
}

Bytes bag_keys(const Terms &terms, TableType type, const std::vector<SlotObject> &objects, Counts &counts) {
    std::vector<Command> inserts;
    inserts.reserve(objects.size());
    for (const auto &object : objects) {
        inserts.push_back({.key = object.key, .seq = object.seq, .kind = Command::Kind::insert, .object = object.term});
    }
    std::ranges::reverse(inserts);
    return inserted_keys(terms, type, std::move(inserts), counts);
}

// The address the allocator would give a block of `size` bytes, without taking it.
std::uint32_t peek(const Table &table, std::uint64_t size) {
    auto probe = const_cast<Table &>(table).allocator();
    return probe.alloc(size, table.name()).address;
}

// Bytes of blocks from which a copy fills the file first.
constexpr std::uint64_t FILL_THRESHOLD = std::uint64_t{60} * CHUNK_SIZE;

// OTP's write_bytes/3: bytes 0..255 over and over from the end of the file to the end of the blocks.
void fill(Table &table, const std::vector<BulkCollection> &collections) {
    std::uint64_t total = 0;
    for (const auto &collection : collections) {
        total += collection.block.size();
    }
    if (total < FILL_THRESHOLD) {
        return;
    }
    const auto start = table.file().size();
    const auto end = peek(table, BUMP);
    Bytes filler(end > start ? end - start : 0);
    for (std::size_t at = 0; at < filler.size(); ++at) {
        filler[at] = static_cast<std::byte>(at & 0xFFU);
    }
    table.file().pwrite(start, filler);
}

// The bytes of a set or bag collection's keys parsed for a repair: {Key, ObjectBytes} per object.
void key_objects(const Terms &terms, std::span<const std::byte> bytes, std::uint32_t keypos,
                 std::vector<std::pair<Term, Bytes>> &out) {
    for (std::size_t at = 0; at < bytes.size();) {
        const auto size = read_u32(bytes, at);
        if (size < 4 || at + size > bytes.size()) {
            throw BadObject{};
        }
        const auto object = bytes.subspan(at + 4, size - 4);
        out.emplace_back(Terms::element(terms.decode(object), keypos), Bytes(object.begin(), object.end()));
        at += size;
    }
}

std::vector<std::pair<Term, Bytes>> collection_keys(const Terms &terms, std::span<const std::byte> keys,
                                                    const Head &head) {
    std::vector<std::pair<Term, Bytes>> objects;
    if (head.type == TableType::set) {
        key_objects(terms, keys, head.keypos, objects);
        return objects;
    }
    for (std::size_t at = 0; at < keys.size();) {
        const auto size = read_u32(keys, at);
        if (size < 4 || at + size > keys.size()) {
            throw BadObject{};
        }
        key_objects(terms, keys.subspan(at + 4, size - 4), head.keypos, objects);
        at += size;
    }
    return objects;
}

// The scan of a repair over a file's bytes from the base: dets_v9:fsck_input/4 and fsck_objs/5.
class RepairScan final {
  public:
    RepairScan(const Table &target, const Terms &terms, Bytes bytes, std::uint64_t biggest)
        : target_(target), terms_(terms), bytes_(std::move(bytes)), biggest_(biggest) {}

    std::vector<BulkObject> run() {
        for (std::uint64_t at = BASE; at + OBJECT_HEADER <= bytes_.size();) {
            const auto step = step_at(at);
            if (!step) {
                break;
            }
            at += *step;
        }
        return std::move(objects_);
    }

  private:
    // The bytes to the next position at `at`; none at a collection the file ends in.
    std::optional<std::uint64_t> step_at(std::uint64_t at) {
        const auto length = read_u32(bytes_, at);
        if (read_u32(bytes_, at + STATUS_POS) != ACTIVE || length < OBJECT_HEADER) {
            return BUMP;
        }
        if (at + length > bytes_.size()) {
            return length <= biggest_ ? std::nullopt : std::optional<std::uint64_t>(BUMP);
        }
        try {
            const auto keys = std::span(bytes_).subspan(at + OBJECT_HEADER, length - OBJECT_HEADER);
            for (auto &[key, object] : collection_keys(terms_, keys, target_.head())) {
                objects_.push_back({.slot = target_.slot_of(terms_, key), .seq = seq_++, .bytes = std::move(object)});
            }
            return power(log2(length));
        } catch (const BadObject &) {
            return BUMP;
        }
    }

    const Table &target_;
    const Terms &terms_;
    Bytes bytes_;
    // Collections longer than this that the file ends in are skipped instead of ending the scan.
    std::uint64_t biggest_;
    std::vector<BulkObject> objects_;
    std::uint32_t seq_ = 0;
};
} // namespace

BulkCollections bulk_collections(const Table &table, const Terms &terms, std::vector<BulkObject> objects) {
    std::ranges::sort(objects, bulk_less);
    BulkCollections result;
    const auto &head = table.head();
    for (std::size_t at = 0; at < objects.size();) {
        auto end = at;
        while (end < objects.size() && objects[end].slot == objects[at].slot) {
            ++end;
        }
        const auto sorted = slot_objects(terms, head.keypos, std::span(objects).subspan(at, end - at));
        const auto keys = head.type == TableType::set ? set_keys(sorted, result.counts)
                                                      : bag_keys(terms, head.type, sorted, result.counts);
        result.collections.push_back({.slot = objects[at].slot, .block = block_of(keys)});
        at = end;
    }
    return result;
}

void write_bulk(Table &table, const std::vector<BulkCollection> &collections, const Counts &counts, bool prefill) {
    auto &head = table.head();
    const auto segments = static_cast<std::uint32_t>(BASE + no_parts(head.next) * PART_BYTES);
    const auto segments_end = peek(table, SEGMENT_BYTES - 1);
    head.no_objects = static_cast<std::uint32_t>(counts.objects);
    head.no_keys = static_cast<std::uint32_t>(counts.keys);
    std::map<int, std::vector<const BulkCollection *>, std::greater<>> sizes;
    for (const auto &collection : collections) {
        sizes[log2(collection.block.size()) + 1].push_back(&collection);
    }
    Bytes pointers(segments_end - segments);
    Writes blocks;
    for (const auto &[log_size, members] : sizes) {
        const auto size = power(log_size - 1);
        const auto base = peek(table, size - 1);
        table.allocator().alloc_many(size * members.size(), table.name());
        if (head.collections) {
            (*head.collections)[log_size - 1] += static_cast<std::int64_t>(members.size());
        }
        Bytes run;
        for (std::size_t index = 0; index < members.size(); ++index) {
            const auto &block = members[index]->block;
            const auto at = SLOT_BYTES * static_cast<std::size_t>(members[index]->slot);
            put_u32(pointers, at, read_u32(block, 0));
            put_u32(pointers, at + 4, static_cast<std::uint32_t>(base + index * size));
            append(run, block);
        }
        blocks.emplace_back(base, std::move(run));
    }
    head.max_object_size = head.collections ? max_object_size(*head.collections) : 0;
    if (prefill) {
        fill(table, collections);
    }
    Writes writes{{segments, std::move(pointers)}};
    std::ranges::move(blocks, std::back_inserter(writes));
    table.apply(writes);
}

std::vector<BulkObject> repair_input(const File &source, const FileHeader &header, const Table &target,
                                     const Terms &terms) {
    const auto size = source.size();
    auto biggest = header.has_md5 && header.collections ? power(max_object_size(*header.collections)) : size;
    return RepairScan(target, terms, source.pread(0, static_cast<std::size_t>(size)),
                      std::max<std::uint64_t>(biggest, CHUNK_SIZE))
        .run();
}
} // namespace clause::runtime::builtins::dets

namespace clause::runtime::builtins::dets {
namespace {
// The slot of a block's first key.
std::uint32_t block_slot(const Table &table, const Terms &terms, std::span<const std::byte> block) {
    const auto first = table.head().type == TableType::set ? 12U : 16U;
    try {
        const auto term = terms.decode(block.subspan(std::min<std::size_t>(first, block.size())));
        return table.slot_of(terms, Terms::element(term, table.head().keypos));
    } catch (const BadObject &) {
        bad_object("compact_objs", table.name());
    }
}
} // namespace

std::vector<BulkCollection> compact_input(const Table &source, const Terms &terms) {
    std::vector<BulkCollection> blocks;
    for (const auto &[from, to] : source.allocator().all_allocated()) {
        const auto bytes = source.pread(from, to - from);
        for (std::size_t at = 0; at + 12 <= to - from;) {
            if (at + 12 > bytes.size()) {
                fail(Failure::Shape::named, "premature_eof", source.name());
            }
            if (read_u32(bytes, at + STATUS_POS) != ACTIVE) {
                at += SEGMENT_BYTES;
                continue;
            }
            const auto size = static_cast<std::size_t>(power(size_class(read_u32(bytes, at)) - 1));
            Bytes block(size);
            std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(at), std::min(size, bytes.size() - at),
                        block.begin());
            blocks.push_back({.slot = block_slot(source, terms, block), .block = std::move(block)});
            at += size;
        }
    }
    return blocks;
}
} // namespace clause::runtime::builtins::dets
