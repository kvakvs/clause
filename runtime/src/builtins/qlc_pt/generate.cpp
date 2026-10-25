#include "clauses.hpp"
#include <algorithm>
#include <clause/runtime/code_server.hpp>

// The run-time qlc transform's output (plan 21 step LIB73): transform/2 replaces each QLC with a #qlc_lc{} whose fun
// returns {qlc_v1, QFun, CodeF, Qdata, QOpt} or {simple_v1, ...}, as qlc_pt.erl generates them for qlc.erl.
namespace clause::runtime::builtins::qlc_pt {
using front_end::forms::element;
using front_end::forms::is_atom;
using front_end::forms::is_node;

namespace {
// The {QId, {QIVs, {QualData, GoI, SI}}} of the generated qualifiers (QCs) as parts.
struct QualClause {
    Term id;
    TermList ivs;
    Term data;
    std::int64_t go = 0;
    std::int64_t state = 0;
};

bool is_gen(const QualClause &qc) { return is_atom(element(qc.data, 0), "gen"); }

// erlang:term_to_binary(Term, [compressed]) through the registered builtin.
Term compressed_binary(Transformer &t, const Term &term) {
    auto &context = t.build().context();
    const auto *builtin = context.code_server().builtins().find("erlang", "term_to_binary", 2);
    const std::array<Word, 2> args{term.word(), t.list(std::array{t.atom("compressed"_atom)}).word()};
    const auto result = call_builtin(context, *builtin, args.data());
    if (result == 0) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarg};
    }
    return need(Term::from_word(result, context));
}
} // namespace

// --- the options fun (opt_info/7) ---

namespace {
// column_fun/3: per column of a qualifier, the values to look up and the filters the lookup skips.
Term column_fun(Transformer &t, const TermList &columns, std::int64_t number, const Term &anno) {
    const Code zero(t, t.anno0());
    TermList clauses;
    for (const auto &entry : columns) {
        const auto column = element(entry, 0);
        if (string::small_of(element(column, 0)) != number) {
            continue;
        }
        const auto values = elements(element(entry, 1));
        const auto cons = list_to_cons(t.build(), values);
        TermList filter_numbers;
        for (const auto &filter : elements(element(element(entry, 2), 1))) {
            filter_numbers.push_back(zero.i(*string::small_of(filter)));
        }
        const auto filters =
            t.build().node("tuple"_atom, t.anno0(),
                           t.list(std::array{zero.a(element(element(entry, 2), 0).atom_spelling_unchecked()),
                                             list_to_cons(t.build(), filter_numbers)}));
        const auto tag =
            !vars_of(cons).empty() && values.size() > 1 ? AtomName("usort_needed"_atom) : AtomName("values"_atom);
        const auto value = t.build().node("tuple"_atom, t.anno0(), t.list(std::array{zero.a(tag), cons, filters}));
        clauses.push_back(zero.clause(std::array{t.abstr(element(column, 1), t.anno0())}, t.nil(), std::array{value}));
    }
    clauses.push_back(zero.clause(std::array{zero.v(t.atom("_"_atom))}, t.nil(), std::array{zero.a("false"_atom)}));
    const auto annotated = with_anno(t.build(), t.list(clauses), anno);
    return t.build().node("fun"_atom, anno, t.pair(t.atom("clauses"_atom), annotated));
}

// opt_column_constants/1: the template's columns only with one constant.
TermList opt_column_constants(const Term &column_constants) {
    TermList out;
    for (const auto &entry : elements(column_constants)) {
        if (string::small_of(element(element(entry, 0), 0)) != 0 || elements(element(entry, 1)).size() == 1) {
            out.push_back(entry);
        }
    }
    return out;
}

// opt_constants/2.
TermList opt_constants(Transformer &t, const Code &code, const TermList &column_constants) {
    TermList numbers;
    for (const auto &entry : column_constants) {
        numbers.push_back(element(element(entry, 0), 0));
    }
    TermList clauses;
    for (const auto &number : usorted(std::move(numbers))) {
        const auto fun = column_fun(t, column_constants, *string::small_of(number), code.anno());
        clauses.push_back(code.clause(std::array{code.i(*string::small_of(number))}, t.nil(), std::array{fun}));
    }
    clauses.push_back(
        code.clause(std::array{code.v(t.atom("_"_atom))}, t.nil(), std::array{code.a("no_column_fun"_atom)}));
    return clauses;
}

// What opt_info/7 combines.
struct OptInputs {
    const TermList &template_info;
    const Term &sizes;
    const Term &join_info;
    const TermList &match_specs;
    const Term &eq_column_constants;
    const Term &equal_column_constants;
};

// One option's fun: Default for any other argument.
Term option_fun(const Code &code, TermList clauses, const Term &fallback_head, const Term &fallback) {
    clauses.push_back(code.clause(std::array{fallback_head}, code.t().nil(), std::array{fallback}));
    return code.fun(clauses);
}

Term size_option(Transformer &t, const Code &code, const Term &sizes) {
    TermList clauses;
    for (const auto &entry : sorted(elements(sizes))) {
        clauses.push_back(code.clause(std::array{code.i(*string::small_of(element(entry, 0)))}, t.nil(),
                                      std::array{code.i(*string::small_of(element(entry, 1)))}));
    }
    return option_fun(code, clauses, code.v(t.atom("_"_atom)), code.a("undefined"_atom));
}

Term template_option(Transformer &t, const Code &code, const TermList &template_info) {
    TermList clauses;
    for (const auto &info : template_info) {
        for (const auto &entry : elements(element(info, 1))) {
            const std::array head{t.abstr(element(entry, 0), code.anno()), element(info, 0)};
            clauses.push_back(code.clause(head, t.nil(), std::array{t.abstr(element(entry, 1), code.anno())}));
        }
    }
    auto ordered = sorted(std::move(clauses));
    const std::array any{code.v(t.atom("_"_atom)), code.v(t.atom("_"_atom))};
    ordered.push_back(code.clause(any, t.nil(), std::array{code.nil()}));
    return code.fun(ordered);
}

// The n_leading_constant_columns and constant_columns funs of the compared constants.
std::pair<Term, Term> constant_column_options(Transformer &t, const Code &code, const TermList &equal_constants) {
    TermList constant_columns;
    for (const auto &entry : equal_constants) {
        if (elements(element(entry, 1)).size() == 1) {
            constant_columns.push_back(element(entry, 0));
        }
    }
    const auto family = family_of(t.build(), constant_columns);
    TermList leading;
    TermList columns;
    for (const auto &entry : family) {
        const auto cols = elements(element(entry, 1));
        std::int64_t first_missing = 1;
        while (std::ranges::any_of(cols, [&](const Term &col) { return string::small_of(col) == first_missing; })) {
            ++first_missing;
        }
        const auto number = *string::small_of(element(entry, 0));
        if (first_missing > 0) {
            leading.push_back(code.clause(std::array{code.i(number)}, t.nil(), std::array{code.i(first_missing - 1)}));
        }
        columns.push_back(
            code.clause(std::array{code.i(number)}, t.nil(), std::array{t.abstr(element(entry, 1), code.anno())}));
    }
    return {option_fun(code, leading, code.v(t.atom("_"_atom)), code.i(0)),
            option_fun(code, columns, code.v(t.atom("_"_atom)), code.nil())};
}

Term match_spec_option(Transformer &t, const Code &code, const TermList &match_specs) {
    TermList clauses;
    for (const auto &entry : match_specs) {
        const auto value = t.build().node(
            "tuple"_atom, code.anno(), t.list(std::array{element(entry, 1), t.abstr(element(entry, 2), code.anno())}));
        clauses.push_back(
            code.clause(std::array{code.i(*string::small_of(element(entry, 0)))}, t.nil(), std::array{value}));
    }
    return option_fun(code, clauses, code.v(t.atom("_"_atom)), code.a("undefined"_atom));
}

Term opt_info(Transformer &t, const Code &code, const OptInputs &in) {
    std::vector<std::pair<AtomName, Term>> options;
    if (!in.join_info.is_nil()) {
        options.emplace_back("join"_atom, t.abstr(in.join_info, code.anno()));
    }
    options.emplace_back("size"_atom, size_option(t, code, in.sizes));
    options.emplace_back("template"_atom, template_option(t, code, in.template_info));
    const auto eq_constants = opt_column_constants(in.eq_column_constants);
    const auto constants = opt_constants(t, code, eq_constants);
    options.emplace_back("constants"_atom, code.fun(constants));
    const auto equal_constants = opt_column_constants(in.equal_column_constants);
    const auto equal_clauses = opt_constants(t, code, equal_constants);
    if (!same(t.list(constants), t.list(equal_clauses))) {
        options.emplace_back("equal_constants"_atom, code.fun(equal_clauses));
    }
    const auto [leading, columns] = constant_column_options(t, code, equal_constants);
    options.emplace_back("n_leading_constant_columns"_atom, leading);
    options.emplace_back("constant_columns"_atom, columns);
    options.emplace_back("match_specs"_atom, match_spec_option(t, code, in.match_specs));
    TermList clauses;
    for (const auto &[tag, value] : options) {
        clauses.push_back(code.clause(std::array{code.a(tag)}, t.nil(), std::array{value}));
    }
    return option_fun(code, clauses, code.v(t.atom("_"_atom)), code.a("undefined"_atom));
}
} // namespace

