#include "sofs.hpp"

namespace clause::runtime::builtins::sofs {
bool valid_type(const Term &term) {
    if (term.is_atom()) {
        return !atom_is(term, "_");
    }
    if (const auto member = list_type(term)) {
        return atom_is(*member, "_") || valid_type(*member);
    }
    if (!term.is_tuple() || term.tuple_size_unchecked() == 0) {
        return false;
    }
    const auto types = need(term.tuple_elements());
    return std::ranges::all_of(types, valid_type);
}

namespace {
// Merge independent tuple fields, stopping as soon as any component is incompatible.
std::optional<Term> unify_tuple(const Terms &terms, const Left &left, const Right &right) {
    if (!left.term.is_tuple() || !right.term.is_tuple()) {
        return std::nullopt;
    }
    auto first = need(left.term.tuple_elements());
    const auto second = need(right.term.tuple_elements());
    if (first.size() != second.size()) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < first.size(); ++i) {
        const auto member = unify(terms, {first[i]}, {second[i]});
        if (!member) {
            return std::nullopt;
        }
        first[i] = *member;
    }
    return terms.tuple(first);
}
} // namespace

std::optional<Term> unify(const Terms &terms, const Left &left, const Right &right) {
    if (same(left.term, right.term) || atom_is(right.term, "_")) {
        return left.term;
    }
    if (atom_is(left.term, "_")) {
        return right.term;
    }
    const auto a = list_type(left.term);
    const auto b = list_type(right.term);
    if (a && b) {
        const auto member = unify(terms, {*a}, {*b});
        return member ? std::optional{terms.list(Items{*member})} : std::nullopt;
    }
    return unify_tuple(terms, left, right);
}

Term unified(const Terms &terms, const Left &left, const Right &right) {
    const auto result = unify(terms, left, right);
    if (!result) {
        error(terms, "type_mismatch");
    }
    return *result;
}

void match(const Terms &terms, const Left &left, const Right &right) { static_cast<void>(unified(terms, left, right)); }
} // namespace clause::runtime::builtins::sofs
