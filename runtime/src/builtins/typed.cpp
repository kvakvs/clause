#include "typed.hpp"

namespace clause::runtime::builtins {
TermResult<std::optional<detail::Integer>> Argument<detail::Integer>::convert(const Term &term) {
    if (!term.is_integer()) {
        return std::nullopt;
    }
    return detail::integer_read(term).transform([](detail::Integer value) { return std::optional{std::move(value)}; });
}

TermResult<std::optional<double>> Argument<double>::convert(const Term &term) {
    if (!term.is_float()) {
        return std::nullopt;
    }
    return std::optional{term.float_value_unchecked()};
}

TermResult<std::optional<ListArgument>> Argument<ListArgument>::convert(const Term &term) {
    ListArgument result{term, {}};
    auto list = term;
    while (list.is_cons()) {
        result.elements.push_back(list.head_unchecked());
        list = list.tail_unchecked();
    }
    return list.is_nil() ? std::optional{std::move(result)} : std::nullopt;
}

TermResult<std::optional<TupleArgument>> Argument<TupleArgument>::convert(const Term &term) {
    if (!term.is_tuple()) {
        return std::nullopt;
    }
    return term.tuple_elements().transform(
        [&](std::vector<Term> elements) { return std::optional{TupleArgument{term, std::move(elements)}}; });
}

TermResult<std::optional<BinaryArgument>> Argument<BinaryArgument>::convert(const Term &term) {
    if (!term.is_binary()) {
        return std::nullopt;
    }
    return term.binary_bytes().transform(
        [](std::vector<std::byte> bytes) { return std::optional{BinaryArgument{std::move(bytes)}}; });
}

TermResult<std::optional<AtomArgument>> Argument<AtomArgument>::convert(const Term &term) {
    if (!term.is_atom()) {
        return std::nullopt;
    }
    return std::optional{AtomArgument{term, term.atom_spelling_unchecked()}};
}
} // namespace clause::runtime::builtins
