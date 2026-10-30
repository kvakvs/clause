#include "../../terms/bitstrings.hpp"
#include "../text.hpp"
#include "conversion_text.hpp"
#include "terms.hpp"
#include <array>
#include <clause/abi/equality.hpp>
#include <clause/runtime/atoms.hpp>
#include <vector>

// The binary, atom and number conversions of plan step 41D (docs/builtins.md): the binary twins of the list
// conversions, existing-atom lookups, float text, bitstring lists, iolist sizes and identity text. They run to
// completion: their work is bounded by the size of their arguments.
namespace clause::runtime::builtins {
namespace {
using detail::Integer;

// Whether an encoding argument is latin1; the atoms utf8 and unicode are UTF-8, anything else badarg.
bool latin1(const Term &encoding) {
    const auto name = encoding.is_atom() ? encoding.atom_spelling_unchecked() : std::string_view{};
    if (name == "latin1") {
        return true;
    }
    if (name != "utf8" && name != "unicode") {
        bad_argument();
    }
    return false;
}

// A binary of the bytes of `text`.
TermResult<Term> binary_of(ProcessContext &context, std::string_view text) {
    const auto *data = reinterpret_cast<const std::byte *>(text.data());
    return TermFactory(context).binary(std::span(data, text.size()));
}

// The bytes of a binary as unsigned characters.
std::span<const unsigned char> characters(const BinaryArgument &binary) {
    return {reinterpret_cast<const unsigned char *>(binary.bytes.data()), binary.bytes.size()};
}

// atom_to_binary(Atom, Encoding): latin1 has no byte for a character past 255 (badarg).
TermResult<Term> atom_to_binary2(ProcessContext &context, const AtomArgument &atom, const Term &encoding) {
    if (!latin1(encoding)) {
        return binary_of(context, atom.spelling);
    }
    std::string bytes;
    for (const auto code : code_points(atom.spelling)) {
        if (code > 255) {
            bad_argument();
        }
        bytes.push_back(static_cast<char>(code));
    }
    return binary_of(context, bytes);
}

// atom_to_binary(Atom): UTF-8.
TermResult<Term> atom_to_binary1(ProcessContext &context, const AtomArgument &atom) {
    return binary_of(context, atom.spelling);
}

// The UTF-8 spelling of an atom name given as bytes in an encoding: too_long past 255 characters.
std::expected<std::string, NameError> binary_name(const BinaryArgument &binary, bool latin) {
    std::u32string codes;
    if (latin) {
        codes.assign(binary.bytes.size(), 0);
        std::ranges::transform(binary.bytes, codes.begin(), [](std::byte b) { return std::to_integer<char32_t>(b); });
    } else if (const auto decoded = decode_utf8(binary.bytes)) {
        codes = *decoded;
    } else {
        return std::unexpected(NameError::badarg);
    }
    if (codes.size() > MAX_ATOM_CHARACTERS) {
        return std::unexpected(NameError::too_long);
    }
    return utf8(codes);
}

// The atom of a name, interning it unless only existing atoms are allowed (badarg for a missing one).
TermResult<Term> atom_of(ProcessContext &context, const std::expected<std::string, NameError> &name, bool existing) {
    if (!name) {
        name_failure(name.error());
    }
    if (!existing) {
        return context.atom_storage().intern(*name);
    }
    const auto atom = context.atom_storage().existing(*name);
    if (!atom) {
        bad_argument();
    }
    return *atom;
}

// binary_to_atom(Binary, Encoding) and binary_to_existing_atom/2.
template <bool Existing>
TermResult<Term> binary_to_atom2(ProcessContext &context, const BinaryArgument &binary, const Term &encoding) {
    return atom_of(context, binary_name(binary, latin1(encoding)), Existing);
}

// binary_to_atom(Binary) and binary_to_existing_atom/1: UTF-8.
template <bool Existing> TermResult<Term> binary_to_atom1(ProcessContext &context, const BinaryArgument &binary) {
    return atom_of(context, binary_name(binary, false), Existing);
}

// list_to_existing_atom(String).
TermResult<Term> list_to_existing_atom(ProcessContext &context, const Term &list) {
    return atom_of(context, atom_name(list), true);
}

// integer_to_binary(Integer, Base) and integer_to_binary/1.
TermResult<Term> integer_to_binary2(ProcessContext &context, const Integer &value, std::int64_t base) {
    return binary_of(context, integer_digits(value, base_of(base)));
}

TermResult<Term> integer_to_binary1(ProcessContext &context, const Integer &value) {
    return integer_to_binary2(context, value, 10);
}

// binary_to_integer(Binary, Base) and binary_to_integer/1.
TermResult<Term> binary_to_integer2(ProcessContext &context, const BinaryArgument &binary, std::int64_t base) {
    const auto radix = base_of(base);
    return integer_term(context, characters(binary), radix);
}

TermResult<Term> binary_to_integer1(ProcessContext &context, const BinaryArgument &binary) {
    return binary_to_integer2(context, binary, 10);
}

// float_to_binary(Float, Options) and float_to_binary/1.
TermResult<Term> float_to_binary2(ProcessContext &context, double number, const Term &options) {
    return binary_of(context, float_with_options(number, options));
}

TermResult<Term> float_to_binary1(ProcessContext &context, double number) {
    return float_to_binary2(context, number, need(Term::from_word(abi::v1::empty_list)));
}

// binary_to_float(Binary).
TermResult<Term> binary_to_float(ProcessContext &context, const BinaryArgument &binary) {
    return TermFactory(context).floating(float_of_text(characters(binary)));
}

// list_to_float(String).
TermResult<Term> list_to_float(ProcessContext &context, const Term &list) {
    const auto text = byte_codes(list);
    if (!text) {
        bad_argument();
    }
    return TermFactory(context).floating(float_of_text(*text));
}

// A proper list of small integers.
TermResult<Term> integer_list(ProcessContext &context, std::span<const std::byte> bytes, const Term &tail) {
    std::vector<Word> words(bytes.size());
    std::ranges::transform(bytes, words.begin(), [](std::byte b) {
        return *abi::v1::NativeIntegerEncoding::encode(std::to_integer<std::int64_t>(b));
    });
    return TermFactory(context).list_words(words, tail);
}

// binary_to_list(Binary, Start, Stop): bytes Start..Stop, numbered from 1.
TermResult<Term> binary_to_list3(ProcessContext &context, const BinaryArgument &binary, std::int64_t start,
                                 std::int64_t stop) {
    const auto size = static_cast<std::int64_t>(binary.bytes.size());
    if (start < 1 || stop < start || stop > size) {
        bad_argument();
    }
    const auto first = static_cast<std::size_t>(start - 1);
    const auto count = static_cast<std::size_t>(stop - start + 1);
    return integer_list(context, std::span(binary.bytes).subspan(first, count),
                        need(Term::from_word(abi::v1::empty_list)));
}

// bitstring_to_list(Bitstring): its bytes, then a bitstring of the bits past the last whole byte.
TermResult<Term> bitstring_to_list(ProcessContext &context, const Term &value) {
    if (!value.is_bitstring()) {
        bad_argument();
    }
    const auto bits = value.bit_size_unchecked();
    auto bytes = need(value.bitstring_bytes());
    bytes.resize(bits / 8);
    auto tail = need(Term::from_word(abi::v1::empty_list));
    if (bits % 8 != 0) {
        const std::array rest{need(detail::BitAccess::slice(context.heap(), value, bits - bits % 8, bits % 8))};
        tail = need(TermFactory(context).list(rest));
    }
    return integer_list(context, bytes, tail);
}

// Bits appended MSB first: the bytes so far and the count of bits used.
struct BitBuffer {
    std::vector<std::byte> bytes;
    std::size_t bits = 0;

