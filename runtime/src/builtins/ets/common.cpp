#include "../../memory/heap_object.hpp"
#include "../../scheduler/executor.hpp"
#include "../../terms/identities.hpp"
#include "../erlang/hashing.hpp"
#include "ets.hpp"
#include <terms.hpp>

// Helpers of the ets builtins (plan 21 step LIB10A): table arguments, access rights, keys and copies.
namespace clause::runtime::builtins::ets {
namespace {
// Spread phash2's 32 bits over 64, so the high bits choose a table part and the low ones a bucket in it (the
// splitmix64 finalizer).
std::uint64_t mixed(std::uint64_t hash) noexcept {
    hash ^= hash >> 30;
    hash *= 0xbf58476d1ce4e5b9ULL;
    hash ^= hash >> 27;
    hash *= 0x94d049bb133111ebULL;
    return hash ^ (hash >> 31);
}

// Whether the process of `pid` may use a table as `access` asks (OTP's access rights).
bool permitted(const Table &table, Word pid, Access access) noexcept {
    const auto owner = table.owner() == pid;
    switch (access) {
    case Access::read:
        return owner || table.protection() != Protection::private_access;
    case Access::write:
        return owner || table.protection() == Protection::public_access;
    case Access::owner:
        return owner;
    default:
        return true;
    }
}
} // namespace

std::shared_ptr<Table> table_if_any(ProcessContext &context, const TableArgument &argument) {
    if (argument.term.is_atom()) {
        return context.ets_tables().named(argument.term.word());
    }
    const auto number = detail::reference_number(argument.term);
    if (!number) {
        bad_argument();
    }
    return context.ets_tables().find(*number);
}

std::shared_ptr<Table> table(ProcessContext &context, const TableArgument &argument, Access access) {
    auto found = table_if_any(context, argument);
    if (!found || !permitted(*found, self(context), access)) {
        bad_argument();
    }
    return found;
}

void need_table(bool alive) {
    if (!alive) {
        bad_argument();
    }
}

Key key(ProcessContext &context, const Table &table, const Term &term) {
    return {.word = term.word(), .hash = table.hashed() ? mixed(term_hash(context, term)) : 0};
}

Stored stored(ProcessContext &context, const Table &table, const Term &object) {
    const auto keypos = table.options().keypos;
    if (!object.is_tuple() || object.tuple_size_unchecked() < keypos) {
        bad_argument();
    }
    return {.key = key(context, table, object.tuple_element_unchecked(keypos - 1)),
            .object = need(DetachedTerm::copy(context, object))};
}

std::vector<Stored> stored_objects(ProcessContext &context, const Table &table, const Term &objects) {
    std::vector<Stored> result;
    if (objects.is_tuple()) {
        result.push_back(stored(context, table, objects));
        return result;
    }
    auto rest = objects;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        result.push_back(stored(context, table, rest.head_unchecked()));
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return result;
}

Term copy_out(ProcessContext &context, Word stored) {
    return need(detail::TermAccess::make(stored).copy_to(context.heap()));
}

Term objects_list(ProcessContext &context, const Objects &objects) {
    std::vector<Term> copies;
    copies.reserve(objects.size());
    for (const auto &object : objects) {
        copies.push_back(copy_out(context, object.word()));
    }
    return need(TermFactory(context).list(copies));
}

Term key_term(ProcessContext &context, const Table &table, const Objects &objects) {
    return copy_out(context, detail::ets::key_of(objects.front().word(), table.options().keypos));
}

Term table_term(ProcessContext &context, const Table &table) {
    return table.options().named ? need(Term::from_word(table.name(), context)) : identifier(context, table);
}

Term identifier(ProcessContext &context, const Table &table) {
    return need(TermFactory(context).reference_number(table.number()));
}

Term end_of_table(ProcessContext &context) { return atom(context, "$end_of_table"_atom); }

std::string_view spelling(const Term &term) {
    return term.is_atom() ? term.atom_spelling_unchecked() : std::string_view{};
}

Word self(ProcessContext &context) { return detail::pid_of(context); }
} // namespace clause::runtime::builtins::ets
