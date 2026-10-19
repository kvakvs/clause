#include "select.hpp"
#include <array>
#include <chrono>
#include <terms.hpp>

// The ets read builtins (plan 21 steps LIB10A, LIB10C, LIB10D, docs/builtins.md#ets): lookup/2, member/2,
// lookup_element/3,4, tab2list/1, slot/2 and safe_fixtable/2. Objects are copied into the caller's heap while their
// part is locked.
namespace clause::runtime::builtins::ets {
namespace {
using detail::ets::Index;

// lookup(Table, Key): the objects of Key.
Term lookup(ProcessContext &context, const TableArgument &argument, const Term &term) {
    const auto found = table(context, argument, Access::read);
    const auto at = key(context, *found, term);
    auto result = need(TermFactory(context).nil());
    need_table(found->read(at, [&](const Index &index) {
        if (const auto *objects = index.find(at)) {
            result = objects_list(context, *objects);
        }
    }));
    return result;
}

// member(Table, Key).
Term member(ProcessContext &context, const TableArgument &argument, const Term &term) {
    const auto found = table(context, argument, Access::read);
    const auto at = key(context, *found, term);
    auto present = false;
    need_table(found->read(at, [&at, &present](const Index &index) { present = index.find(at) != nullptr; }));
    return need(TermFactory(context).boolean(present));
}

// A 1-based element position argument; badarg unless a positive small integer.
std::size_t position(const Term &term) {
    const auto value = small(term.word());
    if (!value || *value < 1) {
        bad_argument();
    }
    return static_cast<std::size_t>(*value);
}

// Element `at` of a stored object, copied out; badarg when the object is shorter.
Term element(ProcessContext &context, const DetachedTerm &object, std::size_t at) {
    const auto stored = object.term();
    if (stored.tuple_size_unchecked() < at) {
        bad_argument();
    }
    return copy_out(context, stored.tuple_element_unchecked(at - 1).word());
}

// The element at `at` of a key's object, or the list of them in a bag; none when the key is not in the table.
std::optional<Term> elements(ProcessContext &context, const TableArgument &argument, const Term &term, std::size_t at) {
    const auto found = table(context, argument, Access::read);
    const auto probe = key(context, *found, term);
    std::optional<Term> result;
    need_table(found->read(probe, [&](const Index &index) {
        const auto *objects = index.find(probe);
        if (objects == nullptr) {
            return;
        }
        if (found->options().type == TableType::set || found->options().type == TableType::ordered_set) {
            result = element(context, objects->front(), at);
            return;
        }
        std::vector<Term> values;
        for (const auto &object : *objects) {
            values.push_back(element(context, object, at));
        }
        result = need(TermFactory(context).list(values));
    }));
    return result;
}

// lookup_element(Table, Key, Pos): badarg when Key is not in the table.
Term lookup_element3(ProcessContext &context, const TableArgument &argument, const Term &term, const Term &at) {
    const auto value = elements(context, argument, term, position(at));
    if (!value) {
        bad_argument();
    }
    return *value;
}

// lookup_element(Table, Key, Pos, Default): Default when Key is not in the table.
Term lookup_element4(ProcessContext &context, const TableArgument &argument, const Term &term, const Term &at,
                     const Term &fallback) {
    return elements(context, argument, term, position(at)).value_or(fallback);
}

// tab2list(Table): every object, in walk order, in portions (plan 21 step LIB10F).
Word tab2list(ProcessContext &context, const TableArgument &argument) {
    return walk_table(context, table(context, argument, Access::read), nullptr, Gather::objects, false);
}

// slot(Table, I): the objects of slot I, '$end_of_table' just past the last slot, badarg beyond it.
Term slot(ProcessContext &context, const TableArgument &argument, const Term &index) {
    const auto found = table(context, argument, Access::read);
    const auto number = small(index.word());
    if (!number || *number < 0) {
        bad_argument();
    }
    // The objects are copied while their part is locked, so no other process frees them meanwhile.
    std::vector<Term> objects;
    const auto reached = found->visit_slot(static_cast<std::size_t>(*number), [&](std::span<const Word> stored) {
        objects.reserve(stored.size());
        for (const auto word : stored) {
            objects.push_back(copy_out(context, word));
        }
    });
    need_table(reached != Reached::deleted && reached != Reached::missing);
    if (reached == Reached::end) {
        return end_of_table(context);
    }
    return need(TermFactory(context).list(objects));
}

// safe_fixtable(Table, Fix): one more or one fewer fixation by the caller.
Term safe_fixtable(ProcessContext &context, const TableArgument &argument, const Term &fix) {
    const auto found = table(context, argument, Access::read);
    const auto flag = spelling(fix);
    if (flag != "true" && flag != "false") {
        bad_argument();
    }
    auto &tables = context.ets_tables();
    if (flag == "true") {
        using namespace std::chrono;
        const detail::ets::FixTime time{
            .monotonic_nanos = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count(),
            .system_micros = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count()};
        need_table(tables.fix(*found, self(context), time));
    } else {
        need_table(tables.unfix(*found, self(context)));
    }
    return atom(context, "true");
}

constexpr std::array ETS_READ_BUILTINS{
    typed_entry<lookup>("ets", "lookup"),
    typed_entry<member>("ets", "member"),
    typed_entry<lookup_element3>("ets", "lookup_element"),
    typed_entry<lookup_element4>("ets", "lookup_element"),
    typed_entry<tab2list>("ets", "tab2list"),
    typed_entry<slot>("ets", "slot"),
    typed_entry<safe_fixtable>("ets", "safe_fixtable"),
};
} // namespace
} // namespace clause::runtime::builtins::ets

namespace clause::runtime {
std::span<const BuiltinEntry> ets_read_builtins() noexcept { return builtins::ets::ETS_READ_BUILTINS; }
} // namespace clause::runtime
