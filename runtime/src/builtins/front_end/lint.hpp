#pragma once
#include "../collections/collections.hpp"
#include "forms.hpp"
#include "guard_tests.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// erl_lint in C++ (plan 21 step LIB34C, docs/builtins.md#erl-lint): module/1,2,3, the expression checks erl_eval
// uses and the guard and format helpers, written after OTP's erl_lint clause for clause. The traversal keeps OTP's
// state in one Lint object; variable tables are sorted vectors like OTP's orddicts, and sets and maps OTP keeps in term
// order are std::set/std::map over term order.
namespace clause::runtime::builtins::front_end::lint {
// Term order (collections::compare), as ordsets, orddicts, gb_sets and sofs keep their elements.
struct TermLess {
    bool operator()(const Term &left, const Term &right) const { return collections::compare(left, right) < 0; }
};

using TermSet = std::set<Term, TermLess>;
template <typename Value> using TermMap = std::map<Term, Value, TermLess>;

// A variable's state: bound, exported from a construct, unsafe after one, or a stack trace variable in a guard.
enum class VarKind : std::uint8_t { bound, exported, unsafe, stacktrace };

// What the variable table records of one variable (OTP's {State, Usage, Annos}).
struct VarInfo {
    VarKind kind = VarKind::bound;
    // The {Tag, Location} of the construct an exported or unsafe variable comes from.
    Term where;
    bool used = false;
    // The annotations where the variable was bound, in term order.
    std::vector<Term> annos;
};

// A VarInfo of all its fields.
inline VarInfo var_info(VarKind kind, const Term &where, bool used, std::vector<Term> annos) {
    VarInfo info;
    info.kind = kind;
    info.where = where;
    info.used = used;
    info.annos = std::move(annos);
    return info;
}

struct VarEntry {
    Term name;
    VarInfo info;
};

// A variable table sorted by name, as OTP's orddict.
using VarTable = std::vector<VarEntry>;

// The unused variables of a scope, named so they are not passed in place of the scope's table.
struct UnusedVars {
    const VarTable &table;
};

// The table of variables bound before, named so it is not passed in place of the fresh ones.
struct OldVars {
    const VarTable &table;
};

// The variable table operations of erl_lint (vtupdate/2, vtmerge/2, vtnew/2, vtold/2, ...).
VarTable vtupdate(const VarTable &updated, const VarTable &table);
VarTable vtmerge(const VarTable &left, const VarTable &right);
VarTable vtmerge(std::span<const VarTable> tables);
VarTable vtnew(const VarTable &fresh, OldVars old);
VarTable vtold(const VarTable &fresh, OldVars old);
VarTable vt_no_unsafe(const VarTable &table);
VarTable vt_no_unused(const VarTable &table);
const VarInfo *vtfind(const VarTable &table, const Term &name);
// vt_mark_new/3: the variables of `updated` not in `table`, their state merged with `kind` at `where`.
VarTable vt_mark_new(VarKind kind, const Term &where, const VarTable &updated, const VarTable &table);
// merge_annos/2.
std::vector<Term> merge_annos(const std::vector<Term> &left, const std::vector<Term> &right);
// merge_state/2 of two variables' states, the result written to `into`.
void merge_state(VarInfo &into, const VarInfo &other);

// Shapes of the terms the forms are made of.
inline bool is_pair(const Term &term) { return term.is_tuple() && term.tuple_size_unchecked() == 2; }

inline bool is_triple(const Term &term) { return term.is_tuple() && term.tuple_size_unchecked() == 3; }

// Whether a compile option is {name, Value}.
inline bool is_option_pair(const Term &option, std::string_view name) {
    return is_pair(option) && forms::is_atom(forms::element(option, 0), name);
}

// lists:flatten([Term]).
std::vector<Term> flatten(const Term &term);
// The value of attribute `name` when `form` is one.
std::optional<Term> attribute(const Term &form, std::string_view name);

enum class Flavor : std::uint8_t { tuple, native };

// A record definition: where it is, its flavour and its normalised fields (newest first, as def_fields/4 keeps them).
struct RecordDef {
    Term anno;
    Flavor flavor = Flavor::tuple;
    std::vector<Term> fields;
};

// A type definition: type, opaque or nominal, and where.
struct TypeInfo {
    Term attr;
    Term anno;
};

// One use of a type: where, and the type id it is used from ({export, []}, {record, R}, {spec, MFA}, {type, TA}).
struct UsedType {
    Term anno;
    Term at;
};

// Usage of functions, imports, records and types (OTP's #usage{}).
struct Usage {
    // Who calls whom: FA -> called FAs; none when unused functions are not checked.
    std::optional<TermMap<std::vector<Term>>> calls;
    // {{F, A}, M} of the imports used.
    TermSet imported;
    TermSet used_records;
    // Each type's uses, oldest first.
    TermMap<std::vector<UsedType>> used_types;
};

// The context of a guard expression: a guard, a binary pattern size or a map pattern key.
enum class GexprContext : std::uint8_t { guard, bin_seg_size, map_key };

// A documentation attribute seen and not yet consumed: where, and whether it hides the item.
struct DocSeen {
    Term anno;
    bool hidden = false;
};

// A behaviour's callback lists as erl_lint.erl looked them up: {ok, Value} or error, for callbacks and optional ones.
struct BehaviourInfo {
    Term callbacks;
    Term optional_callbacks;
};

// The parts of the state an expression check may change, so callers can keep some and drop the rest.
struct Snapshot {
    std::size_t errors = 0;
    std::size_t warnings = 0;
    std::size_t called = 0;
    Usage usage;
    TermMap<TermSet> remote_self_calls;
    bool load_nif = false;
    std::optional<TermMap<Term>> fun_used_vars;
};

// The variables a pattern uses (vt) and binds (fresh).
struct PatternVars {
    VarTable vt;
    VarTable fresh;
};

// A segment size as pat_bit_size/4 answers: the size (default, unknown or an integer) and its variables.
struct SizeVars {
    Term size;
    PatternVars vars;
};

// The fold of pattern_element/4: the variables so far and the previous segment's size and annotation.
struct SegmentFold {
    PatternVars vars;
    Term previous_size;
    Term previous_anno;
};

// The bindings of an expression command, as the list erl_eval hands over.
struct BindingList {
    Term list;
};

// A comprehension's template and qualifiers.
struct Comprehension {
    Term template_;
    Term qualifiers;
};

// A generator's pattern and source.
struct Generator {
    Term pattern;
    Term source;
};

// What check_record/4 hands its check: the flavour and the fields, none for a native record without definition.
struct RecordView {
    Flavor flavor = Flavor::tuple;
    std::optional<std::vector<Term>> fields;
};

// How check_existing_field/11 reports a native field without a value: an error, a warning, or not (patterns).
enum class Diagnostics : std::uint8_t { error, warning, none };

// Whether field values are checked as expressions or guard expressions.
enum class Checker : std::uint8_t { expression, guard };

// The fields of one record use and what check_fields/8 checks them against.
struct FieldCheck {
    std::vector<Term> fields;
    RecordView view;
    Term name;
    Diagnostics diagnostics = Diagnostics::error;
};

// The variables of one field of a tuple record pattern; `matched` is false where OTP's fold drops the new variables.
struct FieldPattern {
    VarTable vt;
    VarTable fresh;
    bool matched = false;
};

// The tables a record pattern's fields are checked with.
struct PatternScope {
    VarTable vt;
    VarTable old;
};

// A format call's warning: its level, annotation (none for the call's), format and arguments.
struct FormatWarning {
    std::int64_t level = 1;
    std::optional<Term> anno;
    std::string format;
    std::vector<Term> args;
};

// erl_bits:set_bit_type/2 as the lint reads it: the size, the type's properties and the unit, or the error.
struct BitTypeResult {
    // all, undefined, an integer or unknown, as bit_size_check/4 receives it.
    Term size;
    Term type;
    bool float_type = false;
    bool binary_type = false;
    bool utf_type = false;
    std::optional<std::int64_t> unit;
    std::optional<Term> error;
};

// An annotation with the term it is about: an error or warning to report, a name or value to check.
struct AnnoTerm {
    Term anno;
    Term term;
};

// A remote name: the module and the function or type name.
struct Remote {
    Term module;
    Term name;
};

// The errors of the nowarn_bif_clash compile attributes: bare ones, and ones with a function list.
struct BifClashErrors {
    std::vector<Term> plain;
    std::vector<Term> specific;
};

// A behaviour attribute with the callbacks it requires and the ones it leaves optional.
struct BehaviourCallbacks {
    Term anno;
    Term behaviour;
    std::vector<Term> required;
    std::vector<Term> optional;
};

// The compile options a lint runs with, as the list they are given in.
struct CompileOptions {
    Term list;
};

// The lint of one module or command.
class Lint final {
  public:
    Lint(ProcessContext &context, const Term &file, const CompileOptions &options);

