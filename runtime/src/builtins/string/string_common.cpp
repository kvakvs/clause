#include "../../terms/bitstrings.hpp"
#include "../collections/collections.hpp"
#include "../lists/list_walk.hpp"
#include "../text.hpp"
#include "../unicode/chardata.hpp"
#include "string.hpp"
#include <array>

// The helpers string.erl's functions share (plan 21 step LIB4).
namespace clause::runtime::builtins::string {
Term Terms::nil() const { return need(TermFactory(context_).nil()); }

Term Terms::cons(const Term &head, const Term &tail) const { return need(TermFactory(context_).cons(head, tail)); }

Term Terms::integer(std::int64_t value) const { return need(TermFactory(context_).integer(value)); }

Term Terms::list(std::span<const Term> items) const { return need(TermFactory(context_).list(items)); }

Term Terms::list(std::span<const Term> items, const Term &tail) const {
    return need(TermFactory(context_).list(items, tail));
}

Term Terms::atom(const AtomName name) const { return builtins::atom(context_, name); }

Term Terms::boolean(bool value) const { return need(TermFactory(context_).boolean(value)); }

Term Terms::tuple(std::span<const Term> items) const { return need(TermFactory(context_).tuple(items)); }

Term Terms::binary(std::span<const std::byte> bytes) const { return need(TermFactory(context_).binary(bytes)); }

Term Terms::utf8(std::u32string_view codes) const {
    std::string text;
    for (const auto code : codes) {
        encode(code, text);
    }
    return binary(std::as_bytes(std::span(text)));
}

Term Terms::slice(const Term &binary, std::size_t from, std::size_t count) const {
    return need(detail::BitAccess::slice(context_.heap(), binary, from * 8, count * 8));
}

Term Terms::rev(const Term &term) const {
    if (term.is_integer()) {
        return term;
    }
    std::vector<Term> items;
    Term rest = term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        items.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    if (items.size() == 1 && items[0].is_binary()) {
        return items[0];
    }
    std::ranges::reverse(items);
    return list(items);
}

Term Terms::append(const Term &cluster, const Term &str) const {
    const bool empty_binary = str.is_binary() && str.bit_size_unchecked() == 0;
    if (empty_binary) {
        if (cluster.is_integer()) {
            const std::array one{cluster};
            return list(one);
        }
        return cluster;
    }
    if (str.is_binary()) {
        const std::array pair{cluster, str};
        return list(pair);
    }
    if (cluster.is_integer()) {
        return cons(cluster, str);
    }
    std::vector<Term> items;
    Term rest = cluster;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        items.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return list(items, str);
}

Term Terms::stack(const Term &bin, const Term &st) const {
    if (st.is_nil()) {
        return bin;
    }
    if ((bin.is_binary() && bin.bit_size_unchecked() == 0) || bin.is_nil()) {
        return st;
    }
    return cons(bin, st);
}

Term Terms::btoken(const Term &part, const TokenArg &reversed) const {
    const auto &token = reversed.term;
    if (token.is_nil()) {
        return part;
    }
    if (token.is_cons() && token.head_unchecked().is_integer() && token.tail_unchecked().is_nil()) {
        std::string text;
        encode(static_cast<char32_t>(*small_of(token.head_unchecked())), text);
        auto bytes =
            std::vector<std::byte>(std::as_bytes(std::span(text)).begin(), std::as_bytes(std::span(text)).end());
        const auto tail = need(part.binary_bytes());
        bytes.insert(bytes.end(), tail.begin(), tail.end());
        return binary(bytes);
    }
    std::vector<Term> items;
    Term rest = token;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        items.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    std::ranges::reverse(items);
    if (need(part.bit_size()) == 0) {
        return list(items);
    }
    const std::array pair{list(items), part};
    return list(pair);
}

namespace {
// A conversion's result, or OTP's {error | incomplete, Converted, Rest} tuple with Converted in the result's form.
Term converted(const Terms &terms, const unicode::Conversion &conversion, bool as_binary) {
    auto done = as_binary ? terms.utf8(conversion.codes) : [&] {
        std::vector<Term> items;
        for (const auto code : conversion.codes) {
            items.push_back(terms.integer(static_cast<std::int64_t>(code)));
        }
        return terms.list(items);
    }();
    if (conversion.outcome == unicode::Conversion::Outcome::done) {
        return done;
    }
    const auto tag = conversion.outcome == unicode::Conversion::Outcome::error ? "error" : "incomplete";
    const std::array items{terms.atom(tag), done, conversion.rest};
    return terms.tuple(items);
}
} // namespace

Term Terms::characters_to_list(const Term &data) const {
    return converted(*this, unicode::convert(context_, data, unicode::Encoding{}, false), false);
}

Term Terms::characters_to_binary(const Term &data) const {
    return converted(*this, unicode::convert(context_, data, unicode::Encoding{}, false), true);
}

Form form_of(const Term &data) {
    if (data.is_cons()) {
        const auto head = data.head_unchecked();
        return head.is_integer() ? Form::point : head.is_binary() ? Form::binary_head : Form::list;
    }
    if (data.is_nil()) {
        return Form::list;
    }
    return data.is_binary() ? Form::binary : Form::other;
}

bool ascii_pair_at(const Term &data) {
    if (!data.is_cons()) {
        return false;
    }
    const auto tail = data.tail_unchecked();
    return tail.is_cons() && ascii_pair(data.head_unchecked(), tail.head_unchecked());
}

std::optional<std::int64_t> small_of(const Term &term) {
    const auto value = term.is_integer() ? term.integer_value() : std::unexpected(TermError::wrong_type);
    return value ? std::optional{*value} : std::nullopt;
}

bool is_char(const Term &term, std::int64_t code) { return small_of(term) == code; }

bool ascii_pair(const Term &cp1, const Term &cp2) {
    const auto first = small_of(cp1);
    const auto second = small_of(cp2);
    return first && second && *first >= 0 && *first < 256 && *second >= 0 && *second < 256 && *first != '\r';
}

bool same(const Term &left, const Term &right) { return collections::same(left, right); }

bool is_empty(const Term &data) {
    std::vector<Term> pending{data};
    while (!pending.empty()) {
        const auto term = pending.back();
        pending.pop_back();
        if (term.is_nil() || (term.is_binary() && term.bit_size_unchecked() == 0)) {
            continue;
        }
        if (!term.is_cons()) {
            return false;
        }
        pending.push_back(term.tail_unchecked());
        pending.push_back(term.head_unchecked());
    }
    return true;
}

std::u32string cluster_codes(const Term &cluster) {
    std::u32string codes;
    if (cluster.is_integer()) {
        codes.push_back(static_cast<char32_t>(*small_of(cluster)));
        return codes;
    }
    for (Term rest = cluster; rest.is_cons(); rest = rest.tail_unchecked()) {
        codes.push_back(static_cast<char32_t>(*small_of(rest.head_unchecked())));
    }
    return codes;
}

std::span<const std::byte> binary_bytes(const Term &binary) {
    const auto bytes = detail::trusted_binary_bytes(binary.word());
    if (!bytes) {
        bad_argument();
    }
    return *bytes;
}

std::pair<char32_t, std::size_t> first_char(const Term &binary) {
    const auto head = utf8_head(binary);
    if (!head) {
        bad_argument();
    }
    return *head;
}

std::optional<std::pair<char32_t, std::size_t>> utf8_head(const Term &binary) {
    const auto bytes = binary_bytes(binary);
    if (bytes.empty()) {
        return std::nullopt;
    }
    std::size_t at = 0;
    const auto code = next_utf8(bytes, at);
    return code ? std::optional{std::pair{*code, at}} : std::nullopt;
}

[[noreturn]] void badarg_rest(ProcessContext &context, const Term &rest) {
    const Terms terms(context);
    const std::array reason{terms.atom("badarg"_atom), rest};
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = terms.tuple(reason).word()};
}

[[noreturn]] void badarg_error(ProcessContext &context, const Term &error_tuple) {
    badarg_rest(context, need(error_tuple.tuple_element(1)));
}

[[noreturn]] void case_clause(const Term &term) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::case_clause, .payload = term.word()};
}
} // namespace clause::runtime::builtins::string
