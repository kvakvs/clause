#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The constants and helpers of OTP's dets file format version 9 (plan 21 step LIB53, docs/builtins.md#dets), as
// dets.hrl and dets_v9.erl define them: a file header, 256 segment array part pointers, segment array parts of 512
// segment pointers, segments of 256 slots of <<Size:32, Pointer:32>>, and object collections placed by a buddy
// system whose free lists are written at the end of a properly closed file.
namespace clause::runtime::builtins::dets {
using Bytes = std::vector<std::byte>;

inline constexpr std::uint32_t MAGIC = 0x0abcdef;
inline constexpr std::uint32_t FREE = 0x3abcdef;
inline constexpr std::uint32_t ACTIVE = 0x12345678;
inline constexpr std::uint32_t FORMAT_VERSION = 9;
inline constexpr std::uint32_t NOT_PROPERLY_CLOSED = 0;
inline constexpr std::uint32_t CLOSED_PROPERLY = 1;
// The marker after the last free list of a file.
inline constexpr std::uint32_t END_FREE = 12345;

// The header without its reserved bytes, and its end.
inline constexpr std::uint32_t HEAD_BYTES = 56 + 112 + 16 + 4;
inline constexpr std::uint32_t RESERVED_BYTES = 124;
inline constexpr std::uint32_t HEAD_END = HEAD_BYTES + RESERVED_BYTES;
// The buddy system counters of the header: sizes 2^4 to 2^31.
inline constexpr std::uint32_t COUNTER_BYTES = 28 * 4;
inline constexpr std::uint32_t MD5_BYTES = 16;
inline constexpr std::uint32_t FREELIST_POS = 0;
inline constexpr std::uint32_t CLOSED_PROPERLY_POS = 8;
inline constexpr std::uint32_t VERSION_POS = 16;

inline constexpr std::uint32_t SEGMENT_SLOTS = 256;
inline constexpr std::uint32_t SEGMENT_SLOTS_LOG2 = 8;
inline constexpr std::uint32_t SEGMENT_BYTES = 2048;
inline constexpr std::uint32_t PART_SEGMENTS = 512;
inline constexpr std::uint32_t PART_SEGMENTS_LOG2 = 9;
inline constexpr std::uint32_t PART_BYTES = 2048;
inline constexpr std::uint32_t PARTS = 256;
// The first byte after the segment array part pointers.
inline constexpr std::uint32_t BASE = HEAD_END + 4 * PARTS;
inline constexpr std::uint32_t MAX_SLOTS = PARTS * PART_SEGMENTS * SEGMENT_SLOTS;
inline constexpr std::uint32_t SLOT_BYTES = 8;

// The <<Size:32, Status:32>> header of an object collection.
inline constexpr std::uint32_t OBJECT_HEADER = 8;
inline constexpr std::uint32_t STATUS_POS = 4;
// The step repair takes over bytes framing no collection.
inline constexpr std::uint32_t BUMP = 16;
inline constexpr std::uint32_t CHUNK_SIZE = 8192;
// Bytes of collections read at once while flushing or growing (MAXCOLL).
inline constexpr std::uint32_t MAX_COLLECTION = 10 * CHUNK_SIZE;
inline constexpr int MAX_BUDDY = 32;
// Bytes at the top of the 2 GB file space reserved for the free lists.
inline constexpr std::uint64_t MAX_FREE_LISTS = 50000000;
// Bytes of one free list record on file, header included.
inline constexpr std::uint32_t MAX_FREE_OBJECT = 4096;

inline constexpr std::uint32_t DEFAULT_MIN_SLOTS = 256;
inline constexpr std::uint32_t DEFAULT_MAX_SLOTS = 32 * 1024 * 1024;
// The sequence number of a key's first stored object; the others follow it.
inline constexpr std::int64_t SEQ_START = -(std::int64_t{1} << 26);

enum class TableType : std::uint8_t { set = 1, bag = 2, duplicate_bag = 3 };
enum class HashMethod : std::uint8_t { phash = 0, phash2 = 1 };

// OTP's dets_utils:log2/1: the least K with 2^K >= N, except that log2(1) is 1 (and log2(0) is 0).
int log2(std::uint64_t value);

// The buddy size class (1 + log2) of a collection of `bytes` bytes, dets_v9:sz2pos/1.
inline int size_class(std::uint64_t bytes) { return 1 + log2(bytes); }

inline std::uint64_t power(int exponent) { return std::uint64_t{1} << static_cast<unsigned>(exponent); }

// Big-endian 32-bit words, as every number of the format is stored.
std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t at);
void append_u32(Bytes &out, std::uint32_t value);
void put_u32(std::span<std::byte> bytes, std::size_t at, std::uint32_t value);
void append(Bytes &out, std::span<const std::byte> bytes);

// The collections of a table per log2 size, kept with their zero counts as OTP's orddict keeps them; none for a
// version 9(a) file, which counts none.
using Collections = std::optional<std::map<int, std::int64_t>>;

// The file header and table state OTP keeps in its #head{} record.
struct Head {
    std::uint32_t m = 0;
    std::uint32_t m2 = 0;
    std::uint32_t next = 0;
    std::uint32_t n = 0;
    std::uint32_t no_objects = 0;
    std::uint32_t no_keys = 0;
    // The 2-log of the biggest collection; none when the file counts no collections.
    std::optional<int> max_object_size = 0;
    TableType type = TableType::set;
    std::uint32_t keypos = 1;
    std::uint32_t min_slots = DEFAULT_MIN_SLOTS;
    std::uint32_t max_slots = DEFAULT_MAX_SLOTS;
    HashMethod hash = HashMethod::phash2;
    bool has_md5 = true;
    Collections collections = std::map<int, std::int64_t>{};
    // The base of the buddy system and the position of the free lists on file.
    std::uint32_t base = BASE;
    std::uint32_t freelists_p = 0;
};

// The 2-log of the biggest counted collection, 0 when none; dets_v9:max_objsize/1.
int max_object_size(const std::map<int, std::int64_t> &collections);
} // namespace clause::runtime::builtins::dets
