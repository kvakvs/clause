#include "chardata.hpp"
#include <array>
#include <terms.hpp>

// The unicode module's builtins (plan step 41Y1, docs/builtins.md#unicode-module): characters_to_binary/1,2,3,
// characters_to_list/1,2, bin_is_7bit/1, bom_to_encoding/1 and encoding_to_bom/1.
namespace clause::runtime::builtins::unicode {
namespace {
using Outcome = Conversion::Outcome;
constexpr Encoding UTF8{.kind = Encoding::Kind::utf8};

// The converted part as a term: a binary in `output`, or a list of code points without one.
Term converted(ProcessContext &context, const std::u32string &codes, std::optional<Encoding> output) {
    TermFactory factory(context);
    if (output) {
        Bytes bytes;
        for (const auto code : codes) {
            encode(code, *output, bytes);
        }
        return need(factory.binary(bytes));
    }
    std::vector<Term> items;
    items.reserve(codes.size());
    for (const auto code : codes) {
        items.push_back(need(factory.integer(code)));
    }
    return need(factory.list(items));
}

// The result of a conversion: the converted term, or {error | incomplete, Converted, Rest}.
TermResult<Term> result(ProcessContext &context, const Conversion &conversion, std::optional<Encoding> output) {
    auto value = converted(context, conversion.codes, output);
    if (conversion.outcome == Outcome::done) {
        return value;
    }
    TermFactory factory(context);
    const std::array parts{need(factory.atom(conversion.outcome == Outcome::error ? "error"_atom : "incomplete"_atom)),
                           value, conversion.rest};
    return factory.tuple(parts);
}

// characters_to_binary(Data, InEncoding, OutEncoding).
TermResult<Term> to_binary3(ProcessContext &context, const Term &data, const EncodingArgument &input,
                            const EncodingArgument &output) {
    const auto in = encoding(input.term);
    const auto out = encoding(output.term);
    return result(context, convert(context, data, in, out.kind == Encoding::Kind::latin1), out);
}

TermResult<Term> to_binary2(ProcessContext &context, const Term &data, const EncodingArgument &input) {
    return result(context, convert(context, data, encoding(input.term), false), UTF8);
}

TermResult<Term> to_binary1(ProcessContext &context, const Term &data) {
    return result(context, convert(context, data, UTF8, false), UTF8);
}

// characters_to_list(Data, InEncoding) and characters_to_list(Data).
TermResult<Term> to_list2(ProcessContext &context, const Term &data, const EncodingArgument &input) {
    return result(context, convert(context, data, encoding(input.term), false), std::nullopt);
}

TermResult<Term> to_list1(ProcessContext &context, const Term &data) {
    return result(context, convert(context, data, UTF8, false), std::nullopt);
}

// bin_is_7bit(Binary): whether it is a binary whose every byte is below 128.
TermResult<Term> bin_is_7bit(ProcessContext &context, const Term &binary) {
    const auto bytes = binary.is_binary() ? need(binary.binary_bytes()) : Bytes{std::byte{128}};
    return TermFactory(context).boolean(
        std::ranges::all_of(bytes, [](std::byte byte) { return std::to_integer<unsigned>(byte) < 128; }));
}

// The encoding term of `encoding`: utf8, latin1 or {utf16 | utf32, big | little}.
Term encoding_term(ProcessContext &context, Encoding encoding) {
    TermFactory factory(context);
    if (encoding.kind == Encoding::Kind::utf8 || encoding.kind == Encoding::Kind::latin1) {
        return need(factory.atom(encoding.kind == Encoding::Kind::utf8 ? "utf8"_atom : "latin1"_atom));
    }
    const std::array parts{need(factory.atom(encoding.kind == Encoding::Kind::utf16 ? "utf16"_atom : "utf32"_atom)),
                           need(factory.atom(encoding.little ? "little"_atom : "big"_atom))};
    return need(factory.tuple(parts));
}

// bom_to_encoding(Bin): {Encoding, BomLength} of the byte order mark Bin starts with, {latin1, 0} without one.
TermResult<Term> bom_to_encoding(ProcessContext &context, const BinaryArgument &binary) {
    // utf32 little first: its mark starts with utf16 little's.
    static constexpr std::array candidates{
        Encoding{.kind = Encoding::Kind::utf8},
        Encoding{.kind = Encoding::Kind::utf32, .little = true},
        Encoding{.kind = Encoding::Kind::utf32, .little = false},
        Encoding{.kind = Encoding::Kind::utf16, .little = true},
        Encoding{.kind = Encoding::Kind::utf16, .little = false},
    };
    Encoding found{.kind = Encoding::Kind::latin1};
    std::size_t length = 0;
    for (const auto candidate : candidates) {
        const auto mark = byte_order_mark(candidate);
        if (binary.bytes.size() >= mark.size() &&
            std::ranges::equal(mark, std::span(binary.bytes).first(mark.size()))) {
            found = candidate;
            length = mark.size();
            break;
        }
    }
    TermFactory factory(context);
    const std::array parts{encoding_term(context, found), need(factory.integer(static_cast<std::int64_t>(length)))};
    return factory.tuple(parts);
}

// encoding_to_bom(Encoding): the byte order mark of the encoding (empty for latin1); an unknown encoding has no clause,
// as in OTP.
TermResult<Term> encoding_to_bom(ProcessContext &context, const Term &argument) {
    try {
        return TermFactory(context).binary(byte_order_mark(encoding(argument)));
    } catch (const BuiltinFailure &failure) {
        if (failure.term) {
            throw;
        }
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::function_clause};
    }
}

constexpr std::array UNICODE_BUILTINS{
    typed_entry<to_binary1>("unicode", "characters_to_binary"),
    typed_entry<to_binary2>("unicode", "characters_to_binary"),
    typed_entry<to_binary3>("unicode", "characters_to_binary"),
    typed_entry<to_list1>("unicode", "characters_to_list"),
    typed_entry<to_list2>("unicode", "characters_to_list"),
    // OTP's internal helpers (plan 21 step LIB4B): characters_to_list/2 and characters_to_binary/2 under their names.
    typed_entry<to_list2>("unicode", "characters_to_list_int"),
    typed_entry<to_binary2>("unicode", "characters_to_binary_int"),
    typed_entry<bin_is_7bit>("unicode", "bin_is_7bit"),
    typed_entry<bom_to_encoding>("unicode", "bom_to_encoding"),
    typed_entry<encoding_to_bom>("unicode", "encoding_to_bom"),
};
} // namespace
} // namespace clause::runtime::builtins::unicode

namespace clause::runtime {
std::span<const BuiltinEntry> unicode_builtins() noexcept { return builtins::unicode::UNICODE_BUILTINS; }
} // namespace clause::runtime
