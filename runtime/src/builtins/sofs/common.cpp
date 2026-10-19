#include "sofs.hpp"

namespace clause::runtime::builtins::sofs {
bool atom_is(const Term &term, std::string_view name) { return term.is_atom() && need(term.atom_spelling()) == name; }

Term at(const Term &term, std::size_t index) {
    if (!term.is_tuple() || index >= term.tuple_size_unchecked()) {
        bad_argument();
    }
    return term.tuple_element_unchecked(index);
}

Term tuple(const Terms &terms, std::initializer_list<Term> values) {
    return terms.tuple(std::span<const Term>(values.begin(), values.size()));
}

Items elements(const Term &term) {
    Items result;
    auto rest = term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        result.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return result;
}

Items unique(Items values) {
    std::stable_sort(values.begin(), values.end(), [](const Term &a, const Term &b) { return compare(a, b) < 0; });
    const auto end =
        std::unique(values.begin(), values.end(), [](const Term &a, const Term &b) { return compare(a, b) == 0; });
    values.erase(end, values.end());
    return values;
}

bool is_record(const Term &term) {
    if (!term.is_tuple() || term.tuple_size_unchecked() != 3) {
        return false;
    }
    const auto tag = at(term, 0);
    return atom_is(tag, "Set") || atom_is(tag, "OrdSet");
}

Set record(const Term &term) {
    if (!is_record(term)) {
        no_clause();
    }
    return {at(term, 1), at(term, 2), atom_is(at(term, 0), "Set")};
}

Set unordered(const Term &term) {
    auto set = record(term);
    if (!set.unordered_) {
        no_clause();
    }
    return set;
}

Term wrap(const Terms &terms, const Set &set) {
    return tuple(terms, {terms.atom(set.unordered_ ? "Set"_atom : "OrdSet"_atom), set.data_, set.type_});
}

Term make_set(const Terms &terms, const Items &values, const Value &type) {
    return wrap(terms, {terms.list(values), type.term, true});
}

Term empty(const Terms &terms) { return make_set(terms, {}, {terms.atom("_"_atom)}); }

Term full_type(const Terms &terms, const Set &set) { return set.unordered_ ? terms.list(Items{set.type_}) : set.type_; }

std::optional<Term> list_type(const Term &term) {
    if (term.is_cons() && term.tail_unchecked().is_nil()) {
        return term.head_unchecked();
    }
    return std::nullopt;
}

[[noreturn]] void error(const Terms &terms, AtomName reason) {
    const auto payload = terms.atom(reason);
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = payload.word()};
}

std::array<Term, 2> relation_type(const Terms &terms, const Set &set) {
    if (atom_is(set.type_, "_")) {
        return {set.type_, set.type_};
    }
    if (!set.type_.is_tuple() || set.type_.tuple_size_unchecked() != 2) {
        bad_argument();
    }
    static_cast<void>(terms);
    return {at(set.type_, 0), at(set.type_, 1)};
}

std::array<Term, 2> family_type(const Set &set) {
    if (!set.type_.is_tuple() || set.type_.tuple_size_unchecked() != 2) {
        bad_argument();
    }
    auto member = list_type(at(set.type_, 1));
    if (!member) {
        bad_argument();
    }
    return {at(set.type_, 0), *member};
}

void check_function(const Terms &terms, const Items &pairs) {
    std::optional<Term> previous;
    for (const auto &pair : pairs) {
        const auto key = at(pair, 0);
        if (previous && compare(*previous, key) == 0) {
            error(terms, "bad_function"_atom);
        }
        previous = key;
    }
}

Items group(const Terms &terms, Items pairs) {
    std::stable_sort(pairs.begin(), pairs.end(), [](const Term &a, const Term &b) { return compare(a, b) < 0; });
    return group_adjacent(terms, pairs, true);
}

Items group_adjacent(const Terms &terms, const Items &pairs, bool sort_members) {
    Items result;
    std::size_t start = 0;
    while (start < pairs.size()) {
        const auto key = at(pairs[start], 0);
        Items values;
        auto end = start;
        while (end < pairs.size() && compare(key, at(pairs[end], 0)) == 0) {
            values.push_back(at(pairs[end++], 1));
        }
        if (sort_members) {
            std::stable_sort(values.begin(), values.end(),
                             [](const Term &a, const Term &b) { return compare(a, b) < 0; });
        }
        result.push_back(tuple(terms, {key, terms.list(values)}));
        start = end;
    }
    return result;
}
} // namespace clause::runtime::builtins::sofs
