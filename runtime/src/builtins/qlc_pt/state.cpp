#include "../front_end/abstract.hpp"
#include "../front_end/front_end.hpp"
#include "qlc_pt.hpp"
#include <algorithm>

// The run-time qlc transform's state (plan 21 step LIB73): NodeInfo (OTP's ets table of node data by unique line),
// the walks over QLCs and qualifiers, no_shadows/2 and the erl_lint and erl_expand_records calls.
namespace clause::runtime::builtins::qlc_pt {
using front_end::forms::element;
using front_end::forms::is_atom;
using front_end::forms::is_node;

std::int64_t qid_no(const Term &qid) { return string::small_of(element(qid, 2)).value_or(0); }

Term qid_lcid(const Term &qid) { return element(qid, 1); }

Transformer::Transformer(ProcessContext &context, const Term &behaviours)
    : build_(context), file_(build_.atom("undefined"_atom)) {
    for (const auto &entry : elements(behaviours)) {
        behaviours_.insert_or_assign(
            element(entry, 0),
            front_end::lint::BehaviourInfo{.callbacks = element(entry, 1), .optional_callbacks = element(entry, 2)});
    }
}

Term Transformer::tuple(std::initializer_list<Term> items) const {
    return build_.tuple(std::span(items.begin(), items.size()));
}

Term Transformer::loc(const Term &anno) const { return front_end::anno_location(build_.context(), anno); }

Term Transformer::var(const Term &anno, const Term &name) const { return build_.node("var"_atom, anno, name); }

Term Transformer::atom_node(const Term &anno, const Term &name) const { return build_.atom_node(anno, name); }

Term Transformer::integer_node(const Term &anno, std::int64_t value) const { return build_.integer_node(anno, value); }

Term Transformer::abstr(const Term &term, const Term &anno) const {
    return front_end::abstract_term(build_.context(), term, loc(anno), atom("utf8"_atom));
}

Term Transformer::unique_var() const { return var(anno0(), need(TermFactory(build_.context()).make_reference())); }

Term Transformer::no_compiler_warning(const Term &line) const {
    return front_end::anno_set(build_.context(), "generated", atom("true"_atom), line);
}

namespace {
// find_integers/1's integers/2: the positive small integers of a term.
void positive_integers(const Term &term, TermList &out) {
    if (term.is_tuple()) {
        for (std::size_t at = 0; at < term.tuple_size_unchecked(); ++at) {
            positive_integers(element(term, at), out);
        }
        return;
    }
    if (term.is_cons()) {
        auto rest = term;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            positive_integers(rest.head_unchecked(), out);
        }
        positive_integers(rest, out);
        return;
    }
    if (const auto value = string::small_of(term); value && *value > 0) {
        out.push_back(term);
    }
}

TermList integers_under(const Builder &build, const Term &forms, const Term &anno) {
    TermList found;
    positive_integers(with_anno(build, forms, anno), found);
    return usorted(std::move(found));
}

// erl_anno:line/1.
Term line_of(ProcessContext &context, const Term &anno) {
    const auto location = front_end::anno_location(context, anno);
    return location.is_tuple() ? element(location, 0) : location;
}
} // namespace

// exclude_integers_from_unique_line_numbers/2: integers of the forms other than annotations are never slots.
void Transformer::exclude_integers(const Term &forms) {
    const auto kept = ord_intersection(integers_under(build_, forms, anno0()), integers_under(build_, forms, anno1()));
    for (const auto &integer : kept) {
        node_info_[*string::small_of(integer)] = std::monostate{};
    }
}

std::int64_t Transformer::next_slot() {
    while (node_info_.contains(++var_n_)) {
    }
    return var_n_;
}

NodeEntry *Transformer::node(const Term &location) {
    const auto key = string::small_of(location);
    if (!key) {
        return nullptr;
    }
    const auto found = node_info_.find(*key);
    return found == node_info_.end() ? nullptr : &found->second;
}

Term Transformer::save_anno(const Term &abstr) {
    return front_end::abstract::map_annos(build_.context(), abstr, [&](const Term &anno) {
        const auto slot = next_slot();
        NodeData data;
        data.location = loc(anno);
        node_info_[slot] = data;
        return integer(slot);
    });
}