// --- joins (join_quals/6) ---

namespace {
// anon_pattern/1: variables occurring once become '_'.
Term anon_pattern(const Transformer &t, const Term &pattern) {
    const auto repeated = usorted(minus(occurring_vars(pattern), {vars_of(pattern)}));
    return rebuilt(t.build(), pattern, [&](const Term &node) -> std::optional<Term> {
        if (!is_node(node, "var", 3)) {
            return std::nullopt;
        }
        return member(element(node, 2), repeated) ? node : t.var(element(node, 1), t.atom("_"_atom));
    });
}

// list2op/2.
Term list_to_op(const Transformer &t, const TermList &items, const Term &op) {
    if (items.empty()) {
        lists::no_clause();
    }
    auto out = items.back();
    for (auto at = items.size() - 1; at > 0; --at) {
        out = t.build().node("op"_atom, t.anno0(), op, items[at - 1], out);
    }
    return out;
}

// join_handle/4: a fun filtering a join operand by its pattern and the constants of its columns.
Term join_handle(Transformer &t, const Code &code, const Term &pattern, const TermList &aux, const Term &constants) {
    const auto &f = aux[0];
    const auto &h = aux[1];
    const auto &o = aux[2];
    const auto &c = aux[3];
    if (is_node(pattern, "var", 3) && constants.is_nil()) {
        return code.fun(std::array{code.clause(std::array{h}, t.nil(), std::array{h})});
    }
    const Code zero(t, t.anno0());
    TermList tests;
    for (const auto &column : elements(constants)) {
        const auto call =
            zero.call(zero.a("element"_atom), std::array{zero.i(*string::small_of(element(column, 0))), o});
        TermList ops;
        for (const auto &constant : elements(element(column, 1))) {
            ops.push_back(t.build().node("op"_atom, t.anno0(), element(constant, 1), element(constant, 0), call));
        }
        tests.push_back(list_to_op(t, ops, t.atom("or"_atom)));
    }
    const auto guards = tests.empty() ? t.nil() : t.list(std::array{t.list(tests)});
    const auto recurse = code.call(f, std::array{f, c});
    const auto matched = code.clause(std::array{pattern}, guards, std::array{code.cons(o, code.closure(recurse))});
    const auto other = code.clause(std::array{code.v(t.atom("_"_atom))}, t.nil(), std::array{recurse});
    const auto choice = t.build().node("case"_atom, code.anno(), o, t.list(std::array{matched, other}));
    const auto is_function =
        t.list(std::array{t.list(std::array{code.call(code.a("is_function"_atom), std::array{c})})});
    const std::array loop{
        code.clause(std::array{code.v(t.atom("_"_atom)), code.nil()}, t.nil(), std::array{code.nil()}),
        code.clause(std::array{f, code.cons(o, c)}, t.nil(), std::array{choice}),
        code.clause(std::array{f, c}, is_function, std::array{code.call(f, std::array{f, code.call(c, {})})}),
        code.clause(std::array{code.v(t.atom("_"_atom)), c}, t.nil(), std::array{c})};
    const auto start = t.build().node("match"_atom, code.anno(), f, code.fun(loop));
    const std::array body{start, code.closure(code.call(f, std::array{f, h}))};
    return code.fun(std::array{code.clause(std::array{h}, t.nil(), body)});
}

Term join_handle_constants(const Transformer &t, const Term &qid, const Term &extra_constants) {
    if (const auto found = keyfind(element(qid, 2), 0, elements(extra_constants))) {
        return element(*found, 1);
    }
    return t.nil();
}

TermList join_qnums(const Transformer &t, const Term &cols) {
    TermList out;
    for (const auto &join : elements(cols)) {
        const auto selection = elements(element(join, 0));
        if (selection.size() == 2) {
            out.push_back(t.pair(element(selection[0], 0), element(selection[1], 0)));
        }
    }
    return usorted(std::move(out));
}

// What join_quals/6 needs besides the qualifiers.
struct JoinInputs {
    const Term &join_info;
    const Term &anno;
    const Term &lc_number;
    const Term &extra_constants;
    const std::set<Word> &all_vars;
};

// The position after the last qualifier: its QId, and the GoI and SI the next qualifier gets.
QualClause after_last(const Transformer &t, const std::vector<QualClause> &qcs) {
    QualClause last{.id = qcs.front().id, .ivs = {}, .data = t.nil(), .go = 0, .state = 0};
    for (const auto &qc : qcs) {
        const auto states = is_gen(qc) ? 2 : 1;
        if (qc.go >= last.go) {
            last.go = qc.go + states + 1;
            last.state = qc.state + states;
        }
        last.id = less(last.id, qc.id) ? qc.id : last.id;
    }
    return last;
}

// join_quals/6's QNums: the joined generator numbers with the join operator, merge joins matching first.
TermList join_numbers(const Transformer &t, const Term &join_info) {
    const auto with_op = [&](const TermList &nums, AtomName op) {
        TermList out;
        for (const auto &pair : nums) {
            out.push_back(t.tuple({element(pair, 0), element(pair, 1), t.atom(op)}));
        }
        return out;
    };
    if (!join_info.is_tuple()) {
        return usorted(with_op(join_qnums(t, join_info), "=="_atom));
    }
    const auto equal_nums = join_qnums(t, element(join_info, 0));
    const auto match_nums = join_qnums(t, element(join_info, 1));
    auto out = with_op(match_nums, "=:="_atom);
    const auto compared = with_op(minus(equal_nums, {match_nums}), "=="_atom);
    out.insert(out.end(), compared.begin(), compared.end());
    return usorted(std::move(out));
}

// The one generator numbered `number`; badmatch for none or several.
const QualClause &numbered_generator(const std::vector<QualClause> &qcs, const Term &number) {
    const QualClause *found = nullptr;
    for (const auto &qc : qcs) {
        if (is_gen(qc) && string::small_of(number) == qid_no(qc.id)) {
            if (found != nullptr) {
                throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch};
            }
            found = &qc;
        }
    }
    if (found == nullptr) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch};
    }
    return *found;
}

