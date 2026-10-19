#pragma once
#include "../typed.hpp"
#include <clause/runtime/builtin_registry.hpp>
#include <span>
#include <utility>
#include <vector>

// maps_order comparison funs (plan 21 step LIB4F, docs/builtins.md#io-lib): a builtin cannot call a fun, so a call
// that meets a map ordered by one writes it in Clause's key order, records it and, at its end, continues in
// clause_io_lib:sort_maps/6 instead of returning. That sorts the keys as maps:iterator/2 does and runs the call again
// through clause_io_lib:apply_ordered/4, which hands the sorted keys to it; maps met only then go round again.
namespace clause::runtime::builtins::io_lib {
// The associations of `map` in the order `fun` sorts its keys, from the running call's sorted keys; Clause's key
// order, with the map recorded, when they are not there yet. Badarg outside a call that may sort keys.
std::vector<std::pair<Term, Term>> sorted_entries(const Term &map, Word fun);

// Whether the running call met a map whose sorted keys it lacks: its text is then not written (io:format).
bool keys_missing() noexcept;

namespace key_orders_detail {
// The maps a call met without their sorted keys, and the sorted keys it was given ([{Fun, Map, Keys}], 0 for none).
struct Scope {
    Word known = 0;
    std::vector<std::pair<Word, Word>> missing;
    Scope *outer = nullptr;
};

// Open a scope for a call, with the sorted keys apply_ordered handed over, if any.
void open(Scope &scope) noexcept;
void close(Scope &scope) noexcept;
// The call's result, or, when it met maps without sorted keys, a continuation in clause_io_lib:sort_maps/6;
// `body` is the call's registered body, `words` its arguments.
Word finish(ProcessContext &context, BuiltinBody body, std::span<const Word> words, Word result, const Scope &scope,
            bool badarg);
} // namespace key_orders_detail

// The body of an io_lib or io builtin that may write maps ordered by comparison funs; `Badarg` when the call's
// errors become badarg (io_lib:format's catch), and so do the funs'.
template <BuiltinBody Body, bool Badarg> Word ordering(ProcessContext &context, std::span<const Word> words) {
    key_orders_detail::Scope scope;
    key_orders_detail::open(scope);
    const auto result = Body(context, words);
    key_orders_detail::close(scope);
    return key_orders_detail::finish(context, &ordering<Body, Badarg>, words, result, scope, Badarg);
}

// The registry entry of a typed builtin wrapped by ordering.
template <auto Function, bool Badarg>
constexpr BuiltinEntry ordering_entry(std::string_view module, std::string_view function) {
    return BuiltinEntry{module, function, typed_detail::Signature<decltype(Function)>::arity,
                        &ordering<typed<Function>, Badarg>};
}
} // namespace clause::runtime::builtins::io_lib