Term Transformer::restore_anno(const Term &abstr) const {
    auto &self = const_cast<Transformer &>(*this);
    return front_end::abstract::map_annos(build_.context(), abstr, [&](const Term &anno) {
        const auto *entry = self.node(loc(anno));
        if (entry == nullptr || std::holds_alternative<std::monostate>(*entry)) {
            return anno;
        }
        const auto *data = std::get_if<NodeData>(entry);
        if (data == nullptr) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmap};
        }
        if (!data->location) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::badkey};
        }
        return front_end::anno_set_location(build_.context(), *data->location, anno);
    });
}

Term Transformer::restore_locations(const Term &term) const {
    auto &self = const_cast<Transformer &>(*this);
    return rebuilt(build_, term, [&](const Term &node) -> std::optional<Term> {
        if (node.is_tuple() || node.is_cons()) {
            return std::nullopt;
        }
        const auto *entry = self.node(node);
        if (entry == nullptr || std::holds_alternative<std::monostate>(*entry)) {
            return node;
        }
        const auto *data = std::get_if<NodeData>(entry);
        if (data == nullptr || !data->location) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::case_clause};
        }
        return *data->location;
    });
}

namespace {
// [LC | Os] with Os a proper list shorter than 2.
std::optional<std::pair<Term, Term>> query_args(const Term &args) {
    if (!args.is_cons()) {
        return std::nullopt;
    }
    const auto rest = args.tail_unchecked();
    const auto proper_short = rest.is_nil() || (rest.is_cons() && rest.tail_unchecked().is_nil());
    if (!proper_short) {
        return std::nullopt;
    }
    return std::pair{args.head_unchecked(), rest};
}

bool is_qlc_q(const Term &callee, bool imported) {
    if (is_node(callee, "remote", 4)) {
        const auto module = element(callee, 2);
        const auto function = element(callee, 3);
        return is_node(module, "atom", 3) && is_atom(element(module, 2), "qlc") && is_node(function, "atom", 3) &&
               is_atom(element(function, 2), "q");
    }
    return imported && is_node(callee, "atom", 3) && is_atom(element(callee, 2), "q");
}
} // namespace

std::optional<Term> Transformer::qlc_call(const Term &call, const QlcVisit &visit, std::int64_t &number) {
    if (!is_node(call, "call", 4) || !is_qlc_q(element(call, 2), imported_)) {
        return std::nullopt;
    }
    const auto args = query_args(element(call, 3));
    if (!args) {
        return std::nullopt;
    }
    const auto options = qlcmf(args->second, visit, number);
    const auto lc = qlcmf(args->first, visit, number);
    const auto lcid = pair(integer(number), line_of(build_.context(), element(call, 1)));
    const auto query = visit(lcid, lc);
    ++number;
    return build_.node("call"_atom, element(call, 1), element(call, 2), build_.terms().cons(query, options));
}

Term Transformer::qlcmf(const Term &term, const QlcVisit &visit, std::int64_t &number) {
    if (term.is_cons()) {
        TermList heads;
        auto rest = term;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            heads.push_back(qlcmf(rest.head_unchecked(), visit, number));
        }
        return build_.terms().list(heads, qlcmf(rest, visit, number));
    }
    if (!term.is_tuple()) {
        return term;
    }
    if (auto call = qlc_call(term, visit, number)) {
        return *call;
    }
    if (is_node(term, "attribute", 4) && is_atom(element(term, 2), "file") && element(term, 3).is_tuple() &&
        element(term, 3).tuple_size_unchecked() == 2) {
        file_ = element(element(term, 3), 0);
        return term;
    }
    TermList items;
    for (std::size_t at = 0; at < term.tuple_size_unchecked(); ++at) {
        items.push_back(qlcmf(element(term, at), visit, number));
    }
    return build_.tuple(items);
}

Term Transformer::qlc_mapfold(const Term &forms, const QlcVisit &visit) {
    std::int64_t number = 1;
    const auto out = qlcmf(forms, visit, number);
    file_ = atom("undefined"_atom);
    return out;
}

