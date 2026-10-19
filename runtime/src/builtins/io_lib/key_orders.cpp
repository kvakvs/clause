#include "key_orders.hpp"
#include "../../memory/heap_object.hpp"
#include <array>
#include <clause/runtime/code_server.hpp>
#include <stdexcept>
#include <terms.hpp>

// maps_order comparison funs (plan 21 step LIB4F): the sorted keys of the running call, the maps it met without
// them, and clause_io_lib:apply_ordered/4, which runs a call again with sorted keys.
namespace clause::runtime::builtins::io_lib {
namespace key_orders_detail {
namespace {
// The innermost open scope of this thread, and the sorted keys apply_ordered hands the next scope.
thread_local Scope *current = nullptr;
thread_local Word handed = 0;

bool same(const Term &left, Word right) {
    return left.word() == right || need(left.exactly_equal(detail::TermAccess::make(right)));
}

// The keys `fun` sorted for `map` in the sorted keys [{Fun, Map, Keys}], if there.
std::optional<Term> keys_in(Word known, const Term &map, Word fun) {
    for (auto rest = detail::TermAccess::make(known); rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto row = rest.head_unchecked();
        if (same(row.tuple_element_unchecked(0), fun) && same(row.tuple_element_unchecked(1), map.word())) {
            return row.tuple_element_unchecked(2);
        }
    }
    return std::nullopt;
}

// Record a map met without its sorted keys, once.
void record(Scope &scope, const Term &map, Word fun) {
    for (const auto &[seen_fun, seen_map] : scope.missing) {
        if (seen_fun == fun && same(map, seen_map)) {
            return;
        }
    }
    scope.missing.emplace_back(fun, map.word());
}

// The registry names of a builtin body, found among the production builtins.
const BuiltinEntry &entry_of(BuiltinBody body) {
    for (const auto family : production_builtins()) {
        for (const auto &entry : family) {
            if (entry.body == body) {
                return entry;
            }
        }
    }
    throw std::logic_error("ordering builtin without a registry entry");
}

// The maps met, as [{Fun, Map}].
Term missing_list(ProcessContext &context, const Scope &scope) {
    TermFactory factory(context);
    std::vector<Term> rows;
    rows.reserve(scope.missing.size());
    for (const auto &[fun, map] : scope.missing) {
        rows.push_back(need(factory.tuple(std::array{detail::TermAccess::make(fun), detail::TermAccess::make(map)})));
    }
    return need(factory.list(rows));
}
} // namespace

void open(Scope &scope) noexcept {
    scope.known = std::exchange(handed, 0);
    scope.outer = std::exchange(current, &scope);
}

void close(Scope &scope) noexcept { current = scope.outer; }

Word finish(ProcessContext &context, BuiltinBody body, std::span<const Word> words, Word result, const Scope &scope,
            bool badarg) {
    if (scope.missing.empty() || failed(context)) {
        return result;
    }
    // The call's own continuation, if it trapped to write its text, is dropped with its result.
    static_cast<void>(context.stack().take_trap());
    context.stack().drop_trap_state();
    const auto &entry = entry_of(body);
    TermFactory factory(context);
    std::vector<Term> arguments;
    arguments.reserve(words.size());
    for (const auto word : words) {
        arguments.push_back(detail::TermAccess::make(word));
    }
    const std::array state{
        missing_list(context, scope).word(),     scope.known == 0 ? need(factory.nil()).word() : scope.known,
        need(factory.atom(entry.module)).word(), need(factory.atom(entry.function)).word(),
        need(factory.list(arguments)).word(),    need(factory.boolean(badarg)).word()};
    const auto *frame = static_cast<const abi::v1::FrameDescriptor *>(context.code_server().function_frame(
        need(factory.atom("clause_io_lib"_atom)), need(factory.atom("sort_maps"_atom)), 6));
    if (frame == nullptr) {
        bad_argument();
    }
    context.stack().trap(*frame, state);
    return 0;
}
} // namespace key_orders_detail

bool keys_missing() noexcept {
    return key_orders_detail::current != nullptr && !key_orders_detail::current->missing.empty();
}

std::vector<std::pair<Term, Term>> sorted_entries(const Term &map, Word fun) {
    auto *scope = key_orders_detail::current;
    if (scope == nullptr) {
        bad_argument();
    }
    const auto keys = scope->known == 0 ? std::nullopt : key_orders_detail::keys_in(scope->known, map, fun);
    if (!keys) {
        key_orders_detail::record(*scope, map, fun);
        return need(map.map_entries());
    }
    std::vector<std::pair<Term, Term>> entries;
    for (auto rest = *keys; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto key = rest.head_unchecked();
        entries.emplace_back(key, *need(map.map_find(key)));
    }
    return entries;
}

namespace {
// clause_io_lib:apply_ordered(Sorted, Module, Function, Args): run the builtin Module:Function on Args, handing it
// the sorted keys [{Fun, Map, Keys}].
Word apply_ordered(ProcessContext &context, std::span<const Word> words) {
    const auto module = need(Term::from_word(words[1], context));
    const auto function = need(Term::from_word(words[2], context));
    std::vector<Word> arguments;
    for (auto rest = need(Term::from_word(words[3], context)); rest.is_cons(); rest = rest.tail_unchecked()) {
        arguments.push_back(rest.head_unchecked().word());
    }
    const auto *builtin = context.code_server().builtins().find(module.atom_spelling_unchecked(),
                                                                function.atom_spelling_unchecked(), arguments.size());
    if (builtin == nullptr) {
        return badarg(context);
    }
    key_orders_detail::handed = words[0];
    const auto result = call_builtin_portion(context, *builtin, arguments.data());
    key_orders_detail::handed = 0;
    return result;
}

constexpr std::array KEY_ORDER_BUILTINS{
    BuiltinEntry{"clause_io_lib", "apply_ordered", 4, &apply_ordered},
};
} // namespace
} // namespace clause::runtime::builtins::io_lib

namespace clause::runtime {
std::span<const BuiltinEntry> io_lib_order_builtins() noexcept { return builtins::io_lib::KEY_ORDER_BUILTINS; }
} // namespace clause::runtime
