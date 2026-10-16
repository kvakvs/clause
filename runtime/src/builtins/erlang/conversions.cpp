#include "../../terms/integers.hpp"
#include "../portions.hpp"
#include "../text.hpp"
#include "../typed.hpp"
#include "conversion_text.hpp"
#include "terms.hpp"
#include <algorithm>
#include <array>
#include <boost/container/small_vector.hpp>
#include <charconv>
#include <clause/abi/equality.hpp>
#include <clause/runtime/atoms.hpp>
#include <clause/runtime/output.hpp>
#include <vector>

// The conversion builtins (docs/builtins.md): atoms, integers, floats, lists and binaries, with OTP's badarg and
// system_limit rules. Binary and iolist conversions run in bounded portions (docs/builtins.md#portions); the others
// read inputs bounded by the atom, integer and float limits and run to completion, as in OTP. Text rules shared with
// the binary conversions are in conversion_text.cpp.
namespace clause::runtime::builtins {
namespace {
using detail::Integer;

// list_to_atom(String): more than 255 characters is system_limit; a full atom table stops the program.
BuiltinResult<Term> list_to_atom(ProcessContext &context, const Term &list) {
    const auto name = atom_name(list);
    if (!name) {
        return std::unexpected(BuiltinFailure{name.error() == NameError::too_long ? abi::v1::ErrorReason::system_limit
                                                                                  : abi::v1::ErrorReason::badarg});
    }
    return context.atom_storage().intern(*name).transform_error(
        [](TermError error) { return BuiltinFailure{.term = error}; });
}

// integer_to_list(Integer, Base).
TermResult<Term> integer_to_list2(ProcessContext &context, const Integer &value, std::int64_t base) {
    return byte_list(context, integer_digits(value, base_of(base)));
}

// integer_to_list(Integer).
TermResult<Term> integer_to_list1(ProcessContext &context, const Integer &value) {
    return integer_to_list2(context, value, 10);
}

// list_to_integer(String, Base); a value past the integer limit is system_limit.
TermResult<Term> list_to_integer2(ProcessContext &context, const Term &list, std::int64_t base) {
    const auto radix = base_of(base);
    const auto text = byte_codes(list);
    if (!text) {
        bad_argument();
    }
    return integer_term(context, *text, radix);
}

// list_to_integer(String).
TermResult<Term> list_to_integer1(ProcessContext &context, const Term &list) {
    return list_to_integer2(context, list, 10);
}

// float_to_list(Float, Options): later options override earlier ones.
TermResult<Term> float_to_list2(ProcessContext &context, double number, const Term &options) {
    return byte_list(context, float_with_options(number, options));
}

// float_to_list(Float).
TermResult<Term> float_to_list1(ProcessContext &context, double number) {
    return float_to_list2(context, number, Term::from_word(abi::v1::empty_list).value());
}

// The bytes of binary_to_list/1's argument still to put in the list, consumed from the end.
class ByteState final : public TrapState {
  public:
    std::vector<std::byte> bytes;
};

Word byte_list_continue(ProcessContext &context, Arguments state);

// Continuation of binary_to_list/1: [Tail], the list built so far.
constexpr BuiltinFrame BYTE_LIST_FRAME = continuation_frame(&guarded<byte_list_continue>, 1);

// Cons the last bytes onto `tail` in portions.
Word byte_portion(ProcessContext &context, const Term &tail) {
    auto &stack = context.stack();
    auto &bytes = trap_state<ByteState>(context).bytes;
    const auto count = std::min(bytes.size(), stack.budget());
    std::vector<Word> words(count);
    std::ranges::transform(std::span(bytes).last(count), words.begin(), [](std::byte b) {
        return *abi::v1::NativeIntegerEncoding::encode(std::to_integer<std::int64_t>(b));
    });
    const auto list = need(TermFactory(context).list_words(words, tail));
    bytes.resize(bytes.size() - count);
    stack.spend(count);
    if (!bytes.empty()) {
        stack.trap(BYTE_LIST_FRAME.frame, std::array{list.word()});
        return 0;
    }
    stack.drop_trap_state();
    return list.word();
}

Word byte_list_continue(ProcessContext &context, Arguments state) {
    return byte_portion(context, term_of(context, state[0]));
}

// binary_to_list(Binary): its bytes; other bitstrings are badarg.
Word binary_to_list(ProcessContext &context, Arguments arguments) {
    const auto binary = term_of(context, arguments[0]);
    if (!binary.is_binary()) {
        bad_argument();
    }
    auto state = std::make_unique<ByteState>();
    state->bytes = need(binary.binary_bytes());
    context.stack().keep_trap_state(std::move(state));
    return byte_portion(context, need(Term::from_word(abi::v1::empty_list)));
}

// Append the bytes of a binary; false for any other term.
bool append_binary(const Term &term, std::vector<std::byte> &bytes) {
    const auto content = term.is_binary() ? term.binary_bytes() : std::unexpected(TermError::wrong_type);
    if (content) {
        bytes.insert(bytes.end(), content->begin(), content->end());
    }
    return content.has_value();
}

// Append one iolist byte or binary; false for any other term.
bool append_leaf(const Term &element, std::vector<std::byte> &bytes) {
    const auto value = small(element.word());
    if (value && *value >= 0 && *value <= 255) {
        bytes.push_back(static_cast<std::byte>(*value));
        return true;
    }
    return append_binary(element, bytes);
}

// The bytes of an iolist read so far, and as words the lists still to read, the next one last.
class IolistState final : public TrapState {
  public:
    std::vector<std::byte> bytes;
    // Elements read in the current portion.
    std::size_t read = 0;
};

// Read one list until its end, a nested list (queued to be read before the rest of this one) or the end of the
// portion's `budget` (the rest is queued); false for an element or tail that is no iolist part.
bool read_list(Term list, IolistState &state, std::size_t budget) {
    for (; list.is_cons(); ++state.read) {
        if (state.read == budget) {
            state.words().push_back(list.word());
            return true;
        }
        auto element = next(list);
        if (element.is_nil() || element.is_cons()) {
            state.words().push_back(list.word());
            state.words().push_back(element.word());
            ++state.read;
            return true;
        }
        if (!append_leaf(element, state.bytes)) {
            return false;
        }
    }
    return list.is_nil() || append_binary(list, state.bytes);
}

Word iolist_continue(ProcessContext &context, Arguments state);

// Continuation of list_to_binary/1 and iolist_to_binary/1; its state is in the IolistState.
constexpr BuiltinFrame IOLIST_FRAME = continuation_frame(&guarded<iolist_continue>, 0);

// Read the queued lists in portions, then make the binary; anything but an iolist is badarg.
Word iolist_continue(ProcessContext &context, Arguments) {
    auto &stack = context.stack();
    auto &state = trap_state<IolistState>(context);
    const auto budget = stack.budget();
    state.read = 0;
    while (!state.words().empty() && state.read < budget) {
        const auto list = term_of(context, state.words().back());
        state.words().pop_back();
        if (!read_list(list, state, budget)) {
            bad_argument();
        }
    }
    stack.spend(state.read);
    if (!state.words().empty()) {
        stack.trap(IOLIST_FRAME.frame, {});
        return 0;
    }
    const auto binary = need(TermFactory(context).binary(state.bytes));
    stack.drop_trap_state();
    return binary.word();
}

// list_to_binary(IoList): the argument must be a list.
Word list_to_binary(ProcessContext &context, Arguments arguments) {
    if (!term_of(context, arguments[0]).is_list()) {
        bad_argument();
    }
    auto state = std::make_unique<IolistState>();
    state->words().push_back(arguments[0]);
    context.stack().keep_trap_state(std::move(state));
    return iolist_continue(context, {});
}

// iolist_to_binary(IoListOrBinary): a binary is returned as it is.
Word iolist_to_binary(ProcessContext &context, Arguments arguments) {
    return term_of(context, arguments[0]).is_binary() ? arguments[0] : list_to_binary(context, arguments);
}

// The ~w text of an identity as a list: pid_to_list/1 accepts only pids, ref_to_list/1 only references.
template <bool Pid> TermResult<Term> identity_to_list(ProcessContext &context, const Term &value) {
    if (Pid ? !value.is_pid() : !value.is_reference()) {
        bad_argument();
    }
    return byte_list(context, need(format_term(value, TermStyle::write)));
}

// The word of a list of character words onto [], built in one step.
Word char_words(ProcessContext &context, std::span<const Word> characters) {
    if (const auto bumped = detail::TermAccess::bump_list(context.heap(), characters, abi::v1::empty_list)) {
        return *bumped;
    }
    TermFactory factory(context);
    return need(factory.list_of_words(characters, need(factory.nil()))).word();
}

// integer_to_list(Integer): a small integer's digits from its word with no staging allocation (word-level
// conversions); other integers take the multiprecision path.
Word integer_to_list_word(ProcessContext &context, Arguments arguments) {
    if (const auto value = small(arguments[0])) {
        std::array<char, 24> digits{};
        const auto written = std::to_chars(digits.data(), digits.data() + digits.size(), *value);
        boost::container::small_vector<Word, 24> characters;
        for (const auto *at = digits.data(); at != written.ptr; ++at) {
            characters.push_back(*abi::v1::NativeIntegerEncoding::encode(static_cast<unsigned char>(*at)));
        }
        return char_words(context, characters);
    }
    const auto term = term_of(context, arguments[0]);
    if (!term.is_integer()) {
        bad_argument();
    }
    return need(integer_to_list1(context, need(detail::integer_read(term)))).word();
}

// Up to 20 ASCII characters of a proper list read from its cells; none for a longer, improper or non-ASCII list.
std::optional<std::string> short_text(ProcessContext &context, Word list) {
    std::string text;
    auto rest = term_of(context, list);
    bool ascii = true;
    walk(rest, 20, [&](Word head) {
        const auto code = small(head);
        ascii = ascii && code && *code >= 0 && *code < 128;
        if (ascii) {
            text.push_back(static_cast<char>(*code));
        }
    });
    return ascii && rest.is_nil() ? std::optional{std::move(text)} : std::nullopt;
}

// The small integer a short decimal text spells, with an optional sign; none for any other text, which takes the
// general path. from_chars takes a leading minus only; a plus is OTP's other sign, never followed by a minus.
std::optional<std::int64_t> small_decimal(std::string_view text) {
    const bool plus = text.starts_with('+');
    const auto digits = text.substr(plus ? 1 : 0);
    if (digits.empty() || (plus && digits.front() == '-')) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    const bool whole = parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size();
    return whole && abi::v1::NativeIntegerEncoding::encode(value) ? std::optional{value} : std::nullopt;
}

// list_to_integer(String): a short decimal string from its words (word-level conversions); others take the general
// path, which also reports every error.
Word list_to_integer_word(ProcessContext &context, Arguments arguments) {
    const auto text = short_text(context, arguments[0]);
    if (const auto value = text ? small_decimal(*text) : std::nullopt) {
        return *abi::v1::NativeIntegerEncoding::encode(*value);
    }
    return need(list_to_integer1(context, term_of(context, arguments[0]))).word();
}

// atom_to_list(Atom): its characters decoded straight into list words (word-level conversions).
Word atom_to_list_word(ProcessContext &context, Arguments arguments) {
    const auto atom = term_of(context, arguments[0]);
    const auto spelling = atom.is_atom() ? atom.atom_spelling() : std::unexpected(TermError::wrong_type);
    if (!spelling) {
        bad_argument();
    }
    boost::container::small_vector<Word, 64> characters;
    for (const auto code : code_points(*spelling)) {
        characters.push_back(*abi::v1::NativeIntegerEncoding::encode(code));
    }
    return char_words(context, characters);
}

constexpr std::array CONVERSION_BUILTINS{
    BuiltinEntry{"erlang", "atom_to_list", 1, &guarded<atom_to_list_word>},
    typed_entry<list_to_atom>("erlang", "list_to_atom"),
    BuiltinEntry{"erlang", "integer_to_list", 1, &guarded<integer_to_list_word>},
    typed_entry<integer_to_list2>("erlang", "integer_to_list"),
    BuiltinEntry{"erlang", "list_to_integer", 1, &guarded<list_to_integer_word>},
    typed_entry<list_to_integer2>("erlang", "list_to_integer"),
    typed_entry<float_to_list1>("erlang", "float_to_list"),
    typed_entry<float_to_list2>("erlang", "float_to_list"),
    BuiltinEntry{"erlang", "binary_to_list", 1, &guarded<binary_to_list>},
    BuiltinEntry{"erlang", "list_to_binary", 1, &guarded<list_to_binary>},
    BuiltinEntry{"erlang", "iolist_to_binary", 1, &guarded<iolist_to_binary>},
    typed_entry<identity_to_list<true>>("erlang", "pid_to_list"),
    typed_entry<identity_to_list<false>>("erlang", "ref_to_list"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> conversion_builtins() noexcept { return builtins::CONVERSION_BUILTINS; }
} // namespace clause::runtime