Term Transformer::qual_fold(const Term &forms, const QualVisit &visit) {
    return qlc_mapfold(forms, [&](const Term &lcid, const Term &lc) {
        if (!is_node(lc, "lc", 4)) {
            return lc;
        }
        TermList qualifiers;
        std::int64_t number = 1;
        for (const auto &qualifier : elements(element(lc, 3))) {
            const auto qid = tuple({atom("qid"_atom), lcid, integer(number++)});
            qualifiers.push_back(visit(qid, qualifier));
        }
        return build_.node("lc"_atom, element(lc, 1), element(lc, 2), list(qualifiers));
    });
}

// --- no_shadows/2 ---

namespace {
// OTP's {LI, Vs, UV, AllVars, Singletons, State} without Vs: last index per name, uses per indexed name.
struct Shadows {
    std::map<Word, std::int64_t> last_index;
    std::map<Word, std::int64_t> uses;
    std::set<Word> all_vars;
    std::set<Word> singletons;
};

// Vs: the shadowing variables in scope with their current index.
using ActiveVars = std::map<Word, std::int64_t>;
// The pattern variables already renamed in one pattern: {V, VN}.
using PatternVars = std::vector<std::pair<Word, Term>>;

class Unshadower final {
  public:
    Unshadower(Transformer &transformer, Shadows &shadows) : t_(transformer), shadows_(shadows) {}

    Term nos(const Term &term, const ActiveVars &vs);

  private:
    Term nos_children(const Term &term, const ActiveVars &vs);
    Term nos_fun(const Term &node, const ActiveVars &vs);
    Term nos_named_fun(const Term &node, const ActiveVars &vs);
    Term nos_lc(const Term &node, const ActiveVars &vs);
    Term nos_clauses(const Term &clauses, const ActiveVars &vs);
    Term nos_var(const Term &node, const ActiveVars &vs);
    Term nos_pattern(const Term &term, ActiveVars &vs, PatternVars &pvs);
    Term pattern_var(const Term &node, ActiveVars &vs, PatternVars &pvs);
    std::optional<Term> used_var(const Term &name, const ActiveVars &vs);
    Term next_var(const Term &name, ActiveVars &vs);
    void note_name(const Term &node);

    Term indexed(const Term &name, std::int64_t index) const {
        return name_suffix(t_.build(), name.atom_spelling_unchecked(), index);
    }

    Transformer &t_;
    Shadows &shadows_;
};

Term Unshadower::nos(const Term &term, const ActiveVars &vs) {
    if (is_node(term, "fun", 3) && is_node(element(term, 2), "clauses", 2)) {
        return nos_fun(term, vs);
    }
    if (is_node(term, "named_fun", 4)) {
        return nos_named_fun(term, vs);
    }
    if (is_node(term, "lc", 4)) {
        return nos_lc(term, vs);
    }
    if (is_node(term, "var", 3) && !is_atom(element(term, 2), "_")) {
        return nos_var(term, vs);
    }
    return nos_children(term, vs);
}

// nos/2 of any other tuple or list: its elements in the same scope.
Term Unshadower::nos_children(const Term &term, const ActiveVars &vs) {
    if (term.is_tuple()) {
        TermList items;
        for (const auto &item : tuple_items(term)) {
            items.push_back(nos(item, vs));
        }
        return t_.build().tuple(items);
    }
    if (!term.is_cons()) {
        return term;
    }
    TermList heads;
    auto rest = term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        heads.push_back(nos(rest.head_unchecked(), vs));
    }
    return t_.build().terms().list(heads, nos(rest, vs));
}

// The clauses of a fun: each head renamed as a pattern, its guards and body in the head's scope.
Term Unshadower::nos_clauses(const Term &clauses, const ActiveVars &vs) {
    TermList out;
    for (const auto &clause : elements(clauses)) {
        if (!is_node(clause, "clause", 5)) {
            continue;
        }
        auto scope = vs;
        PatternVars pvs;
        const auto head = nos_pattern(element(clause, 2), scope, pvs);
        const auto guard = nos(element(clause, 3), scope);
        const auto body = nos(element(clause, 4), scope);
        out.push_back(t_.build().node("clause"_atom, element(clause, 1), head, guard, body));
    }
    return t_.list(out);
}

Term Unshadower::nos_fun(const Term &node, const ActiveVars &vs) {
    const auto clauses =
        t_.build().tuple(std::array{t_.atom("clauses"_atom), nos_clauses(element(element(node, 2), 1), vs)});
    return t_.build().node("fun"_atom, element(node, 1), clauses);
}