// One join generator: the first generator's variable, both patterns, the join of their handles and constants.
QualClause join_qual(Transformer &t, const Code &code, const TermList &aux,
                     const std::array<const QualClause *, 2> &joined, const Term &op, const JoinInputs &in) {
    const auto &first = *joined[0];
    const auto &second = *joined[1];
    auto ivs = first.ivs;
    const auto second_ivs = minus(second.ivs, {TermList{element(second.data, 3)}});
    ivs.insert(ivs.end(), second_ivs.begin(), second_ivs.end());
    const auto p1 = element(first.data, 1);
    const auto p2 = element(second.data, 1);
    const auto cs1 = join_handle_constants(t, first.id, in.extra_constants);
    const auto cs2 = join_handle_constants(t, second.id, in.extra_constants);
    const auto h1 = join_handle(t, code, anon_pattern(t, p1), aux, cs1);
    const auto h2 = join_handle(t, code, anon_pattern(t, p2), aux, cs2);
    const auto join_le = t.build().tuple(
        std::array{t.atom("join"_atom), op, element(first.id, 2), element(second.id, 2), h1, h2, cs1, cs2});
    const auto data = t.tuple({t.atom("gen"_atom), code.cons(p1, p2), join_le, element(first.data, 3)});
    return {.id = t.nil(), .ivs = ivs, .data = data, .go = 0, .state = 0};
}

