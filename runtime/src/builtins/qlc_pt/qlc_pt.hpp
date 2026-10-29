#pragma once
#include "../front_end/forms.hpp"
#include "../front_end/guard_tests.hpp"
#include "../front_end/lint.hpp"
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// The qlc parse transform at run time (plan 21 step LIB73, docs/builtins.md#qlc-pt): OTP's qlc_pt.erl algorithm in
// C++ over the abstract format, for qlc_pt:transform_expression/2, transform_from_evaluator/2 and parse_transform/2.
// Intermediate data (qualifiers, frames, columns) are Erlang terms shaped as OTP's, so sorting and comparing them
// follow OTP's term order exactly. Match specification attempts run the C++ ms_transform (front_end/ms_transform.hpp)
// in place, so each transform walks once.
namespace clause::runtime::builtins::qlc_pt {
using front_end::forms::Builder;
using front_end::lint::TermLess;
using TermList = std::vector<Term>;

// --- Term lists (the lists, ordsets and sofs calls of qlc_pt.erl) ---

// Erlang's term order (numbers by value): <, == and =:=.
bool less(const Term &left, const Term &right);
bool equal(const Term &left, const Term &right);
bool same(const Term &left, const Term &right);
// lists:sort/1 (stable) and lists:usort/1 (== duplicates dropped).
TermList sorted(TermList items);
TermList usorted(TermList items);

// The right operand of Left -- Right, a role of its own beside the list it is removed from.
struct Removed {
    const TermList &items;
};

// Left -- Right: the first =:= occurrence of each element of Right removed.
TermList minus(TermList left, const Removed &right);
// lists:member/2 (=:=).
bool member(const Term &item, std::span<const Term> items);
// lists:keyfind(Key, Position, Tuples) with a 0-based position (==).
std::optional<Term> keyfind(const Term &key, std::size_t position, std::span<const Term> tuples);
// ordsets:subtract/2 and ordsets:intersection/2 over usorted lists.
TermList ord_subtract(const TermList &left, const TermList &right);
TermList ord_intersection(const TermList &left, const TermList &right);
// The elements of a proper list; function_clause for anything else.
TermList elements(const Term &list);
// lists:flatten/1.
TermList flattened(const Term &deep);

// sofs:to_external(sofs:relation_to_family(sofs:relation(Pairs))): {Key, UsortedValues} by usorted key.
TermList family_of(const Builder &build, TermList pairs);
// sofs:family_union/2 of two families ({Key, Set} lists in key order): the union of the sets per key.
TermList family_union(const Builder &build, const TermList &left, const TermList &right);
// sofs:to_external(sofs:family_union(Family)): the union of each key's sets of sets.
TermList family_union_sets(const Builder &build, const TermList &family);

// --- Walks over the abstract format and the transform's own nodes ---

// qlc:vars/1: the usorted names of the {var, _, V} in a term, V not '_'.
TermList vars_of(const Term &term);
// qlc:var_fold(fun({var, _, V}) -> V end, [], Term): every variable name, newest first.
TermList occurring_vars(const Term &term);
// A rebuild of `term` in which `visit` answers some nodes; the others are walked as tuples and lists.
using NodeVisit = std::function<std::optional<Term>(const Term &)>;
Term rebuilt(const Builder &build, const Term &term, const NodeVisit &visit);
// var_map(F, E): every {var, _, _} replaced by `map`, named funs' names included.
Term var_mapped(const Builder &build, const Term &term, const std::function<Term(const Term &)> &map);
// anon_var/2: each '_' variable renamed to the next integer from `next`.
Term anonymous_numbered(const Builder &build, const Term &term, std::int64_t &next);
// tuple2cons/1, list2cons/1, cons2tuple/1 and cons2list/1 (the {cons_tuple, Cons} form of tuples).
Term tuple_to_cons(const Builder &build, const Term &term);
Term list_to_cons(const Builder &build, std::span<const Term> items, const std::optional<Term> &tail = std::nullopt);
Term cons_to_tuple(const Builder &build, const Term &term);
// pre_expand/1: local and tuple calls as erlang: and M:F calls, record/2 as is_record/2.
Term pre_expanded(const Builder &build, const Term &term);
// set_anno/2 and reset_anno/1: every annotation replaced.
Term with_anno(const Builder &build, const Term &term, const Term &anno);
// qlc:name_suffix/2 and qlc:aux_name/3.
Term name_suffix(const Builder &build, std::string_view prefix, std::int64_t suffix);
Term aux_name(const Builder &build, std::string_view prefix, std::int64_t first, const std::set<Word> &taken);
// The text of an atom, or of an integer, as lists:concat/1 writes it.
std::string concat_part(const Term &part);
// qlc:all_selections/1.
TermList all_selections(const Builder &build, const TermList &choices);
// The elements of a tuple.
TermList tuple_items(const Term &tuple);
// Whether `test` holds for an element of a tuple, or for a head or the last tail of a list.
bool any_child(const Term &term, const std::function<bool(const Term &)> &test);
// A walk threading a frame: each child of a tuple or list mapped by `map`, other terms kept.
using ThreadedMap = std::function<std::pair<Term, Term>(const Term &child, const Term &frame)>;
std::pair<Term, Term> map_threaded(const Builder &build, const Term &term, const Term &frame, const ThreadedMap &map);
// The arguments of a call of Module:Function written {remote, _, {atom, _, M}, {atom, _, F}}, none for others.
std::optional<TermList> remote_call_args(const Term &call, std::string_view module, std::string_view function);
// The I of an erlang:element(I, T) call with an integer I > 0, and T.
std::optional<std::pair<std::int64_t, Term>> element_call(const Term &call);

// --- The transform ---

// NodeInfo data of a node (OTP's map): its original location and what the checks noted on it.
struct NodeData {
    std::optional<Term> location;
    bool inside_lc = false;
    std::optional<Term> genvar_pos;
    std::optional<Term> name;
};

// NodeInfo's {Location, {QId, before | after}} entries of intro_variables/2.
struct IntroMark {
    Term qid;
    bool after = false;
};

// One NodeInfo entry: an integer kept from the forms ({I}), a node's data or an introduced-variable mark.
using NodeEntry = std::variant<std::monostate, NodeData, IntroMark>;

// The callback of qlc_mapfold/4: the new term for a QLC's argument, given its identifier {No, Line}.
using QlcVisit = std::function<Term(const Term &lcid, const Term &lc)>;
// The callback of qual_fold/5: the new qualifier for a qualifier and its #qid{}.
using QualVisit = std::function<Term(const Term &qid, const Term &qualifier)>;

// What a unification binds: OTP's #bind{} records ({bind, Var, Value, Op}), newest first, or the atom failed.
using BindTest = std::function<bool(const Term &op, const Term &value)>;
using DerefTest = std::function<bool(const Term &value, const Term &op)>;

// The imported variables of a filter (an ordset), or every variable (OTP's `all`).
struct Imported {
    bool all = false;
    TermList vars;
};

// qual_data/1: the {QId, {fil, F}} and {QId, {gen, P, LE}} of qualifiers {QId, GoI, SI, Q}.
struct QualData {
    TermList filters;
    TermList generators;
};

// A value and the operator it is compared or matched with ({Value, Op} of deref/4 and var_values/2).
struct ValueOp {
    Term value;
    Term op;
};

// initiate/2's forms: with unique annotations, and the same without shadowed variables.
struct ModuleForms {
    Term forms;
    Term no_shadows;
};

// The messages no_duplicates/5 sorts out: errors, warnings and the transform's extra warnings.
struct Messages {
    Term errors;
    Term warnings;
    Term extra_warnings;
};

// The fun try_ms/4 gives ets:fun2ms/1: fun(Pattern) when Filter -> Expr end.
struct MsQuery {
    Term expr;
    Term pattern;
    Term filter;
};

// The introduced variables of a QLC's qualifiers, [{QId, [V]}].
struct IntroVars {
    const TermList &entries;
};

// What every analysis of one QLC shares: the qualifiers each filter depends on ([{QId, [QId]}]) and the variables
// all qualifiers introduce (AllIVs).
struct Analysis {
    const TermList &dependencies;
    const TermList &all_ivs;
};

// The transform of one module or expression: OTP's #state{} and the NodeInfo table.
class Transformer final {
  public:
    Transformer(ProcessContext &context, const Term &behaviours);

