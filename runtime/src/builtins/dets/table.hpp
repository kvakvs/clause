#pragma once
#include "free_lists.hpp"
#include "header.hpp"
#include "objects.hpp"
#include <map>

// An open dets file (plan 21 step LIB53): its head, file, buddy system and the cached positions of its segment
// array parts and segments, with dets_v9.erl's operations on them. One process (the table's) uses a table at a
// time, so nothing here locks.
namespace clause::runtime::builtins::dets {
// Writes in the order dets_utils:pwrite/2 applies them: a later write wins where two overlap.
using Writes = std::vector<std::pair<std::uint64_t, Bytes>>;

// An object waiting for a bulk write: its slot, its sequence number and its external format.
struct BulkObject {
    std::uint32_t slot;
    std::uint32_t seq;
    Bytes bytes;
};

// The numbers of objects and keys a bulk write counted.
struct Counts {
    std::uint64_t objects = 0;
    std::uint64_t keys = 0;
};

// One collection of a bulk write: its slot and its whole buddy block (size word, status, keys and padding).
struct BulkCollection {
    std::uint32_t slot;
    Bytes block;
};

// One object of a key while a collection is rewritten: the term, its sequence number (stored objects count from
// SEQ_START), and either its stored bytes or the command that brings it.
struct Item {
    enum class Source : std::uint8_t { stored, insert, delete_object };
    Term term;
    std::int64_t seq = 0;
    Source source = Source::stored;
    // The stored bytes with their size word.
    std::span<const std::byte> bytes;
};

// A stored key of a collection as dets_v9:per_key/2 reads it: the key, the bytes of all its objects (with their
// size words), its first object and the bytes of the others.
struct StoredKey {
    Term key;
    std::span<const std::byte> bytes;
    Item first;
    std::span<const std::byte> rest;
};

// What a new table is: its type, key position and the bounds of its slots.
struct TableShape {
    TableType type;
    std::uint32_t keypos;
    std::uint32_t min_slots;
    std::uint32_t max_slots;
};

// Where a collection is rewritten: its block (0 for none) and the block's size, and its slot's pointer.
struct Place {
    std::uint32_t collection;
    std::uint32_t size;
    std::uint32_t slot;
};

class Table final {
  public:
    // A new file: dets_v9:initiate_file/11, the slots rounded up to powers of two.
    static Table initiate(File file, const TableShape &shape, bool init_segments);
    // A new file with the slots, hash method and counts of `source`, its segments left to a bulk write; the copy a
    // compaction writes (dets_v9:prep_table_copy/9).
    static Table copy_of(File file, const Head &source);
    // A file whose header checked well, with the free lists read from it; the segments are cached.
    static Table opened(File file, Head head, FreeLists lists, bool read_only);

    Head &head() { return head_; }

    const Head &head() const { return head_; }

    File &file() { return file_; }

    const File &file() const { return file_; }

    const std::string &name() const { return file_.name(); }

    Allocator &allocator() { return allocator_; }

    const Allocator &allocator() const { return allocator_; }

    // The objects init_table/2,3 has given so far, written at once when it ends.
    std::vector<BulkObject> &pending() { return pending_; }

    // The collections of bchunk/2 data init_table/3 has given so far.
    std::vector<BulkCollection> &blocks() { return blocks_; }

    // The head before init_table started the file again; a failed init_table leaves it in place, as OTP does.
    std::optional<Head> &previous() { return previous_; }

    bool read_only() const { return read_only_; }

    bool fixed() const { return allocator_.fixed(); }

    // The slot of a key: linear hashing over m and n.
    std::uint32_t slot_of(const Terms &terms, const Term &key) const;
    // The file position of a slot's <<Size:32, Pointer:32>>.
    std::uint32_t slot_position(std::uint32_t slot) const;
    // The objects of a slot in stored order; none for a slot beyond the table.
    std::optional<std::vector<Term>> slot_objects(const Terms &terms, std::uint32_t slot) const;

