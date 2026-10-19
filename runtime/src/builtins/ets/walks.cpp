#include "ets.hpp"
#include <array>
#include <terms.hpp>

// The ets walk builtins (plan 21 step LIB10D, docs/builtins.md#ets): first/1, next/2, last/1, prev/2 and their
// *_lookup forms, which give {Key, Objects}. A hash table walks its parts and buckets in order and backwards as
// forwards; a step from a key it does not hold is badarg unless the table is fixed and keeps the key. An ordered_set
// walks in term order from any key.
namespace clause::runtime::builtins::ets {
namespace {
// What a step gives: the key, or {Key, Objects}.
enum class Shape : std::uint8_t { key, lookup };

// The value of the objects a step reached, built while their part is locked.
Term reached_value(ProcessContext &context, const Table &table, const Objects &objects, Shape shape) {
    const auto key = key_term(context, table, objects);
    if (shape == Shape::key) {
        return key;
    }
    return need(TermFactory(context).tuple(std::array{key, objects_list(context, objects)}));
}

// Run one walk step and give its value, '$end_of_table' at the end, or badarg.
template <typename Walk> Term walk(ProcessContext &context, const TableArgument &argument, Shape shape, Walk step) {
    const auto found = table(context, argument, Access::read);
    std::optional<Term> value;
    const auto reached =
        step(*found, [&](const Objects &objects) { value = reached_value(context, *found, objects, shape); });
    need_table(reached == Reached::objects || reached == Reached::end);
    return value ? *value : end_of_table(context);
}

template <Shape S> Term first(ProcessContext &context, const TableArgument &argument) {
    return walk(context, argument, S, [](const Table &table, const auto &visit) { return table.first(visit); });
}

template <Shape S> Term last(ProcessContext &context, const TableArgument &argument) {
    return walk(context, argument, S, [](const Table &table, const auto &visit) { return table.last(visit); });
}

template <Shape S> Term next(ProcessContext &context, const TableArgument &argument, const Term &from) {
    return walk(context, argument, S, [&context, &from](const Table &table, const auto &visit) {
        return table.next(key(context, table, from), visit);
    });
}

template <Shape S> Term prev(ProcessContext &context, const TableArgument &argument, const Term &from) {
    return walk(context, argument, S, [&context, &from](const Table &table, const auto &visit) {
        return table.prev(key(context, table, from), visit);
    });
}

constexpr std::array ETS_WALK_BUILTINS{
    typed_entry<first<Shape::key>>("ets", "first"),
    typed_entry<next<Shape::key>>("ets", "next"),
    typed_entry<last<Shape::key>>("ets", "last"),
    typed_entry<prev<Shape::key>>("ets", "prev"),
    typed_entry<first<Shape::lookup>>("ets", "first_lookup"),
    typed_entry<next<Shape::lookup>>("ets", "next_lookup"),
    typed_entry<last<Shape::lookup>>("ets", "last_lookup"),
    typed_entry<prev<Shape::lookup>>("ets", "prev_lookup"),
};
} // namespace
} // namespace clause::runtime::builtins::ets

namespace clause::runtime {
std::span<const BuiltinEntry> ets_walk_builtins() noexcept { return builtins::ets::ETS_WALK_BUILTINS; }
} // namespace clause::runtime
