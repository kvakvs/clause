#include "sofs.hpp"

namespace clause::runtime::builtins::sofs {
namespace {
// Check external callback and restriction-key types before invoking callbacks on data.
Term match_types(ProcessContext &context, const Left &left, const Right &right) {
    Terms terms(context);
    match(terms, left, right);
    return terms.boolean(true);
}

// Wrap a callback argument according to its actual external shape.
Term element(ProcessContext &context, const Left &value, const Right &type) {
    return wrap(Terms(context), {value.term, type.term, value.term.is_list()});
}

// Validate a callback's returned sofs record and refine the shared output type.
Term accept(ProcessContext &context, const Left &value, const Right &type) {
    Terms terms(context);
    if (!is_record(value.term)) {
        bad_argument();
    }
    const auto set = record(value.term);
    const auto next = unified(terms, {type.term}, {full_type(terms, set)});
    if (!set.unordered_ && !same(next, set.type_)) {
        const auto payload = tuple(terms, {terms.atom("case_clause"), next});
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = payload.word()};
    }
    return tuple(terms, {set.data_, next});
}

// Number structural tuple leaves for the external SetFun order probe.
Term number(const Terms &terms, const Term &type, std::int64_t &next) {
    if (!type.is_tuple()) {
        return terms.integer(next++);
    }
    auto children = need(type.tuple_elements());
    for (auto &child : children) {
        child = number(terms, child, next);
    }
    return terms.tuple(children);
}

// Produce the numbered input used to validate an external function's ordering behavior.
Term numbered(ProcessContext &context, const Term &type) {
    auto next = std::int64_t{1};
    return number(Terms(context), type, next);
}

// Flatten tuple and list results in the same traversal order as the external function probe.
void flatten(const Term &value, Items &out) {
    if (value.is_tuple()) {
        for (const auto &child : need(value.tuple_elements())) {
            flatten(child, out);
        }
        return;
    }
    if (value.is_list()) {
        for (const auto &child : elements(value)) {
            flatten(child, out);
        }
        return;
    }
    out.push_back(value);
}

// Test whether numbered outputs require sorting, raising badarith for invalid arithmetic probes.
Term order_probe(ProcessContext &context, const Term &value) {
    Terms terms(context);
    Items leaves;
    flatten(value, leaves);
    auto next = terms.integer(1);
    for (const auto &leaf : leaves) {
        if (compare(leaf, next) > 0) {
            return terms.boolean(true);
        }
        const auto increment = collections::add(context, leaf, terms.integer(1));
        if (compare(increment, next) > 0) {
            next = increment;
        }
    }
    return terms.boolean(false);
}

// Assemble callback substitution or family-projection results without reinterpreting external values.
Term finish(ProcessContext &context, const Value &operation, const Left &source, const Right &pairs,
            const Index &type) {
    Terms terms(context);
    const auto set = unordered(source.term);
    const auto values = elements(pairs.term);
    if (atom_is(operation.term, "family_projection")) {
        const auto family = family_type(set);
        return make_set(terms, values, {tuple(terms, {family[0], type.term})});
    }
    if (atom_is(operation.term, "substitution")) {
        return make_set(terms, values, {tuple(terms, {set.type_, type.term})});
    }
    Items inverse;
    for (const auto &pair : values) {
        inverse.push_back(tuple(terms, {at(pair, 1), at(pair, 0)}));
    }
    std::ranges::reverse(inverse);
    const auto family = group(terms, std::move(inverse));
    return make_set(terms, family, {tuple(terms, {type.term, terms.list(Items{set.type_})})});
}

// Split consecutive callback mappings by the forward traversal of an ordered key set.
std::array<Items, 2> filtered(const Items &pairs, std::span<const Term> keys) {
    std::array<Items, 2> parts;
    std::size_t position = 0;
    for (const auto &pair : pairs) {
        const auto key = at(pair, 0);
        while (position < keys.size() && compare(key, keys[position]) > 0) {
            ++position;
        }
        const auto found = position < keys.size() && compare(key, keys[position]) == 0;
        parts[found ? 0 : 1].push_back(at(pair, 1));
    }
    return parts;
}

// Invert callback mappings, retaining the external ordering probe's choice of sorting.
Items inverse_pairs(const Terms &terms, const Term &pairs, bool sort) {
    Items result;
    for (const auto &pair : elements(pairs)) {
        result.push_back(tuple(terms, {at(pair, 1), at(pair, 0)}));
    }
    if (sort) {
        std::ranges::reverse(result);
        std::stable_sort(result.begin(), result.end(), [](const Term &a, const Term &b) { return compare(a, b) < 0; });
    }
    return result;
}

// Restrict callback mappings after checking their types, retaining every input representative.
Term filter_mapping(const Terms &terms, const Value &operation, const Left &mapping, const Right &keys, bool sort) {
    const auto relation = unordered(mapping.term);
    const auto key_set = unordered(keys.term);
    const auto type = relation_type(terms, relation);
    match(terms, {type[1]}, {key_set.type_});
    auto parts = filtered(inverse_pairs(terms, relation.data_, sort), elements(key_set.data_));
    if (sort || atom_is(operation.term, "partition")) {
        for (auto &part : parts) {
            std::stable_sort(part.begin(), part.end(), [](const Term &a, const Term &b) { return compare(a, b) < 0; });
        }
    }
    const auto yes = make_set(terms, parts[0], {type[0]});
    if (atom_is(operation.term, "restriction")) {
        return yes;
    }
    const auto no = make_set(terms, parts[1], {type[0]});
    return atom_is(operation.term, "drestriction") ? no : tuple(terms, {yes, no});
}

// Internal wrapped functions always sort their inverse mappings before restriction.
Term filter(ProcessContext &context, const Value &operation, const Left &mapping, const Right &keys) {
    return filter_mapping(Terms(context), operation, mapping, keys, true);
}

// External restriction follows the numbered order probe even for data-dependent callbacks.
Term filter_external(ProcessContext &context, const Value &operation, const Left &mapping, const Right &keys,
                     const Index &sort) {
    return filter_mapping(Terms(context), operation, mapping, keys, need(sort.term.boolean_value()));
}

// External partitions group adjacent callback results after applying the numbered ordering probe.
Term finish_external(ProcessContext &context, const Value &operation, const Left &source, const Right &pairs,
                     const Index &type, const OtherIndex &sort) {
    if (atom_is(operation.term, "substitution")) {
        return finish(context, operation, source, pairs, type);
    }
    Terms terms(context);
    const auto set = unordered(source.term);
    const auto ordered = need(sort.term.boolean_value());
    const auto inverse = inverse_pairs(terms, pairs.term, ordered);
    const auto groups = group_adjacent(terms, inverse, ordered);
    return make_set(terms, groups, {tuple(terms, {type.term, terms.list(Items{set.type_})})});
}

constexpr std::array CALLBACKS{
    typed_entry<match_types>("clause_sofs", "match_types"),
    typed_entry<element>("clause_sofs", "element"),
    typed_entry<accept>("clause_sofs", "accept"),
    typed_entry<numbered>("clause_sofs", "numbered"),
    typed_entry<order_probe>("clause_sofs", "order_probe"),
    typed_entry<finish>("clause_sofs", "finish"),
    typed_entry<filter>("clause_sofs", "filter"),
    typed_entry<filter_external>("clause_sofs", "filter_external"),
    typed_entry<finish_external>("clause_sofs", "finish_external"),
};
} // namespace
} // namespace clause::runtime::builtins::sofs

namespace clause::runtime {
// Publish data-only helpers used by sofs' Erlang callback loops.
std::span<const BuiltinEntry> sofs_callbacks_builtins() noexcept { return builtins::sofs::CALLBACKS; }
} // namespace clause::runtime