Term Unshadower::nos_named_fun(const Term &node, const ActiveVars &vs) {
    if (is_atom(element(node, 2), "_")) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch};
    }
    auto scope = vs;
    PatternVars pvs;
    const auto name = nos_pattern(t_.var(element(node, 1), element(node, 2)), scope, pvs);
    return t_.build().node("named_fun"_atom, element(name, 1), element(name, 2), nos_clauses(element(node, 3), scope));
}

Term Unshadower::nos_lc(const Term &node, const ActiveVars &vs) {
    auto scope = vs;
    TermList qualifiers;
    for (const auto &qualifier : elements(element(node, 3))) {
        const auto generator = is_node(qualifier, "generate", 4) || is_node(qualifier, "b_generate", 4);
        if (!generator) {
            qualifiers.push_back(nos(qualifier, scope));
            continue;
        }
        const auto expr = nos(element(qualifier, 3), scope);
        PatternVars pvs;
        const auto pattern = nos_pattern(element(qualifier, 2), scope, pvs);
        qualifiers.push_back(t_.build().tuple(std::array{element(qualifier, 0), element(qualifier, 1), pattern, expr}));
    }
    const auto expr = nos(element(node, 2), scope);
    return t_.build().node("lc"_atom, element(node, 1), expr, t_.list(qualifiers));
}

Term Unshadower::nos_var(const Term &node, const ActiveVars &vs) {
    const auto name = used_var(element(node, 2), vs);
    if (!name) {
        return node;
    }
    note_name(node);
    return t_.var(element(node, 1), *name);
}

Term Unshadower::nos_pattern(const Term &term, ActiveVars &vs, PatternVars &pvs) {
    if (is_node(term, "var", 3) && !is_atom(element(term, 2), "_")) {
        return pattern_var(term, vs, pvs);
    }
    if (term.is_tuple()) {
        TermList items;
        for (std::size_t at = 0; at < term.tuple_size_unchecked(); ++at) {
            items.push_back(nos_pattern(element(term, at), vs, pvs));
        }
        return t_.build().tuple(items);
    }
    if (!term.is_cons()) {
        return term;
    }
    TermList heads;
    auto rest = term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        heads.push_back(nos_pattern(rest.head_unchecked(), vs, pvs));
    }
    return t_.build().terms().list(heads, nos_pattern(rest, vs, pvs));
}

Term Unshadower::pattern_var(const Term &node, ActiveVars &vs, PatternVars &pvs) {
    const auto name = element(node, 2);
    const auto known = std::ranges::find_if(pvs, [&](const auto &entry) { return entry.first == name.word(); });
    Term renamed = name;
    if (known != pvs.end()) {
        static_cast<void>(used_var(name, vs));
        renamed = known->second;
    } else {
        const auto indexed_name = next_var(name, vs);
        renamed = shadows_.singletons.contains(indexed_name.word()) ? t_.atom("_"_atom) : indexed_name;
        pvs.insert(pvs.begin(), {name.word(), indexed_name});
    }
    note_name(node);
    return t_.var(element(node, 1), renamed);
}

std::optional<Term> Unshadower::used_var(const Term &name, const ActiveVars &vs) {
    const auto found = vs.find(name.word());
    if (found == vs.end()) {
        return std::nullopt;
    }
    const auto indexed_name = indexed(name, found->second);
    const auto uses = shadows_.uses.find(indexed_name.word());
    if (uses == shadows_.uses.end()) {
        bad_argument();
    }
    ++uses->second;
    return indexed_name;
}

Term Unshadower::next_var(const Term &name, ActiveVars &vs) {
    for (;;) {
        const auto index = ++shadows_.last_index[name.word()];
        const auto indexed_name = indexed(name, index);
        if (!shadows_.all_vars.contains(indexed_name.word())) {
            shadows_.uses[indexed_name.word()] = 0;
            vs[name.word()] = index;
            return indexed_name;
        }
    }
}

