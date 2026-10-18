#include "sofs.hpp"

namespace clause::runtime::builtins::sofs {
namespace {
// Infer a structural type independently for the first value at a wildcard position.
Normalized infer(const Terms &terms, const Term &value);

// Normalize a list with a shared evolving member type and a fixed original constraint.
Normalized normalize_list(const Terms &terms, const Value &value, const Left &type, const Right &original) {
    auto current = type.term;
    Items result;
    for (const auto &item : elements(value.term)) {
        auto normalized = normalize(terms, {item}, {current}, original);
        current = normalized.type_;
        result.push_back(normalized.data_);
    }
    std::ranges::reverse(result);
    return {terms.list(unique(std::move(result))), terms.list(Items{current})};
}

// Normalize tuple components using their own evolving and original types.
Normalized normalize_tuple(const Terms &terms, const Value &value, const Left &type, const Right &original) {
    if (!value.term.is_tuple() || !type.term.is_tuple()) {
        bad_argument();
    }
    auto data = need(value.term.tuple_elements());
    auto types = need(type.term.tuple_elements());
    if (data.empty() || data.size() != types.size()) {
        bad_argument();
    }
    for (std::size_t i = 0; i < data.size(); ++i) {
        auto initial = atom_is(original.term, "_") ? original.term : at(original.term, i);
        auto normalized = normalize(terms, {data[i]}, {types[i]}, {initial});
        data[i] = normalized.data_;
        types[i] = normalized.type_;
    }
    return {terms.tuple(data), terms.tuple(types)};
}

Normalized infer(const Terms &terms, const Term &value) {
    const auto any = terms.atom("_");
    if (value.is_list()) {
        return normalize_list(terms, {value}, {any}, {any});
    }
    if (value.is_tuple()) {
        auto values = need(value.tuple_elements());
        if (values.empty()) {
            bad_argument();
        }
        Items types;
        for (auto &item : values) {
            const auto normalized = infer(terms, item);
            item = normalized.data_;
            types.push_back(normalized.type_);
        }
        return {terms.tuple(values), terms.tuple(types)};
    }
    return {value, terms.atom("atom")};
}

// Detect explicit tuple-only types for the constructor's allocation-free leaf validation path.
bool tuple_only(const Term &type) {
    if (type.is_atom()) {
        return true;
    }
    if (!type.is_tuple()) {
        return false;
    }
    const auto children = need(type.tuple_elements());
    return std::ranges::all_of(children, tuple_only);
}

// Validate explicit tuple structure while preserving atomic leaves verbatim.
void check_tuple(const Left &value, const Right &type) {
    if (type.term.is_atom()) {
        return;
    }
    if (!value.term.is_tuple() || value.term.tuple_size_unchecked() != type.term.tuple_size_unchecked()) {
        bad_argument();
    }
    for (std::size_t i = 0; i < type.term.tuple_size_unchecked(); ++i) {
        check_tuple({at(value.term, i)}, {at(type.term, i)});
    }
}
} // namespace

Normalized normalize(const Terms &terms, const Value &value, const Left &type, const Right &original) {
    if (atom_is(type.term, "_")) {
        return infer(terms, value.term);
    }
    if (type.term.is_atom()) {
        if (atom_is(original.term, "_") && (value.term.is_list() || value.term.is_tuple())) {
            bad_argument();
        }
        return {value.term, type.term};
    }
    if (const auto member = list_type(type.term)) {
        const auto initial = atom_is(original.term, "_") ? original.term : *list_type(original.term);
        return normalize_list(terms, value, {*member}, {initial});
    }
    return normalize_tuple(terms, value, type, original);
}

Term setify(const Terms &terms, const Left &data, const Right &type) {
    if (const auto member = list_type(type.term)) {
        if (member->is_atom() && !atom_is(*member, "_")) {
            return make_set(terms, unique(elements(data.term)), {*member});
        }
        if (member->is_tuple() && member->tuple_size_unchecked() != 0 && tuple_only(*member)) {
            const auto values = elements(data.term);
            for (const auto &value : values) {
                check_tuple({value}, {*member});
            }
            return make_set(terms, unique(values), {*member});
        }
        auto result = normalize_list(terms, {data.term}, {*member}, {*member});
        return wrap(terms, {result.data_, *list_type(result.type_), true});
    }
    auto result = normalize(terms, {data.term}, {type.term}, {type.term});
    return wrap(terms, {result.data_, result.type_, false});
}
} // namespace clause::runtime::builtins::sofs
