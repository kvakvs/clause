#include "../front_end/abstract.hpp"
#include "qlc_pt.hpp"
#include <algorithm>

// The run-time qlc transform's messages and entry points (plan 21 step LIB73): the lint runs over the QLCs that find
// introduced and generator variables and the errors and warnings inside QLCs, then transform_expression/3 and
// parse_transform/2 as qlc_pt.erl runs them.
namespace clause::runtime::builtins::qlc_pt {
using front_end::forms::element;
using front_end::forms::is_atom;
using front_end::forms::is_node;

namespace {
bool is_generator(const Term &qualifier) {
    return is_node(qualifier, "generate", 4) || is_node(qualifier, "b_generate", 4);
}

// embed_vars/2: [{V1, ..., Vn} || _ <- []].
Term embed_vars(const Transformer &t, std::span<const Term> vars, const Term &anno) {
    const auto generator =
        t.build().node("generate"_atom, anno, t.var(anno, t.atom("_"_atom)), t.build().node("nil"_atom, anno));
    return t.build().node("lc"_atom, anno, t.build().node("tuple"_atom, anno, t.list(vars)),
                          t.list(std::array{generator}));
}

// The {Location, Module, Descriptor} messages of an error list ({File, Messages} pairs), flattened.
TermList messages_of(const Term &errors) {
    TermList out;
    for (const auto &file : elements(errors)) {
        for (const auto &message : elements(element(file, 1))) {
            out.push_back(message);
        }
    }
    return out;
}

// A lint message {L, erl_lint, {Tag, V, ...}} of a descriptor of `size` elements: its location and variable.
std::optional<std::pair<Term, Term>> lint_var_message(const Term &message, std::string_view tag, std::size_t size) {
    if (!(message.is_tuple() && message.tuple_size_unchecked() == 3 && is_atom(element(message, 1), "erl_lint"))) {
        return std::nullopt;
    }
    const auto descriptor = element(message, 2);
    if (!is_node(descriptor, tag, size)) {
        return std::nullopt;
    }
    return std::pair{element(message, 0), element(descriptor, 1)};
}
} // namespace

namespace {
// intro_variables/2's fold: the generators' pattern variables, and each filter between two LCs naming its variables
// ({V1, ..., Vn} || _ <- []), their nodes marked before and after the filter in NodeInfo.
class IntroFold final {
  public:
    explicit IntroFold(Transformer &transformer) : t_(transformer) {}

    void enter(const Term &qid) {
        qid_ = qid;
        qids_.push_back(t_.pair(qid, t_.nil()));
    }

    Term visit(const Term &qualifier) {
        if (is_generator(qualifier)) {
            for (const auto &name : vars_of(element(qualifier, 2))) {
                gen_vars_.insert(gen_vars_.begin(), t_.pair(qid_, name));
            }
            return qualifier;
        }
        TermList vars;
        for (const auto &name : vars_of(qualifier)) {
            vars.push_back(t_.var(t_.anno0(), name));
        }
        const auto lc = embed_vars(t_, vars, t_.anno0());
        const auto before = marked(lc, false);
        const auto after = marked(lc, true);
        return t_.build().node("block"_atom, t_.anno0(), t_.list(std::array{before, qualifier, after}));
    }

    // {QId, []} of every qualifier, and {QId, V} of the generators' variables.
    TermList qids_;
    TermList gen_vars_;

  private:
    // intro_anno/4: the LC under new slots, each marked {QId, before | after}.
    Term marked(const Term &lc, bool after) {
        return front_end::abstract::map_annos(t_.build().context(), t_.save_anno(lc), [&](const Term &anno) {
            *t_.node(t_.loc(anno)) = IntroMark{.qid = qid_, .after = after};
            return anno;
        });
    }

