#include "sofs.hpp"

namespace clause::runtime::builtins::sofs {
namespace {
// Validate an integer selector after the caller has handled untyped sets.
std::size_t coordinate(const Index &index, const Term &type) {
    const auto value = small(index.term.word());
    if (!type.is_tuple() || !value || *value < 1 || static_cast<std::uint64_t>(*value) > type.tuple_size_unchecked()) {
        bad_argument();
    }
    return static_cast<std::size_t>(*value - 1);
}

// Attach the selected key to each complete tuple for grouping.
Items keyed(const Terms &terms, const Items &values, std::size_t index) {
    Items result;
    for (const auto &value : values) {
        result.push_back(tuple(terms, {at(value, index), value}));
    }
    return result;
}

// Projection or substitution using a validated coordinate.
Term projected(const Terms &terms, const Set &set, std::size_t index, bool substitution) {
    Items result;
    for (const auto &value : elements(set.data_)) {
        const auto component = at(value, index);
        result.push_back(substitution ? tuple(terms, {value, component}) : component);
    }
    const auto type = at(set.type_, index);
    if (substitution) {
        return make_set(terms, result, {tuple(terms, {set.type_, type})});
    }
    if (index != 0) {
        std::ranges::reverse(result);
    }
    return make_set(terms, unique(std::move(result)), {type});
}

// Partition or partition_family using a validated coordinate.
Term partitioned(const Terms &terms, const Set &set, std::size_t index, bool family) {
    auto groups = group(terms, keyed(terms, elements(set.data_), index));
    const auto members = terms.list(Items{set.type_});
    if (family) {
        return make_set(terms, groups, {tuple(terms, {at(set.type_, index), members})});
    }
    Items result;
    for (const auto &pair : groups) {
        result.push_back(at(pair, 1));
    }
    return make_set(terms, unique(std::move(result)), {members});
}

// Split tuples by membership of their selected coordinate in the supplied key set.
std::array<Items, 2> split(const Items &values, std::span<const Term> keys, std::size_t index) {
    std::array<Items, 2> result;
    for (const auto &value : values) {
        const auto key = at(value, index);
        const auto found = std::ranges::any_of(keys, [&](const Term &item) { return compare(key, item) == 0; });
        result[found ? 0 : 1].push_back(value);
    }
    return result;
}

// Pick a restriction result or return both sides of a partition.
Term restriction_result(const Terms &terms, const std::array<Items, 2> &parts, const Term &type,
                        std::string_view operation) {
    const auto kept = make_set(terms, parts[0], {type});
    if (operation == "restriction") {
        return kept;
    }
    const auto removed = make_set(terms, parts[1], {type});
    return operation == "drestriction" ? removed : tuple(terms, {kept, removed});
}

// Private integer projection entry used by Erlang's SetFun dispatcher.
Term projection(ProcessContext &context, const Index &index, const Left &set) {
    return select(Terms(context), index, set, "projection");
}

// Private integer substitution entry used by Erlang's SetFun dispatcher.
Term substitution(ProcessContext &context, const Index &index, const Left &set) {
    return select(Terms(context), index, set, "substitution");
}

// Private integer partition entry used by Erlang's SetFun dispatcher.
Term partition2(ProcessContext &context, const Index &index, const Left &set) {
    return select(Terms(context), index, set, "partition");
}

// Private integer family partition entry used by Erlang's SetFun dispatcher.
Term partition_family(ProcessContext &context, const Index &index, const Left &set) {
    return select(Terms(context), index, set, "partition_family");
}

// Private integer restriction entry used by Erlang's SetFun dispatcher.
Term restriction3(ProcessContext &context, const Index &index, const Left &set, const Right &keys) {
    return restrict(Terms(context), index, set, keys, "restriction");
}

// Private integer complementary restriction entry used by Erlang's SetFun dispatcher.
Term drestriction3(ProcessContext &context, const Index &index, const Left &set, const Right &keys) {
    return restrict(Terms(context), index, set, keys, "drestriction");
}

// Private integer partition/3 entry used by Erlang's SetFun dispatcher.
Term partition3(ProcessContext &context, const Index &index, const Left &set, const Right &keys) {
    return restrict(Terms(context), index, set, keys, "partition");
}

// Public domain restriction defaults to the first coordinate.
Term restriction2(ProcessContext &context, const Left &set, const Right &keys) {
    Terms terms(context);
    return restrict(terms, {terms.integer(1)}, set, keys, "restriction");
}

// Public domain complementary restriction defaults to the first coordinate.
Term drestriction2(ProcessContext &context, const Left &set, const Right &keys) {
    Terms terms(context);
    return restrict(terms, {terms.integer(1)}, set, keys, "drestriction");
}

constexpr std::array SELECTORS{
    typed_entry<projection>("clause_sofs", "projection"),
    typed_entry<substitution>("clause_sofs", "substitution"),
    typed_entry<partition2>("clause_sofs", "partition"),
    typed_entry<partition_family>("clause_sofs", "partition_family"),
    typed_entry<restriction3>("clause_sofs", "restriction"),
    typed_entry<drestriction3>("clause_sofs", "drestriction"),
    typed_entry<partition3>("clause_sofs", "partition"),
    typed_entry<restriction2>("sofs", "restriction"),
    typed_entry<drestriction2>("sofs", "drestriction"),
};
} // namespace

Term select(const Terms &terms, const Index &index, const Left &set, std::string_view operation) {
    const auto value = unordered(set.term);
    if (atom_is(value.type_, "_")) {
        return set.term;
    }
    const auto position = coordinate(index, value.type_);
    if (operation == "projection" || operation == "substitution") {
        return projected(terms, value, position, operation == "substitution");
    }
    return partitioned(terms, value, position, operation == "partition_family");
}

Term restrict(const Terms &terms, const Index &index, const Left &set, const Right &keys, std::string_view operation) {
    const auto value = unordered(set.term);
    const auto key_set = unordered(keys.term);
    if (atom_is(value.type_, "_")) {
        return operation == "partition" ? tuple(terms, {set.term, set.term}) : set.term;
    }
    const auto position = coordinate(index, value.type_);
    match(terms, {at(value.type_, position)}, {key_set.type_});
    const auto parts = split(elements(value.data_), elements(key_set.data_), position);
    return restriction_result(terms, parts, value.type_, operation);
}
} // namespace clause::runtime::builtins::sofs

namespace clause::runtime {
// Publish integer SetFun workers and the two public domain-restriction shortcuts.
std::span<const BuiltinEntry> sofs_selectors_builtins() noexcept { return builtins::sofs::SELECTORS; }
} // namespace clause::runtime
