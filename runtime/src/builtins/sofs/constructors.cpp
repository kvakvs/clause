#include "sofs.hpp"

namespace clause::runtime::builtins::sofs {
namespace {
// Infer the complete shape of an external term.
Term from_term1(ProcessContext &context, const Term &value) {
    Terms terms(context);
    const auto any = terms.atom("_");
    return setify(terms, {value}, {value.is_list() ? terms.list(Items{any}) : any});
}

// Normalize an external term under an explicitly validated public type.
Term from_term2(ProcessContext &context, const Left &value, const Right &type) {
    if (!valid_type(type.term)) {
        bad_argument();
    }
    return setify(Terms(context), value, type);
}

// Trust external data and its declared type without sorting or validation.
Term from_external(ProcessContext &context, const Left &value, const Right &type) {
    const auto member = list_type(type.term);
    return wrap(Terms(context), {value.term, member.value_or(type.term), member.has_value()});
}

// Construct the untyped empty set.
Term empty_set(ProcessContext &context) { return empty(Terms(context)); }

// Recognize the grammar of public sofs types.
Term is_type(ProcessContext &context, const Term &value) { return Terms(context).boolean(valid_type(value)); }

// Construct an atomic set without inspecting the elements' shapes.
Term set1(ProcessContext &context, const Term &value) {
    Terms terms(context);
    return make_set(terms, unique(elements(value)), {terms.atom("atom")});
}

// Construct an unordered set under a singleton element type.
Term set2(ProcessContext &context, const Left &value, const Right &type) {
    if (!list_type(type.term)) {
        bad_argument();
    }
    return setify(Terms(context), value, type);
}

// Assemble a tuple of records without unifying their independent component types.
Term ordered_sets(const Terms &terms, const Term &value) {
    Items data;
    Items types;
    for (const auto &item : need(value.tuple_elements())) {
        if (!is_record(item)) {
            bad_argument();
        }
        auto set = record(item);
        data.push_back(set.data_);
        types.push_back(full_type(terms, set));
    }
    return wrap(terms, {terms.tuple(data), terms.tuple(types), false});
}

// Assemble an unordered collection of records, merging compatible types.
Term from_sets(ProcessContext &context, const Term &value) {
    Terms terms(context);
    if (value.is_tuple()) {
        return ordered_sets(terms, value);
    }
    auto type = terms.atom("_");
    Items data;
    for (const auto &item : elements(value)) {
        if (!is_record(item)) {
            bad_argument();
        }
        const auto set = record(item);
        type = unified(terms, {full_type(terms, set)}, {type});
        data.push_back(set.data_);
    }
    std::ranges::reverse(data);
    return make_set(terms, unique(std::move(data)), {type});
}

// Construct a relation from an integer arity or an explicit singleton type.
bool atomic_components(const Term &type) {
    if (!type.is_tuple() || !valid_type(type)) {
        return false;
    }
    const auto values = need(type.tuple_elements());
    return std::ranges::all_of(values, [](const Term &item) { return item.is_atom(); });
}

// Normalize structured relation elements, refining both current and original types after each item.
Term structured_relation(const Terms &terms, const Left &value, const Right &type) {
    auto current = type.term;
    Items result;
    for (const auto &item : elements(value.term)) {
        const auto normalized = normalize(terms, {item}, {current}, {current});
        current = normalized.type_;
        result.push_back(normalized.data_);
    }
    if (!current.is_tuple() && !atom_is(current, "_")) {
        bad_argument();
    }
    std::ranges::reverse(result);
    return make_set(terms, unique(std::move(result)), {current});
}

// Construct a relation from an integer arity or an explicit singleton type.
Term relation2(ProcessContext &context, const Left &value, const Right &type) {
    Terms terms(context);
    auto constraint = type.term;
    if (constraint.is_integer()) {
        const auto count = small(constraint.word());
        if (!count || *count < 1 || static_cast<std::uint64_t>(*count) > MAX_TUPLE_ARITY) {
            bad_argument();
        }
        constraint = terms.list(Items{terms.tuple(Items(static_cast<std::size_t>(*count), terms.atom("atom")))});
    }
    const auto member = list_type(constraint);
    if (!member) {
        bad_argument();
    }
    const auto result =
        atomic_components(*member) ? setify(terms, value, {constraint}) : structured_relation(terms, value, {*member});
    const auto set = unordered(result);
    if (!atom_is(set.type_, "_") && !set.type_.is_tuple()) {
        bad_argument();
    }
    return result;
}

// Infer relation arity from its first tuple, using binary arity for an empty input.
Term relation1(ProcessContext &context, const Term &value) {
    Terms terms(context);
    auto size = std::size_t{2};
    if (!value.is_nil()) {
        if (!value.is_cons() || !value.head_unchecked().is_tuple()) {
            bad_argument();
        }
        size = value.head_unchecked().tuple_size_unchecked();
    }
    return relation2(context, {value}, {terms.integer(static_cast<std::int64_t>(size))});
}

// Construct a function, checking uniqueness only after canonicalizing all pairs.
Term function2(ProcessContext &context, const Left &value, const Right &type) {
    if (!valid_type(type.term)) {
        bad_argument();
    }
    Terms terms(context);
    const auto result = relation2(context, value, type);
    const auto set = unordered(result);
    static_cast<void>(relation_type(terms, set));
    check_function(terms, elements(set.data_));
    return result;
}

// Default function type has two atomic components.
Term function1(ProcessContext &context, const Term &value) {
    Terms terms(context);
    return function2(context, {value}, {terms.list(Items{tuple(terms, {terms.atom("atom"), terms.atom("atom")})})});
}

// Construct a family; nested member sets are normalized before checking functionality.
Term atomic_family(const Terms &terms, const Left &value, const Right &type) {
    auto values = elements(value.term);
    std::stable_sort(values.begin(), values.end(), [](const Term &a, const Term &b) { return compare(a, b) < 0; });
    Items result;
    for (const auto &pair : values) {
        if (!pair.is_tuple() || pair.tuple_size_unchecked() != 2) {
            bad_argument();
        }
        const auto normalized = tuple(terms, {at(pair, 0), terms.list(unique(elements(at(pair, 1))))});
        if (result.empty() || compare(at(result.back(), 0), at(pair, 0)) != 0) {
            result.push_back(normalized);
        } else if (compare(at(result.back(), 1), at(normalized, 1)) != 0) {
            error(terms, "bad_function");
        }
    }
    return make_set(terms, result, {type.term});
}

// Decide whether a valid family type uses the atomic-key, atomic-member constructor path.
bool atomic_family_type(const Term &type) {
    if (!type.is_tuple() || type.tuple_size_unchecked() != 2 || !at(type, 0).is_atom()) {
        return false;
    }
    const auto member = list_type(at(type, 1));
    return member && member->is_atom();
}

// Construct a family; nested member sets are normalized before checking functionality.
Term family2(ProcessContext &context, const Left &value, const Right &type) {
    if (!valid_type(type.term)) {
        bad_argument();
    }
    Terms terms(context);
    const auto member = list_type(type.term);
    if (!member) {
        bad_argument();
    }
    if (atomic_family_type(*member)) {
        return atomic_family(terms, value, {*member});
    }
    const auto result = structured_relation(terms, value, {*member});
    const auto set = unordered(result);
    if (!atom_is(set.type_, "_")) {
        static_cast<void>(family_type(set));
    }
    check_function(terms, elements(set.data_));
    return result;
}

// Default family type has atomic keys and atomic members.
Term family1(ProcessContext &context, const Term &value) {
    Terms terms(context);
    const auto type = tuple(terms, {terms.atom("atom"), terms.list(Items{terms.atom("atom")})});
    return family2(context, {value}, {terms.list(Items{type})});
}

constexpr std::array CONSTRUCTORS{
    typed_entry<from_term1>("sofs", "from_term"),
    typed_entry<from_term2>("sofs", "from_term"),
    typed_entry<from_external>("sofs", "from_external"),
    typed_entry<empty_set>("sofs", "empty_set"),
    typed_entry<is_type>("sofs", "is_type"),
    typed_entry<set1>("sofs", "set"),
    typed_entry<set2>("sofs", "set"),
    typed_entry<from_sets>("sofs", "from_sets"),
    typed_entry<relation1>("sofs", "relation"),
    typed_entry<relation2>("sofs", "relation"),
    typed_entry<function1>("sofs", "a_function"),
    typed_entry<function2>("sofs", "a_function"),
    typed_entry<family1>("sofs", "family"),
    typed_entry<family2>("sofs", "family"),
};
} // namespace
} // namespace clause::runtime::builtins::sofs

namespace clause::runtime {
// Publish sofs constructors separately from the algebra and relation entry tables.
std::span<const BuiltinEntry> sofs_constructors_builtins() noexcept { return builtins::sofs::CONSTRUCTORS; }
} // namespace clause::runtime