    Transformer &t_;
    // The qualifier being visited.
    Term qid_;
};

// The {QId, V} of the lint messages {L, erl_lint, {Tag, V, ...}} located at intro marks before or after a filter.
TermList intro_marks(Transformer &t, const TermList &messages, const std::pair<std::string_view, std::size_t> &tag,
                     bool after) {
    TermList out;
    for (const auto &message : messages) {
        const auto found = lint_var_message(message, tag.first, tag.second);
        if (!found) {
            continue;
        }
        const auto *entry = t.node(found->first);
        const auto *mark = entry == nullptr ? nullptr : std::get_if<IntroMark>(entry);
        if (mark != nullptr && mark->after == after) {
            out.push_back(t.pair(mark->qid, found->second));
        }
    }
    return out;
}
} // namespace

// intro_variables/2: the variables each qualifier introduces, [{QId, [V]}] for every qualifier. A filter introduces
// the variables unbound before it and bound (and safe) after it.
TermList Transformer::intro_variables(const Term &forms_no_shadows) {
    IntroFold fold(*this);
    const auto marked = qual_fold(forms_no_shadows, [&](const Term &qid, const Term &qualifier) {
        fold.enter(qid);
        return fold.visit(qualifier);
    });
    const auto errors = compile_forms(marked, {}).first;
    const auto messages = elements(errors).empty() ? TermList{} : messages_of(errors);
    const std::pair<std::string_view, std::size_t> unbound{"unbound_var", 2};
    auto introduced =
        minus(minus(intro_marks(*this, messages, unbound, false), {intro_marks(*this, messages, unbound, true)}),
              {intro_marks(*this, messages, {"unsafe_var", 3}, true)});
    introduced.insert(introduced.end(), fold.gen_vars_.begin(), fold.gen_vars_.end());
    return family_union(build_, sorted(fold.qids_), family_of(build_, introduced));
}

namespace {
// used_genvar_check/2's fold: one expression per list expression naming variables introduced before it.
class GenvarCheck final {
  public:
    GenvarCheck(Transformer &transformer, const TermList &intro_vars)
        : t_(transformer), pending_(intro_vars), exprs_{t_.atom_node(t_.anno0(), t_.atom("true"_atom))} {}

    // A qualifier of the fold: a generator's list expression is checked, then its introduced variables count.
    void visit(const Term &qualifier) {
        if (is_generator(qualifier)) {
            generator(qualifier);
        }
    }

    // The qualifier's #qid{}: a QLC's first qualifier starts its variables afresh; its own come after it.
    void enter(const Term &qid) {
        if (qid_no(qid) == 1) {
            so_far_.clear();
        }
    }

    void leave(const Term &qid) { take_intro_vars(qid); }

    const TermList &exprs() const { return exprs_; }

    bool done() const { return pending_.empty(); }

  private:
    // qlc:var_fold(F, [], LE) renames every variable in order, newest first; only introduced ones count.
    void generator(const Term &qualifier) {
        TermList vars;
        const NodeVisit visit = [&](const Term &node) -> std::optional<Term> {
            if (is_node(node, "var", 3) && !is_atom(element(node, 2), "_")) {
                vars.push_back(renamed(node));
                return node;
            }
            return std::nullopt;
        };
        static_cast<void>(rebuilt(t_.build(), element(qualifier, 3), visit));
        TermList used;
        for (auto at = vars.size(); at > 0; --at) {
            if (member(element(vars[at - 1], 2), so_far_)) {
                used.push_back(vars[at - 1]);
            }
        }
        if (!used.empty()) {
            exprs_.push_back(embed_vars(t_, used, element(qualifier, 1)));
        }
    }

    // A variable of a list expression under a new slot whose genvar_pos is the original variable.
    Term renamed(const Term &old_var) {
        const auto original = t_.undo_no_shadows(old_var);
        const auto new_var = t_.save_anno(old_var);
        auto &data = std::get<NodeData>(*t_.node(t_.loc(element(new_var, 1))));
        data.genvar_pos = t_.tuple({t_.loc(element(original, 1)), t_.current_file(), element(original, 2)});
        return new_var;
    }