    // module/1,2,3 over the forms, with the behaviours' callback lists.
    void module(const Term &forms, const TermMap<BehaviourInfo> &behaviours);
    // exprs_opt/3 over the expressions with the bindings.
    void command(const Term &exprs, const BindingList &bindings);
    // used_vars/2: the fun_used_vars map.
    Term used_vars(const Term &exprs, const Term &bindings);
    // {ok, Warnings} or {error, Errors, Warnings}.
    Term status() const;
    // format_error_1/1 as clause_erl_lint:format_spec/1 answers it.
    Term format_spec(const Term &error);

    // --- state (OTP's #lint{}) ---
    forms::Builder build_;
    enum class Phase : std::uint8_t { start, attribute, function } phase_ = Phase::start;
    Term module_;
    std::vector<std::pair<Term, Term>> behaviour_;
    TermSet exports_;
    TermMap<Term> imports_;
    TermMap<TermSet> remote_self_calls_;
    Term compile_;
    std::vector<Term> compile_list_;
    TermMap<RecordDef> records_;
    TermSet locals_;
    bool no_auto_all_ = false;
    TermSet no_auto_;
    TermMap<Term> rec_imports_;
    TermSet defined_;
    // The on_load functions, newest first, and the annotation of the last.
    std::vector<Term> on_load_;
    Term on_load_anno_;
    TermSet not_deprecated_;
    TermSet not_removed_;
    TermSet not_unsafe_;
    // The function being checked ({F, A}), or none.
    std::optional<Term> func_;
    Term type_id_;
    std::int64_t warn_format_ = 1;
    std::set<std::string_view> enabled_;
    std::vector<Term> nowarn_bif_clash_;
    // {File, Info} in the order they were added.
    std::vector<std::pair<Term, Term>> errors_;
    std::vector<std::pair<Term, Term>> warnings_;
    Term file_;
    bool recdef_top_ = false;
    bool xqlc_ = false;
    // {{F, A}, Anno} of the calls, oldest first.
    std::vector<std::pair<Term, Term>> called_;
    std::optional<TermMap<Term>> fun_used_vars_;
    Usage usage_;
    TermMap<Term> specs_;
    TermSet hidden_docs_;
    TermMap<Term> callbacks_;
    TermMap<Term> optional_callbacks_;
    TermMap<TypeInfo> types_;
    TermSet exp_types_;
    std::vector<Term> features_;
    std::optional<VarTable> bvt_;
    GexprContext gexpr_context_ = GexprContext::guard;
    bool load_nif_ = false;
    std::optional<DocSeen> doc_defined_;
    std::optional<DocSeen> moduledoc_defined_;
    // The forms with their files in their annotations, for the checks after the traversal.
    Term forms_;
    // What guard_scope/0 hands the guard checks: record definitions and local or imported functions.
    std::map<Word, Term> guard_records_;
    std::set<FunctionKey> guard_overridden_;

