#include "../../process/identities.hpp"
#include "../../scheduler/executor.hpp"
#include "../../terms/atom_table.hpp"
#include "../../terms/identities.hpp"
#include "../typed.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// list_to_pid/1, list_to_port/1 and list_to_ref/1 (moved together for plan 30 step DIST2c,
// docs/distribution.md#node-monitors): the text of an identity of this node, node part 0, or, as OTP's, of a node with
// an up connection, its node part the node name atom's number, which then gets the connection's creation.
namespace clause::runtime::builtins {
namespace {
// A decimal number of the text: its value cut to 64 bits and whether it fits them.
struct Decimal final {
    std::uint64_t value = 0;
    bool exact = true;
};

// A number of one or more decimal digits, cut to 64 bits as OTP cuts another node's numbers; badarg for other text.
Decimal decimal(std::string_view digits) {
    if (digits.empty() || !std::ranges::all_of(digits, [](char digit) { return digit >= '0' && digit <= '9'; })) {
        bad_argument();
    }
    constexpr auto MAX = ~std::uint64_t{0};
    Decimal number;
    for (const auto digit : digits) {
        const auto value = static_cast<std::uint64_t>(digit - '0');
        number.exact = number.exact && number.value <= (MAX - value) / 10;
        number.value = number.value * 10 + value;
    }
    return number;
}

// The numbers of "<prefix>N1.N2...Nk>"; badarg for other text.
std::vector<Decimal> numbers(std::string_view text, std::string_view prefix) {
    if (!text.starts_with(prefix) || !text.ends_with('>')) {
        bad_argument();
    }
    std::vector<Decimal> result;
    auto rest = text.substr(prefix.size(), text.size() - prefix.size() - 1);
    for (auto dot = rest.find('.'); dot != std::string_view::npos; dot = rest.find('.')) {
        result.push_back(decimal(rest.substr(0, dot)));
        rest.remove_prefix(dot + 1);
    }
    result.push_back(decimal(rest));
    return result;
}

// The numbers of the text of a list of characters (see numbers()); badarg for anything else.
std::vector<Decimal> numbers(const ListArgument &text, std::string_view prefix) {
    std::string spelling;
    for (const auto &code : text.elements) {
        const auto value = small(code.word());
        if (!value || *value < 0 || *value > 127) {
            bad_argument();
        }
        spelling.push_back(static_cast<char>(*value));
    }
    return numbers(spelling, prefix);
}

// The exact value of a number below `limit`; badarg for another.
std::uint64_t below(const Decimal &number, std::uint64_t limit) {
    if (!number.exact || number.value >= limit) {
        bad_argument();
    }
    return number.value;
}

// An identity of the node whose name atom has the number `node`, with its up connection's creation; badarg when no
// connection to such a node is up.
detail::ExternalIdentity connected(ProcessContext &context, const Decimal &node, TermKind kind) {
    const auto word = static_cast<Word>(abi::v1::fixed_atom(below(node, detail::atom_table::CAPACITY)));
    const auto creation = detail::Executor::of(context).connected_creation(word);
    if (!creation) {
        bad_argument();
    }
    return {.kind = kind, .node = word, .creation = *creation, .numbers = {}, .count = 0};
}

// A term of this node's pid or port word, or badarg for one this program never issued.
Term issued(ProcessContext &context, Word word) {
    const auto term = Term::from_word(word, context);
    if (!term) {
        bad_argument();
    }
    return *term;
}

// list_to_pid("<Node.N.S>"): this node's pid of number S * 2^28 + N, only one this program issued; another node's
// numbers cut to 32 bits, as OTP's.
TermResult<Term> list_to_pid(ProcessContext &context, const ListArgument &text) {
    const auto parts = numbers(text, "<");
    if (parts.size() != 3) {
        bad_argument();
    }
    if (parts[0].value != 0 || !parts[0].exact) {
        auto identity = connected(context, parts[0], TermKind::external_pid);
        identity.numbers = {static_cast<std::uint32_t>(parts[1].value), static_cast<std::uint32_t>(parts[2].value)};
        identity.count = 2;
        return detail::ExternalAccess::make(context.heap(), identity);
    }
    const auto number = below(parts[1], std::uint64_t{1} << 28);
    const auto serial = below(parts[2], std::uint64_t{1} << 32);
    return issued(context, detail::pid_word(static_cast<Word>(serial << 28 | number)));
}

// list_to_port("#Port<Node.N>"): this node's port of number N, only one this program issued; another node's number
// cut to 64 bits.
TermResult<Term> list_to_port(ProcessContext &context, const ListArgument &text) {
    const auto parts = numbers(text, "#Port<");
    if (parts.size() != 2) {
        bad_argument();
    }
    if (parts[0].value != 0 || !parts[0].exact) {
        auto identity = connected(context, parts[0], TermKind::external_port);
        identity.numbers = {static_cast<std::uint32_t>(parts[1].value),
                            static_cast<std::uint32_t>(parts[1].value >> 32U)};
        identity.count = 2;
        return detail::ExternalAccess::make(context.heap(), identity);
    }
    if (!parts[1].exact) {
        bad_argument();
    }
    return issued(context, detail::port_word(static_cast<Word>(parts[1].value)));
}

// list_to_ref("#Ref<Node.A.B.C>"): this node's reference of number A * 2^50 + B * 2^18 + C; another node's
// reference of one to five 32-bit words, the last printed lowest.
TermResult<Term> list_to_ref(ProcessContext &context, const ListArgument &text) {
    const auto parts = numbers(text, "#Ref<");
    const auto words = parts.size() - 1;
    if (parts[0].value != 0 || !parts[0].exact) {
        if (words == 0 || words > detail::layout::MAX_EXTERNAL_NUMBERS) {
            bad_argument();
        }
        auto identity = connected(context, parts[0], TermKind::external_reference);
        for (std::size_t index = 0; index < words; ++index) {
            identity.numbers.at(index) =
                static_cast<std::uint32_t>(below(parts[words - index], std::uint64_t{1} << 32));
        }
        identity.count = words;
        return detail::ExternalAccess::make(context.heap(), identity);
    }
    if (words != 3) {
        bad_argument();
    }
    const auto high = below(parts[1], std::uint64_t{1} << 14);
    const auto middle = below(parts[2], std::uint64_t{1} << 32);
    const auto low = below(parts[3], std::uint64_t{1} << 18);
    return TermFactory(context).reference_number(high << 50 | middle << 18 | low);
}

constexpr std::array IDENTITY_TEXT_BUILTINS{
    typed_entry<list_to_pid>("erlang", "list_to_pid"),
    typed_entry<list_to_port>("erlang", "list_to_port"),
    typed_entry<list_to_ref>("erlang", "list_to_ref"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> identity_text_builtins() noexcept { return builtins::IDENTITY_TEXT_BUILTINS; }
} // namespace clause::runtime
