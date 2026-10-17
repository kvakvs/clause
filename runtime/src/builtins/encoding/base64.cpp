#include "../../terms/integers.hpp"
#include "../collections/collections.hpp"
#include <array>
#include <string_view>

// The base64 builtins (plan 21 step LIB5, docs/builtins.md#encodings): RFC 4648 base64 with the standard or urlsafe
// alphabet and optional padding. Decoding walks its input as OTP's clause state machines do, so results and errors
// agree for any input: whitespace is skipped, input ending inside a group is missing_padding (function_clause after
// one character), a character outside the alphabet fails when its group is built (badarg, or badarith inside OTP's
// shifts), anything but whitespace after the padding is a case_clause; mime_decode skips what is not in the alphabet.
namespace clause::runtime::builtins::encoding {
namespace {
using collections::no_clause;
using collections::Prefix;

constexpr std::string_view STANDARD = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::string_view URLSAFE = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

struct DataRole;
struct OptionsRole;
using Data = Named<DataRole>;
using Options = Named<OptionsRole>;

// The options map: the alphabet and whether padding is written or required.
struct Coding {
    bool urlsafe = false;
    bool padding = true;
};

// One option: mode standard or urlsafe, padding true or false; anything else changes nothing.
void apply_setting(Coding &coding, std::string_view name, std::string_view setting) {
    if (name == "mode" && (setting == "standard" || setting == "urlsafe")) {
        coding.urlsafe = setting == "urlsafe";
    } else if (name == "padding" && (setting == "true" || setting == "false")) {
        coding.padding = setting == "true";
    }
}

// The options of a map; function_clause for anything else. Unknown keys and values are ignored, as OTP's
// get_padding/1 and get_*_offset/1 ignore them.
Coding coding_of(const Options &options) {
    if (!options.term.is_map()) {
        no_clause();
    }
    Coding coding;
    for (const auto &[key, value] : need(options.term.map_entries())) {
        if (key.is_atom() && value.is_atom()) {
            apply_setting(coding, key.atom_spelling_unchecked(), value.atom_spelling_unchecked());
        }
    }
    return coding;
}

// One input character decoded: a 6-bit value, whitespace (TAB, LF, CR, space), the padding '=', or a character
// outside the alphabet.
struct Symbol {
    enum class Kind : std::uint8_t { value, space, pad, bad };
    Kind kind = Kind::bad;
    std::uint8_t value = 0;

    bool is_value() const { return kind == Kind::value; }
};

// The symbol at position `index` of OTP's 512-entry decoding table (standard alphabet, then urlsafe); badarg past
// it.
Symbol table_symbol(std::int64_t index) {
    if (index < 0 || index >= 512) {
        bad_argument();
    }
    const auto alphabet = index >= 256 ? URLSAFE : STANDARD;
    const auto byte = static_cast<char>(index % 256);
    if (byte == '\t' || byte == '\n' || byte == '\r' || byte == ' ') {
        return {Symbol::Kind::space, 0};
    }
    if (byte == '=') {
        return {Symbol::Kind::pad, 0};
    }
    const auto at = alphabet.find(byte);
    return at == std::string_view::npos ? Symbol{} : Symbol{Symbol::Kind::value, static_cast<std::uint8_t>(at)};
}

// The decoders' input, read lazily: a binary's bytes or a list's elements (badarith unless an integer, as OTP adds
// the table offset; an improper tail function_clause when reached).
class Input final {
  public:
    Input(const Term &data, bool urlsafe) : offset_(urlsafe ? 256 : 0), binary_(data.is_binary()), rest_(data) {
        if (binary_) {
            bytes_ = need(data.binary_bytes());
        }
    }

    std::optional<Symbol> next() {
        if (binary_) {
            return at_ == bytes_.size()
                       ? std::nullopt
                       : std::optional{table_symbol(std::to_integer<std::int64_t>(bytes_[at_++]) + offset_)};
        }
        if (rest_.is_nil()) {
            return std::nullopt;
        }
        if (!rest_.is_cons()) {
            no_clause();
        }
        const auto element = rest_.head_unchecked();
        rest_ = rest_.tail_unchecked();
        if (!element.is_integer()) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
        }
        const auto value = small(element.word());
        if (!value) {
            bad_argument();
        }
        return table_symbol(*value + offset_);
    }