    // --- helpers (lint_state.cpp) ---
    void apply_warning_option(const Term &option);
    void read_bif_clash_and_features();
    void add_pseudo_functions();

    Term atom(AtomName name) const { return build_.atom(name); }

    Term pair(const Term &first, const Term &second) const;
    Term triple(const Term &first, const Term &second, const Term &third) const;
    Term chars(std::string_view text) const;
    Term fa(const Term &name, std::int64_t arity) const;
    bool warn_enabled(std::string_view category) const;
    std::pair<Term, Term> loc(const Term &anno) const;
    Term set_file(const Term &anno, const Term &file) const;
    Term bitsize_error(const Term &error) const;
    void add_error(const AnnoTerm &report);
    void add_error_info(const Term &info);
    void add_warning(const AnnoTerm &report);
    void add_warning_info(const Term &info);
    void maybe_add_warning(const Term &anno, const Term &warning);
    Snapshot snapshot() const;

    bool is_local_function(const Term &key) const { return locals_.contains(key); }

    bool is_imported_function(const Term &key) const { return imports_.contains(key); }

    bool is_imported_from_erlang(const Term &key) const;

    bool autoimport_suppressed(const Term &key) const { return no_auto_all_ || no_auto_.contains(key); }

    bool bif_clash_disabled(const Term &key) const;
    bool no_guard_bif_clash(const Term &key) const;
    bool export_all() const;
    TermSet exports() const;
    void check_module_name(const AnnoTerm &module);
    std::optional<Term> most_possible(const Term &name, const std::vector<Term> &names) const;
    void keyword_warning(const Term &anno, const Term &name);
    void call_function(const AnnoTerm &callee, std::int64_t arity);
    void used_record(const Term &name);
    void used_type(const Term &type_pair, const Term &anno);
    GuardScope guard_scope();