    // q_intro_vars/3.
    void take_intro_vars(const Term &qid) {
        if (pending_.empty() || !same(element(pending_.front(), 0), qid)) {
            lists::no_clause();
        }
        const auto ivs = elements(element(pending_.front(), 1));
        so_far_.insert(so_far_.begin(), ivs.begin(), ivs.end());
        pending_.erase(pending_.begin());
    }

    Transformer &t_;
    // The {QId, IVs} not reached yet.
    TermList pending_;
    // The expressions of the generated function.
    TermList exprs_;
    // The variables introduced by the current QLC's qualifiers so far.
    TermList so_far_;
};
} // namespace

Term Transformer::used_genvar_check(const Term &forms_no_shadows) {
    GenvarCheck check(*this, intro_vars_);
    static_cast<void>(qual_fold(forms_no_shadows, [&](const Term &qid, const Term &qualifier) {
        check.enter(qid);
        check.visit(qualifier);
        check.leave(qid);
        return qualifier;
    }));
    if (!check.done()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch};
    }
    const auto &exprs = check.exprs();
    std::set<Word> functions;
    for (const auto &form : elements(forms_no_shadows)) {
        if (is_node(form, "function", 5)) {
            functions.insert(element(form, 2).word());
        }
    }
    const auto name = aux_name(build_, "used_genvar", 1, functions);
    const auto clause = build_.node("clause"_atom, anno0(), nil(), nil(), list(exprs));
    return build_.node("function"_atom, anno0(), name, integer(0), list(std::array{clause}));
}

namespace {
// The qlc message of a generator kind qlc cannot run, none for other qualifiers.
std::optional<AtomName> generator_error(const Term &qualifier) {
    if (!(qualifier.is_tuple() && qualifier.tuple_size_unchecked() == 4)) {
        return std::nullopt;
    }
    const auto tag = element(qualifier, 0);
    if (is_atom(tag, "b_generate") || is_atom(tag, "b_generate_strict")) {
        return AtomName("binary_generator"_atom);
    }
    if (is_atom(tag, "m_generate") || is_atom(tag, "m_generate_strict")) {
        return AtomName("map_generator"_atom);
    }
    if (is_atom(tag, "generate_strict")) {
        return AtomName("strict_generator"_atom);
    }
    return std::nullopt;
}
} // namespace

namespace {
// compile_messages/4's BGens: the generators qlc cannot run, newest first.
TermList generator_errors(Transformer &t, const Term &forms) {
    TermList out;
    static_cast<void>(t.qual_fold(forms, [&](const Term &, const Term &qualifier) {
        if (const auto error = generator_error(qualifier)) {
            const auto message = t.tuple({t.loc(element(qualifier, 1)), t.atom("qlc"_atom), t.atom(*error)});
            out.insert(out.begin(), t.pair(t.current_file(), t.list(std::array{message})));
        }
        return qualifier;
    }));
    return out;
}

// genvar_pos/2: the original {Location, File, Var} of a renamed list expression variable, or its location.
Term genvar_pos(Transformer &t, const Term &location) {
    const auto *entry = t.node(location);
    if (entry == nullptr) {
        return location;
    }
    const auto *data = std::get_if<NodeData>(entry);
    if (data == nullptr || !data->genvar_pos) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::case_clause};
    }
    return *data->genvar_pos;
}

// used_genvar_messages/2: the unbound variables of the generated function as used generator variables.
TermList used_genvar_messages(Transformer &t, const Term &errors) {
    TermList out;
    for (const auto &message : messages_of(errors)) {
        const auto found = lint_var_message(message, "unbound_var", 2);
        const auto position = found ? genvar_pos(t, found->first) : t.nil();
        if (position.is_tuple() && position.tuple_size_unchecked() == 3) {
            const auto descriptor = t.pair(t.atom("used_generator_variable"_atom), element(position, 2));
            const auto info = t.tuple({element(position, 0), t.atom("qlc"_atom), descriptor});
            out.push_back(t.pair(element(position, 1), t.list(std::array{info})));
        }
    }
    return out;
}