  private:
    std::int64_t offset_;
    bool binary_;
    std::vector<std::byte> bytes_;
    std::size_t at_ = 0;
    Term rest_;
};

// The decoded bytes, as a binary or a list. A group holding a symbol that is not a value fails as OTP builds it:
// a binary's segment of a non-value is badarg; a shift of one (and a list group's arithmetic) badarith.
class Output final {
  public:
    explicit Output(bool listed) : listed_(listed) {}

    void four(const std::array<Symbol, 4> &group) {
        if (!std::ranges::all_of(group, &Symbol::is_value)) {
            throw BuiltinFailure{.reason = listed_ ? abi::v1::ErrorReason::badarith : abi::v1::ErrorReason::badarg};
        }
        const auto bits = (group[0].value << 18U) | (group[1].value << 12U) | (group[2].value << 6U) | group[3].value;
        push(bits >> 16U);
        push((bits >> 8U) & 0xFFU);
        push(bits & 0xFFU);
    }

    // The end of a group of two symbols (one byte) or three (two bytes).
    void two(const Symbol &first, const Symbol &second) {
        shifted(second);
        valued(first);
        push((first.value << 2U) | (second.value >> 4U));
    }

    void three(const std::array<Symbol, 4> &group) {
        shifted(group[2]);
        valued(group[0]);
        valued(group[1]);
        const auto bits = (group[0].value << 10U) | (group[1].value << 4U) | (group[2].value >> 2U);
        push(bits >> 8U);
        push(bits & 0xFFU);
    }

    Term result(ProcessContext &context) const {
        if (!listed_) {
            return need(TermFactory(context).binary(bytes_));
        }
        Prefix list;
        for (const auto byte : bytes_) {
            list.push(need(TermFactory(context).integer(std::to_integer<std::int64_t>(byte))));
        }
        return list.done(context);
    }

  private:
    static void shifted(const Symbol &symbol) {
        if (!symbol.is_value()) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
        }
    }

    static void valued(const Symbol &symbol) {
        if (!symbol.is_value()) {
            bad_argument();
        }
    }

    void push(unsigned value) { bytes_.push_back(static_cast<std::byte>(value)); }

    bool listed_;
    std::vector<std::byte> bytes_;
};

[[noreturn]] void missing_padding(ProcessContext &context) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                         .payload = need(TermFactory(context).atom("missing_padding")).word()};
}

// After the padding only whitespace may follow; anything else is {case_clause, Symbol} as OTP's only_ws raises.
void only_space(ProcessContext &context, Input &input) {
    while (const auto symbol = input.next()) {
        if (symbol->kind == Symbol::Kind::space) {
            continue;
        }
        const auto value = symbol->is_value()
                               ? need(TermFactory(context).integer(symbol->value))
                               : need(TermFactory(context).atom(symbol->kind == Symbol::Kind::pad ? "eq" : "bad"));
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::case_clause, .payload = value.word()};
    }
}

// The last two or three symbols of a group ended by the padding or by the end of the input.
void finish(Output &output, const std::array<Symbol, 4> &group, std::size_t held) {
    if (held == 2 || group[2].kind == Symbol::Kind::pad) {
        output.two(group[0], group[1]);
    } else {
        output.three(group);
    }
}

// The end of decode's input: a group of one is function_clause, an unpadded one of two or three missing_padding
// unless padding is optional.
Term strict_end(ProcessContext &context, Output &output, const std::array<Symbol, 4> &group, std::size_t held,
                const Coding &coding) {
    if (held == 1) {
        no_clause();
    }
    if (held >= 2 && coding.padding) {
        missing_padding(context);
    }
    if (held >= 2) {
        finish(output, group, held);
    }
    return output.result(context);
}

// decode/1,2 and decode_to_string/1,2: every symbol but whitespace counts toward a group of four; a padding fourth
// symbol ends the data.
Term strict(ProcessContext &context, Input input, const Coding &coding, bool listed) {
    Output output(listed);
    std::array<Symbol, 4> group{};
    std::size_t held = 0;
    while (const auto symbol = input.next()) {
        if (symbol->kind == Symbol::Kind::space) {
            continue;
        }
        if (held == 3 && symbol->kind == Symbol::Kind::pad) {
            finish(output, group, held);
            only_space(context, input);
            return output.result(context);
        }
        group[held++] = *symbol;
        if (held == 4) {
            output.four(group);
            held = 0;
        }
    }
    return strict_end(context, output, group, held, coding);
}