    // The collection a slot points to (its size word, status and objects), with its position; empty for an empty
    // slot. Throws the bad object `where` for an unreadable one.
    struct Collection {
        std::uint32_t pointer = 0;
        Bytes bytes;
    };

    Collection read_slot(std::uint32_t slot_position, const char *where) const;
    // The keys of a collection's bytes (size word and status first); throws BadObject.
    std::vector<StoredKey> per_key(const Terms &terms, std::span<const std::byte> collection) const;
    // The objects of a key's other objects' bytes, numbered from SEQ_START + 1.
    static std::vector<Item> other_objects(const Terms &terms, std::span<const std::byte> bytes);
    // The largest collection a slot may point to (maxobjsize).
    std::uint64_t max_collection() const;

    void apply(const Writes &writes);
    Bytes pread(std::uint64_t at, std::size_t size) const;
    // Reads that must all return their bytes; premature_eof otherwise.
    std::vector<Bytes> pread_all(const std::vector<std::pair<std::uint64_t, std::uint32_t>> &positions) const;

    // Grow by up to `new_keys` slots past the number of keys (at most 512 at a time); whether to grow again later
    // (dets_v9:may_grow/3 with many_times).
    bool may_grow(const Terms &terms, std::uint64_t new_keys, bool many_times);

    // A collection added or removed for the buddy counters (one_bucket_added/removed).
    void collection_added(int log2_size);
    void collection_removed(int log2_size);

    // The changes of a collection rewritten at `place` to `bytes` (its keys, without the header):
    // dets_v9:updated/9, the slot pointer writes and the collection writes. `same_counts` says the numbers of
    // objects and keys stayed as they were.
    struct Updated {
        Writes pointers;
        Writes collections;
    };

    Updated updated(const Place &place, const Bytes &bytes, bool changed, bool same_counts);

    // Mark the file not properly closed and cut its free lists; dets_v9:mark_dirty/1.
    void mark_dirty();
    // Write the free lists and a closed header; dets_v9:do_perform_save/1 and ensure_written/2.
    void save(bool sync);

    // The parameters bchunk/2 and init_table/3 exchange; none for a version 9(a) file.
    bool has_collections() const { return head_.collections.has_value(); }

    void fix() { allocator_.fix(); }

    void unfix() { allocator_.unfix(); }

  private:
    Table(File file, Head head, Allocator allocator, bool read_only);
    // Write the header, the segment array parts and the segments of a new file: dets_v9:init_file/17.
    static Table started(File file, const Head &head, bool init_segments);

    void cache_segments();
    // Allocate the segment `segment` (and its array part when new); the writes of zeros and pointers.
    Writes allocate_segment(std::uint32_t segment);
    Writes allocate_part(std::uint32_t part_position);
    void grow(const Terms &terms, std::uint64_t extra);
    // A changed collection written over its block, or moved to a new block when its size class changed.
    Updated rewritten(const Place &place, const Bytes &bytes, bool same_counts);
    void re_hash(const Terms &terms, std::uint32_t slot_start);

    File file_;
    Head head_;
    Allocator allocator_;
    // Segment array part pointer position to part address, and segment pointer position to segment address.
    std::map<std::uint32_t, std::uint32_t> parts_;
    std::map<std::uint32_t, std::uint32_t> segments_;
    bool read_only_;
    std::vector<BulkObject> pending_;
    std::vector<BulkCollection> blocks_;
    std::optional<Head> previous_;
};

// The free lists a properly closed file holds at its free lists pointer; none when they do not read well.
std::optional<FreeLists> read_free_lists(const File &file, const Head &head);

[[noreturn]] void bad_object(const std::string &where, const std::string &file);
std::uint32_t no_parts(std::uint32_t slots);
std::int64_t no_segments(std::int64_t slots);
} // namespace clause::runtime::builtins::dets