    // --- forms and attributes (lint_forms.cpp) ---
    void forms(const Term &forms);
    std::vector<Term> forms_with_files(const Term &forms) const;
    void read_no_auto_import();
    void warn_export_all(const std::vector<Term> &all);
    BifClashErrors bif_clash_errors(const Term &forms);
    void disallowed_compile_flags(const Term &forms);
    void not_filtered(const Term &forms);
    void form(const Term &form);
    void start_state(const Term &form);
    void attribute_state(const Term &form);
    bool typed_attribute(const Term &form, bool in_functions);
    void function_attribute(const Term &form, std::string_view tag);
    void function_state(const Term &form);
    void track_doc(const Term &form);
    void untrack_doc(bool function, const Term &name, std::int64_t arity);
    void export_attr(const AnnoTerm &exported);
    void export_type(const AnnoTerm &exported);
    std::vector<std::pair<Term, std::optional<Term>>> import_clashes(const TermSet &functions);
    bool is_bif_import(const Term &function) const;
    bool report_import_clashes(const Term &anno, const std::vector<std::pair<Term, std::optional<Term>>> &clashes);
    void import(const AnnoTerm &imported);
    std::vector<Term> record_import_clashes(const std::vector<Term> &names);
    void import_native_record(const AnnoTerm &imported);
    void on_load(const AnnoTerm &declared);
    void module_attr(const AnnoTerm &declared);
    void behaviour_attr(const AnnoTerm &declared);
    Term normalised_field(const Term &field, Flavor flavor);
    void record_def(const Term &anno, Flavor flavor, const Term &value);
    std::vector<Term> def_fields(const std::vector<Term> &fields, Flavor flavor, const Term &name);
    Term checked_default(const Term &field);
    void function(const Term &form);
    void define_function(const Term &anno, const Term &name, std::int64_t arity);
    void clauses(const Term &list);
    void clause(const Term &clause);
    Term nowarn_anno() const;
    std::int64_t list_length(const Term &list) const;