// lc_nodes/2: every node of a QLC marked inside_lc.
void mark_inside(Transformer &t, const Term &lc) {
    front_end::abstract::walk_annos(lc, [&](const Term &anno) {
        auto line = t.loc(anno);
        line = line.is_tuple() ? element(line, 0) : line;
        auto *entry = t.node(line);
        auto *data = entry == nullptr ? nullptr : std::get_if<NodeData>(entry);
        if (data == nullptr) {
            const auto unmatched = entry == nullptr || std::holds_alternative<std::monostate>(*entry);
            throw BuiltinFailure{.reason = unmatched ? abi::v1::ErrorReason::badmatch : abi::v1::ErrorReason::badmap};
        }
        data->inside_lc = true;
        return front_end::abstract::Visit::next;
    });
}

// lc_loc/2.
bool inside_lc(Transformer &t, const Term &location) {
    const auto *entry = t.node(location);
    if (entry == nullptr || std::holds_alternative<std::monostate>(*entry)) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::case_clause};
    }
    const auto *data = std::get_if<NodeData>(entry);
    return data != nullptr && data->inside_lc;
}

// lc_messages/2: per file, the messages located inside QLCs.
TermList lc_messages(Transformer &t, const Term &messages) {
    TermList out;
    for (const auto &file : elements(messages)) {
        TermList kept;
        for (const auto &message : elements(element(file, 1))) {
            if (message.is_tuple() && message.tuple_size_unchecked() == 3 && inside_lc(t, element(message, 0))) {
                kept.push_back(message);
            }
        }
        out.push_back(t.pair(element(file, 0), t.list(kept)));
    }
    return out;
}
} // namespace

std::pair<Term, Term> Transformer::compile_messages(const ModuleForms &module, const TermList &options) {
    const auto &forms = module.forms;
    const auto bad_generators = generator_errors(*this, forms);
    const auto gen_form = used_genvar_check(module.no_shadows);
    const auto used_genvar = used_genvar_messages(*this, compile_forms(list(std::array{gen_form}), options).first);
    // badarg/2 and lc_nodes/2 in one walk: the QLC arguments that are no list comprehensions, nodes inside QLCs.
    TermList not_lcs;
    const auto marked = qlc_mapfold(forms, [&](const Term &lcid, const Term &lc) {
        if (!is_node(lc, "lc", 4)) {
            const auto info = tuple({element(lcid, 1), atom("qlc"_atom), atom("not_a_query_list_comprehension"_atom)});
            not_lcs.insert(not_lcs.begin(), pair(file_, list(std::array{info})));
        }
        mark_inside(*this, lc);
        return lc;
    });
    const auto [errors, warnings] = compile_forms(marked, options);
    auto all_errors = not_lcs;
    for (const auto &group : {used_genvar, lc_messages(*this, errors), bad_generators}) {
        all_errors.insert(all_errors.end(), group.begin(), group.end());
    }
    return {list(all_errors), list(lc_messages(*this, warnings))};
}

namespace {
// mforms/1: one {File, [Message]} per message, sorted.
TermList one_per_message(const Builder &build, const Term &groups) {
    TermList out;
    for (const auto &file : elements(groups)) {
        for (const auto &message : elements(element(file, 1))) {
            out.push_back(build.tuple(std::array{element(file, 0), build.list(std::array{message})}));
        }
    }
    return sorted(std::move(out));
}

// mforms2/2: [{attribute, 0, file, {File, 0}}, {Tag, Message}] per message, flattened in sorted order.
Term message_forms(const Builder &build, AtomName tag, const TermList &groups) {
    TermList pairs;
    for (const auto &file : sorted(groups)) {
        for (const auto &message : elements(element(file, 1))) {
            const auto attribute = build.node("attribute"_atom, build.integer(0), build.atom("file"_atom),
                                              build.tuple(std::array{element(file, 0), build.integer(0)}));
            pairs.push_back(build.list(std::array{attribute, build.tuple(std::array{build.atom(tag), message})}));
        }
    }
    return build.list(flattened(build.list(sorted(std::move(pairs)))));
}
} // namespace