    Term parse_transform(const Term &forms, const Term &options);
    // transform_expression/3: {ok, Expr} or {not_ok, ...}; `lint_errors` keeps every error (else {error, qlc, R}).
    Term transform_expression(const Term &lc, bool lint_errors, const Term &bindings);

    // Whether transform/2 ran (it sets the backtrace depth, which qlc_pt.erl repeats).
    bool transformed() const { return transformed_; }

    // --- terms ---
    const Builder &build() const { return build_; }

    Term atom(AtomName name) const { return build_.atom(name); }

    Term integer(std::int64_t value) const { return build_.integer(value); }

    Term nil() const { return build_.nil(); }

    Term list(std::span<const Term> items) const { return build_.list(items); }

    Term tuple(std::initializer_list<Term> items) const;

    Term pair(const Term &first, const Term &second) const { return tuple({first, second}); }

    // erl_anno:new(0), erl_anno:new(1) and erl_anno:location/1.
    Term anno0() const { return integer(0); }

    Term anno1() const { return integer(1); }

    Term loc(const Term &anno) const;
    // {var, Anno, Name}, {atom, Anno, Name}, {integer, Anno, I}.
    Term var(const Term &anno, const Term &name) const;
    Term atom_node(const Term &anno, const Term &name) const;
    Term integer_node(const Term &anno, std::int64_t value) const;
    // erl_parse:abstract(Term, loc(Anno)).
    Term abstr(const Term &term, const Term &anno) const;
    // {var, anno0(), make_ref()}.
    Term unique_var() const;