std::vector<QualClause> join_quals(Transformer &t, const std::vector<QualClause> &qcs, const JoinInputs &in) {
    auto next = after_last(t, qcs);
    const auto prefix = [&](std::string_view name) { return std::string(name) + concat_part(in.lc_number) + "_0_"; };
    const Code code(t, in.anno);
    TermList aux;
    for (const auto *name : {"F", "H", "O", "C"}) {
        aux.push_back(code.v(aux_name(t.build(), prefix(name), 1, in.all_vars)));
    }
    std::vector<QualClause> out;
    for (const auto &join : join_numbers(t, in.join_info)) {
        const std::array joined{&numbered_generator(qcs, element(join, 0)), &numbered_generator(qcs, element(join, 1))};
        auto qual = join_qual(t, code, aux, joined, element(join, 2), in);
        next.id = t.tuple({element(next.id, 0), element(next.id, 1), t.integer(qid_no(next.id) + 1)});
        qual.id = next.id;
        qual.go = next.go;
        qual.state = next.state;
        out.push_back(std::move(qual));
        next.go += 3;
        next.state += 2;
    }
    return out;
}
} // namespace

// --- qdata/2 and qcode/5 ---

namespace {
// qcon/1: [{Column, [{Constant, Op}]}] as abstract code.
Term qcon(const Transformer &t, const Term &constants) {
    const Code zero(const_cast<Transformer &>(t), t.anno0());
    TermList columns;
    for (const auto &column : elements(constants)) {
        TermList ops;
        for (const auto &constant : elements(element(column, 1))) {
            ops.push_back(
                t.build().node("tuple"_atom, t.anno0(),
                               t.list(std::array{element(constant, 0), t.abstr(element(constant, 1), t.anno0())})));
        }
        columns.push_back(t.build().node(
            "tuple"_atom, t.anno0(),
            t.list(std::array{zero.i(*string::small_of(element(column, 0))), list_to_cons(t.build(), ops)})));
    }
    return list_to_cons(t.build(), columns);
}

Term qdata(Transformer &t, const Code &code, const std::vector<QualClause> &qcs) {
    TermList entries;
    for (const auto &qc : qcs) {
        const std::array position{code.i(qid_no(qc.id)), code.i(qc.go), code.i(qc.state)};
        if (!is_gen(qc)) {
            entries.push_back(
                t.build().node("tuple"_atom, code.anno(),
                               t.list(std::array{position[0], position[1], position[2], code.a("fil"_atom)})));
            continue;
        }
        const auto le = element(qc.data, 2);
        Term init = code.closure(le);
        if (le.is_tuple() && le.tuple_size_unchecked() == 8 && is_atom(element(le, 0), "join")) {
            const auto constants =
                t.build().node("tuple"_atom, code.anno(),
                               t.list(std::array{qcon(t, element(le, 6)), qcon(t, element(le, 7)), code.a("v1"_atom)}));
            init = t.build().node(
                "tuple"_atom, code.anno(),
                t.list(std::array{code.a("join"_atom), code.a(element(le, 1).atom_spelling_unchecked()),
                                  code.i(*string::small_of(element(le, 2))), code.i(*string::small_of(element(le, 3))),
                                  element(le, 4), element(le, 5), code.closure(constants)}));
        }
        const auto gen = t.build().node("tuple"_atom, code.anno(), t.list(std::array{code.a("gen"_atom), init}));
        entries.push_back(
            t.build().node("tuple"_atom, code.anno(), t.list(std::array{position[0], position[1], position[2], gen})));
    }
    auto out = code.nil();
    for (auto at = entries.size(); at > 0; --at) {
        out = code.cons(entries[at - 1], out);
    }
    return out;
}

Term qcode(Transformer &t, const Code &code, const Term &expr, const std::vector<QualClause> &qcs,
           const front_end::lint::TermMap<Term> &source) {
    std::vector<std::pair<std::int64_t, Term>> parts{{1, expr}};
    for (const auto &qc : qcs) {
        const auto original = is_gen(qc) ? element(qc.data, 1) : source.at(qc.id);
        parts.emplace_back(qc.go, t.undo_no_shadows(original));
    }
    std::ranges::stable_sort(parts, {}, &std::pair<std::int64_t, Term>::first);
    TermList binaries;
    for (const auto &[go, part] : parts) {
        static_cast<void>(go);
        const auto binary = compressed_binary(t, part);
        const auto in_place = string::binary_bytes(binary);
        const std::vector<std::byte> bytes(in_place.begin(), in_place.end());
        TermList chars;
        for (const auto byte : bytes) {
            chars.push_back(t.integer(static_cast<std::int64_t>(byte)));
        }
        const auto chars_list = t.list(chars);
        const auto string_node = t.build().node("string"_atom, code.anno(), chars_list);
        const auto element_node = t.build().node("bin_element"_atom, code.anno(), string_node, t.atom("default"_atom),
                                                 t.atom("default"_atom));
        binaries.push_back(t.build().node("bin"_atom, code.anno(), t.list(std::array{element_node})));
    }
    const auto tuple = t.build().node("tuple"_atom, code.anno(), t.list(binaries));
    return code.fun(std::array{code.clause({}, t.nil(), std::array{tuple})});
}
} // namespace

// --- match specifications (match_spec_quals/4) ---