std::pair<Term, Term> Transformer::no_duplicates(const Term &forms, const Messages &messages, const TermList &options) {
    const auto &errors = messages.errors;
    const auto &warnings = messages.warnings;
    const auto &extra_warnings = messages.extra_warnings;
    TermList extra;
    for (const auto &warning : elements(extra_warnings)) {
        const auto messages = elements(element(warning, 1));
        const auto tagged = messages.size() == 1 && messages[0].is_tuple() && messages[0].tuple_size_unchecked() == 3 &&
                            is_atom(element(messages[0], 1), "qlc");
        if (tagged && !is_atom(element(messages[0], 2), "nomatch_pattern") &&
            !is_atom(element(messages[0], 2), "nomatch_filter")) {
            extra.push_back(warning);
        }
    }
    auto all_warnings = one_per_message(build_, warnings);
    all_warnings.insert(all_warnings.end(), extra.begin(), extra.end());
    const auto [lint_errors, lint_warnings] = compile_forms(forms, options);
    const auto kept_errors = minus(one_per_message(build_, errors), {one_per_message(build_, lint_errors)});
    const auto kept_warnings =
        minus(one_per_message(build_, list(all_warnings)), {one_per_message(build_, lint_warnings)});
    return {message_forms(build_, "error"_atom, kept_errors), message_forms(build_, "warning"_atom, kept_warnings)};
}

namespace {
// transform_expression/3's As: a variable per {Name, Value} binding.
TermList binding_args(const Transformer &t, const Term &bindings) {
    if (!bindings.is_cons() && !bindings.is_nil()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::bad_generator, .payload = bindings.word()};
    }
    TermList args;
    for (const auto &binding : elements(bindings)) {
        if (binding.is_tuple() && binding.tuple_size_unchecked() == 2) {
            args.push_back(t.var(t.anno1(), element(binding, 0)));
        }
    }
    return args;
}

// The transformed query of {function, 1, bar, Ar, [{clause, 1, As, [], [NF]}]}; badmatch for any other shape.
Term transformed_query(const Transformer &t, const Term &last, const TermList &args) {
    const auto clauses = is_node(last, "function", 5) ? elements(element(last, 4)) : TermList{};
    const auto function = clauses.size() == 1 && same(element(last, 1), t.anno1()) &&
                          is_atom(element(last, 2), "bar") &&
                          string::small_of(element(last, 3)) == static_cast<std::int64_t>(args.size());
    const auto clause = function && same(element(clauses[0], 1), t.anno1()) &&
                        same(element(clauses[0], 2), t.list(args)) && element(clauses[0], 3).is_nil();
    const auto body = clause ? elements(element(clauses[0], 4)) : TermList{};
    if (body.size() != 1) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch};
    }
    return body.front();
}

// transform_expression/3's errors: every one ({not_ok, [{error, E}]}) or the first as {error, qlc, Reason}.
Term expression_errors(const Transformer &t, const Term &errors, bool lint_errors) {
    TermList tagged;
    for (const auto &message : messages_of(errors)) {
        tagged.push_back(t.pair(t.atom("error"_atom), message));
    }
    tagged = sorted(std::move(tagged));
    if (lint_errors) {
        return t.pair(t.atom("not_ok"_atom), t.list(tagged));
    }
    const auto reason = element(tagged.front(), 1);
    return t.pair(t.atom("not_ok"_atom), t.tuple({t.atom("error"_atom), t.atom("qlc"_atom), reason}));
}
} // namespace

