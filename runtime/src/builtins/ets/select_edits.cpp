#include "../../match_spec/compiler.hpp"
#include "select.hpp"
#include <array>
#include <terms.hpp>

// The ets select builtins that edit (plan 21 step LIB11, docs/builtins.md#ets): select_delete/2 erases the objects
// the specification gives true for, select_replace/2 replaces each matched object by the result, which must keep its
// key (OTP's db_match_keeps_key decides from the specification itself). Each part is edited under its own lock.
namespace clause::runtime::builtins::ets {
namespace {
namespace match_spec = detail::match_spec;
using detail::ets::Edit;
using detail::ets::EditDecision;
using detail::ets::Index;

// The edits of every object a program may match: the bound keys' objects, else every part's; the edited count.
std::size_t edit_candidates(ProcessContext &context, Table &found, const Program &program, const EditDecision &decide) {
    std::size_t changed = 0;
    if (const auto keys = bound_keys(found, program)) {
        for (const auto &key_term : *keys) {
            const auto at = key(context, found, key_term);
            need_table(found.write(at, [&](Index &index) { changed += index.edit(&at, decide); }));
        }
        return changed;
    }
    need_table(found.edit_parts([&](Index &index) { changed += index.edit(nullptr, decide); }));
    return changed;
}

// One pair of a matched term and the term a body builds, compared by db_match_eq_body.
struct Compared {
    Term matched;
    Term built;
    bool constant;
};

// A construction outside a constant unwrapped: {{...}} is its tuple, {const, X} is X compared as a constant; none for
// a call, which is never sure to build the matched term.
std::optional<Compared> unwrapped(const Compared &pair) {
    const auto &built = pair.built;
    if (built.tuple_size_unchecked() == 1 && built.tuple_element_unchecked(0).is_tuple()) {
        return Compared{.matched = pair.matched, .built = built.tuple_element_unchecked(0), .constant = false};
    }
    if (built.tuple_size_unchecked() == 2 && match_spec::is_atom(built.tuple_element_unchecked(0), "const")) {
        return Compared{.matched = pair.matched, .built = built.tuple_element_unchecked(1), .constant = true};
    }
    return std::nullopt;
}

// Push the pairs of elements of two lists or two tuples of one size; false when their shapes differ.
bool push_children(const Compared &pair, std::vector<Compared> &pending) {
    const auto &[left, right, constant] = pair;
    if (right.is_cons()) {
        if (!left.is_cons()) {
            return false;
        }
        pending.push_back({.matched = left.head_unchecked(), .built = right.head_unchecked(), .constant = constant});
        pending.push_back({.matched = left.tail_unchecked(), .built = right.tail_unchecked(), .constant = constant});
        return true;
    }
    if (!left.is_tuple() || left.tuple_size_unchecked() != right.tuple_size_unchecked()) {
        return false;
    }
    for (std::size_t at = 0; at < right.tuple_size_unchecked(); ++at) {
        pending.push_back({.matched = left.tuple_element_unchecked(at),
                           .built = right.tuple_element_unchecked(at),
                           .constant = constant});
    }
    return true;
}

// Whether two leaves are surely one term: exactly equal, no map, and no '_', '$$', '$_' (or, in a constant, variable)
// on the matched side.
bool same_leaf(const Compared &pair) {
    if (pair.built.is_map() || !detail::ets::same_object(pair.matched.word(), pair.built.word())) {
        return false;
    }
    const auto &left = pair.matched;
    const auto special =
        match_spec::is_atom(left, "_") || match_spec::is_atom(left, "$$") || match_spec::is_atom(left, "$_");
    return !special && !(pair.constant && match_spec::variable_number(left));
}

// Whether a construction is the term a head matched (OTP's db_match_eq_body).
// One pair of the comparison: false when it shows the construction differs, its children queued otherwise.
bool compare(Compared pair, std::vector<Compared> &pending) {
    // Outside a constant a tuple is a construction: {{...}} compares its elements, {const, X} compares X.
    auto built_tuple = pair.built.is_tuple();
    if (built_tuple && !pair.constant) {
        const auto next = unwrapped(pair);
        if (!next) {
            return false;
        }
        built_tuple = !next->constant || next->built.is_tuple();
        pair = *next;
    }
    const auto container = built_tuple || pair.built.is_cons();
    return container ? push_children(pair, pending) : same_leaf(pair);
}

bool same_construction(const Term &matched, const Term &built, bool constant) {
    std::vector<Compared> pending{{.matched = matched, .built = built, .constant = constant}};
    while (!pending.empty()) {
        const auto pair = pending.back();
        pending.pop_back();
        if (!compare(pair, pending)) {
            return false;
        }
    }
    return true;
}

// The element at the key position of a tuple, if it is that long.
std::optional<Term> key_at(const Term &term, std::size_t keypos) {
    if (!term.is_tuple() || term.tuple_size_unchecked() < keypos) {
        return std::nullopt;
    }
    return term.tuple_element_unchecked(keypos - 1);
}

// The one result of a body of one expression, if it is that.
std::optional<Term> only_result(const Term &body) {
    if (!body.is_cons() || !body.tail_unchecked().is_nil()) {
        return std::nullopt;
    }
    return body.head_unchecked();
}

// The tuple a result builds ({{...}}) or gives as a constant ({const, {...}}), and whether it is the constant.
std::optional<std::pair<Term, bool>> built_tuple(const Term &result) {
    if (!result.is_tuple()) {
        return std::nullopt;
    }
    const auto size = result.tuple_size_unchecked();
    if (size == 2 && match_spec::is_atom(result.tuple_element_unchecked(0), "const")) {
        return std::pair{result.tuple_element_unchecked(1), true};
    }
    if (size == 1) {
        return std::pair{result.tuple_element_unchecked(0), false};
    }
    return std::nullopt;
}

// Whether a built key is {element, KeyPos, '$_'}.
bool object_key(const Term &built, std::size_t keypos) {
    return built.is_tuple() && built.tuple_size_unchecked() == 3 &&
           match_spec::is_atom(built.tuple_element_unchecked(0), "element") &&
           small(built.tuple_element_unchecked(1).word()) == static_cast<std::int64_t>(keypos) &&
           match_spec::is_atom(built.tuple_element_unchecked(2), "$_");
}

// Whether a clause {Head, Guards, Body}'s body surely keeps the key its head matched: '$_', a tuple built or constant
// with the head's key in the key position, or {element, KeyPos, '$_'} there (OTP's db_match_keeps_key).
bool keeps_key(std::size_t keypos, const Term &clause) {
    const auto head = clause.tuple_element_unchecked(0);
    const auto result = only_result(clause.tuple_element_unchecked(2));
    if (result && match_spec::is_atom(*result, "$_")) {
        return true;
    }
    const auto matched = key_at(head, keypos);
    const auto tuple = result ? built_tuple(*result) : std::nullopt;
    const auto built = tuple ? key_at(tuple->first, keypos) : std::nullopt;
    if (!matched || !built) {
        return false;
    }
    const auto constant = tuple->second;
    return same_construction(*matched, *built, constant) || (!constant && object_key(*built, keypos));
}

// select_delete(Table, MatchSpec): the objects for which the specification gives true are erased; their count.
Term select_delete(ProcessContext &context, const TableArgument &argument, const Term &spec) {
    const auto found = table(context, argument, Access::write);
    const auto program = compiled(context, spec);
    const auto erased = edit_candidates(context, *found, *program, [&](Word object) {
        const auto result = run_stored(context, *program, object);
        return Edit{.action = result && result->word() == abi::v1::true_atom ? Edit::Action::erase : Edit::Action::keep,
                    .replacement = DetachedTerm{}};
    });
    return need(TermFactory(context).integer(static_cast<std::int64_t>(erased)));
}

// Whether a result is an object of the same key, which may replace the stored one.
bool replaces(const Table &table, Word stored, const Term &result) {
    const auto keypos = table.options().keypos;
    const auto key_term = key_at(result, keypos);
    if (!key_term) {
        return false;
    }
    const auto old = detail::ets::key_of(stored, keypos);
    return table.hashed() ? detail::ets::same_key(old, key_term->word())
                          : detail::ets::key_order(old, key_term->word()) == 0;
}

// select_replace(Table, MatchSpec): each matched object is replaced by the result; the count. Bags are badarg, as
// is a specification that may change a key.
Term select_replace(ProcessContext &context, const TableArgument &argument, const Term &spec) {
    const auto found = table(context, argument, Access::write);
    const auto program = compiled(context, spec);
    if (found->options().type == TableType::bag) {
        bad_argument();
    }
    for (auto rest = program->source.term(); rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto clause = rest.head_unchecked();
        if (!keeps_key(found->options().keypos, clause)) {
            bad_argument();
        }
    }
    const auto replaced = edit_candidates(context, *found, *program, [&](Word object) {
        const auto result = run_stored(context, *program, object);
        if (!result || !replaces(*found, object, *result)) {
            return Edit{.action = Edit::Action::keep, .replacement = DetachedTerm{}};
        }
        return Edit{.action = Edit::Action::replace, .replacement = need(DetachedTerm::copy(context, *result))};
    });
    return need(TermFactory(context).integer(static_cast<std::int64_t>(replaced)));
}

constexpr std::array ETS_SELECT_EDIT_BUILTINS{
    typed_entry<select_delete>("ets", "select_delete"),
    typed_entry<select_delete>("ets", "internal_select_delete"),
    typed_entry<select_replace>("ets", "select_replace"),
};
} // namespace
} // namespace clause::runtime::builtins::ets

namespace clause::runtime {
std::span<const BuiltinEntry> ets_select_edit_builtins() noexcept { return builtins::ets::ETS_SELECT_EDIT_BUILTINS; }
} // namespace clause::runtime