    // Append the first `count` bits of `source`.
    void append(std::span<const std::byte> source, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            const auto bit = (std::to_integer<unsigned>(source[i / 8]) >> (7 - i % 8)) & 1U;
            if (bits % 8 == 0) {
                bytes.push_back(std::byte{0});
            }
            bytes.back() |= static_cast<std::byte>(bit << (7 - bits % 8));
            ++bits;
        }
    }
};

// Append one element of a bitstring list: a byte or a bitstring; false for anything else.
bool append_element(const Term &element, BitBuffer &buffer, bool bitstrings) {
    const auto value = small(element.word());
    if (value && *value >= 0 && *value <= 255) {
        const std::array byte{static_cast<std::byte>(*value)};
        buffer.append(byte, 8);
        return true;
    }
    const bool accepted = bitstrings ? element.is_bitstring() : element.is_binary();
    if (!accepted) {
        return false;
    }
    buffer.append(need(element.bitstring_bytes()), element.bit_size_unchecked());
    return true;
}

// Read one list until a nested list (queued before the rest of this list) or its end; its tail must be [] or a
// binary (a bitstring for bitstring lists). False for anything else.
bool read_list(Term list, std::vector<Term> &pending, BitBuffer &buffer, bool bitstrings) {
    for (; list.is_cons(); list = list.tail_unchecked()) {
        const auto element = list.head_unchecked();
        if (element.is_list()) {
            pending.push_back(list.tail_unchecked());
            pending.push_back(element);
            return true;
        }
        if (!append_element(element, buffer, bitstrings)) {
            return false;
        }
    }
    return list.is_nil() || (!small(list.word()) && append_element(list, buffer, bitstrings));
}

// Read an iolist (or, with `bitstrings`, a bitstring list) depth first into `buffer`; false when it is none.
bool flatten(const Term &root, BitBuffer &buffer, bool bitstrings) {
    std::vector<Term> pending{root};
    while (!pending.empty()) {
        const auto list = pending.back();
        pending.pop_back();
        if (!read_list(list, pending, buffer, bitstrings)) {
            return false;
        }
    }
    return true;
}

// list_to_bitstring(BitstringList).
TermResult<Term> list_to_bitstring(ProcessContext &context, const Term &list) {
    BitBuffer buffer;
    if (!list.is_list() || !flatten(list, buffer, true)) {
        bad_argument();
    }
    return TermFactory(context).bitstring(buffer.bytes, buffer.bits);
}

// iolist_size(IoListOrBinary): the bytes iolist_to_binary/1 would hold.
TermResult<Term> iolist_size(ProcessContext &context, const Term &value) {
    if (value.is_binary()) {
        return TermFactory(context).integer(static_cast<std::int64_t>(value.bit_size_unchecked() / 8));
    }
    BitBuffer buffer;
    if (!value.is_list() || !flatten(value, buffer, false)) {
        bad_argument();
    }
    return TermFactory(context).integer(static_cast<std::int64_t>(buffer.bits / 8));
}

// split_binary(Bin, Pos): the first Pos bytes and the rest.
TermResult<Term> split_binary(ProcessContext &context, const Term &value, std::int64_t position) {
    const auto bits = value.is_bitstring() ? value.bit_size_unchecked() : 0;
    if (!value.is_bitstring() || position < 0 || static_cast<std::size_t>(position) * 8 > bits) {
        bad_argument();
    }
    const auto cut = static_cast<std::size_t>(position) * 8;
    const std::array parts{need(detail::BitAccess::slice(context.heap(), value, 0, cut)),
                           need(detail::BitAccess::slice(context.heap(), value, cut, bits - cut))};
    return TermFactory(context).tuple(parts);
}

constexpr std::array BINARY_CONVERSION_BUILTINS{
    typed_entry<atom_to_binary1>("erlang", "atom_to_binary"),
    typed_entry<atom_to_binary2>("erlang", "atom_to_binary"),
    typed_entry<binary_to_atom1<false>>("erlang", "binary_to_atom"),
    typed_entry<binary_to_atom2<false>>("erlang", "binary_to_atom"),
    typed_entry<binary_to_atom1<true>>("erlang", "binary_to_existing_atom"),
    typed_entry<binary_to_atom2<true>>("erlang", "binary_to_existing_atom"),
    typed_entry<list_to_existing_atom>("erlang", "list_to_existing_atom"),
    typed_entry<integer_to_binary1>("erlang", "integer_to_binary"),
    typed_entry<integer_to_binary2>("erlang", "integer_to_binary"),
    typed_entry<binary_to_integer1>("erlang", "binary_to_integer"),
    typed_entry<binary_to_integer2>("erlang", "binary_to_integer"),
    typed_entry<float_to_binary1>("erlang", "float_to_binary"),
    typed_entry<float_to_binary2>("erlang", "float_to_binary"),
    typed_entry<binary_to_float>("erlang", "binary_to_float"),
    typed_entry<list_to_float>("erlang", "list_to_float"),
    typed_entry<binary_to_list3>("erlang", "binary_to_list"),
    typed_entry<bitstring_to_list>("erlang", "bitstring_to_list"),
    typed_entry<list_to_bitstring>("erlang", "list_to_bitstring"),
    typed_entry<split_binary>("erlang", "split_binary"),
    typed_entry<iolist_size>("erlang", "iolist_size"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> binary_conversion_builtins() noexcept { return builtins::BINARY_CONVERSION_BUILTINS; }
} // namespace clause::runtime
