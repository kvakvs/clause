#include "options.hpp"
#include <array>
#include <utility>

// The options of ets:new/2 (plan 21 step LIB10A), read as OTP reads them: the last type and access option win, a
// malformed option or list is badarg, and the concurrency tweaks take effect only on tables other processes may
// use, which is what ets:info/2 then reports.
namespace clause::runtime::builtins::ets {
namespace {
using detail::ets::TableOptions;
using detail::ets::WriteConcurrency;

// The option list as read so far.
struct Reading {
    NewOptions result;
    WriteConcurrency write_concurrency = WriteConcurrency::off;
    bool read_concurrency = false;
    // OTP's default for decentralized counters as the options set it, and the explicit option, if any.
    bool decentralized = false;
    std::optional<bool> decentralized_option;
};

// A boolean atom's value; none for any other term.
std::optional<bool> boolean(const Term &term) {
    const auto name = spelling(term);
    if (name == "true" || name == "false") {
        return name == "true";
    }
    return std::nullopt;
}

// A type option; `set`, the default, changes nothing, as in OTP ([bag, set] makes a bag).
bool type_option(Reading &reading, std::string_view name) {
    constexpr std::array types{std::pair{"ordered_set", TableType::ordered_set}, std::pair{"bag", TableType::bag},
                               std::pair{"duplicate_bag", TableType::duplicate_bag}};
    if (name == "set") {
        return true;
    }
    for (const auto &[spelled, type] : types) {
        if (name == spelled) {
            reading.result.table.type = type;
            reading.decentralized = reading.decentralized || type == TableType::ordered_set;
            return true;
        }
    }
    return false;
}

// An access option; `protected`, the default, changes nothing, as in OTP ([public, protected] makes a public table).
bool access_option(Reading &reading, std::string_view name) {
    constexpr std::array accesses{std::pair{"public", Protection::public_access},
                                  std::pair{"private", Protection::private_access}};
    if (name == "protected") {
        return true;
    }
    for (const auto &[spelled, protection] : accesses) {
        if (name == spelled) {
            reading.result.table.protection = protection;
            return true;
        }
    }
    return false;
}

bool atom_option(Reading &reading, std::string_view name) {
    if (name == "named_table") {
        reading.result.table.named = true;
        return true;
    }
    if (name == "compressed") {
        reading.result.table.compressed = true;
        return true;
    }
    return type_option(reading, name) || access_option(reading, name);
}

bool write_concurrency(Reading &reading, const Term &value) {
    if (spelling(value) == "auto") {
        reading.write_concurrency = WriteConcurrency::automatic;
        reading.decentralized = true;
        return true;
    }
    const auto flag = boolean(value);
    if (!flag) {
        return false;
    }
    if (*flag && reading.result.table.type != TableType::ordered_set) {
        reading.decentralized = false;
    }
    reading.write_concurrency = *flag ? WriteConcurrency::on : WriteConcurrency::off;
    return true;
}

// {heir, none} or {heir, Pid}: a heir without data inherits without a message.
bool heir_option(Reading &reading, const Term &value) {
    if (spelling(value) == "none") {
        reading.result.heir = std::nullopt;
        return true;
    }
    if (!value.is_pid()) {
        return false;
    }
    reading.result.heir = Heir{.pid = value.word(), .data = std::nullopt};
    return true;
}

bool pair_option(Reading &reading, std::string_view name, const Term &value) {
    if (name == "keypos") {
        const auto position = small(value.word());
        if (!position || *position < 1) {
            return false;
        }
        reading.result.table.keypos = static_cast<std::size_t>(*position);
        return true;
    }
    if (name == "write_concurrency") {
        return write_concurrency(reading, value);
    }
    if (name == "heir") {
        return heir_option(reading, value);
    }
    const auto flag = boolean(value);
    if (flag && name == "read_concurrency") {
        reading.read_concurrency = *flag;
        return true;
    }
    if (flag && name == "decentralized_counters") {
        reading.decentralized_option = *flag;
        return true;
    }
    return false;
}

bool option(Reading &reading, const Term &option) {
    if (option.is_atom()) {
        return atom_option(reading, option.atom_spelling_unchecked());
    }
    if (!option.is_tuple()) {
        return false;
    }
    const auto size = option.tuple_size_unchecked();
    const auto name = spelling(option.tuple_element_unchecked(0));
    if (size == 2) {
        return pair_option(reading, name, option.tuple_element_unchecked(1));
    }
    if (size == 3 && name == "heir" && option.tuple_element_unchecked(1).is_pid()) {
        reading.result.heir =
            Heir{.pid = option.tuple_element_unchecked(1).word(), .data = option.tuple_element_unchecked(2)};
        return true;
    }
    return false;
}

// The reported tweaks: the concurrency options hold only for tables other processes may use.
void settle(Reading &reading) {
    auto &table = reading.result.table;
    const auto shared = table.protection != Protection::private_access;
    table.write_concurrency = shared ? reading.write_concurrency : WriteConcurrency::off;
    table.read_concurrency = shared && reading.read_concurrency;
    const auto decentralized = reading.decentralized_option.value_or(reading.decentralized);
    table.decentralized_counters = table.write_concurrency != WriteConcurrency::off && decentralized;
}
} // namespace

NewOptions new_options(const Term &options) {
    Reading reading;
    auto rest = options;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        if (!option(reading, rest.head_unchecked())) {
            bad_argument();
        }
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    settle(reading);
    return reading.result;
}

std::optional<HeirChange> heir_change(const Term &option) {
    const auto size = option.tuple_size_unchecked();
    const auto heir = option.tuple_element_unchecked(1);
    if (size == 2 && spelling(heir) == "none") {
        return HeirChange{.heir = std::nullopt};
    }
    if (!heir.is_pid() || size > 3) {
        return std::nullopt;
    }
    const auto data = size == 3 ? std::optional{option.tuple_element_unchecked(2)} : std::nullopt;
    return HeirChange{.heir = Heir{.pid = heir.word(), .data = data}};
}
} // namespace clause::runtime::builtins::ets
