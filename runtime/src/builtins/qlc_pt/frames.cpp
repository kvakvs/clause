#include "../collections/collections.hpp"
#include "../front_end/abstract.hpp"
#include "qlc_pt.hpp"
#include <algorithm>
#include <cmath>

// The run-time qlc transform's unification (plan 21 step LIB73): OTP's frames of #bind{} records, which tell the
// columns patterns and filters compare or match with constants and other columns.
namespace clause::runtime::builtins::qlc_pt {
using front_end::forms::element;
using front_end::forms::is_atom;
using front_end::forms::is_node;

namespace {
bool is_var(const Term &term) { return is_node(term, "var", 3); }

bool is_failed(const Term &frame) { return is_atom(frame, "failed"); }

// is_unique_var/1.
bool is_unique_var(const Term &var) { return element(var, 2).is_reference(); }

// The {bind, Var, Value, Op} of a frame's binds whose variable is `var` (=:=), newest first.
TermList var_bindings(const Term &var, const TermList &binds) {
    TermList out;
    for (const auto &bind : binds) {
        if (same(element(bind, 1), var)) {
            out.push_back(bind);
        }
    }
    return out;
}

bool is_imported(const Term &name, const Imported &imported) {
    return std::ranges::binary_search(imported.vars, name, less);
}

bool has_imported_vars(const Term &value, const Imported &imported) {
    const auto names = vars_of(value);
    if (imported.all) {
        return !names.empty();
    }
    return std::ranges::any_of(names, [&](const Term &name) { return member(name, imported.vars); });
}

// has_integer/1: an integer, or a float equal to an integer, anywhere in the abstract code.
bool has_integer(const Term &term) {
    if (is_node(term, "integer", 3) && element(term, 2).is_number()) {
        return true;
    }
    if (is_node(term, "float", 3) && element(term, 2).is_float()) {
        const auto value = need(element(term, 2).float_value());
        return std::round(value) == value;
    }
    return any_child(term, has_integer);
}

// deref_list/1: the values of one combination and '=:=' only when every one is matched.
Term deref_list(const Transformer &t, const TermList &combination) {
    TermList values;
    bool exact = !combination.empty();
    for (const auto &value_op : combination) {
        values.push_back(element(value_op, 0));
        exact = exact && is_atom(element(value_op, 1), "=:=");
    }
    return t.pair(t.list(values), t.atom(exact ? AtomName("=:="_atom) : AtomName("=="_atom)));
}

// all_comb/1 folded into deref_list/1.
void combinations(const Transformer &t, std::span<const TermList> choices, TermList &picked, TermList &out) {
    if (choices.empty()) {
        out.push_back(deref_list(t, picked));
        return;
    }
    for (const auto &choice : choices.front()) {
        picked.push_back(choice);
        combinations(t, choices.subspan(1), picked, out);
        picked.pop_back();
    }
}
} // namespace

TermList Transformer::frame_binds(const Term &frame) { return elements(frame); }

bool Transformer::free_of_integers(const Term &value, const Imported &imported) const {
    return !has_integer(value) && !has_imported_vars(value, imported);
}

std::optional<Term> Transformer::normalise(const Term &expr) const {
    const auto constant = var_mapped(build_, cons_to_tuple(build_, expr), [&](const Term &node) {
        return build_.node("atom"_atom, element(node, 1), element(node, 2));
    });
    try {
        return front_end::abstract::normalise(build_.context(), constant);
    } catch (const BuiltinFailure &failure) {
        if (failure.term) {
            throw;
        }
        return std::nullopt;
    }
}

namespace {
// value_op/3: the operator a dereferenced value keeps through a binding made with `bind_op`.
Term value_op(const Transformer &t, const Term &value_op, const Term &bind_op, const Imported &imported) {
    if (is_atom(element(value_op, 1), "==")) {
        return t.atom("=="_atom);
    }
    if (is_atom(bind_op, "=:=") || t.free_of_integers(element(value_op, 0), imported)) {
        return t.atom("=:="_atom);
    }
    return t.atom("=="_atom);
}

// deref/4 of a variable: the values of its bindings with their operators, or the variable itself.
TermList deref_bound(const Transformer &t, const Term &var, const Term &frame, const DerefTest &test,
                     const Imported &imported) {
    TermList found;
    for (const auto &bind : var_bindings(var, elements(frame))) {
        for (const auto &value_op_pair : t.deref(element(bind, 2), frame, test, imported)) {
            const auto op = value_op(t, value_op_pair, element(bind, 3), imported);
            if (test(element(value_op_pair, 0), op)) {
                found.push_back(t.pair(element(value_op_pair, 0), op));
            }
        }
    }
    return found.empty() ? TermList{t.pair(var, t.atom("=:="_atom))} : usorted(std::move(found));
}

// deref/4 of a list: one value per combination of its elements' values.
TermList deref_listed(const Transformer &t, const TermList &items, const Term &frame, const DerefTest &test,
                      const Imported &imported) {
    std::vector<TermList> choices;
    for (const auto &item : items) {
        choices.push_back(t.deref(item, frame, test, imported));
    }
    TermList picked;
    TermList out;
    combinations(t, choices, picked, out);
    return usorted(std::move(out));
}
} // namespace

TermList Transformer::deref(const Term &term, const Term &frame, const DerefTest &test,
                            const Imported &imported) const {
    if (is_var(term)) {
        return deref_bound(*this, term, frame, test, imported);
    }
    if (term.is_tuple()) {
        TermList out;
        for (const auto &listed : deref_listed(*this, tuple_items(term), frame, test, imported)) {
            out.push_back(pair(build_.tuple(elements(element(listed, 0))), element(listed, 1)));
        }
        return out;
    }
    if (term.is_cons() || term.is_nil()) {
        return deref_listed(*this, elements(term), frame, test, imported);
    }
    return {pair(term, atom("=:="_atom))};
}

TermList Transformer::deref_values(const Term &term, const Term &frame, const DerefTest &test,
                                   const Imported &imported) const {
    TermList out;
    for (const auto &value_op_pair : deref(term, frame, test, imported)) {
        if (test(element(value_op_pair, 0), element(value_op_pair, 1))) {
            out.push_back(element(value_op_pair, 0));
        }
    }
    return usorted(std::move(out));
}

namespace {
// deref_value/5: the dereferenced values of a value bound with an operator.
TermList deref_value(const Transformer &t, const ValueOp &bound, const Term &frame, const DerefTest &test,
                     const Imported &imported) {
    TermList out;
    for (const auto &value_op_pair : t.deref(bound.value, frame, test, imported)) {
        out.push_back(t.pair(element(value_op_pair, 0), value_op(t, value_op_pair, bound.op, imported)));
    }
    return usorted(std::move(out));
}
} // namespace

TermList Transformer::deref_var(const Term &var, const Term &frame, const DerefTest &test,
                                const Imported &imported) const {
    TermList out;
    for (const auto &bind : var_bindings(var, elements(frame))) {
        for (const auto &value_op_pair :
             deref_value(*this, {.value = element(bind, 2), .op = element(bind, 3)}, frame, test, imported)) {
            out.push_back(value_op_pair);
        }
    }
    return usorted(std::move(out));
}

namespace {
// occurs/3: whether `var` occurs in `term` through the frame's bindings.
bool occurs(const Term &var, const Term &term, const Term &frame) {
    if (same(var, term)) {
        return true;
    }
    if (is_var(term)) {
        return std::ranges::any_of(var_bindings(term, elements(frame)),
                                   [&](const Term &bind) { return occurs(var, element(bind, 2), frame); });
    }
    return any_child(term, [&](const Term &child) { return occurs(var, child, frame); });
}

// swap_vv/3: whether one of a variable's bindings binds it to `value` (=:=).
bool bound_to(const TermList &bindings, const Term &value) {
    return std::ranges::any_of(bindings, [&](const Term &bind) { return same(element(bind, 2), value); });
}

// do_swap_var_value/4: an imported variable goes to the end of a binding chain ({Var, Value}).
bool swaps(const std::array<Term, 2> &binding, const Term &frame, const Imported &imported) {
    const auto &[var, value] = binding;
    if (!is_var(var) || !is_var(value)) {
        return false;
    }
    const auto binds = elements(frame);
    if (bound_to(var_bindings(var, binds), value)) {
        return false;
    }
    if (bound_to(var_bindings(value, binds), var)) {
        return true;
    }
    return is_imported(element(var, 2), imported) && !is_imported(element(value, 2), imported);
}

// A unification's settings: the operator, the bind test, the imported variables and whether it is safe.
struct Unification {
    const Term &op;
    const BindTest &bind;
    const Imported &imported;
    bool safe = false;
};

Term add_binding(const Transformer &t, const Unification &u, const Term &var0, const Term &value0, const Term &frame) {
    const auto swapped = swaps({var0, value0}, frame, u.imported);
    const auto &var = swapped ? value0 : var0;
    const auto &value = swapped ? var0 : value0;
    if (!u.bind(u.op, value)) {
        return u.safe ? t.atom("failed"_atom) : frame;
    }
    if (occurs(var, value, frame)) {
        return t.atom("failed"_atom);
    }
    return t.build().terms().cons(t.tuple({t.atom("bind"_atom), var, value, u.op}), frame);
}

// var_values/2: the {Value, Op} the variable is bound to.
TermList var_values(const Transformer &t, const Term &var, const Term &frame) {
    TermList out;
    for (const auto &bind : var_bindings(var, elements(frame))) {
        out.push_back(t.pair(element(bind, 2), element(bind, 3)));
    }
    return out;
}

// The variable and the {Value, Op} of a binding to make.
struct Binding {
    Term var;
    ValueOp value;
};

// already_bound/4: every dereferenced value of the binding's value (through its operator) is one of the variable's.
bool already_bound(const Transformer &t, const Binding &binding, const Term &frame) {
    const DerefTest same_op = [&](const Term &, const Term &bind_op) { return same(binding.value.op, bind_op); };
    const Imported all{.all = true, .vars = {}};
    const auto values = deref_value(t, binding.value, frame, same_op, all);
    const auto vars = t.deref_var(binding.var, frame, same_op, all);
    return minus(values, {vars}).empty();
}
} // namespace

Term Transformer::unify_var_bindings(const TermList &values, const ValueOp &target, const Term &frame,
                                     const BindTest &bind, const Imported &imported, bool safe) {
    auto out = frame;
    for (const auto &value_op_pair : values) {
        const auto both_exact = is_atom(target.op, "=:=") && is_atom(element(value_op_pair, 1), "=:=");
        const auto deref_op = atom(both_exact ? AtomName("=:="_atom) : AtomName("=="_atom));
        out = unify(deref_op, element(value_op_pair, 0), target.value, out, bind, imported, safe);
        if (is_failed(out)) {
            return out;
        }
    }
    return out;
}

namespace {
// maybe_add_binding/8.
Term maybe_add_binding(Transformer &t, const Unification &u, const TermList &values, const Term &var, const Term &value,
                       const Term &frame) {
    const auto unified = t.unify_var_bindings(values, {.value = value, .op = u.op}, frame, u.bind, u.imported, u.safe);
    if (is_failed(unified) || already_bound(t, {.var = var, .value = {.value = value, .op = u.op}}, unified)) {
        return unified;
    }
    return add_binding(t, u, var, value, unified);
}

// extend_frame/7.
Term extend_frame(Transformer &t, const Unification &u, const Term &var, const Term &value, const Term &frame) {
    const auto values = var_values(t, var, frame);
    if (!values.empty()) {
        return maybe_add_binding(t, u, values, var, value, frame);
    }
    if (!is_var(value)) {
        return add_binding(t, u, var, value, frame);
    }
    const auto value_values = var_values(t, value, frame);
    if (value_values.empty()) {
        return add_binding(t, u, value, var, frame);
    }
    return maybe_add_binding(t, u, value_values, value, var, frame);
}

bool is_cons_tuple(const Term &term) {
    return term.is_tuple() && term.tuple_size_unchecked() == 2 && is_atom(element(term, 0), "cons_tuple");
}

// unify/7's last clause: two constants agree or the frame fails; non-constants fail only a safe unification.
Term unify_constants(const Transformer &t, const Unification &u, const std::array<Term, 2> &sides, const Term &frame) {
    const auto left_value = t.normalise(sides[0]);
    const auto right_value = t.normalise(sides[1]);
    if (!left_value || !right_value) {
        return u.safe ? t.atom("failed"_atom) : frame;
    }
    const auto exact = is_atom(u.op, "=:=");
    const auto agree =
        exact ? same(*left_value, *right_value) : is_atom(u.op, "==") && equal(*left_value, *right_value);
    return agree ? frame : t.atom("failed"_atom);
}
} // namespace

Term Transformer::unify(const Term &op, const Term &left, const Term &right, const Term &frame, const BindTest &bind,
                        const Imported &imported, bool safe) {
    if (is_failed(frame) || same(left, right)) {
        return frame;
    }
    const Unification u{.op = op, .bind = bind, .imported = imported, .safe = safe};
    if (is_var(left) || is_var(right)) {
        return is_var(left) ? extend_frame(*this, u, left, right, frame) : extend_frame(*this, u, right, left, frame);
    }
    if (is_cons_tuple(left) && is_cons_tuple(right)) {
        return unify(op, element(left, 1), element(right, 1), frame, bind, imported, safe);
    }
    if (!is_node(left, "cons", 4) || !is_node(right, "cons", 4)) {
        return unify_constants(*this, u, {left, right}, frame);
    }
    const auto heads = unify(op, element(left, 2), element(right, 2), frame, bind, imported, safe);
    return unify(op, element(left, 3), element(right, 3), heads, bind, imported, safe);
}

std::pair<Term, Term> Transformer::element_calls(const Term &term, const Term &frame, const BindTest &bind,
                                                 const Imported &imported) {
    if (const auto call = element_call(term)) {
        // element(I, T) as {V1, ..., VI | TupleTail} unified with T, the call standing for VI.
        const auto tail = unique_var();
        TermList vars;
        for (std::int64_t at = 0; at < call->first; ++at) {
            vars.push_back(unique_var());
        }
        const auto tuple_node = pair(atom("cons_tuple"_atom), list_to_cons(build_, vars, tail));
        const auto [inner, inner_frame] = element_calls(call->second, frame, bind, imported);
        return {vars.back(), unify(atom("=:="_atom), tuple_node, inner, inner_frame, bind, imported)};
    }
    return map_threaded(build_, term, frame, [&](const Term &child, const Term &current) {
        return element_calls(child, current, bind, imported);
    });
}

std::pair<Term, Term> Transformer::unify_column(const Term &frame, std::int64_t column, const Term &var_name,
                                                const BindTest &bind, const Imported &imported) {
    const auto a = anno0();
    const auto callee =
        build_.node("remote"_atom, a, atom_node(a, atom("erlang"_atom)), atom_node(a, atom("element"_atom)));
    const auto call = build_.node("call"_atom, a, callee, list(std::array{integer_node(a, column), var(a, var_name)}));
    return element_calls(call, frame, bind, imported);
}

std::pair<Term, Term> Transformer::prep_expr(const Term &expr, const Term &frame, const BindTest &bind,
                                             const Imported &imported) {
    return element_calls(tuple_to_cons(build_, expand_expr_records(expr)), frame, bind, imported);
}

namespace {
// match_in_pattern/3: the frame of the =/2 matches inside a pattern, and the pattern with each match replaced.
std::pair<Term, Term> match_in_pattern(Transformer &t, const Term &term, const Term &frame, const BindTest &bind) {
    if (is_node(term, "match", 4)) {
        const auto [left, left_frame] = match_in_pattern(t, element(term, 2), frame, bind);
        const auto [right, right_frame] = match_in_pattern(t, element(term, 3), left_frame, bind);
        const auto kept = bind(t.atom("=:="_atom), left) ? left : right;
        return {kept, t.unify(t.atom("=:="_atom), left, right, right_frame, bind, {})};
    }
    return map_threaded(t.build(), term, frame, [&](const Term &child, const Term &current) {
        return match_in_pattern(t, child, current, bind);
    });
}
} // namespace

Term Transformer::pattern(const Term &pattern0, std::int64_t &anon, const Term &frame, const BindTest &bind,
                          Term &pattern_var) {
    auto expanded = pattern0;
    try {
        expanded = expand_pattern_records(pattern0);
    } catch (const BuiltinFailure &failure) {
        if (failure.term) {
            throw;
        }
    }
    const auto reset = with_anno(build_, expanded, anno0());
    const auto numbered = anonymous_numbered(build_, reset, anon);
    const auto [matched, match_frame] = match_in_pattern(*this, tuple_to_cons(build_, numbered), frame, bind);
    const auto [called, call_frame] = element_calls(matched, match_frame, bind, {});
    const auto unique = unique_var();
    pattern_var = element(unique, 2);
    return unify(atom("=:="_atom), unique, called, call_frame, bind, {});
}

namespace {
bool is_op(const Term &node, std::initializer_list<std::string_view> ops) {
    if (!is_node(node, "op", 5)) {
        return false;
    }
    return std::ranges::any_of(ops, [&](std::string_view op) { return is_atom(element(node, 2), op); });
}

// erlang:is_record(T, R) and erlang:is_record(T, R, Size) calls: their T and R.
std::optional<std::pair<Term, Term>> is_record_call(const Term &node, std::size_t arity) {
    const auto args = remote_call_args(node, "erlang", "is_record");
    if (!args || args->size() != arity) {
        return std::nullopt;
    }
    return std::pair{(*args)[0], (*args)[1]};
}

// A filter walk: filter1/3 or, when safe, safe_filter1/3.
class FilterWalk final {
  public:
    FilterWalk(Transformer &transformer, const BindTest &bind, const Imported &imported, bool safe)
        : t_(transformer), bind_(bind), imported_(imported), safe_(safe) {}

