#include "sofs.hpp"

namespace clause::runtime::builtins::sofs {
namespace {
// All matching-domain range pairs, before deduplication.
Items paired_ranges(const Terms &terms, const Items &left, std::span<const Term> right) {
    Items result;
    for (const auto &first : left) {
        for (const auto &second : right) {
            if (compare(at(first, 0), at(second, 0)) == 0) {
                result.push_back(tuple(terms, {at(first, 1), at(second, 1)}));
            }
        }
    }
    std::ranges::reverse(result);
    return unique(std::move(result));
}

// Add one component to every partial Cartesian tuple.
Items cartesian_step(const Terms &terms, const Items &prefixes, std::span<const Term> values) {
    Items result;
    for (const auto &prefix : prefixes) {
        for (const auto &value : values) {
            auto tuple_values = need(prefix.tuple_elements());
            tuple_values.push_back(value);
            result.push_back(terms.tuple(tuple_values));
        }
    }
    return result;
}

// Binary Cartesian products return the first untyped operand verbatim.
std::optional<Term> untyped_product(const Items &sets) {
    if (sets.size() != 2) {
        return std::nullopt;
    }
    const auto first = unordered(sets[0]);
    const auto second = unordered(sets[1]);
    if (atom_is(first.type_, "_")) {
        return sets[0];
    }
    return atom_is(second.type_, "_") ? std::optional{sets[1]} : std::nullopt;
}

// Cartesian tuple product with the special binary-product empty type rule.
Term product_of(const Terms &terms, const Items &sets) {
    if (sets.empty()) {
        bad_argument();
    }
    if (const auto untyped = untyped_product(sets)) {
        return *untyped;
    }
    Items types;
    Items result{terms.tuple(Items{})};
    bool any = false;
    for (const auto &value : sets) {
        const auto set = unordered(value);
        types.push_back(set.type_);
        any = any || atom_is(set.type_, "_");
        result = cartesian_step(terms, result, elements(set.data_));
    }
    if (sets.size() != 2 && result.empty()) {
        return empty(terms);
    }
    return make_set(terms, result, {any ? terms.atom("_") : terms.tuple(types)});
}

// Validate a list or tuple argument for the n-ary relative product.
Items relation_list(const Term &value) {
    if (value.is_tuple() && !is_record(value)) {
        return need(value.tuple_elements());
    }
    if (!value.is_list()) {
        no_clause();
    }
    return elements(value);
}

// Extend domain-keyed range tuples with one relation's matching values.
Items relative_step(const Terms &terms, const Items &prefixes, std::span<const Term> pairs) {
    Items result;
    for (const auto &prefix : prefixes) {
        for (const auto &pair : pairs) {
            if (compare(at(prefix, 0), at(pair, 0)) != 0) {
                continue;
            }
            auto values = need(at(prefix, 1).tuple_elements());
            values.push_back(at(pair, 1));
            result.push_back(tuple(terms, {at(prefix, 0), terms.tuple(values)}));
        }
    }
    return unique(std::move(result));
}

// Cartesian product of two unordered sets.
Term product2(ProcessContext &context, const Left &left, const Right &right) {
    return product_of(Terms(context), Items{left.term, right.term});
}

// Cartesian product of a nonempty tuple of unordered sets.
Term product1(ProcessContext &context, const Term &value) {
    if (!value.is_tuple()) {
        no_clause();
    }
    const auto sets = need(value.tuple_elements());
    if (sets.size() != 2 &&
        std::ranges::any_of(sets, [](const Term &item) { return !is_record(item) || !record(item).unordered_; })) {
        bad_argument();
    }
    return product_of(Terms(context), sets);
}

// Binary or n-ary public relative product.
Term relative2(ProcessContext &context, const Left &left, const Right &right) {
    Terms terms(context);
    static_cast<void>(unordered(right.term));
    if (is_record(left.term) && record(left.term).unordered_) {
        return relative_product(terms, left, right);
    }
    static_cast<void>(relation_type(terms, unordered(right.term)));
    return relative_product(terms, {relative_many(terms, left.term)}, right);
}

// N-ary public relative product over relations sharing one domain.
Term relative1(ProcessContext &context, const Term &value) { return relative_many(Terms(context), value); }

// Public common-domain relative product.
Term common_product(ProcessContext &context, const Left &left, const Right &right) {
    return relative_product1(Terms(context), left, right);
}

// Compose functions, requiring the first range to be covered by the second domain.
Term composite(ProcessContext &context, const Left &left, const Right &right) {
    Terms terms(context);
    const auto first = unordered(left.term);
    const auto second = unordered(right.term);
    const auto types1 = relation_type(terms, first);
    const auto types2 = relation_type(terms, second);
    match(terms, {types1[1]}, {types2[0]});
    if (atom_is(first.type_, "_")) {
        return left.term;
    }
    if (atom_is(second.type_, "_")) {
        return right.term;
    }
    const auto a = elements(first.data_);
    const auto b = elements(second.data_);
    check_function(terms, a);
    check_function(terms, b);
    const auto result = relative_product(terms, left, right);
    if (elements(unordered(result).data_).size() != a.size()) {
        error(terms, "bad_function");
    }
    return result;
}

// Validate a relation selector, preserving the untyped relation's unrestricted positions.
std::size_t position(const Set &set, const Term &index) {
    if (!index.is_integer()) {
        no_clause();
    }
    if (atom_is(set.type_, "_")) {
        return 0;
    }
    const auto value = small(index.word());
    if (!set.type_.is_tuple() || !value || *value < 1 ||
        static_cast<std::uint64_t>(*value) > set.type_.tuple_size_unchecked()) {
        bad_argument();
    }
    return static_cast<std::size_t>(*value - 1);
}

// Join two tuple terms while omitting the matched coordinate from the second.
Term joined_tuple(const Terms &terms, const Left &left, const Right &right, std::size_t omit) {
    auto data = need(left.term.tuple_elements());
    auto tail = need(right.term.tuple_elements());
    tail.erase(tail.begin() + static_cast<std::ptrdiff_t>(omit));
    data.insert(data.end(), tail.begin(), tail.end());
    return terms.tuple(data);
}

// Collect matching tuple pairs for join without requiring equal coordinate type labels.
Items joined_data(const Terms &terms, const Left &left, const Right &right,
                  std::pair<std::size_t, std::size_t> positions) {
    Items result;
    for (const auto &a : elements(left.term)) {
        for (const auto &b : elements(right.term)) {
            if (compare(at(a, positions.first), at(b, positions.second)) == 0) {
                result.push_back(joined_tuple(terms, {a}, {b}, positions.second));
            }
        }
    }
    std::ranges::reverse(result);
    std::stable_sort(result.begin(), result.end(), [](const Term &a, const Term &b) { return compare(a, b) < 0; });
    return result;
}

// Join tuple relations at arbitrary one-based coordinates.
Term join(ProcessContext &context, const Left &left, const Index &i, const Right &right, const OtherIndex &j) {
    Terms terms(context);
    const auto a = unordered(left.term);
    const auto b = unordered(right.term);
    const auto first = position(a, i.term);
    const auto second = position(b, j.term);
    if (atom_is(a.type_, "_")) {
        return left.term;
    }
    if (atom_is(b.type_, "_")) {
        return right.term;
    }
    const auto type = joined_tuple(terms, {a.type_}, {b.type_}, second);
    return make_set(terms, joined_data(terms, {a.data_}, {b.data_}, {first, second}), {type});
}

// Apply a separate binary relation to each coordinate of the input relation.
Term multiple_relative_product(ProcessContext &context, const Left &relations, const Right &relation) {
    Terms terms(context);
    const auto set = unordered(relation.term);
    if (!relations.term.is_tuple()) {
        no_clause();
    }
    const auto inputs = need(relations.term.tuple_elements());
    if (atom_is(set.type_, "_")) {
        return empty(terms);
    }
    if (!set.type_.is_tuple() || set.type_.tuple_size_unchecked() != inputs.size()) {
        bad_argument();
    }
    Items transformed;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        Items raised;
        for (const auto &value : elements(set.data_)) {
            raised.push_back(tuple(terms, {at(value, i), value}));
        }
        const auto type = tuple(terms, {at(set.type_, i), set.type_});
        const auto input = make_set(terms, unique(std::move(raised)), {type});
        transformed.push_back(relative_product1(terms, {input}, {inputs[i]}));
    }
    return relative_many(terms, terms.list(transformed));
}

constexpr std::array PRODUCTS{
    typed_entry<product1>("sofs", "product"),
    typed_entry<product2>("sofs", "product"),
    typed_entry<relative1>("sofs", "relative_product"),
    typed_entry<relative2>("sofs", "relative_product"),
    typed_entry<common_product>("sofs", "relative_product1"),
    typed_entry<composite>("sofs", "composite"),
    typed_entry<join>("sofs", "join"),
    typed_entry<multiple_relative_product>("sofs", "multiple_relative_product"),
};
} // namespace

