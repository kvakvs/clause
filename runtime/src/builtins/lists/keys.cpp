#include "list_walk.hpp"
#include <array>
#include <terms.hpp>

// The lists builtins over lists of key tuples (plan step 41T, docs/builtins.md#lists-module): keymember/3 and
// keysearch/3, which are BIFs in OTP (badarg), and keydelete/3, which is Erlang (function_clause). A key matches when
// element N of a tuple element compares equal (==); other elements are skipped.
namespace clause::runtime::builtins::lists {
namespace {
// Whether `element` is a tuple whose element `position` (1-based) compares equal to `key`.
bool has_key(const Term &element, const Term &key, std::size_t position) {
    return element.is_tuple() && need(element.tuple_size()) >= position &&
           equal(need(element.tuple_element(position - 1)), key);
}

// The first tuple of `list` with `key` at `position`, as the BIFs search: badarg for a position that is not a
// positive small integer, or for an improper tail reached before a match.
std::optional<Term> search(const Term &key, std::int64_t position, Term list) {
    if (position < 1) {
        bad_argument();
    }
    for (; list.is_cons(); list = need(list.tail())) {
        auto element = need(list.head());
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
    const std::array parts{need(factory.atom("value")), *found};
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
    for (; rest.is_cons(); rest = need(rest.tail())) {
        const auto element = need(rest.head());
        if (has_key(element, key, *index)) {
            return TermFactory(context).list(kept, need(rest.tail()));
        }
        kept.push_back(element);
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return TermFactory(context).list(kept);
}

constexpr std::array KEY_BUILTINS{
    typed_entry<keymember>("lists", "keymember"),
    typed_entry<keysearch>("lists", "keysearch"),
    typed_entry<keydelete>("lists", "keydelete"),
};
} // namespace
} // namespace clause::runtime::builtins::lists

namespace clause::runtime {
std::span<const BuiltinEntry> lists_key_builtins() noexcept { return builtins::lists::KEY_BUILTINS; }
} // namespace clause::runtime