    // --- checks after the traversal (lint_checks.cpp) ---
    void post_traversal_check(const Term &forms, const TermMap<BehaviourInfo> &behaviours);
    void check_behaviour(const TermMap<BehaviourInfo> &behaviours);
    void behaviour_callbacks(const TermMap<BehaviourInfo> &behaviours, BehaviourCallbacks &out);
    void behaviour_deprecated(const AnnoTerm &behaviour, const std::vector<Term> &callbacks, const TermSet &exported);
    void check_attribute_functions(const Term &forms, std::string_view attribute_name);
    std::vector<Term> attribute_errors(std::string_view attribute_name, const Term &item,
                                       const std::vector<Term> &exported);
    std::vector<Term> attribute_function(std::string_view attribute_name, const Term &name, const Term &arity,
                                         const std::vector<Term> &exported);
    bool is_builtin(const Term &module, const Term &name, std::int64_t arity) const;
    bool is_exported_or_builtin(const Term &function, std::int64_t arity, const std::vector<Term> &exported) const;
    void warn_unused_imports(const Term &form, const Term &value);
    void check_imports(const Term &forms);
    void check_option_functions(const Term &forms, std::string_view tag, AtomName type);
    void func_location_error(AtomName type, const std::vector<std::pair<Term, Term>> &bad,
                             const std::vector<Term> &defined);
    void warn_unused_function(const std::vector<Term> &forms, const Term &function);
    void check_unused_functions(const Term &forms);
    void check_undefined_functions();
    void check_specs_without_function();
    void check_functions_without_spec(const Term &forms);
    void check_undefined_types();
    std::optional<Term> first_file(const Term &forms) const;
    void check_unused_types(const Term &forms);
    void check_untyped_records(const Term &forms);
    void check_on_load();
    void check_export_record(const Term &forms);
    void check_unused_records(const Term &forms);
    void check_native_records_header(const Term &forms);
    void check_local_opaque_types();
    void check_dialyzer_attribute(const Term &forms);
    void check_callback_information();
    void check_nifs(const Term &forms);
    void check_unexported_functions();

    // --- expressions (lint_exprs.cpp) ---
    Term where(const Term &tag, const Term &anno) const;
    VarTable exprs(const Term &list, const VarTable &vt);
    VarTable expr(const Term &node, const VarTable &vt);
    VarTable expr_list(const std::vector<Term> &list, const VarTable &vt);
    VarTable vtupd_export_expr_list(const Term &place, const std::vector<Term> &list, const VarTable &vt);
    VarTable vtupd_unsafe(const Term &place, const VarTable &fresh, const VarTable &old);
    VarTable vtupd_export(const Term &place, const VarTable &fresh, const VarTable &old);
    VarEntry merge_pattern_vars(const VarEntry &left, const VarEntry &right);
    VarTable vtmerge_pat(const VarTable &left, const VarTable &right);
    VarTable expr_variable(const Term &node, const VarTable &vt);
    VarTable expr_constant(const Term &node, const VarTable &vt);
    VarTable expr_atom(const Term &node, const VarTable &vt);
    VarTable expr_cons(const Term &node, const VarTable &vt);
    VarTable expr_comprehension(const Term &node, const VarTable &vt);
    VarTable expr_tuple(const Term &node, const VarTable &vt);
    void check_assoc_fields(const Term &fields);
    VarTable map_fields(const Term &place, const VarTable &vt, const Term &fields, bool guard_context);
    VarTable expr_map(const Term &node, const VarTable &vt);
    VarTable expr_map_update(const Term &node, const VarTable &vt);
    VarTable expr_record(const Term &node, const VarTable &vt);
    VarTable expr_binary(const Term &node, const VarTable &vt);
    VarTable expr_block(const Term &node, const VarTable &vt);
    VarTable expr_if(const Term &node, const VarTable &vt);
    VarTable expr_case(const Term &node, const VarTable &vt);
    VarTable expr_receive_after(const Term &node, const VarTable &vt);
    VarTable expr_fun(const Term &node, const VarTable &vt);
    void local_fun(const Term &anno, const Term &name, const Term &arity_term);
    VarTable expr_named_fun(const Term &node, const VarTable &vt);
    VarTable expr_call(const Term &node, const VarTable &vt);
    Term is_record_callee(const Term &callee, std::size_t arity);
    VarTable remote_call(const AnnoTerm &call, const std::vector<Term> &args, const VarTable &vt);
    void warn_invalid_call(const AnnoTerm &call);
    VarTable expr_try(const Term &node, const VarTable &vt);
    VarTable expr_catch(const Term &node, const VarTable &vt);
    VarTable expr_match(const Term &node, const VarTable &vt);
    VarTable expr_maybe(const Term &node, const VarTable &vt);
    VarTable expr_maybe_else(const Term &node, const VarTable &vt);
    VarTable expr_unary(const Term &node, const VarTable &vt);
    VarTable expr_binary_op(const Term &node, const VarTable &vt);
    VarTable expr_remote(const Term &node, const VarTable &vt);
    bool check_imported_call(const AnnoTerm &call, const std::vector<Term> &args, const Term &key);
    bool check_bif_call(const AnnoTerm &call, const Term &key, std::int64_t arity);
    void check_call(const Term &anno, const Term &name, const std::vector<Term> &args, const Term &name_anno);
    void check_remote_function(const Term &anno, const Remote &function, const std::vector<Term> &args);
    void check_load_nif(const Term &anno);
    const TermSet &obsolete_filter(std::string_view kind) const;
    void deprecated_function(const Term &anno, const Remote &function, std::int64_t arity);
    void check_remote_self_call(const Term &anno, const Remote &function, std::int64_t arity);
    VarTable fun_clauses(const Term &clauses, const VarTable &vt);
    VarTable fun_clauses1(const Term &clauses, const VarTable &vt);
    VarTable fun_clause(const Term &clause, const VarTable &vt0);
    VarTable icrt_clauses(const Term &clauses, const Term &in, const VarTable &vt);
    std::vector<VarTable> icrt_clause_tables(const Term &clauses, const VarTable &vt0);
    std::vector<VarTable> catch_clause_tables(const Term &clauses, const VarTable &vt0);
    VarTable taint_stack_var(const Term &stack, const VarTable &vt);
    VarTable check_unused_vars(const VarTable &vt, const VarTable &old);
    VarTable check_old_unused_vars(const VarTable &vt, const VarTable &old);
    static VarTable unused_vars(const VarTable &vt);
    VarTable warn_unused_vars(UnusedVars unused_table, const VarTable &vt);
    void shadow_vars(const VarTable &vt, const VarTable &old, const Term &in);
    VarTable expr_var(const Term &name, const Term &anno, const VarTable &vt);
    VarTable exported_var(const Term &name, const Term &anno, const VarInfo &info);
    void unbound_var(const Term &name, const Term &anno, const VarTable &vt);
    bool export_var_subexpr(const Term &place) const;
    void warn_underscore_match(const Term &name, const Term &anno);