Term Transformer::transform_expression(const Term &lc, bool lint_errors, const Term &bindings) {
    const auto args = binding_args(*this, bindings);
    const auto query = build_.node(
        "call"_atom, anno1(),
        build_.node("remote"_atom, anno1(), atom_node(anno1(), atom("qlc"_atom)), atom_node(anno1(), atom("q"_atom))),
        list(std::array{lc}));
    const auto clause = build_.node("clause"_atom, anno1(), list(args), nil(), list(std::array{query}));
    const auto arity = integer(static_cast<std::int64_t>(args.size()));
    const auto function = build_.node("function"_atom, anno1(), atom("bar"_atom), arity, list(std::array{clause}));
    const auto file =
        build_.node("attribute"_atom, anno1(), atom("file"_atom),
                    pair(build_.terms().list(std::array{integer('f'), integer('o'), integer('o')}), anno1()));
    const auto module = build_.node("attribute"_atom, anno1(), atom("module"_atom), atom("foo"_atom));
    const auto forms = initiate(list(std::array{file, module, function}), false);
    const auto errors = restore_locations(compile_messages(forms, {}).first);
    if (!elements(errors).empty()) {
        return expression_errors(*this, errors, lint_errors);
    }
    const auto last = elements(restore_anno(transform(forms.no_shadows))).back();
    return pair(atom("ok"_atom), transformed_query(*this, last, args));
}

namespace {
// An -import(qlc, [..., q/1, ...]) attribute.
bool imports_q_attribute(const Term &form) {
    if (!(is_node(form, "attribute", 4) && is_atom(element(form, 2), "import"))) {
        return false;
    }
    const auto import = element(form, 3);
    if (!(import.is_tuple() && import.tuple_size_unchecked() == 2 && is_atom(element(import, 0), "qlc"))) {
        return false;
    }
    return std::ranges::any_of(elements(element(import, 1)), [](const Term &function) {
        return function.is_tuple() && function.tuple_size_unchecked() == 2 && is_atom(element(function, 0), "q") &&
               string::small_of(element(function, 1)) == 1;
    });
}

// is_qlc_q_imported/1.
bool imports_q(const TermList &forms) { return std::ranges::any_of(forms, imports_q_attribute); }
} // namespace

Term Transformer::parse_transform(const Term &forms0, const Term &options) {
    const auto module = initiate(forms0, imports_q(elements(forms0)));
    const auto &forms = module.forms;
    if (member(atom("type_checker"_atom), elements(options))) {
        const auto record = elements(
            element(abstr(tuple({atom("qlc_lc"_atom), atom("undefined"_atom), atom("undefined"_atom)}), anno0()), 2));
        return qlc_mapfold(forms, [&](const Term &, const Term &lc) {
            const auto simple =
                build_.node("tuple"_atom, anno0(),
                            list(std::array{atom_node(anno0(), atom("simple_v1"_atom)),
                                            atom_node(anno0(), atom("V"_atom)), lc, abstr(loc(anno0()), anno0())}));
            return build_.node("tuple"_atom, anno0(), list(std::array{record[0], simple, record[2]}));
        });
    }
    const auto option_list = elements(options);
    const auto [errors, warnings] = compile_messages(module, option_list);
    if (elements(errors).empty()) {
        const auto new_forms = transform(module.no_shadows);
        const Messages messages{.errors = nil(), .warnings = warnings, .extra_warnings = list(xwarnings_)};
        const auto [none, warning_forms] = no_duplicates(new_forms, messages, option_list);
        static_cast<void>(none);
        auto out = elements(restore_locations(warning_forms));
        const auto restored = elements(restore_anno(new_forms));
        out.insert(out.end(), restored.begin(), restored.end());
        return list(out);
    }
    const Messages messages{.errors = errors, .warnings = warnings, .extra_warnings = nil()};
    const auto [error_forms, warning_forms] = no_duplicates(module.no_shadows, messages, option_list);
    auto out = elements(error_forms);
    const auto more = elements(warning_forms);
    out.insert(out.end(), more.begin(), more.end());
    out = elements(restore_locations(list(out)));
    const auto originals = elements(forms0);
    out.insert(out.end(), originals.begin(), originals.end());
    return list(out);
}

