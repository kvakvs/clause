#pragma once
#include "../../ets/tables.hpp"
#include "../typed.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

// Helpers of the ets builtins (plan 21 step LIB10, docs/builtins.md#ets): table arguments and access rights, keys
// and objects, and copies of stored objects into the caller's heap. Every failure raises badarg, as OTP's do.
namespace clause::runtime::builtins::ets {
using detail::DetachedTerm;
using detail::ets::Key;
using detail::ets::Objects;
using detail::ets::Protection;
using detail::ets::Reached;
using detail::ets::Table;
using detail::ets::TableType;

// The table argument of an ets builtin, a role of its own so it is never swapped with a key or an object.
struct TableRole;
using TableArgument = Named<TableRole>;

// What an operation needs of a table: nothing (info), reading, writing, or being its owner.
enum class Access : std::uint8_t { info, read, write, owner };

// The table a table argument names, a table identifier or the name of a named table, when the caller may use it as
// `access` asks; badarg otherwise.
std::shared_ptr<Table> table(ProcessContext &context, const TableArgument &argument, Access access);
// The table, or null when a well-formed argument names no table (info/1,2); badarg for any other term.
std::shared_ptr<Table> table_if_any(ProcessContext &context, const TableArgument &argument);
// Raise badarg when an operation found its table deleted.
void need_table(bool alive);

// The key `term` of a lookup in `table`, hashed for a hash table.
Key key(ProcessContext &context, const Table &table, const Term &term);

// An object for `table`: a tuple holding a key position, copied out of the caller's heap with its key; badarg for
// any other term.
struct Stored {
    Key key;
    DetachedTerm object;
};

Stored stored(ProcessContext &context, const Table &table, const Term &object);
// Every object of a proper list of objects, or the one object of a tuple; badarg otherwise.
std::vector<Stored> stored_objects(ProcessContext &context, const Table &table, const Term &objects);

// A stored term copied into the caller's heap.
Term copy_out(ProcessContext &context, Word stored);
// Stored objects copied into the caller's heap as a list.
Term objects_list(ProcessContext &context, const Objects &objects);
// The key of a stored object, copied into the caller's heap.
Term key_term(ProcessContext &context, const Table &table, const Objects &objects);

// The term a table goes by: its name when named, its identifier otherwise.
Term table_term(ProcessContext &context, const Table &table);
// The identifier of a table.
Term identifier(ProcessContext &context, const Table &table);

// The end-of-walk atom.
Term end_of_table(ProcessContext &context);
// The spelling of an atom; empty for any other term.
std::string_view spelling(const Term &term);
// The calling process's pid word.
Word self(ProcessContext &context);
} // namespace clause::runtime::builtins::ets