    // --- state.cpp: NodeInfo, walks over QLCs, no_shadows ---
    // initiate/2: the forms with unique annotations and the same without shadowed variables; sets the state.
    ModuleForms initiate(const Term &forms0, bool imported);
    Term save_anno(const Term &abstr);
    Term restore_anno(const Term &abstr) const;
    Term restore_locations(const Term &term) const;
    Term qlc_mapfold(const Term &forms, const QlcVisit &visit);
    Term qual_fold(const Term &forms, const QualVisit &visit);
    Term undo_no_shadows(const Term &term) const;
    Term no_shadows(const Term &forms);
    bool is_guard_test(const Term &expr) const;
    // compile_forms/2 through erl_lint (the compiler is not available): {Errors, Warnings}.
    std::pair<Term, Term> compile_forms(const Term &forms, const TermList &options);
    Term expand_expr_records(const Term &expr);
    Term expand_pattern_records(const Term &pattern);
    // erl_anno:set_generated(true, erl_anno:new(Line)).
    Term no_compiler_warning(const Term &line) const;

    // The file of the qualifier being walked (OTP's qlc_current_file), undefined outside a file.
    Term current_file() const { return file_; }

    NodeEntry *node(const Term &location);

    // --- messages.cpp: lint messages of the QLCs ---
    std::pair<Term, Term> compile_messages(const ModuleForms &forms, const TermList &options);
    TermList intro_variables(const Term &forms_no_shadows);
    Term used_genvar_check(const Term &forms_no_shadows);
    std::pair<Term, Term> no_duplicates(const Term &forms, const Messages &messages, const TermList &options);

    // --- frames.cpp: unification ---
    Term pattern(const Term &pattern, std::int64_t &anon, const Term &frame, const BindTest &bind, Term &pattern_var);
    TermList filter(const Term &expr, const TermList &frames, const BindTest &bind, const Imported &imported,
                    bool safe);
    Term unify(const Term &op, const Term &left, const Term &right, const Term &frame, const BindTest &bind,
               const Imported &imported, bool safe = false);
    Term unify_var_bindings(const TermList &values, const ValueOp &target, const Term &frame, const BindTest &bind,
                            const Imported &imported, bool safe);
    std::pair<Term, Term> element_calls(const Term &term, const Term &frame, const BindTest &bind,
                                        const Imported &imported);
    std::pair<Term, Term> unify_column(const Term &frame, std::int64_t column, const Term &var, const BindTest &bind,
                                       const Imported &imported);
    std::pair<Term, Term> prep_expr(const Term &expr, const Term &frame, const BindTest &bind,
                                    const Imported &imported);
    TermList deref(const Term &term, const Term &frame, const DerefTest &test, const Imported &imported) const;
    TermList deref_values(const Term &term, const Term &frame, const DerefTest &test, const Imported &imported) const;
    TermList deref_var(const Term &var, const Term &frame, const DerefTest &test, const Imported &imported) const;
    std::optional<Term> normalise(const Term &expr) const;