    // --- scopes and comprehensions (lint_scopes.cpp) ---
    Term state_term(const VarInfo &info) const;
    VarTable icrt_export(const std::vector<VarTable> &tables, const VarTable &vt, const Term &in_where);
    VarTable handle_comprehension(const Comprehension &comprehension, const VarTable &vt0);
    VarTable comprehension_exprs(const Term &template_, const VarTable &vt);
    VarTable comprehension_expr(const Term &node, const VarTable &vt);
    std::pair<VarTable, VarTable> lc_quals(const Term &qualifiers, const VarTable &vt0);
    void lc_qual(const Term &qualifier, VarTable &vt, VarTable &uvt);
    void compr_assign(const Term &qualifier, VarTable &vt, VarTable &uvt);
    std::vector<Term> filter_generators(const Term &qualifiers);
    std::pair<VarTable, VarTable> handle_generators(const std::vector<Term> &generators, const VarTable &vt,
                                                    const VarTable &uvt);
    std::pair<VarTable, VarTable> handle_generator(const Generator &generator, const VarTable &vt, const VarTable &uvt,
                                                   bool check_unused_source);
    std::pair<VarTable, VarTable> generated(const Term &pattern, const VarTable &evt, const VarTable &vt,
                                            const VarTable &uvt);
    std::pair<VarTable, VarTable> generated_from(const Term &pattern, const VarTable &vt1, const VarTable &uvt);
    PatternVars comprehension_pattern(const Term &pattern, const VarTable &vt);
    void bitstring_generator_pattern(const Term &pattern);

