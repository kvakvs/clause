#include "../../memory/heap_object.hpp"
#include "../../terms/integers.hpp"
#include "ets.hpp"
#include <algorithm>
#include <array>
#include <iterator>
#include <terms.hpp>

// The ets update builtins (plan 21 step LIB10C, docs/builtins.md#ets): update_counter/3,4 and update_element/3,4
// on set and ordered_set tables. An update reads the object, builds the new one in the caller's heap and stores it
// with the key's part locked throughout, so it is atomic; every operation is checked before anything is stored.
// The 4-argument forms insert their default object, with the key put in its key position, when the key is missing.
namespace clause::runtime::builtins::ets {
namespace {
using detail::Integer;
using detail::ets::Index;

// The elements of the object an update changes.
using Fields = std::vector<Term>;
// The operations and the default object of an update: roles of their own beside the key.
using Operations = Named<struct OperationsRole>;
using DefaultObject = Named<struct DefaultObjectRole>;

// A 1-based field position of an update, which must not be the key position; badarg otherwise.
std::size_t field_position(const Term &term, std::size_t keypos) {
    const auto value = small(term.word());
    if (!value || *value < 1 || static_cast<std::size_t>(*value) == keypos) {
        bad_argument();
    }
    return static_cast<std::size_t>(*value);
}

Integer integer_of(const Term &term) {
    auto value = need(Argument<Integer>::convert(term));
    if (!value) {
        bad_argument();
    }
    return std::move(*value);
}

// One counter operation: the field, its increment and, if given, the threshold past which it is set to a value.
struct CounterOp {
    std::size_t position;
    Integer increment;
    std::optional<std::pair<Integer, Integer>> limit;
};

// {Pos, Incr} or {Pos, Incr, Threshold, SetValue}; badarg for anything else.
CounterOp counter_op(const Term &op, std::size_t keypos) {
    if (!op.is_tuple() || (op.tuple_size_unchecked() != 2 && op.tuple_size_unchecked() != 4)) {
        bad_argument();
    }
    CounterOp result{.position = field_position(op.tuple_element_unchecked(0), keypos),
                     .increment = integer_of(op.tuple_element_unchecked(1)),
                     .limit = std::nullopt};
    if (op.tuple_size_unchecked() == 4) {
        result.limit = std::pair{integer_of(op.tuple_element_unchecked(2)), integer_of(op.tuple_element_unchecked(3))};
    }
    return result;
}

// The operations of an UpdateOp argument: an increment of the field after the key, one operation tuple or a list of
// them; whether the result is a list.
struct CounterOps {
    std::vector<CounterOp> ops;
    bool listed = false;
};

CounterOps counter_ops(const Term &ops, std::size_t keypos) {
    CounterOps result;
    if (ops.is_integer()) {
        result.ops.push_back({.position = keypos + 1, .increment = integer_of(ops), .limit = std::nullopt});
        return result;
    }
    if (ops.is_tuple()) {
        result.ops.push_back(counter_op(ops, keypos));
        return result;
    }
    result.listed = true;
    auto rest = ops;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        result.ops.push_back(counter_op(rest.head_unchecked(), keypos));
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return result;
}

// Apply counter operations to the fields in order; the new values.
std::vector<Term> count(ProcessContext &context, Fields &fields, const CounterOps &ops) {
    std::vector<Term> values;
    for (const auto &op : ops.ops) {
        if (op.position > fields.size() || !fields[op.position - 1].is_integer()) {
            bad_argument();
        }
        auto value = integer_of(fields[op.position - 1]) + op.increment;
        if (op.limit && (op.increment >= 0 ? value > op.limit->first : value < op.limit->first)) {
            value = op.limit->second;
        }
        fields[op.position - 1] = need(detail::IntegerAccess::make(context.heap(), value));
        values.push_back(fields[op.position - 1]);
    }
    return values;
}

// One element update {Pos, Value}.
struct ElementOp {
    std::size_t position;
    Term value;
};

ElementOp element_op(const Term &op, std::size_t keypos) {
    if (!op.is_tuple() || op.tuple_size_unchecked() != 2) {
        bad_argument();
    }
    return {.position = field_position(op.tuple_element_unchecked(0), keypos), .value = op.tuple_element_unchecked(1)};
}

std::vector<ElementOp> element_ops(const Term &ops, std::size_t keypos) {
    std::vector<ElementOp> result;
    if (ops.is_tuple()) {
        result.push_back(element_op(ops, keypos));
        return result;
    }
    auto rest = ops;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        result.push_back(element_op(rest.head_unchecked(), keypos));
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return result;
}

// Apply element updates to the fields in order, each checked first.
void set_elements(Fields &fields, const std::vector<ElementOp> &ops) {
    for (const auto &op : ops) {
        if (op.position > fields.size()) {
            bad_argument();
        }
    }
    for (const auto &op : ops) {
        fields[op.position - 1] = op.value;
    }
}

Fields fields_of(const Term &tuple) {
    Fields fields;
    for (std::size_t at = 0; at < tuple.tuple_size_unchecked(); ++at) {
        fields.push_back(tuple.tuple_element_unchecked(at));
    }
    return fields;
}

// The fields of a default object with the key in its key position; badarg unless a tuple that long.
Fields default_fields(const Term &fallback, std::size_t keypos, const Term &key) {
    if (!fallback.is_tuple() || fallback.tuple_size_unchecked() < keypos) {
        bad_argument();
    }
    auto fields = fields_of(fallback);
    fields[keypos - 1] = key;
    return fields;
}

// The table of an update: a set or ordered_set the caller may write; badarg otherwise.
std::shared_ptr<Table> updated_table(ProcessContext &context, const TableArgument &argument) {
    auto found = table(context, argument, Access::write);
    const auto type = found->options().type;
    if (type != TableType::set && type != TableType::ordered_set) {
        bad_argument();
    }
    return found;
}

// Run an update on the object of `term`: `change` edits its fields and gives the result. None when the key is
// missing and there is no default.
template <typename Change>
std::optional<Term> update(ProcessContext &context, Table &found, const Term &term, const std::optional<Term> &fallback,
                           Change change) {
    const auto at = key(context, found, term);
    std::optional<Term> result;
    need_table(found.write(at, [&](Index &index) {
        const auto *objects = index.find(at);
        if (objects == nullptr && !fallback) {
            return;
        }
        auto fields = objects != nullptr ? fields_of(copy_out(context, objects->front().word()))
                                         : default_fields(*fallback, found.options().keypos, term);
        result = change(fields);
        auto object = need(DetachedTerm::copy(context, need(TermFactory(context).tuple(fields))));
        if (objects != nullptr) {
            index.replace(at, std::move(object));
        } else {
            index.insert(at, std::move(object));
        }
    }));
    return result;
}

// Whether every operation's field holds a small integer word.
bool small_fields(const Fields &fields, const CounterOps &ops) {
    return std::ranges::all_of(ops.ops, [&](const CounterOp &op) {
        return op.position <= fields.size() && small(fields[op.position - 1].word()).has_value();
    });
}

// Apply the operations to a stored tuple's fields where they are, if its counters are and stay small integers; the
// update_counter value, or none when the general path must run.
std::optional<Term> count_stored(ProcessContext &context, std::span<Word> stored, const CounterOps &ops) {
    Fields fields;
    std::ranges::transform(stored, std::back_inserter(fields),
                           [](Word word) { return detail::TermAccess::make(word); });
    if (stored.empty() || !small_fields(fields, ops)) {
        return std::nullopt;
    }
    const auto values = count(context, fields, ops);
    if (!small_fields(fields, ops)) {
        return std::nullopt;
    }
    for (const auto &op : ops.ops) {
        stored[op.position - 1] = fields[op.position - 1].word();
    }
    return ops.listed ? need(TermFactory(context).list(values)) : values.front();
}

// update_counter in place (plan 21 step LIB10F): when the key's object is the table's own copy and its counters stay
// small integers, the stored fields change where they are, as OTP's do; none when the general path must run.
std::optional<Term> count_in_place(ProcessContext &context, Table &found, const Term &term, const CounterOps &ops) {
    const auto at = key(context, found, term);
    std::optional<Term> result;
    need_table(found.write(at, [&](Index &index) {
        auto *objects = index.find_stored(at);
        result = count_stored(context, objects == nullptr ? std::span<Word>{} : objects->front().own_tuple(), ops);
    }));
    return result;
}

// update_counter/3,4: the new value, or their list for a list of operations; badarg for a missing key without a
// default.
Term counter(ProcessContext &context, const TableArgument &argument, const Term &term, const Operations &ops,
             const std::optional<Term> &fallback) {
    const auto found = updated_table(context, argument);
    const auto parsed = counter_ops(ops.term, found->options().keypos);
    if (const auto in_place = count_in_place(context, *found, term, parsed)) {
        return *in_place;
    }
    const auto result = update(context, *found, term, fallback, [&](Fields &fields) {
        auto values = count(context, fields, parsed);
        return parsed.listed ? need(TermFactory(context).list(values)) : values.front();
    });
    if (!result) {
        bad_argument();
    }
    return *result;
}

// update_counter(Table, Key, UpdateOp).
Term update_counter3(ProcessContext &context, const TableArgument &argument, const Term &term, const Operations &ops) {
    return counter(context, argument, term, ops, std::nullopt);
}

// update_counter(Table, Key, UpdateOp, Default).
Term update_counter4(ProcessContext &context, const TableArgument &argument, const Term &term, const Operations &ops,
                     const DefaultObject &fallback) {
    return counter(context, argument, term, ops, fallback.term);
}

// update_element/3,4: true, or false for a missing key without a default.
Term elements(ProcessContext &context, const TableArgument &argument, const Term &term, const Operations &ops,
              const std::optional<Term> &fallback) {
    const auto found = updated_table(context, argument);
    const auto parsed = element_ops(ops.term, found->options().keypos);
    const auto result = update(context, *found, term, fallback, [&](Fields &fields) {
        set_elements(fields, parsed);
        return atom(context, "true"_atom);
    });
    return result ? *result : atom(context, "false"_atom);
}

// update_element(Table, Key, ElementSpec).
Term update_element3(ProcessContext &context, const TableArgument &argument, const Term &term, const Operations &ops) {
    return elements(context, argument, term, ops, std::nullopt);
}

// update_element(Table, Key, ElementSpec, Default).
Term update_element4(ProcessContext &context, const TableArgument &argument, const Term &term, const Operations &ops,
                     const DefaultObject &fallback) {
    return elements(context, argument, term, ops, fallback.term);
}

constexpr std::array ETS_UPDATE_BUILTINS{
    typed_entry<update_counter3>("ets", "update_counter"),
    typed_entry<update_counter4>("ets", "update_counter"),
    typed_entry<update_element3>("ets", "update_element"),
    typed_entry<update_element4>("ets", "update_element"),
};
} // namespace
} // namespace clause::runtime::builtins::ets

namespace clause::runtime {
std::span<const BuiltinEntry> ets_update_builtins() noexcept { return builtins::ets::ETS_UPDATE_BUILTINS; }
} // namespace clause::runtime