    TermList walk(const Term &expr, const TermList &frames);

  private:
    TermList comparison(const Term &expr, const TermList &frames);
    TermList either(const Term &expr, const TermList &frames);
    TermList leaf(const Term &expr, const TermList &frames);
    std::optional<TermList> record_test(const Term &expr, const TermList &frames);

    Transformer &t_;
    const BindTest &bind_;
    const Imported &imported_;
    bool safe_;
};

TermList FilterWalk::comparison(const Term &expr, const TermList &frames) {
    TermList out;
    for (const auto &frame : frames) {
        const auto [left, left_frame] = t_.prep_expr(element(expr, 3), frame, bind_, imported_);
        const auto [right, right_frame] = t_.prep_expr(element(expr, 4), left_frame, bind_, imported_);
        const auto unified = t_.unify(element(expr, 2), left, right, right_frame, bind_, imported_, safe_);
        if (!is_failed(unified)) {
            out.push_back(unified);
        }
    }
    return out;
}

std::optional<TermList> FilterWalk::record_test(const Term &expr, const TermList &frames) {
    if (const auto two = is_record_call(expr, 2)) {
        const auto anno = element(expr, 1);
        const auto callee = t_.build().node("remote"_atom, anno, t_.atom_node(anno, t_.atom("erlang"_atom)),
                                            t_.atom_node(anno, t_.atom("element"_atom)));
        const auto call =
            t_.build().node("call"_atom, anno, callee, t_.list(std::array{t_.integer_node(anno, 1), two->first}));
        return walk(t_.build().node("op"_atom, anno, t_.atom("=:="_atom), call, two->second), frames);
    }
    if (const auto three = is_record_call(expr, 3)) {
        const auto args = t_.list(std::array{three->first, three->second});
        return walk(t_.build().node("call"_atom, element(expr, 1), element(expr, 2), args), frames);
    }
    return std::nullopt;
}

// The frames of either side of or, orelse (and, when not safe, xor), as one path each.
TermList FilterWalk::either(const Term &expr, const TermList &frames) {
    auto out = walk(element(expr, 3), frames);
    const auto right = walk(element(expr, 4), frames);
    out.insert(out.end(), right.begin(), right.end());
    return out;
}

// Any other filter: true keeps the frames, another atom has none; safe filters drop everything else.
TermList FilterWalk::leaf(const Term &expr, const TermList &frames) {
    const auto is_true = is_node(expr, "atom", 3) && is_atom(element(expr, 2), "true");
    if (safe_ || is_node(expr, "atom", 3)) {
        return is_true ? frames : TermList{};
    }
    if (auto tested = record_test(expr, frames)) {
        return *tested;
    }
    return frames;
}

TermList FilterWalk::walk(const Term &expr, const TermList &frames) {
    if (is_op(expr, {"=:=", "=="})) {
        return comparison(expr, frames);
    }
    if (is_op(expr, {"and", "andalso"})) {
        return walk(element(expr, 4), walk(element(expr, 3), frames));
    }
    if (is_op(expr, {"or", "orelse"}) || (!safe_ && is_op(expr, {"xor"}))) {
        return either(expr, frames);
    }
    return leaf(expr, frames);
}
} // namespace

TermList Transformer::filter(const Term &expr, const TermList &frames, const BindTest &bind, const Imported &imported,
                             bool safe) {
    if (frames.empty()) {
        return frames;
    }
    return FilterWalk(*this, bind, imported, safe).walk(pre_expanded(build_, expr), frames);
}

bool Transformer::bindings_is_subset(const TermList &binds, const Term &frame, const Imported &imported) {
    const BindTest any = [](const Term &, const Term &) { return true; };
    const auto &right = frame;
    auto merged = right;
    for (const auto &bind : binds) {
        merged = unify(element(bind, 3), element(bind, 1), element(bind, 2), merged, any, imported);
    }
    if (is_failed(merged)) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::bad_generator, .payload = merged.word()};
    }
    const DerefTest all_values = [](const Term &, const Term &) { return true; };
    const auto subset = [&](const Term &smaller, const Term &larger) {
        TermList names;
        for (const auto &bind : elements(smaller)) {
            if (!is_unique_var(element(bind, 1))) {
                names.push_back(element(bind, 1));
            }
        }
        return std::ranges::all_of(usorted(std::move(names)), [&](const Term &name) {
            const auto a = deref_var(name, smaller, all_values, imported);
            const auto b = deref_var(name, larger, all_values, imported);
            return same(list(a), list(b));
        });
    };
    return subset(merged, right) && subset(right, merged);
}
} // namespace clause::runtime::builtins::qlc_pt