namespace {
// gen_ms/4: the abstract match specification, no_match_spec for the trivial one; badmatch when it is none.
Term gen_ms(Transformer &t, const Term &expr, const Term &pattern, const TermList &filters) {
    const auto reply = t.try_ms({.expr = expr, .pattern = pattern, .filter = t.filters_as_one(filters)});
    if (!is_node(reply, "ok", 3)) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch, .payload = reply.word()};
    }
    const auto ms = element(reply, 1);
    const auto trivial = elements(ms).size() == 1 && [&] {
        const auto clause = elements(ms).front();
        return clause.is_tuple() && clause.tuple_size_unchecked() == 3 && is_atom(element(clause, 0), "$1") &&
               same(element(clause, 1), t.list(std::array{t.atom("true"_atom)})) &&
               same(element(clause, 2), t.list(std::array{t.atom("$1"_atom)}));
    }();
    return trivial ? t.atom_node(t.anno0(), t.atom("no_match_spec"_atom)) : element(reply, 2);
}

// pattern_as_template/2: a top-level variable of the pattern as the template.
bool is_var_node(const Term &node) { return is_node(node, "var", 3); }

bool is_any_var(const Term &node) { return is_var_node(node) && is_atom(element(node, 2), "_"); }

// pattern_as_template/2's clauses for a top-level match: a variable side is the template; none for other matches.
std::optional<std::pair<Term, Term>> match_template(const Transformer &t, const Term &pattern, const Term &templ_var) {
    const auto left = element(pattern, 2);
    const auto right = element(pattern, 3);
    if (is_any_var(right) || is_any_var(left)) {
        const auto &kept = is_any_var(right) ? left : right;
        return std::pair{templ_var, t.build().node("match"_atom, element(pattern, 1), kept, templ_var)};
    }
    if (is_var_node(right) || is_var_node(left)) {
        return std::pair{is_var_node(right) ? right : left, pattern};
    }
    return std::nullopt;
}

std::pair<Term, Term> pattern_as_template(const Transformer &t, const Term &pattern, const Term &templ_var) {
    if (is_any_var(pattern)) {
        return {templ_var, templ_var};
    }
    if (is_var_node(pattern)) {
        return {pattern, pattern};
    }
    if (is_node(pattern, "match", 4)) {
        if (auto matched = match_template(t, pattern, templ_var)) {
            return *matched;
        }
    }
    return {templ_var, t.build().node("match"_atom, t.anno0(), pattern, templ_var)};
}

// The {GNum, Pattern, [], true} candidates of the generators no filter depends on.
TermList unfiltered_candidates(const Transformer &t, const QualData &data, const TermList &dependencies) {
    TermList candidates;
    TermList dependent;
    for (const auto &entry : dependencies) {
        const auto ids = elements(element(entry, 1));
        dependent.insert(dependent.end(), ids.begin(), ids.end());
    }
    TermList generator_ids;
    for (const auto &generator : data.generators) {
        generator_ids.push_back(element(generator, 0));
    }
    const auto no_filter_ids = minus(generator_ids, {dependent});
    for (const auto &generator : data.generators) {
        if (member(element(generator, 0), no_filter_ids)) {
            candidates.push_back(t.tuple({t.integer(qid_no(element(generator, 0))), element(element(generator, 1), 1),
                                          t.nil(), t.atom_node(t.anno0(), t.atom("true"_atom))}));
        }
    }
    return candidates;
}

// match_spec_quals/4's Candidates: {GNum, Pattern, [Filter], F} for each guard filter depending on one generator
// only, then {GNum, Pattern, [], true} for the generators no filter depends on.
// The candidates of one dependency {FilterQId, [GeneratorQId]}: the guard filter with its generator's pattern.
TermList filter_candidates(const Transformer &t, const QualData &data, const Term &dependency,
                           const TermList &filters) {
    TermList candidates;
    const auto ids = elements(element(dependency, 1));
    if (ids.size() != 1) {
        return candidates;
    }
    for (const auto &generator : data.generators) {
        for (const auto &filter : filters) {
            if (same(element(generator, 0), ids[0]) && same(element(filter, 0), element(dependency, 0))) {
                candidates.push_back(t.tuple({t.integer(qid_no(ids[0])), element(element(generator, 1), 1),
                                              t.list(std::array{filter}), element(element(filter, 1), 1)}));
            }
        }
    }
    return candidates;
}

TermList ms_candidates(const Transformer &t, const QualData &data, const Analysis &analysis, const TermList &filters) {
    TermList candidates;
    for (const auto &entry : analysis.dependencies) {
        const auto found = filter_candidates(t, data, entry, filters);
        candidates.insert(candidates.end(), found.begin(), found.end());
    }
    const auto unfiltered = unfiltered_candidates(t, data, analysis.dependencies);
    candidates.insert(candidates.end(), unfiltered.begin(), unfiltered.end());
    return candidates;
}

// The try block of match_spec_quals/4: one match specification for the whole query, when one generator and all
// filters make one; none when the block fails.
std::optional<Term> whole_query_ms(Transformer &t, const Term &expr, const TermList &families, const QualData &data) {
    try {
        if (families.size() != 1 || data.generators.size() != 1) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch};
        }
        const auto key = element(families.front(), 0);
        const auto selected = elements(element(families.front(), 1));
        if (selected.size() != data.filters.size()) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch};
        }
        const auto ms = gen_ms(t, expr, element(key, 1), selected);
        return t.tuple({element(key, 0), ms, t.atom("all"_atom)});
    } catch (const BuiltinFailure &failure) {
        if (failure.term) {
            throw;
        }
        return std::nullopt;
    }
}
} // namespace