// nos_var/3: the original name of a renamed variable node, kept in NodeInfo for undo_no_shadows/2.
void Unshadower::note_name(const Term &node) {
    const auto name = element(node, 2);
    auto *entry = t_.node(t_.loc(element(node, 1)));
    if (entry == nullptr || std::holds_alternative<std::monostate>(*entry)) {
        return;
    }
    auto *data = std::get_if<NodeData>(entry);
    if (data == nullptr) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmap};
    }
    if (!data->name) {
        data->name = name;
    }
}
} // namespace

Term Transformer::no_shadows(const Term &forms) {
    Shadows shadows;
    for (const auto &name : vars_of(forms)) {
        shadows.all_vars.insert(name.word());
    }
    const auto pass = [&] {
        return qlc_mapfold(forms, [&](const Term &, const Term &lc) { return Unshadower(*this, shadows).nos(lc, {}); });
    };
    static_cast<void>(pass());
    for (const auto &[name, uses] : shadows.uses) {
        if (uses == 0) {
            shadows.singletons.insert(name);
        }
    }
    shadows.last_index.clear();
    shadows.uses.clear();
    return pass();
}

Term Transformer::undo_no_shadows(const Term &term) const {
    auto &self = const_cast<Transformer &>(*this);
    return var_mapped(build_, term, [&](const Term &node) {
        const auto *entry = self.node(loc(element(node, 1)));
        const auto *data = entry == nullptr ? nullptr : std::get_if<NodeData>(entry);
        if (data == nullptr || !data->name) {
            return node;
        }
        return var(element(node, 1), *data->name);
    });
}

// --- initiate/2 and the calls of other front-end modules ---

namespace {
// The {Name, Arity} a form defines or imports.
TermList function_names(const Builder &build, const Term &form) {
    if (is_node(form, "function", 5)) {
        return {build.tuple(std::array{element(form, 2), element(form, 3)})};
    }
    if (!(is_node(form, "attribute", 4) && is_atom(element(form, 2), "import"))) {
        return {};
    }
    const auto import = element(form, 3);
    return import.is_tuple() && import.tuple_size_unchecked() == 2 ? flattened(element(import, 1)) : TermList{};
}

// set_up_overridden/1: the local functions and the imported ones.
std::set<front_end::FunctionKey> overridden_functions(const Builder &build, const TermList &forms) {
    TermList names;
    for (const auto &form : forms) {
        const auto defined = function_names(build, form);
        names.insert(names.end(), defined.begin(), defined.end());
    }
    std::set<front_end::FunctionKey> keys;
    for (const auto &name : names) {
        if (name.is_tuple() && name.tuple_size_unchecked() == 2) {
            keys.insert({element(name, 0).word(), string::small_of(element(name, 1)).value_or(-1)});
        }
    }
    return keys;
}
} // namespace

ModuleForms Transformer::initiate(const Term &forms0, bool imported) {
    exclude_integers(forms0);
    const auto forms = elements(forms0);
    overridden_ = overridden_functions(build_, forms);
    imported_ = imported;
    for (const auto &form : forms) {
        if (is_node(form, "attribute", 4) && is_atom(element(form, 2), "record")) {
            records_.push_back(form);
            const auto value = element(form, 3);
            if (value.is_tuple() && value.tuple_size_unchecked() == 2) {
                record_defs_.emplace(element(value, 0).word(), element(value, 1));
            }
        }
    }
    const auto saved = save_anno(forms0);
    const auto no_shadow = no_shadows(saved);
    intro_vars_ = intro_variables(no_shadow);
    return {.forms = saved, .no_shadows = no_shadow};
}

bool Transformer::is_guard_test(const Term &expr) const {
    return front_end::is_guard_test(build_.context(), expr, {.records = &record_defs_, .overridden = &overridden_});
}

