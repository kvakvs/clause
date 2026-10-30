#include "../../terms/identities.hpp"
#include "ets.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <terms.hpp>
#include <utility>

// ets:info/1,2 (plan 21 step LIB10A, docs/builtins.md#ets): the items in OTP's order, undefined for a table name or
// identifier that names no table, and badarg for any other argument or item.
namespace clause::runtime::builtins::ets {
namespace {
using detail::ets::WriteConcurrency;

// The info list's items in OTP's order.
constexpr std::array INFO_ITEMS{"id",
                                "decentralized_counters",
                                "read_concurrency",
                                "write_concurrency",
                                "compressed",
                                "memory",
                                "owner",
                                "heir",
                                "name",
                                "size",
                                "node",
                                "named_table",
                                "type",
                                "keypos",
                                "protection"};

Term boolean(ProcessContext &context, bool value) { return need(TermFactory(context).boolean(value)); }

Term integer(ProcessContext &context, std::size_t value) {
    return need(TermFactory(context).integer(static_cast<std::int64_t>(value)));
}

Term type_name(ProcessContext &context, TableType type) {
    constexpr std::array names{"set", "ordered_set", "bag", "duplicate_bag"};
    return atom(context, names[static_cast<std::size_t>(type)]);
}

Term protection_name(ProcessContext &context, Protection protection) {
    constexpr std::array names{"public", "protected", "private"};
    return atom(context, names[static_cast<std::size_t>(protection)]);
}

Term write_concurrency(ProcessContext &context, WriteConcurrency value) {
    return value == WriteConcurrency::automatic ? atom(context, "auto"_atom)
                                                : boolean(context, value == WriteConcurrency::on);
}

Term pid(ProcessContext &context, Word pid) { return need(Term::from_word(pid, context)); }

Term heir(ProcessContext &context, const Table &table) {
    return table.heir() == 0 ? atom(context, "none"_atom) : pid(context, table.heir());
}

// A count the table gives, raising badarg when it was deleted meanwhile.
Term counted(ProcessContext &context, std::optional<std::size_t> value) {
    need_table(value.has_value());
    return integer(context, *value);
}

// {Time, [{Pid, Count}]} of a fixed table: Time as erlang:timestamp/0 or in native monotonic units; false when the
// table is not fixed.
Term safe_fixed(ProcessContext &context, const Table &table, bool monotonic) {
    const auto fixation = table.fixation();
    if (!fixation) {
        return boolean(context, false);
    }
    TermFactory factory(context);
    std::vector<Term> holders;
    for (const auto &[holder, count] : fixation->holders) {
        holders.push_back(need(factory.tuple(std::array{pid(context, holder), integer(context, count)})));
    }
    const auto micros = fixation->system_micros;
    const auto time = monotonic ? need(factory.integer(fixation->monotonic_nanos))
                                : need(factory.tuple(std::array{need(factory.integer(micros / 1'000'000'000'000)),
                                                                need(factory.integer((micros / 1'000'000) % 1'000'000)),
                                                                need(factory.integer(micros % 1'000'000))}));
    return need(factory.tuple(std::array{time, need(factory.list(holders))}));
}

// The chain lengths of a hash table's buckets, for stats.
struct Chains {
    std::vector<std::size_t> lengths;
    std::size_t kept = 0;
};

Chains chains(const Table &table) {
    Chains result;
    std::vector<Word> objects;
    for (std::size_t slot = 0; table.slot(slot, objects) == Reached::objects; ++slot) {
        result.lengths.push_back(objects.size());
        objects.clear();
    }
    need_table(table.read_parts([&result](const detail::ets::Index &index) { result.kept += index.kept(); }));
    return result;
}

// OTP's internal statistics of a hash table: {Buckets, AverageChain, StdDeviation, ExpectedStdDeviation,
// MinChain, MaxChain, KeptRemovedObjects, Locks}; false for an ordered_set.
Term stats(ProcessContext &context, const Table &table) {
    if (!table.hashed()) {
        return boolean(context, false);
    }
    const auto found = chains(table);
    const auto buckets = static_cast<double>(std::max<std::size_t>(found.lengths.size(), 1));
    double total = 0;
    for (const auto length : found.lengths) {
        total += static_cast<double>(length);
    }
    const auto average = total / buckets;
    double squares = 0;
    for (const auto length : found.lengths) {
        squares += (static_cast<double>(length) - average) * (static_cast<double>(length) - average);
    }
    TermFactory factory(context);
    const auto [low, high] = std::ranges::minmax(found.lengths.empty() ? std::vector<std::size_t>{0} : found.lengths);
    const std::array fields{integer(context, found.lengths.size()),
                            need(factory.floating(average)),
                            need(factory.floating(std::sqrt(squares / buckets))),
                            need(factory.floating(std::sqrt(average))),
                            integer(context, low),
                            integer(context, high),
                            integer(context, found.kept),
                            integer(context, table.part_count())};
    return need(factory.tuple(fields));
}

// The items every table has, also listed by info/1; none for another name.
std::optional<Term> listed_item(ProcessContext &context, const Table &table, std::string_view item) {
    const auto &options = table.options();
    if (item == "id") {
        return identifier(context, table);
    }
    if (item == "decentralized_counters") {
        return boolean(context, options.decentralized_counters);
    }
    if (item == "read_concurrency") {
        return boolean(context, options.read_concurrency);
    }
    if (item == "write_concurrency") {
        return write_concurrency(context, options.write_concurrency);
    }
    if (item == "compressed") {
        return boolean(context, options.compressed);
    }
    if (item == "memory") {
        return counted(context, table.words());
    }
    if (item == "owner") {
        return pid(context, table.owner());
    }
    return std::nullopt;
}

std::optional<Term> named_item(ProcessContext &context, const Table &table, std::string_view item) {
    const auto &options = table.options();
    if (item == "heir") {
        return heir(context, table);
    }
    if (item == "name") {
        return need(Term::from_word(table.name(), context));
    }
    if (item == "size") {
        return counted(context, table.size());
    }
    if (item == "node") {
        return need(detail::this_node_atom(context));
    }
    if (item == "named_table") {
        return boolean(context, options.named);
    }
    if (item == "type") {
        return type_name(context, options.type);
    }
    if (item == "keypos") {
        return integer(context, options.keypos);
    }
    if (item == "protection") {
        return protection_name(context, table.protection());
    }
    return std::nullopt;
}

// The items only info/2 gives.
std::optional<Term> extra_item(ProcessContext &context, const Table &table, std::string_view item) {
    if (item == "fixed") {
        const auto fixed = table.fixed();
        need_table(fixed.has_value());
        return boolean(context, *fixed);
    }
    if (item == "safe_fixed" || item == "safe_fixed_monotonic_time") {
        return safe_fixed(context, table, item == "safe_fixed_monotonic_time");
    }
    if (item == "stats") {
        return stats(context, table);
    }
    // OTP's debugging items: data prints the table, binary lists its off-heap binaries (docs/differences.md).
    if (item == "data") {
        return boolean(context, true);
    }
    if (item == "binary") {
        return need(TermFactory(context).nil());
    }
    return std::nullopt;
}

std::optional<Term> item_value(ProcessContext &context, const Table &table, std::string_view item) {
    if (auto value = listed_item(context, table, item)) {
        return value;
    }
    if (auto value = named_item(context, table, item)) {
        return value;
    }
    return extra_item(context, table, item);
}

// info(Table): every listed item as {Item, Value}.
Term info1(ProcessContext &context, const TableArgument &argument) {
    const auto found = table_if_any(context, argument);
    if (!found) {
        return atom(context, "undefined"_atom);
    }
    TermFactory factory(context);
    std::vector<Term> items;
    items.reserve(INFO_ITEMS.size());
    for (const auto *item : INFO_ITEMS) {
        items.push_back(need(factory.tuple(std::array{atom(context, item), *item_value(context, *found, item)})));
    }
    return need(factory.list(items));
}

// info(Table, Item).
Term info2(ProcessContext &context, const TableArgument &argument, const AtomArgument &item) {
    const auto found = table_if_any(context, argument);
    if (!found) {
        return atom(context, "undefined"_atom);
    }
    const auto value = item_value(context, *found, item.spelling);
    if (!value) {
        bad_argument();
    }
    return *value;
}

constexpr std::array ETS_INFO_BUILTINS{
    typed_entry<info1>("ets", "info"),
    typed_entry<info2>("ets", "info"),
};
} // namespace
} // namespace clause::runtime::builtins::ets

namespace clause::runtime {
std::span<const BuiltinEntry> ets_info_builtins() noexcept { return builtins::ets::ETS_INFO_BUILTINS; }
} // namespace clause::runtime