TermList Transformer::match_spec_quals(const Term &expr, const TermList &qualifiers, const Analysis &analysis) {
    const auto data = qual_data(qualifiers);
    const auto candidates = ms_candidates(*this, data, analysis, filter_list(data.filters, analysis));
    const auto empty = build_.node("nil"_atom, anno0());
    TermList accepted;
    for (const auto &candidate : candidates) {
        const MsQuery query{.expr = empty, .pattern = element(candidate, 1), .filter = element(candidate, 3)};
        if (!is_atom(try_ms(query), "no")) {
            accepted.push_back(pair(pair(element(candidate, 0), element(candidate, 1)), element(candidate, 2)));
        }
    }
    const auto families = family_union_sets(build_, family_of(build_, accepted));
    if (auto whole = whole_query_ms(*this, expr, families, data)) {
        return {*whole};
    }
    const auto templ_var = var(anno0(), integer(0));
    TermList out;
    for (const auto &family : families) {
        const auto key = element(family, 0);
        const auto selected = elements(element(family, 1));
        const auto [template_expr, pattern] = pattern_as_template(*this, element(key, 1), templ_var);
        const auto ms = gen_ms(*this, template_expr, pattern, selected);
        TermList numbers;
        for (const auto &filter : selected) {
            numbers.push_back(integer(qid_no(element(filter, 0))));
        }
        out.push_back(tuple({element(key, 0), ms, list(numbers)}));
    }
    return out;
}

// --- warn_failing_qualifiers/4 and qlc_kind/3 ---

TermList Transformer::warn_failing_qualifiers(const TermList &qualifiers, const Analysis &analysis) {
    const auto data = qual_data(qualifiers);
    const BindTest bind = [&](const Term &, const Term &value) { return is_bindable(value); };
    const auto [frame, pattern_vars] = pattern_frame(data.generators, bind, 1);
    static_cast<void>(pattern_vars);
    const auto info = filter_info(data.filters, analysis);
    auto frames = is_atom(frame, "failed") ? TermList{} : TermList{frame};
    TermList warnings;
    const auto warn = [&](const Term &node, AtomName tag) {
        const auto message = tuple({loc(element(node, 1)), atom("qlc"_atom), atom(tag)});
        warnings.insert(warnings.begin(), pair(file_, list(std::array{message})));
    };
    for (const auto &filter : data.filters) {
        if (frames.empty()) {
            continue;
        }
        const auto expr = element(element(filter, 1), 1);
        auto next = this->filter(with_anno(build_, expr, anno0()), frames, bind, info.imported, false);
        if (next.empty()) {
            warn(expr, "nomatch_filter"_atom);
        }
        frames = std::move(next);
    }
    for (const auto &generator : data.generators) {
        const auto pattern_expr = element(element(generator, 1), 1);
        std::int64_t anon = 1;
        Term pattern_var = nil();
        if (is_atom(pattern(pattern_expr, anon, nil(), bind, pattern_var), "failed")) {
            warn(pattern_expr, "nomatch_pattern"_atom);
        }
    }
    return warnings;
}

namespace {
// qlc_kind/3: {simple, PatternAnno, LE, V} for [V || V <- LE] without filters, none for a general QLC.
std::optional<std::array<Term, 3>> simple_kind(Transformer &t, const Term &orig_expr, const TermList &qualifiers) {
    const auto original = t.qual_data(elements(t.undo_no_shadows(t.list(qualifiers))));
    const auto all_filters = t.filters_as_one(original.filters);
    if (!is_node(orig_expr, "var", 3) || !is_node(all_filters, "atom", 3) ||
        !is_atom(element(all_filters, 2), "true") || original.generators.size() != 1) {
        return std::nullopt;
    }
    const auto pattern = element(element(original.generators.front(), 1), 1);
    if (!is_node(pattern, "var", 3) || !same(element(pattern, 2), element(orig_expr, 2))) {
        return std::nullopt;
    }
    const auto generators = t.qual_data(qualifiers).generators;
    if (generators.size() != 1) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch};
    }
    return std::array{element(pattern, 1), element(element(generators.front(), 1), 2), element(orig_expr, 2)};
}

// simple/4: {simple_v1, V, Init, Location} with the pattern's annotation on its atoms.
Term simple(Transformer &t, const Term &outer, const Term &name, const Term &init, const Term &anno) {
    const Code inner(t, anno);
    return t.build().node(
        "tuple"_atom, outer,
        t.list(std::array{inner.a("simple_v1"_atom), t.atom_node(anno, name), init, t.abstr(t.loc(anno), anno)}));
}
} // namespace

// --- transform/2 ---

namespace {
// The per-file inputs of transform/2's F2.
struct TransformScope {
    std::set<Word> all_vars;
    front_end::lint::TermMap<Term> source;
    TermList intro_vs;
    TermList xwarnings;
};

// aux_var/5: an auxiliary variable name of the QLC numbered `lc_number`, not among the module's variables.
struct AuxNames {
    const Term &lc_number;
    const std::set<Word> &all_vars;

