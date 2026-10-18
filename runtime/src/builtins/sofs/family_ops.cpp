#include "sofs.hpp"

namespace clause::runtime::builtins::sofs {
Term family_to_relation(const Terms &terms, const Term &family) {
    const auto set = unordered(family);
    if (atom_is(set.type_, "_")) {
        return family;
    }
    const auto type = family_type(set);
    Items result;
    for (const auto &pair : elements(set.data_)) {
        for (const auto &member : elements(at(pair, 1))) {
            result.push_back(tuple(terms, {at(pair, 0), member}));
        }
    }
    return make_set(terms, result, {tuple(terms, {type[0], type[1]})});
}

namespace {
// Copy an unmatched family entry only when its side participates in the operation.
void unmatched(Items &result, const Term &pair, Operation operation, bool left) {
    if (operation == Operation::unite || (left && operation == Operation::subtract)) {
        result.push_back(pair);
    }
}

// Merge ordered families while applying the selected operation to values at shared keys.
Items merge_families(const Terms &terms, const Items &left, std::span<const Term> right, Operation operation) {
    Items result;
    auto first = std::span(left);
    auto second = right;
    while (!first.empty() && !second.empty()) {
        const auto order = compare(at(first.front(), 0), at(second.front(), 0));
        if (order == 0) {
            const auto values = combine(elements(at(first.front(), 1)), elements(at(second.front(), 1)), operation);
            result.push_back(tuple(terms, {at(first.front(), 0), terms.list(values)}));
            first = first.subspan(1);
            second = second.subspan(1);
        } else if (order < 0) {
            unmatched(result, first.front(), operation, true);
            first = first.subspan(1);
        } else {
            unmatched(result, second.front(), operation, false);
            second = second.subspan(1);
        }
    }
    for (const auto &pair : first) {
        unmatched(result, pair, operation, true);
    }
    for (const auto &pair : second) {
        unmatched(result, pair, operation, false);
    }
    return result;
}
} // namespace

Term family_binary(const Terms &terms, const Left &left, const Right &right, Operation operation) {
    const auto first = unordered(left.term);
    const auto second = unordered(right.term);
    const auto type = unified(terms, {first.type_}, {second.type_});
    if (atom_is(type, "_")) {
        return left.term;
    }
    static_cast<void>(family_type({first.data_, type, true}));
    const auto data = merge_families(terms, elements(first.data_), elements(second.data_), operation);
    return make_set(terms, data, {type});
}
} // namespace clause::runtime::builtins::sofs