// The states of mime_decode: collecting values, or after a padding character that may still be followed by data.
struct Mime {
    std::array<Symbol, 4> group{};
    std::size_t held = 0;
    bool after_pad = false;
};

// One value of mime_decode: after a padding the third place it stood in is taken, or the group completed.
void mime_value(Output &output, Mime &state, const Symbol &symbol) {
    if (state.after_pad && state.group[2].kind == Symbol::Kind::pad) {
        state.group[2] = symbol;
        state.after_pad = false;
        return;
    }
    state.after_pad = false;
    state.group[state.held++] = symbol;
    if (state.held == 4) {
        output.four(state.group);
        state.held = 0;
    }
}

// A padding character of mime_decode: after two or three values it marks the group (the third place after two);
// any other is skipped.
void mime_pad(Mime &state, const Symbol &symbol) {
    if (state.held < 2 || state.after_pad) {
        return;
    }
    state.after_pad = true;
    if (state.held == 2) {
        state.group[state.held++] = symbol;
    }
}

Term mime_end(ProcessContext &context, Output &output, const Mime &state, const Coding &coding);

// mime_decode/1,2 and mime_decode_to_string/1,2: OTP's lenient state machine, skipping what is not a value.
Term mime(ProcessContext &context, Input input, const Coding &coding, bool listed) {
    Output output(listed);
    Mime state;
    while (const auto symbol = input.next()) {
        if (symbol->is_value()) {
            mime_value(output, state, *symbol);
        } else if (symbol->kind == Symbol::Kind::pad) {
            mime_pad(state, *symbol);
        }
    }
    return mime_end(context, output, state, coding);
}

// The end of mime_decode's input: a group of one is function_clause, an unpadded one of two or three
// missing_padding unless padding is optional.
Term mime_end(ProcessContext &context, Output &output, const Mime &state, const Coding &coding) {
    if (state.held == 1) {
        no_clause();
    }
    if (state.held >= 2 && !state.after_pad && coding.padding) {
        missing_padding(context);
    }
    if (state.held >= 2) {
        finish(output, state.group, state.held);
    }
    return output.result(context);
}

// One output character: element(X + Offset) of OTP's 128-entry alphabet table (standard, then urlsafe); badarg off
// the table.
char alphabet_char(const detail::Integer &index, bool urlsafe) {
    const auto at = index + (urlsafe ? 64 : 0);
    if (at < 0 || at >= 128) {
        bad_argument();
    }
    const auto position = at.convert_to<std::size_t>();
    return position >= 64 ? URLSAFE[position - 64] : STANDARD[position];
}

// The bytes of encode's data, read lazily: a binary's, or a list's elements (badarith unless integers, as OTP's
// arithmetic on them raises; an improper tail function_clause when reached).
class Bytes final {
  public:
    explicit Bytes(const Term &data) : binary_(data.is_binary()), rest_(data) {
        if (binary_) {
            bytes_ = need(data.binary_bytes());
        }
    }

    // Up to three more bytes.
    std::vector<detail::Integer> take() { return binary_ ? take_binary() : take_list(); }

  private:
    std::vector<detail::Integer> take_binary() {
        std::vector<detail::Integer> taken;
        for (; taken.size() < 3 && at_ < bytes_.size(); ++at_) {
            taken.emplace_back(std::to_integer<int>(bytes_[at_]));
        }
        return taken;
    }

    // OTP's clauses match a group's shape before computing with its elements.
    std::vector<detail::Integer> take_list() {
        std::vector<Term> cells;
        for (; cells.size() < 3 && rest_.is_cons(); rest_ = rest_.tail_unchecked()) {
            cells.push_back(rest_.head_unchecked());
        }
        if (cells.size() < 3 && !rest_.is_nil()) {
            no_clause();
        }
        std::vector<detail::Integer> taken;
        for (const auto &cell : cells) {
            if (!cell.is_integer()) {
                throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
            }
            taken.push_back(need(detail::integer_read(cell)));
        }
        return taken;
    }

    bool binary_;
    std::vector<std::byte> bytes_;
    std::size_t at_ = 0;
    Term rest_;
};