    bool is_bindable(const Term &value) const { return normalise(value).has_value(); }

    bool free_of_integers(const Term &value, const Imported &imported) const;
    // bindings_is_subset/3 of a frame's binds and another frame.
    bool bindings_is_subset(const TermList &binds, const Term &frame, const Imported &imported);
    static TermList frame_binds(const Term &frame);

    // --- columns.cpp: constants, sizes and join columns ---
    struct Constants {
        Term eq_column_constants;
        Term equal_column_constants;
        Term extra_constants;
        Term size_info;
    };

    Constants constants_and_sizes(const TermList &qualifiers, const Term &expr, const Analysis &analysis);
    std::pair<Term, Term> join_kind(const TermList &qualifiers, const Term &line, const Analysis &analysis);
    TermList template_columns(const TermList &qualifiers, const Term &expr, const Analysis &analysis);
    TermList qualifier_dependencies(const TermList &qualifiers, const IntroVars &intro) const;
    TermList filter_list(const TermList &filters, const Analysis &analysis) const;

    struct FilterInfo {
        Term filter;
        std::int64_t anon = 0;
        Imported imported;
    };

    FilterInfo filter_info(const TermList &filters, const Analysis &analysis);
    std::pair<Term, TermList> pattern_frame(const TermList &generators, const BindTest &bind, std::int64_t anon);
    Term filters_as_one(const TermList &filters) const;
    QualData qual_data(const TermList &qualifiers) const;

    // --- generate.cpp: the generated code ---
    Term transform(const Term &forms_no_shadows);
    TermList warn_failing_qualifiers(const TermList &qualifiers, const Analysis &analysis);
    TermList match_spec_quals(const Term &expr, const TermList &qualifiers, const Analysis &analysis);

    // --- #state{} ---
    // Whether the module imports qlc:q/1 (q(...) calls are QLCs too).
    bool imported_ = false;
    // Local and imported functions, which are never guard BIFs in filters.
    std::set<front_end::FunctionKey> overridden_;
    // The most arguments a generated fun takes before the rest go into a tuple (erl_eval's limit).
    std::int64_t max_args_ = 20;
    // The module's record attributes, and their field lists by name for guard tests.
    TermList records_;
    std::map<Word, Term> record_defs_;
    // The join and failing-qualifier warnings transform/2 adds, newest first.
    TermList xwarnings_;
    // The variables each qualifier introduces, [{QId, [V]}] in QId order.
    TermList intro_vars_;

  private:
    Term qlcmf(const Term &term, const QlcVisit &visit, std::int64_t &number);
    std::optional<Term> qlc_call(const Term &call, const QlcVisit &visit, std::int64_t &number);
    std::int64_t next_slot();
    void exclude_integers(const Term &forms);

    Builder build_;
    // erl_lint.erl's behaviour callback lists of the transformed module's behaviours.
    front_end::lint::TermMap<front_end::lint::BehaviourInfo> behaviours_;
    // NodeInfo: entries by integer key, and var_n, the last slot handed out.
    std::map<std::int64_t, NodeEntry> node_info_;
    std::int64_t var_n_ = 0;
    Term file_;
    bool transformed_ = false;

  public:
    // try_ms/4: {ok, MS, MS0} when ets:fun2ms(fun(Pattern) when Filter -> Expr end) translates with the module's
    // records into a match specification that compiles, else no.
    Term try_ms(const MsQuery &query);
};

// The qualifier number of a #qid{} ({qid, LcId, No}) and its QLC's identifier.
std::int64_t qid_no(const Term &qid);
Term qid_lcid(const Term &qid);
} // namespace clause::runtime::builtins::qlc_pt