    Term name(const Transformer &t, std::string_view base, std::int64_t qualifier) const {
        const auto prefix = std::string(base) + concat_part(lc_number) + "_" + std::to_string(qualifier) + "_";
        return aux_name(t.build(), prefix, 1, all_vars);
    }
};

// transform/2's QCs: each qualifier with its introduced variables (a generator's variable first) and positions.
std::vector<QualClause> qual_clauses(const Transformer &t, const AuxNames &aux, const TermList &qs,
                                     const IntroVars &intro) {
    const auto &intro_vs = intro.entries;
    std::vector<QualClause> qcs;
    for (std::size_t at = 0; at < qs.size(); ++at) {
        const auto &q = qs[at];
        if (!same(element(intro_vs[at], 0), element(q, 0))) {
            lists::no_clause();
        }
        QualClause qc{.id = element(q, 0),
                      .ivs = elements(element(intro_vs[at], 1)),
                      .data = element(q, 3),
                      .go = *string::small_of(element(q, 1)),
                      .state = *string::small_of(element(q, 2))};
        if (is_atom(element(qc.data, 0), "gen")) {
            const auto gv = aux.name(t, "C", qid_no(qc.id));
            qc.ivs.insert(qc.ivs.begin(), gv);
            qc.data = t.tuple({t.atom("gen"_atom), element(qc.data, 1), element(qc.data, 2), gv});
        }
        qcs.push_back(std::move(qc));
    }
    return qcs;
}

// AllIVs: the introduced variables of every qualifier, the last qualifier's first.
TermList all_intro_vars(const std::vector<QualClause> &qcs) {
    TermList out;
    for (const auto &qc : qcs) {
        out.insert(out.begin(), qc.ivs.begin(), qc.ivs.end());
    }
    return out;
}

// The clause data of every qualifier for clauses/8.
std::vector<Qual> clause_quals(const Transformer &t, const std::vector<QualClause> &xqcs) {
    std::vector<Qual> quals;
    quals.reserve(xqcs.size());
    for (const auto &qc : xqcs) {
        quals.push_back({.generator = is_gen(qc),
                         .go = qc.go,
                         .state = qc.state,
                         .ivs = qc.ivs,
                         .gv = is_gen(qc) ? element(qc.data, 3) : t.nil(),
                         .pattern = element(qc.data, 1),
                         .anno = t.no_compiler_warning(element(qid_lcid(qc.id), 1))});
    }
    return quals;
}

// The names of the start fun's arguments: S0, RL0 and Go0.
struct StartNames {
    Term s0;
    Term rl0;
    Term go0;
};

// FunW: fun(S0, RL0, Go0) -> Fun = fun ... end, Fun(S0, RL0, Fun, Go0, nil...) end.
Term start_fun(Transformer &t, const Code &code, const Lc &lcn, const StartNames &names, const Term &fun_c) {
    const auto nils = replace(lcn.ivs, t.atom("nil"_atom), lcn.ivs);
    const auto as0 = code.pack_args(code.vars(concat({{names.s0, names.rl0, lcn.fun, names.go0}, nils})));
    const auto asw = code.vars(std::array{names.s0, names.rl0, names.go0});
    const auto bind = t.build().node("match"_atom, code.anno(), code.v(lcn.fun), fun_c);
    return code.fun(std::array{code.clause(asw, t.nil(), std::array{bind, code.call(code.v(lcn.fun), as0)})});
}

// What one QLC's transform has found before its generated code is put together.
struct LcResults {
    Transformer::Constants constants;
    Term join_info;
    TermList warnings;
};

// The QLC's analyses in OTP's order: constants and sizes, the join kind and the failing qualifiers.
LcResults analyse_lc(Transformer &t, const Term &lc, const Analysis &analysis, const Term &lc_line) {
    const auto qs = elements(element(lc, 3));
    auto constants = t.constants_and_sizes(qs, element(lc, 2), analysis);
    const auto [join_info, xwarn] = t.join_kind(qs, lc_line, analysis);
    auto warnings = t.warn_failing_qualifiers(qs, analysis);
    const auto more = elements(xwarn);
    warnings.insert(warnings.end(), more.begin(), more.end());
    return {.constants = constants, .join_info = join_info, .warnings = std::move(warnings)};
}

// The fun of #qlc_lc{}: {qlc_v1, QFun, CodeF, Qdata, QOpt}, or {simple_v1, ...} for [V || V <- LE].
Term lc_value(Transformer &t, const Code &code, const Term &orig_expr, const TermList &qs,
              const std::array<Term, 4> &parts) {
    if (const auto kind = simple_kind(t, orig_expr, qs)) {
        return simple(t, code.anno(), (*kind)[2], code.closure((*kind)[1]), (*kind)[0]);
    }
    return t.build().node("tuple"_atom, code.anno(),
                          t.list(std::array{code.a("qlc_v1"_atom), parts[0], parts[1], parts[2], parts[3]}));
}

Term transform_lc(Transformer &t, TransformScope &scope, const Term &lcid, const Term &lc) {
    const auto lc_line = element(lcid, 1);
    const auto qs = elements(element(lc, 3));
    const auto expr = element(lc, 2);
    const AuxNames aux{.lc_number = element(lcid, 0), .all_vars = scope.all_vars};
    // aux_vars(['RL', 'Fun', 'Go', 'C', 'S0', 'RL0', 'Go0', 'AT', 'E'], LcNo, AllVars).
    Lc lcn{.rl = aux.name(t, "RL", 0),
           .fun = aux.name(t, "Fun", 0),
           .go = aux.name(t, "Go", 0),
           .ngv = aux.name(t, "C", 0),
           .at = t.nil(),
           .err = t.nil(),
           .ivs = {}};
    const StartNames start{.s0 = aux.name(t, "S0", 0), .rl0 = aux.name(t, "RL0", 0), .go0 = aux.name(t, "Go0", 0)};
    lcn.at = aux.name(t, "AT", 0);
    lcn.err = aux.name(t, "E", 0);
    if (scope.intro_vs.size() < qs.size()) {
        bad_argument();
    }
    const TermList intro_vs(scope.intro_vs.begin(), scope.intro_vs.begin() + static_cast<std::ptrdiff_t>(qs.size()));
    scope.intro_vs.erase(scope.intro_vs.begin(), scope.intro_vs.begin() + static_cast<std::ptrdiff_t>(qs.size()));
    const auto qcs = qual_clauses(t, aux, qs, {intro_vs});
    lcn.ivs = all_intro_vars(qcs);
    const auto dependencies = t.qualifier_dependencies(qs, {intro_vs});
    const Analysis analysis{.dependencies = dependencies, .all_ivs = lcn.ivs};
    const auto anno = t.no_compiler_warning(lc_line);
    const auto results = analyse_lc(t, lc, analysis, lc_line);
    const JoinInputs join_inputs{.join_info = results.join_info,
                                 .anno = anno,
                                 .lc_number = aux.lc_number,
                                 .extra_constants = results.constants.extra_constants,
                                 .all_vars = scope.all_vars};
    auto xqcs = qcs;
    const auto jqs = join_quals(t, qcs, join_inputs);
    xqcs.insert(xqcs.end(), jqs.begin(), jqs.end());
    const Code code(t, anno);
    const auto fun_w = start_fun(t, code, lcn, start, code.fun(fun_clauses(t, lcn, expr, clause_quals(t, xqcs), anno)));
    const auto orig_expr = t.undo_no_shadows(scope.source.at(lcid));
    const auto q_code = qcode(t, code, orig_expr, xqcs, scope.source);
    const auto q_data = qdata(t, code, xqcs);
    const auto template_info = t.template_columns(qs, expr, analysis);
    const auto match_specs = t.match_spec_quals(expr, qs, analysis);
    const OptInputs opt_inputs{.template_info = template_info,
                               .sizes = results.constants.size_info,
                               .join_info = results.join_info,
                               .match_specs = match_specs,
                               .eq_column_constants = results.constants.eq_column_constants,
                               .equal_column_constants = results.constants.equal_column_constants};
    const auto opt = opt_info(t, code, opt_inputs);
    const auto lc_fun = code.fun(std::array{
        code.clause({}, t.nil(), std::array{lc_value(t, code, orig_expr, qs, {fun_w, q_code, q_data, opt})})});
    const auto record = elements(element(
        t.abstr(t.tuple({t.atom("qlc_lc"_atom), t.atom("undefined"_atom), t.atom("undefined"_atom)}), anno), 2));
    auto warnings = results.warnings;
    warnings.insert(warnings.end(), scope.xwarnings.begin(), scope.xwarnings.end());
    scope.xwarnings = std::move(warnings);
    return t.build().node("tuple"_atom, anno, t.list(std::array{record[0], lc_fun, record[2]}));
}

// transform/2's F1: each qualifier as {QId, GoI, SI, {gen, P, LE} | {fil, F}}, generators taking three goto
// entries and two states, filters two and one.
class QualifierNumbers final {
  public:
    explicit QualifierNumbers(const Transformer &t) : t_(t), qid_(t.nil()) {}