// The characters of one group of three bytes or fewer (the padding after one or two).
void encode_group(std::string &text, const std::vector<detail::Integer> &bytes, const Coding &coding) {
    if (bytes.size() == 3) {
        const detail::Integer bits = (bytes[0] << 16) | (bytes[1] << 8) | bytes[2];
        text += alphabet_char(bits >> 18, coding.urlsafe);
        text += alphabet_char((bits >> 12) & 63, coding.urlsafe);
        text += alphabet_char((bits >> 6) & 63, coding.urlsafe);
        text += alphabet_char(bits & 63, coding.urlsafe);
    } else if (bytes.size() == 2) {
        text += alphabet_char(bytes[0] >> 2, coding.urlsafe);
        text += alphabet_char(((bytes[0] & 3) << 4) | (bytes[1] >> 4), coding.urlsafe);
        text += alphabet_char((bytes[1] & 15) << 2, coding.urlsafe);
        text += coding.padding ? "=" : "";
    } else if (bytes.size() == 1) {
        text += alphabet_char(bytes[0] >> 2, coding.urlsafe);
        text += alphabet_char((bytes[0] & 3) << 4, coding.urlsafe);
        text += coding.padding ? "==" : "";
    }
}

// encode/1,2 and encode_to_string/1,2.
Term encode_data(ProcessContext &context, const Data &data, const Coding &coding, bool listed) {
    Bytes bytes(data.term);
    std::string text;
    for (auto group = bytes.take(); !group.empty(); group = group.size() == 3 ? bytes.take() : decltype(group){}) {
        encode_group(text, group, coding);
    }
    if (!listed) {
        return need(TermFactory(context).binary(std::as_bytes(std::span(text))));
    }
    Prefix list;
    for (const auto c : text) {
        list.push(need(TermFactory(context).integer(static_cast<unsigned char>(c))));
    }
    return list.done(context);
}

Options empty_options(ProcessContext &context) { return {need(TermFactory(context).map({}))}; }

// encode(Data, Options): a binary needs a map; a list's options are checked when read.
Term encode2(ProcessContext &context, const Data &data, const Options &options) {
    if (!data.term.is_list() && !(data.term.is_binary() && options.term.is_map())) {
        no_clause();
    }
    return encode_data(context, data, coding_of(options), false);
}

Term encode1(ProcessContext &context, const Data &data) { return encode2(context, data, empty_options(context)); }

Term encode_to_string2(ProcessContext &context, const Data &data, const Options &options) {
    if ((!data.term.is_binary() && !data.term.is_list()) || !options.term.is_map()) {
        no_clause();
    }
    return encode_data(context, data, coding_of(options), true);
}

Term encode_to_string1(ProcessContext &context, const Data &data) {
    return encode_to_string2(context, data, empty_options(context));
}

template <bool Lenient, bool Listed> Term decode2(ProcessContext &context, const Data &data, const Options &options) {
    if (!data.term.is_binary() && !data.term.is_list()) {
        no_clause();
    }
    const auto coding = coding_of(options);
    Input input(data.term, coding.urlsafe);
    return Lenient ? mime(context, std::move(input), coding, Listed)
                   : strict(context, std::move(input), coding, Listed);
}

template <bool Lenient, bool Listed> Term decode1(ProcessContext &context, const Data &data) {
    return decode2<Lenient, Listed>(context, data, empty_options(context));
}

constexpr std::array BASE64_BUILTINS{
    typed_entry<encode1>("base64", "encode"),
    typed_entry<encode2>("base64", "encode"),
    typed_entry<encode_to_string1>("base64", "encode_to_string"),
    typed_entry<encode_to_string2>("base64", "encode_to_string"),
    typed_entry<decode1<false, false>>("base64", "decode"),
    typed_entry<decode2<false, false>>("base64", "decode"),
    typed_entry<decode1<false, true>>("base64", "decode_to_string"),
    typed_entry<decode2<false, true>>("base64", "decode_to_string"),
    typed_entry<decode1<true, false>>("base64", "mime_decode"),
    typed_entry<decode2<true, false>>("base64", "mime_decode"),
    typed_entry<decode1<true, true>>("base64", "mime_decode_to_string"),
    typed_entry<decode2<true, true>>("base64", "mime_decode_to_string"),
};
} // namespace
} // namespace clause::runtime::builtins::encoding

namespace clause::runtime {
std::span<const BuiltinEntry> base64_builtins() noexcept { return builtins::encoding::BASE64_BUILTINS; }
} // namespace clause::runtime
