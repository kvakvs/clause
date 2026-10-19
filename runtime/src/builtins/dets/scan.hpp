#pragma once
#include "table.hpp"
#include <deque>

// Scans of a dets file in file order (plan 21 step LIB53) for match, select, foldl, traverse and bchunk: OTP's
// chunks, read 8 KB at a time over the areas the buddy system has allocated, so each chunk holds what OTP's holds.
namespace clause::runtime::builtins::dets {
// The continuation of a scan as #dets_cont{} holds it: eof, or the bytes left over at `from` and the allocated
// area [from, to) with the areas after it.
struct ScanState {
    bool eof = false;
    Bytes leftover;
    bool areas_left = false;
    std::uint32_t from = 0;
    std::uint32_t to = 0;
    std::deque<Area> areas;
};

// One chunk: the bytes of each object scanned, in file order, and the continuation.
struct Chunk {
    std::vector<Bytes> objects;
    ScanState next;
};

// The start of a scan: dets:init_scan/2.
ScanState init_scan(const Table &table);
// The next chunk; `count` objects at least when given (select/3 and friends), else about 8 KB of collections.
// Throws Failure for an unreadable file.
Chunk scan(const Table &table, ScanState state, std::optional<std::uint64_t> count);

// The start of a bchunk/2 walk: every allocated area, or eof for an empty file.
ScanState init_bchunk(const Table &table);

// One bchunk/2 chunk: {LogSize, Slot, Block} per collection.
struct Block {
    int log_size;
    std::uint32_t slot;
    Bytes bytes;
};

struct Blocks {
    std::vector<Block> blocks;
    ScanState next;
};

Blocks read_bchunks(const Table &table, const Terms &terms, ScanState state);
} // namespace clause::runtime::builtins::dets