namespace {
// compile_forms/2's forms: no eof, warning or error forms (element(1, F) of each), then {eof, 0}.
TermList lint_input(const Transformer &t, const Term &forms) {
    TermList out;
    for (const auto &form : elements(forms)) {
        if (!form.is_tuple() || form.tuple_size_unchecked() == 0) {
            bad_argument();
        }
        const auto tag = element(form, 0);
        if (!is_atom(tag, "eof") && !is_atom(tag, "warning") && !is_atom(tag, "error")) {
            out.push_back(form);
        }
    }
    out.push_back(t.pair(t.atom("eof"_atom), t.integer(0)));
    return out;
}

// erl_lint.erl's compiler_options/1: the flattened values of the compile attributes.
TermList compiler_options(const Transformer &t, const TermList &forms) {
    TermList compile;
    for (const auto &form : forms) {
        if (is_node(form, "attribute", 4) && is_atom(element(form, 2), "compile")) {
            compile.push_back(element(form, 3));
        }
    }
    return flattened(t.list(compile));
}

// lint_options/1: the options without the bit syntax ones.
TermList lint_options(const TermList &options) {
    TermList out;
    for (const auto &option : options) {
        if (!is_atom(option, "binary_comprehension") && !is_atom(option, "bitlevel_binaries")) {
            out.push_back(option);
        }
    }
    return out;
}
} // namespace

namespace {
// What OTP's compile:noenv_forms/2 returns when erl_lint crashes on forms qlc_pt builds (a filter holding a strict
// generator): {"", [{none, compile, {crash, lint_module, Reason}}]}, which the transform ignores.
Term lint_crash(const Transformer &t) {
    const auto reason = t.tuple({t.atom("error"_atom), t.atom("function_clause"_atom), t.nil()});
    const auto crash = t.tuple({t.atom("crash"_atom), t.atom("lint_module"_atom), reason});
    const auto message = t.tuple({t.atom("none"_atom), t.atom("compile"_atom), crash});
    return t.pair(t.nil(), t.list(std::array{message}));
}
} // namespace

std::pair<Term, Term> Transformer::compile_forms(const Term &forms0, const TermList &options) {
    // erl_lint:module(Forms, lint_options(Options)): the options stand where the file name goes, as in OTP.
    const auto forms = lint_input(*this, forms0);
    front_end::lint::Lint lint(build_.context(), list(lint_options(options)), {list(compiler_options(*this, forms))});
    try {
        lint.module(list(forms), behaviours_);
    } catch (const BuiltinFailure &failure) {
        if (failure.term) {
            throw;
        }
        return {list(std::array{lint_crash(*this)}), nil()};
    }
    const auto status = lint.status();
    if (is_atom(element(status, 0), "ok")) {
        return {nil(), element(status, 1)};
    }
    return {element(status, 1), element(status, 2)};
}

Term Transformer::expand_expr_records(const Term &expr) {
    const auto pe = [&](const Term &term) {
        const Builder &build = build_;
        NodeVisit visit;
        visit = [&](const Term &node) -> std::optional<Term> {
            const auto unary = is_node(node, "op", 4);
            if (!unary && !is_node(node, "op", 5)) {
                return std::nullopt;
            }
            TermList items{element(node, 0), element(node, 1), element(node, 2)};
            for (std::size_t at = 3; at < node.tuple_size_unchecked(); ++at) {
                items.push_back(rebuilt(build, element(node, at), visit));
            }
            return front_end::lint::partial_eval(build.context(), build.tuple(items));
        };
        return rebuilt(build, term, visit);
    };
    const auto clause = build_.node("clause"_atom, anno1(), nil(), nil(), list(std::array{pe(expr)}));
    auto forms = records_;
    forms.push_back(build_.node("function"_atom, anno1(), atom("foo"_atom), integer(0), list(std::array{clause})));
    const auto options = list(std::array{atom("no_strict_record_tests"_atom)});
    const auto expanded = elements(front_end::expand_records_module(build_.context(), list(forms), options));
    const auto last = expanded.back();
    if (!is_node(last, "function", 5)) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch};
    }
    const auto clauses = elements(element(last, 4));
    return elements(element(clauses.front(), 4)).front();
}

Term Transformer::expand_pattern_records(const Term &pattern) {
    const auto truth = atom_node(anno0(), atom("true"_atom));
    const auto clause = build_.node("clause"_atom, anno0(), list(std::array{pattern}), nil(), list(std::array{truth}));
    const auto expr = build_.node("case"_atom, anno0(), truth, list(std::array{clause}));
    const auto expanded = expand_expr_records(expr);
    const auto clauses = elements(element(expanded, 3));
    const auto first = clauses.front();
    if (!is_node(first, "clause", 5) || !equal(element(first, 1), anno0())) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch};
    }
    return elements(element(first, 2)).front();
}
} // namespace clause::runtime::builtins::qlc_pt