Term relative_product1(const Terms &terms, const Left &left, const Right &right) {
    const auto first = unordered(left.term);
    const auto second = unordered(right.term);
    const auto a = relation_type(terms, first);
    const auto b = relation_type(terms, second);
    match(terms, {a[0]}, {b[0]});
    if (atom_is(a[0], "_")) {
        return left.term;
    }
    if (atom_is(b[0], "_")) {
        return right.term;
    }
    const auto data = paired_ranges(terms, elements(first.data_), elements(second.data_));
    return make_set(terms, data, {tuple(terms, {a[1], b[1]})});
}

Term relative_product(const Terms &terms, const Left &left, const Right &right) {
    return relative_product1(terms, {converse(terms, left.term)}, right);
}

Term relative_many(const Terms &terms, const Term &relations) {
    const auto inputs = relation_list(relations);
    if (inputs.empty()) {
        bad_argument();
    }
    auto domain = terms.atom("_");
    Items ranges;
    bool is_empty = false;
    bool untyped = false;
    for (const auto &value : inputs) {
        const auto set = unordered(value);
        const auto type = relation_type(terms, set);
        domain = unified(terms, {type[0]}, {domain});
        ranges.push_back(type[1]);
        untyped = untyped || atom_is(set.type_, "_");
        is_empty = is_empty || set.data_.is_nil();
    }
    if (untyped) {
        return empty(terms);
    }
    const auto type = tuple(terms, {domain, terms.tuple(ranges)});
    if (is_empty) {
        return make_set(terms, {}, {type});
    }
    Items result;
    for (const auto &pair : elements(unordered(inputs.front()).data_)) {
        result.push_back(tuple(terms, {at(pair, 0), terms.tuple(Items{at(pair, 1)})}));
    }
    for (std::size_t i = 1; i < inputs.size(); ++i) {
        result = relative_step(terms, result, elements(unordered(inputs[i]).data_));
    }
    return make_set(terms, result, {type});
}
} // namespace clause::runtime::builtins::sofs

namespace clause::runtime {
// Publish Cartesian, relative and tuple-join product builtins.
std::span<const BuiltinEntry> sofs_products_builtins() noexcept { return builtins::sofs::PRODUCTS; }
} // namespace clause::runtime
