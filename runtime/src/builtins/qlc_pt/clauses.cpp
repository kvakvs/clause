#include "clauses.hpp"
#include <algorithm>

// The clauses of the generated QLC fun (plan 21 step LIB73): one or more per qualifier, the template and the final
// state, as qlc_pt.erl generates them (states and arguments laid out for qlc.erl's evaluation).
namespace clause::runtime::builtins::qlc_pt {
using front_end::forms::element;
using front_end::forms::is_atom;
using front_end::forms::is_node;

Term Code::v(const Term &name) const { return t_.var(anno_, name); }

Term Code::a(AtomName name) const { return t_.atom_node(anno_, t_.atom(name)); }

Term Code::i(std::int64_t value) const { return t_.integer_node(anno_, value); }

Term Code::nil() const { return t_.build().node("nil"_atom, anno_); }

Term Code::call(const Term &callee, std::span<const Term> args) const {
    return t_.build().node("call"_atom, anno_, callee, t_.list(args));
}

Term Code::clause(std::span<const Term> head, const Term &guards, std::span<const Term> body) const {
    return t_.build().node("clause"_atom, anno_, t_.list(head), guards, t_.list(body));
}

Term Code::fun(std::span<const Term> clauses) const {
    return t_.build().node("fun"_atom, anno_, t_.pair(t_.atom("clauses"_atom), t_.list(clauses)));
}

Term Code::closure(const Term &code) const { return fun(std::array{clause({}, t_.nil(), std::array{code})}); }

Term Code::cons(const Term &head, const Term &tail) const { return t_.build().node("cons"_atom, anno_, head, tail); }

TermList Code::vars(std::span<const Term> names) const {
    TermList out;
    for (const auto &name : names) {
        out.push_back(is_atom(name, "nil") ? nil() : v(name));
    }
    return out;
}

Term Code::next(const Term &go, std::int64_t state) const {
    return call(a("element"_atom), std::array{i(state), v(go)});
}

Term Code::is_list(const Term &name) const { return call(a("is_list"_atom), std::array{v(name)}); }

std::variant<TermList, std::pair<TermList, Term>> Code::split_args(const TermList &args) const {
    const auto max = static_cast<std::size_t>(t_.max_args_);
    if (args.size() <= max) {
        return args;
    }
    TermList first(args.begin(), args.begin() + static_cast<std::ptrdiff_t>(max - 1));
    TermList rest(args.begin() + static_cast<std::ptrdiff_t>(max - 1), args.end());
    return std::pair{first, t_.build().node("tuple"_atom, anno_, t_.list(rest))};
}

TermList Code::pack_args(const TermList &args) const {
    auto split = split_args(args);
    if (auto *packed = std::get_if<std::pair<TermList, Term>>(&split)) {
        packed->first.push_back(packed->second);
        return packed->first;
    }
    return args;
}

TermList replace(const TermList &those, const Term &with, const TermList &items) {
    TermList out;
    for (const auto &item : items) {
        out.push_back(member(item, those) ? with : item);
    }
    return out;
}

TermList concat(std::initializer_list<TermList> parts) {
    TermList out;
    for (const auto &part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

namespace {
Term guard_of(const Transformer &t, const Term &test) { return t.list(std::array{t.list(std::array{test})}); }

// final/4: state 0, the answers reversed (eval) or no more answers (cursor).
TermList final_clauses(const Code &code, const Lc &lc) {
    const auto &t = code.t();
    const auto anys = replace(lc.ivs, t.atom("_"_atom), lc.ivs);
    const auto any = t.atom("_"_atom);
    auto head = code.pack_args(concat({{code.i(0)}, code.vars(concat({{lc.rl, any, any}, anys}))}));
    const auto reverse =
        code.call(t.build().node("remote"_atom, code.anno(), code.a("lists"_atom), code.a("reverse"_atom)),
                  std::array{code.v(lc.rl)});
    const auto collected = code.clause(head, guard_of(t, code.is_list(lc.rl)), std::array{reverse});
    head = code.pack_args(concat({{code.i(0)}, code.vars(concat({{any, any, any}, anys}))}));
    return {collected, code.clause(head, t.nil(), std::array{code.nil()})};
}

// template/8: state 1, the template value collected or returned with a continuation.
TermList template_clauses(const Code &code, const Lc &lc, const Term &expr) {
    const auto &t = code.t();
    const auto next = code.next(lc.go, 1);
    const auto collected = code.cons(expr, code.v(lc.rl));
    const auto as0 = code.vars(concat({{lc.rl, lc.fun, lc.go}, lc.ivs}));
    const auto head = code.pack_args(concat({{code.i(1)}, as0}));
    const auto args = code.pack_args(concat({{next, collected}, code.vars(concat({{lc.fun, lc.go}, lc.ivs}))}));
    const auto first = code.clause(head, guard_of(t, code.is_list(lc.rl)), std::array{code.call(code.v(lc.fun), args)});
    Term continuation = t.nil();
    auto split = code.split_args(concat({{next}, as0}));
    if (auto *packed = std::get_if<std::pair<TermList, Term>>(&split)) {
        auto call_args = packed->first;
        call_args.push_back(t.var(code.anno(), lc.at));
        const auto bind = t.build().node("match"_atom, code.anno(), t.var(code.anno(), lc.at), packed->second);
        const auto later = code.closure(code.call(code.v(lc.fun), call_args));
        continuation = t.build().node("block"_atom, code.anno(), t.list(std::array{bind, later}));
    } else {
        continuation = code.closure(code.call(code.v(lc.fun), std::get<TermList>(split)));
    }
    return {first, code.clause(head, t.nil(), std::array{code.cons(expr, continuation)})};
}

// The arguments of a generator's clauses (generator/13): As and the next states' AsM, AsC and AsD.
struct GeneratorArgs {
    TermList as;
    TermList match;
    TermList cont;
    TermList done;
};

GeneratorArgs generator_args(const Code &code, const Lc &lc, const Qual &qual) {
    const auto &t = code.t();
    const TermList common{code.v(lc.rl), code.v(lc.fun), code.v(lc.go)};
    const TermList gv{qual.gv};
    const auto ivs_but_gv = minus(qual.ivs, {gv});
    GeneratorArgs out;
    out.as = concat({{code.i(qual.state + 1)}, common, code.vars(replace(ivs_but_gv, t.atom("_"_atom), lc.ivs))});
    out.match =
        code.pack_args(concat({{code.next(lc.go, qual.go + 1)}, common, code.vars(replace(gv, lc.ngv, lc.ivs))}));
    const auto kept = replace(gv, lc.ngv, replace(ivs_but_gv, t.atom("nil"_atom), lc.ivs));
    out.cont = code.pack_args(concat({{code.i(qual.state + 1)}, common, code.vars(kept)}));
    out.done = code.pack_args(
        concat({{code.next(lc.go, qual.go)}, common, code.vars(replace(qual.ivs, t.atom("nil"_atom), lc.ivs))}));
    return out;
}

// generator_init/9: the state that evaluates the list expression once.
Term generator_init(const Code &code, const Lc &lc, const Qual &qual) {
    const auto &t = code.t();
    const auto as0 = code.vars(concat({{lc.rl, lc.fun, lc.go}, replace({qual.gv}, t.atom("_"_atom), lc.ivs)}));
    const auto head = code.pack_args(concat({{code.i(qual.state)}, as0}));
    const auto args = code.pack_args(
        concat({{code.i(qual.state + 1)}, replace({code.v(t.atom("_"_atom))}, code.next(lc.go, qual.go + 2), as0)}));
    return code.clause(head, t.nil(), std::array{code.call(code.v(lc.fun), args)});
}

// generator/13: the init state, then the loop over a list and over continuations.
TermList generator_clauses(const Code &code, const Lc &lc, const Qual &qual) {
    const auto &t = code.t();
    const auto args = generator_args(code, lc, qual);
    const auto gv = TermList{code.v(qual.gv)};
    const auto next_value = code.v(lc.ngv);
    const auto on = [&](const Term &arg, const TermList &next) {
        return code.clause(code.pack_args(replace(gv, arg, args.as)), t.nil(),
                           std::array{code.call(code.v(lc.fun), next)});
    };
    TermList out{generator_init(code, lc, qual)};
    out.push_back(on(code.cons(qual.pattern, next_value), args.match));
    out.push_back(on(code.cons(code.v(t.atom("_"_atom)), next_value), args.cont));
    out.push_back(on(code.nil(), args.done));
    const auto branch = [&](const Term &value, const TermList &next) {
        return code.clause(std::array{value}, t.nil(), std::array{code.call(code.v(lc.fun), next)});
    };
    const auto error = code.v(lc.err);
    const TermList cases{branch(code.cons(qual.pattern, next_value), args.match),
                         branch(code.cons(code.v(t.atom("_"_atom)), next_value), args.cont),
                         branch(code.nil(), args.done), code.clause(std::array{error}, t.nil(), std::array{error})};
    const auto body = t.build().node("case"_atom, code.anno(), code.call(code.v(qual.gv), {}), t.list(cases));
    out.push_back(code.clause(code.pack_args(args.as), t.nil(), std::array{body}));
    return out;
}

// filter/10: a guard-like filter as an if (guard semantics), any other as a case.
Term filter_clause(const Code &code, const Lc &lc, const Qual &qual) {
    const auto &t = code.t();
    const auto anys = replace(qual.ivs, t.atom("_"_atom), lc.ivs);
    const auto head = code.pack_args(concat({{code.i(qual.state)}, code.vars(concat({{lc.rl, lc.fun, lc.go}, anys}))}));
    const auto args = code.vars(concat({{lc.rl, lc.fun, lc.go}, lc.ivs}));
    const auto on_true = code.pack_args(concat({{code.next(lc.go, qual.go + 1)}, args}));
    const auto on_false = code.pack_args(concat({{code.next(lc.go, qual.go)}, args}));
    const auto to_true = code.call(code.v(lc.fun), on_true);
    const auto to_false = code.call(code.v(lc.fun), on_false);
    Term body = t.nil();
    if (t.is_guard_test(qual.pattern)) {
        const auto when_true = code.clause({}, guard_of(t, qual.pattern), std::array{to_true});
        const auto otherwise = code.clause({}, guard_of(t, code.a("true"_atom)), std::array{to_false});
        body = t.build().node("if"_atom, code.anno(), t.list(std::array{when_true, otherwise}));
    } else {
        const auto when_true = code.clause(std::array{code.a("true"_atom)}, t.nil(), std::array{to_true});
        const auto when_false = code.clause(std::array{code.a("false"_atom)}, t.nil(), std::array{to_false});
        body = t.build().node("case"_atom, code.anno(), qual.pattern, t.list(std::array{when_true, when_false}));
    }
    return code.clause(head, t.nil(), std::array{body});
}
} // namespace

TermList fun_clauses(Transformer &t, const Lc &lc, const Term &expr, std::span<const Qual> quals, const Term &anno) {
    const Code code(t, anno);
    auto out = final_clauses(code, lc);
    const auto templates = template_clauses(code, lc, expr);
    out.insert(out.end(), templates.begin(), templates.end());
    for (const auto &qual : quals) {
        const Code qual_code(t, qual.anno);
        if (qual.generator) {
            const auto generated = generator_clauses(qual_code, lc, qual);
            out.insert(out.end(), generated.begin(), generated.end());
        } else {
            out.push_back(filter_clause(qual_code, lc, qual));
        }
    }
    return out;
}
} // namespace clause::runtime::builtins::qlc_pt