Term Transformer::try_ms(const MsQuery &query) {
    const auto anno = anno1();
    const auto guard = list(std::array{list(std::array{query.filter})});
    const auto clause =
        build_.node("clause"_atom, anno, list(std::array{query.pattern}), guard, list(std::array{query.expr}));
    const auto fun = build_.node("fun"_atom, anno, tuple({atom("clauses"_atom), list(std::array{clause})}));
    const auto call = build_.node(
        "call"_atom, anno,
        build_.node("remote"_atom, anno, atom_node(anno, atom("ets"_atom)), atom_node(anno, atom("fun2ms"_atom))),
        list(std::array{fun}));
    const auto body = build_.node("clause"_atom, anno, nil(), nil(), list(std::array{call}));
    const auto function = build_.node("function"_atom, anno, atom("foo"_atom), integer(0), list(std::array{body}));
    if (auto known = replay_.answer(function)) {
        return *known;
    }
    auto forms = records_;
    forms.push_back(function);
    replay_.ask(function, pair(function, list(forms)));
    return atom("no"_atom);
}

namespace {
// The roles of the builtins' term arguments (typed.hpp's Named).
struct FormsRole;
struct OptionsRole;
struct BehavioursRole;
struct AnswersRole;
struct LcRole;
struct BindingsRole;
struct FlagRole;
using FormsArg = Named<FormsRole>;
using OptionsArg = Named<OptionsRole>;
using BehavioursArg = Named<BehavioursRole>;
using AnswersArg = Named<AnswersRole>;
using LcArg = Named<LcRole>;
using BindingsArg = Named<BindingsRole>;
using FlagArg = Named<FlagRole>;

// A transform run: {done, Result, Transformed}, or {try_ms, Requests} when it made attempts without an answer (a
// failure then may come from attempts taken as failed, and the run is redone with their answers).
template <typename Body>
Term answered(ProcessContext &context, const AnswersArg &answers, const Term &behaviours, Body &&body) {
    Replay replay(answers.term);
    string::Terms terms(context);
    try {
        Transformer transformer(context, replay, behaviours);
        const auto result = body(transformer);
        if (replay.requests().empty()) {
            return terms.tuple(std::array{terms.atom("done"_atom), result, terms.boolean(transformer.transformed())});
        }
    } catch (...) {
        if (replay.requests().empty()) {
            throw;
        }
    }
    return terms.tuple(std::array{terms.atom("try_ms"_atom), terms.list(replay.requests())});
}

// clause_qlc_pt:parse_transform(Forms, Options, Behaviours, Answers).
Term parse_transform4(ProcessContext &context, const FormsArg &forms, const OptionsArg &options,
                      const BehavioursArg &behaviours, const AnswersArg &answers) {
    return answered(context, answers, behaviours.term,
                    [&](Transformer &transformer) { return transformer.parse_transform(forms.term, options.term); });
}

// clause_qlc_pt:transform_expression(LC, Bindings, LintErrors, Answers).
Term transform_expression4(ProcessContext &context, const LcArg &lc, const BindingsArg &bindings,
                           const FlagArg &lint_errors, const AnswersArg &answers) {
    return answered(context, answers, string::Terms(context).nil(), [&](Transformer &transformer) {
        return transformer.transform_expression(lc.term, is_atom(lint_errors.term, "true"), bindings.term);
    });
}

constexpr std::array QLC_PT_BUILTINS{
    typed_entry<parse_transform4>("clause_qlc_pt", "parse_transform"),
    typed_entry<transform_expression4>("clause_qlc_pt", "transform_expression"),
};
} // namespace
} // namespace clause::runtime::builtins::qlc_pt

namespace clause::runtime {
std::span<const BuiltinEntry> qlc_pt_builtins() noexcept { return builtins::qlc_pt::QLC_PT_BUILTINS; }
} // namespace clause::runtime