    // --- patterns and guards (lint_patterns.cpp) ---
    PatternVars pattern(const Term &node, const VarTable &vt, const VarTable &old);
    PatternVars compound_pattern(const Term &node, const VarTable &vt, const VarTable &old);
    PatternVars append_pattern(const Term &node, const VarTable &vt, const VarTable &old);
    PatternVars match_pattern(const Term &node, const VarTable &vt, const VarTable &old);
    PatternVars pattern_list(const std::vector<Term> &patterns, const VarTable &vt, const VarTable &old);
    PatternVars head(const Term &patterns, const VarTable &vt, const VarTable &old);
    PatternVars new_pattern_var(const Term &name, const Term &anno);
    void warn_reused_pattern_var(const Term &name, const Term &anno, const VarInfo &found);
    PatternVars pat_var(const Term &name, const Term &anno, const VarTable &old, const VarTable &fresh);
    PatternVars pat_binsize_var(const Term &name, const Term &anno, const VarTable &vt, const VarTable &fresh);
    PatternVars pattern_map(const Term &fields, const VarTable &vt, const VarTable &old);
    PatternVars pattern_bin(const Term &segments, const VarTable &vt, const VarTable &old);
    bool good_string_size_type(const Term &size, const Term &types) const;
    void pattern_element(const Term &segment, const VarTable &vt, OldVars old, SegmentFold &fold);
    PatternVars pat_bit_expr(const Term &node, const VarTable &old, const VarTable &fresh);
    SizeVars pat_bit_size(const Term &size, const VarTable &vt, const VarTable &fresh);
    VarTable expr_bin(const Term &segments, const VarTable &vt, bool guard_context);
    std::pair<Term, VarTable> bit_size(const Term &size, const VarTable &vt, bool guard_context);
    BitTypeResult bit_type(const AnnoTerm &size, const Term &types);
    Term bit_size_check(const Term &anno, const BitTypeResult &settled);
    VarTable guard(const Term &guard, const VarTable &vt);
    VarTable guard_tests(const Term &tests, const VarTable &vt);
    VarTable guard_test(const Term &test, const VarTable &vt);
    void obsolete_guard(const Term &test);
    VarTable gexpr_list(const std::vector<Term> &list, const VarTable &vt);
    VarTable gexpr(const Term &node, const VarTable &vt);
    VarTable gexpr_unary(const Term &node, const VarTable &vt);
    VarTable gexpr_compound(const Term &node, const VarTable &vt);
    VarTable gexpr_binary(const Term &node, const VarTable &vt);
    VarTable gexpr_local_call(const AnnoTerm &callee, const std::vector<Term> &args, const VarTable &vt);
    VarTable gexpr_erlang_call(const AnnoTerm &callee, const std::vector<Term> &args, const VarTable &vt);
    VarTable gexpr_call(const Term &node, const VarTable &vt);
    VarTable gexpr_is_record(const Term &anno, const std::vector<Term> &args, const VarTable &vt);
    void expr_check_match_zero(const Term &node);
    void warn_obsolete_op(const AnnoTerm &op);