    // The #qid{} of the qualifier numbered next; a QLC's first qualifier starts at 2 and 2.
    void enter(const Term &qid) {
        qid_ = qid;
        if (qid_no(qid) == 1) {
            go_ = 2;
            state_ = 2;
        }
    }

    Term number(const Term &qualifier) {
        const auto generator = is_node(qualifier, "generate", 4);
        const auto data = generator ? t_.tuple({t_.atom("gen"_atom), element(qualifier, 2), element(qualifier, 3)})
                                    : t_.pair(t_.atom("fil"_atom), qualifier);
        const auto out = t_.tuple({qid_, t_.integer(go_), t_.integer(state_), data});
        go_ += generator ? 3 : 2;
        state_ += generator ? 2 : 1;
        return out;
    }

  private:
    const Transformer &t_;
    // The qualifier being numbered.
    Term qid_;
    // The next qualifier's first goto entry and state.
    std::int64_t go_ = 2;
    std::int64_t state_ = 2;
};

// transform/2's Source: the original filters by QId and templates by QLC identifier, for qcode/5.
void collect_source(Transformer &t, const Term &forms, TransformScope &scope) {
    static_cast<void>(t.qual_fold(forms, [&](const Term &qid, const Term &qualifier) {
        if (!is_node(qualifier, "generate", 4)) {
            scope.source.insert_or_assign(qid, qualifier);
        }
        return qualifier;
    }));
    static_cast<void>(t.qlc_mapfold(forms, [&](const Term &lcid, const Term &lc) {
        if (!is_node(lc, "lc", 4)) {
            lists::no_clause();
        }
        scope.source.insert_or_assign(lcid, element(lc, 2));
        return lc;
    }));
}
} // namespace

Term Transformer::transform(const Term &forms_no_shadows) {
    transformed_ = true;
    TransformScope scope;
    for (const auto &name : vars_of(forms_no_shadows)) {
        scope.all_vars.insert(name.word());
    }
    QualifierNumbers numbers(*this);
    const auto numbered = qual_fold(forms_no_shadows, [&](const Term &qid, const Term &qualifier) {
        numbers.enter(qid);
        return numbers.number(qualifier);
    });
    collect_source(*this, forms_no_shadows, scope);
    scope.intro_vs = intro_vars_;
    const auto out = qlc_mapfold(numbered, [&](const Term &lcid, const Term &lc) {
        if (!is_node(lc, "lc", 4)) {
            lists::no_clause();
        }
        return transform_lc(*this, scope, lcid, lc);
    });
    if (!scope.intro_vs.empty()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch};
    }
    xwarnings_ = scope.xwarnings;
    return out;
}
} // namespace clause::runtime::builtins::qlc_pt
