#pragma once
#include "table.hpp"

// Bulk writes of a fresh dets file (plan 21 step LIB53): init_table/2,3 and repairs collect the objects, sort them
// by slot and write every collection at once as dets_v9:output_objs/3 and dets:fsck_copy/4 do. Per buddy size, the
// largest first, the blocks follow each other from the address allocation gives, and the segments are rewritten
// with their slots' pointers.
namespace clause::runtime::builtins::dets {

// The collections of objects sorted by slot and sequence, in slot order, with the counts of objects and keys:
// dets_v9:output_slots/6 and prep_slot/2 (a set keeps a key's last object; bags merge as inserts do).
struct BulkCollections {
    std::vector<BulkCollection> collections;
    Counts counts;
};

BulkCollections bulk_collections(const Table &table, const Terms &terms, std::vector<BulkObject> objects);

// Write the collections into the fresh file of `table`, each size's blocks in the order given. A copy of a
// table's blocks (`prefill`) first fills the file up to its end with OTP's filler bytes when the blocks are big
// (dets_v9:prepare_file_init/5's write_bytes/3).
void write_bulk(Table &table, const std::vector<BulkCollection> &collections, const Counts &counts, bool prefill);

// The blocks of a properly closed file in file order, each ACTIVE collection's whole buddy block with its slot:
// what a compaction copies (dets_v9:compact_input/5). Throws Failure when the file ends early.
std::vector<BulkCollection> compact_input(const Table &source, const Terms &terms);

// The objects of a file as a repair finds them: every ACTIVE collection from the base on, in BUMP steps where none
// starts, hashed for `target`; dets_v9:fsck_input/4.
std::vector<BulkObject> repair_input(const File &source, const FileHeader &header, const Table &target,
                                     const Terms &terms);
} // namespace clause::runtime::builtins::dets
