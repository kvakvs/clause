#pragma once
#include "table.hpp"

// The write cache of a dets table flushed to its file (plan 21 step LIB53): dets_v9:write_cache/1 and
// eval_work_list/2. The table process keeps OTP's cache of {Key, {Seq, Command}} entries; a flush grows the table
// by the keys it may add, then rewrites each slot the commands touch, empty slots at once from the highest down and
// the others in batches of up to MAXCOLL bytes in file order, so collections land where OTP puts them.
namespace clause::runtime::builtins::dets {
struct Command {
    enum class Kind : std::uint8_t { insert, delete_object, delete_key, lookup };
    Term key;
    std::int64_t seq = 0;
    Kind kind = Kind::lookup;
    // The object of an insert or delete_object; the asking process of a lookup.
    Term object;
};

// The objects a lookup found, for the process term that asked.
struct Found {
    Term asker;
    std::vector<Term> objects;
};

// Flush `commands` (the cache, newest first) with at most `inserts` new keys; the lookups' results, last first as
// OTP returns them.
std::vector<Found> write_cache(Table &table, const Terms &terms, std::vector<Command> commands, std::uint64_t inserts);

// The keys of a bag collection built from inserts alone, in key order, as bulk writes build them
// (dets_v9:prep_slot/3); adds the objects kept and the keys to `counts`.
Bytes inserted_keys(const Terms &terms, TableType type, std::vector<Command> inserts, Counts &counts);
} // namespace clause::runtime::builtins::dets
