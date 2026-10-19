#include "list_walk.hpp"
#include <array>
#include <terms.hpp>

// The lists builtins over lists of key tuples (plan step 41T, docs/builtins.md#lists-module): keymember/3 and
// keysearch/3, which are BIFs in OTP (badarg), and keydelete/3, keyreplace/4, keystore/4 and keytake/3, which are
// Erlang (function_clause). A key matches when
// element N of a tuple element compares equal (==); other elements are skipped.
namespace clause::runtime::builtins::lists {
namespace {
// Whether `element` is a tuple whose element `position` (1-based) compares equal to `key`.
bool has_key(const Term &element, const Term &key, std::size_t position) {
    return element.is_tuple() && element.tuple_size_unchecked() >= position &&
           equal(element.tuple_element_unchecked(position - 1), key);
}

// The first tuple of `list` with `key` at `position`, as the BIFs search: badarg for a position that is not a
// positive small integer, or for an improper tail reached before a match.
std::optional<Term> search(const Term &key, std::int64_t position, Term list) {
    if (position < 1) {
        bad_argument();
    }
    for (; list.is_cons(); list = list.tail_unchecked()) {
        auto element = list.head_unchecked();
        if (has_key(element, key, static_cast<std::size_t>(position))) {
            return element;
        }
    }
    if (!list.is_nil()) {
        bad_argument();
    }
    return std::nullopt;
}

// keymember(Key, N, TupleList).
TermResult<Term> keymember(ProcessContext &context, const Term &key, std::int64_t position, const Term &list) {
    return TermFactory(context).boolean(search(key, position, list).has_value());
}

// keysearch(Key, N, TupleList): {value, Tuple} or false.
TermResult<Term> keysearch(ProcessContext &context, const Term &key, std::int64_t position, const Term &list) {
    TermFactory factory(context);
    const auto found = search(key, position, list);
    if (!found) {
        return factory.boolean(false);
    }
    const std::array parts{need(factory.atom("value"_atom)), *found};
    return factory.tuple(parts);
}

// keydelete(Key, N, TupleList): the list without its first tuple with Key at N; the rest after it is kept as is.
TermResult<Term> keydelete(ProcessContext &context, const Term &key, const Count &position, const Term &list) {
    const auto index = position.natural();
    if (!index || *index == 0) {
        no_clause();
    }
    std::vector<Term> kept;
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto element = rest.head_unchecked();
        if (has_key(element, key, *index)) {
            return TermFactory(context).list(kept, rest.tail_unchecked());
        }
        kept.push_back(element);
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return TermFactory(context).list(kept);
}

// A key position argument: a positive integer, else function_clause.
std::size_t key_position(const Count &position) {
    const auto index = position.natural();
    if (!index || *index == 0) {
        no_clause();
    }
    return *index;
}

struct Found {
    // The tuples before the first one with the key, that tuple and the list after it; no tuple when none matched in
    // a proper list.
    std::vector<Term> before;
    std::optional<Term> tuple;
    Term after;
};

// The first tuple of `list` with `key` at `position`; function_clause for an improper list without one.
Found find(const Term &key, std::size_t position, const Term &list) {
    Found found{{}, std::nullopt, list};
    for (; found.after.is_cons(); found.after = found.after.tail_unchecked()) {
        const auto element = found.after.head_unchecked();
        if (has_key(element, key, position)) {
            found.tuple = element;
            found.after = found.after.tail_unchecked();
            return found;
        }
        found.before.push_back(element);
    }
    if (!found.after.is_nil()) {
        no_clause();
    }
    return found;
}

// keyreplace(Key, N, TupleList1, NewTuple) and keystore(Key, N, TupleList1, NewTuple): the first tuple with Key at N
// replaced by NewTuple; without one keyreplace returns the list as it is and keystore appends NewTuple.
template <bool Store>
TermResult<Term> keyreplace(ProcessContext &context, const Term &key, const Count &position, const ListTerm &list,
                            const Term &replacement) {
    const auto index = key_position(position);
    if (!replacement.is_tuple()) {
        no_clause();
    }
    auto found = find(key, index, list.term);
    TermFactory factory(context);
    if (found.tuple) {
        return factory.list(found.before, need(factory.cons(replacement, found.after)));
    }
    if (!Store) {
        return list.term;
    }
    found.before.push_back(replacement);
    return factory.list(found.before);
}

// keytake(Key, N, TupleList1): {value, Tuple, TupleList2} without the first tuple with Key at N, or false.
TermResult<Term> keytake(ProcessContext &context, const Term &key, const Count &position, const Term &list) {
    const auto found = find(key, key_position(position), list);
    TermFactory factory(context);
    if (!found.tuple) {
        return factory.boolean(false);
    }
    const std::array parts{need(factory.atom("value"_atom)), *found.tuple,
                           need(factory.list(found.before, found.after))};
    return factory.tuple(parts);
}

constexpr std::array KEY_BUILTINS{
    typed_entry<keymember>("lists", "keymember"),       typed_entry<keysearch>("lists", "keysearch"),
    typed_entry<keydelete>("lists", "keydelete"),       typed_entry<keyreplace<false>>("lists", "keyreplace"),
    typed_entry<keyreplace<true>>("lists", "keystore"), typed_entry<keytake>("lists", "keytake"),
};
} // namespace
} // namespace clause::runtime::builtins::lists

namespace clause::runtime {
std::span<const BuiltinEntry> lists_key_builtins() noexcept { return builtins::lists::KEY_BUILTINS; }
} // namespace clause::runtime