    // --- records (lint_records.cpp) ---
    std::optional<RecordView> check_record(const Term &anno, const Term &name);
    VarTable record_field_expr(const Term &node, const VarTable &vt, bool guard_context);
    VarTable guard_record(const Term &node, const RecordView &view, const VarTable &vt);
    VarTable record_expr(const Term &node, const VarTable &vt, bool guard_context);
    void record_index(const Term &node);
    VarTable checked_record_source(const AnnoTerm &source, const VarTable &vt);
    void record_field(const Term &field, const RecordView &view, const Term &name);
    VarTable record_update(const Term &node, const VarTable &vt);
    VarTable init_fields(const Term &node, const RecordView &view, const VarTable &vt);
    VarTable ginit_fields(const Term &node, const std::vector<Term> &fields, const VarTable &vt);
    std::vector<Term> default_inits(const std::vector<Term> &given, const Term &anno,
                                    const std::vector<Term> &fields) const;
    VarTable check_fields(const FieldCheck &check, const VarTable &vt, Checker checker);
    VarTable check_value(const Term &value, const VarTable &vt, Checker checker);
    VarTable check_field(const Term &field, const FieldCheck &check, const VarTable &vt, std::vector<Term> &seen,
                         Checker checker);
    VarTable check_variable_field(const Term &field, const FieldCheck &check, const VarTable &vt,
                                  std::vector<Term> &seen, Checker checker);
    void report_novalue(const FieldCheck &check, const AnnoTerm &field);
    VarTable check_existing_field(const AnnoTerm &name, const Term &value, const FieldCheck &check, const VarTable &vt,
                                  Checker checker);
    VarTable check_nn_fields(const Term &name, const std::vector<Term> &fields, const VarTable &vt);
    PatternVars record_pattern(const Term &node, const VarTable &vt, const VarTable &old);
    PatternVars tuple_record_pattern(const AnnoTerm &name, const std::vector<Term> &fields, const RecordDef &record,
                                     const PatternScope &scope);
    PatternVars pattern_fields(const Term &name, const std::vector<Term> &fields, const RecordDef &record,
                               const PatternScope &scope);
    FieldPattern pattern_field_value(const Term &field, const FieldCheck &check, const PatternScope &scope,
                                     std::vector<Term> &seen);
    PatternVars native_record_pattern(const Term &name, const std::vector<Term> &fields, const VarTable &vt,
                                      const VarTable &old);
    void check_native_record_fields_usage(const Term &name, const std::vector<Term> &fields);
    void exist_any_record(const Term &anno, const Term &name);
    void suggest_record_name(const Term &anno, AtomName error, const Term &name);
    void suggest_field_name(const Term &anno, const Term &name, const Term &field, const std::vector<Term> &fields);
    void check_record_info_call(const Term &anno, const std::vector<Term> &args);
    void warn_if_literal_update(const AnnoTerm &update);
    void check_field_default(const Term &name, const Term &field);
    bool field_default_ok(const Term &init) const;
    bool map_defaults_ok(const std::vector<Term> &entries) const;

    // --- types and specs (lint_types.cpp) ---
    void type_def(const Term &attr, const AnnoTerm &declared);
    bool nowarn_builtin(const Term &type_pair) const;
    void check_type(const Term &type);
    void deprecated_type(const Term &anno, const Remote &type, const Term &args);
    void spec_decl(const AnnoTerm &declared);
    void callback_decl(const AnnoTerm &declared);
    void check_spec(const Term &spec, AtomName error, const Term &arity);
    void check_specs(const Term &specs, AtomName error, const Term &arity);
    void optional_callbacks(const AnnoTerm &declared);

    // --- format strings (lint_format.cpp) ---
    void format_function(const Term &anno, const Remote &function, const std::vector<Term> &args);
    std::optional<FormatWarning> check_format(const std::vector<Term> &args);
    Term canonical_args(const Term &args) const;
    std::optional<std::u32string> format_text(const Term &format);
    std::optional<FormatWarning> check_args_list(const Term &args);
    std::optional<FormatWarning> check_format_text(const Term &format, const std::optional<Term> &args);
    std::optional<FormatWarning> check_needs(std::size_t needed, const Term &format_anno,
                                             const std::optional<Term> &args);

    // --- partial evaluation (lint_eval.cpp) ---
    Term partial_eval(const Term &expr) const;
    bool is_pattern_expr(const Term &expr) const;
};

// erl_eval:partial_eval/1: an integer, float or atom node when `expr` folds to one, else `expr`.
Term partial_eval(ProcessContext &context, const Term &expr);
// erl_lint:is_pattern_expr/1.
bool is_pattern_expr(ProcessContext &context, const Term &expr);
// check_format_string(Format, Strict): {ok, Needs} or {error, Message}.
Term check_format_string(ProcessContext &context, const std::u32string &format, bool strict);
BitTypeResult set_bit_type(Lint &lint, const Term &size, const Term &types);
// otp_internal:obsolete/3, obsolete_type/3 and obsolete_callback/3: none for `no`, else the tuple.
std::optional<Term> obsolete(Lint &lint, const Term &module, const Term &name, std::int64_t arity);
std::optional<Term> obsolete_type(Lint &lint, const Term &module, const Term &name, std::int64_t arity);
std::optional<Term> obsolete_callback(Lint &lint, const Term &module, const Term &name, std::int64_t arity);
} // namespace clause::runtime::builtins::front_end::lint
