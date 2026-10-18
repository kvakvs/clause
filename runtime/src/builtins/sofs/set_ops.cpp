#include "sofs.hpp"

namespace clause::runtime::builtins::sofs {
namespace {
// Decide which exclusive side of a merge contributes to the chosen operation.
bool keep_exclusive(Operation operation, bool left) {
    if (operation == Operation::intersect) {
        return false;
    }
    return left || operation != Operation::subtract;
}

// Append the remaining exclusive elements after one side of the merge is exhausted.
void append_rest(Items &result, std::span<const Term> rest, Operation operation, bool left) {
    if (keep_exclusive(operation, left)) {
        result.insert(result.end(), rest.begin(), rest.end());
    }
}
} // namespace

Items combine(const Items &left, const Items &right, Operation operation) {
    Items result;
    std::size_t a = 0;
    std::size_t b = 0;
    while (a < left.size() && b < right.size()) {
        const auto order = compare(left[a], right[b]);
        if (order == 0) {
            if (operation == Operation::unite || operation == Operation::intersect) {
                result.push_back(left[a]);
            }
            ++a;
            ++b;
        } else if (order < 0) {
            append_rest(result, std::span<const Term>(&left[a++], 1), operation, true);
        } else {
            append_rest(result, std::span<const Term>(&right[b++], 1), operation, false);
        }
    }
    append_rest(result, std::span(left).subspan(a), operation, true);
    append_rest(result, std::span(right).subspan(b), operation, false);
    return result;
}

Term binary_set(const Terms &terms, const Left &left, const Right &right, Operation operation) {
    const auto first = unordered(left.term);
    const auto second = unordered(right.term);
    const auto type = unified(terms, {first.type_}, {second.type_});
    return make_set(terms, combine(elements(first.data_), elements(second.data_), operation), {type});
}
} // namespace clause::runtime::builtins::sofs
