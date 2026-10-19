#pragma once
#include "../../match_spec/program.hpp"
#include "ets.hpp"
#include <memory>
#include <optional>
#include <vector>

// Helpers of the ets builtins that run match specifications (plan 21 step LIB11, docs/builtins.md#ets): compiled
// specifications, the keys a specification binds, and the walks that run a program on stored objects.
namespace clause::runtime::builtins::ets {
using detail::match_spec::Program;

// The match specification, pattern and limit arguments of the select builtins, roles of their own beside each other.
using SpecArgument = Named<struct SpecRole>;
using LimitArgument = Named<struct LimitRole>;

// The program of a match specification argument; badarg when it is none (system_limit for nesting past the limit).
std::shared_ptr<const Program> compiled(ProcessContext &context, const Term &spec);
// The specification [{Pattern, [], [Result]}] of match/2 ('$$') and match_object/2 ('$_').
Term pattern_spec(ProcessContext &context, const Term &pattern, std::string_view result);

// The keys every clause's head binds at the table's key position (a head that matches no object of the table binds
// none); none when a clause leaves the key open, so every object must be tried. An ordered_set's keys come sorted
// and once each.
std::optional<std::vector<Term>> bound_keys(const Table &table, const Program &program);

// Run `visit(object)` on each stored object of a key's objects, if any.
template <typename Visit> void each_object(const Objects *objects, Visit &visit) {
    if (objects == nullptr) {
        return;
    }
    for (const auto &object : *objects) {
        visit(object.word());
    }
}

// Run `visit(object)` on every stored object a program may match: the objects of its bound keys, else every object
// in walk order; the table is read under its locks meanwhile.
template <typename Visit>
void each_candidate(ProcessContext &context, const Table &table, const Program &program, Visit visit) {
    if (const auto keys = bound_keys(table, program)) {
        for (const auto &key_term : *keys) {
            const auto at = key(context, table, key_term);
            need_table(table.read(at, [&](const detail::ets::Index &index) { each_object(index.find(at), visit); }));
        }
        return;
    }
    need_table(table.read_parts([&](const detail::ets::Index &index) {
        index.each([&](const Objects &objects) { each_object(&objects, visit); });
    }));
}

// The result of a program on one stored object, if a clause matches.
std::optional<Term> run_stored(ProcessContext &context, const Program &program, Word object);
} // namespace clause::runtime::builtins::ets
