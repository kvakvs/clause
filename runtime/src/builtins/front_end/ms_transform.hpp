#pragma once
#include "forms.hpp"
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

// ms_transform in C++ (C++ ms_transform, docs/builtins.md#ms-transform): the translation of literal funs given to
// ets:fun2ms/1 and dbg:fun2ms/1 into abstract match specifications, over the abstract format, as
// library/stdlib/ms_transform.erl did in Erlang. qlc_pt calls it directly, so its match specification attempts need
// no Erlang round trip.
namespace clause::runtime::builtins::front_end::ms {
using forms::Builder;

// The fun2ms/1 a fun is given to: ets's (one variable, tuple or record parameter) or dbg's (variable or list).
enum class Dialect : std::uint8_t { ets, dbg };
// Whether a guard or a body is translated: body error codes add 100.
enum class Part : std::uint8_t { guard, body };

// OTP's ms_transform error codes (format_error/1 in library/stdlib/ms_transform.erl); a body adds BODY_BASE.
inline constexpr std::int64_t BODY_BASE = 100;
inline constexpr std::int64_t ERR_NOFUN = 1;
inline constexpr std::int64_t ERR_ETS_HEAD = 2;
inline constexpr std::int64_t ERR_DBG_HEAD = 3;
inline constexpr std::int64_t ERR_HEADMATCH = 4;
inline constexpr std::int64_t ERR_SEMI_GUARD = 5;
inline constexpr std::int64_t ERR_HEADBADREC = 7;
inline constexpr std::int64_t ERR_HEADBADFIELD = 8;
inline constexpr std::int64_t ERR_HEADMULTIFIELD = 9;
inline constexpr std::int64_t ERR_HEADDOLLARATOM = 10;
inline constexpr std::int64_t ERR_HEADBINMATCH = 11;
inline constexpr std::int64_t ERR_GENMATCH = 16;
inline constexpr std::int64_t ERR_GENLOCALCALL = 17;
inline constexpr std::int64_t ERR_GENELEMENT = 18;
inline constexpr std::int64_t ERR_GENBADFIELD = 19;
inline constexpr std::int64_t ERR_GENBADREC = 20;
inline constexpr std::int64_t ERR_GENMULTIFIELD = 21;
inline constexpr std::int64_t ERR_GENREMOTECALL = 22;
inline constexpr std::int64_t ERR_GENBINCONSTRUCT = 23;
inline constexpr std::int64_t WARN_SHADOW_VAR = 50;

// A fun that cannot be translated: OTP's throw({ms_error, Anno, Reason}).
struct MsError {
    Term anno;
    Term reason;
};

// A record field: its name, its tuple position (2 for the first field) and its default value node.
struct Field {
    Term name;
    std::int64_t position = 0;
    Term default_value;
};

// A record definition: its name and its fields, first to last.
struct Record {
    Term name;
    std::vector<Field> fields;
};

// A fun's annotation and clauses, as clause_list/5 takes them.
struct FunClauses {
    Term anno;
    Term clauses;
};

// The variables bound around a fun, by the atom words of their names.
using Bound = std::set<Word>;

// A head translation's variables: {Name, Translated} newest first ('_' stays '_'), the next '$N' and the variables
// bound around the fun (OTP's #head{}).
struct HeadScope {
    std::vector<std::pair<Word, Term>> names;
    std::int64_t next = 1;
    const Bound *outer = nullptr;
};

// A guard or body translation: the head's variables and the part (OTP's #tg{}).
struct PartScope {
    const std::vector<std::pair<Word, Term>> &names;
    Part part;
};

// The {Field, Value} of a record pattern or construction's fields in the order written, and the values of its
// `_ = Value` fields (OTP's {default} key).
struct GivenFields {
    std::vector<std::pair<Term, Term>> named;
    std::vector<Term> defaults;
};

// The translation of one module or fun: OTP's #st{} (records, shadowing warnings, records used as tuples).
class Translator final {
  public:
    explicit Translator(ProcessContext &context) : build_(context) {}

    const Builder &build() const { return build_; }

    // A record attribute's {Name, Fields}: its fields numbered from 2; a later definition hides an earlier one.
    void define(const Term &definition);
    // clause_list/5: a fun's clauses as an abstract list of {Head, Guards, Body} tuples, annotated with the fun's
    // annotation; a clause with alternative guards gives one tuple per guard. Throws MsError.
    Term clause_list(const FunClauses &fun, Dialect dialect, const Bound &bound);

    // The shadowing warnings, {Location, {50, Name}} in the order found.
    const std::vector<Term> &warnings() const { return warnings_; }

    // The names of the records whose uses became tuples, in the order used (duplicates kept).
    const std::vector<Term> &used() const { return used_; }

    // record/2: the fields of the newest record `name`, noted as used; null when none is defined.
    const Record *record(const Term &name);

  private:
    // ms_head.cpp: the head pattern.
    Term ms_clause(const Term &clause, Dialect dialect, const Bound &bound);
    Term transform_head(const Term &parameters, HeadScope &scope);
    // A {var, Anno, Name} node's name bound to '$_', and the warning of a head variable bound around the fun.
    void bind_object(const Term &var, HeadScope &scope);
    void shadow_check(const Term &var, const HeadScope &scope);
    Term head(const Term &pattern, HeadScope &scope);
    Term head_atom(const Term &node) const;
    Term head_var(const Term &node, HeadScope &scope);
    Term head_record(const Term &node, HeadScope &scope);
    Term head_children(const Term &term, HeadScope &scope);

    // ms_guards.cpp: guards and bodies; the handlers of part/2 take the node and the scope.
    Term transform_guards(const Term &anno, const Term &guards, const PartScope &scope);
    Term part_list(const Term &anno, std::span<const Term> exprs, const PartScope &scope);
    Term part(const Term &expr, const PartScope &scope);
    Term part_tuple(const Term &anno, std::vector<Term> lead, const Term &args, const PartScope &scope);
    Term part_match(const Term &node, const PartScope &scope);
    Term part_literal(const Term &node, const PartScope &scope);
    Term part_op(const Term &node, const PartScope &scope);
    Term part_tuple_node(const Term &node, const PartScope &scope);
    Term part_cons(const Term &node, const PartScope &scope);
    Term part_atom(const Term &node, const PartScope &scope);
    Term part_var(const Term &node, const PartScope &scope);
    Term part_call(const Term &call, const PartScope &scope);
    Term part_remote(const Term &call, const PartScope &scope);
    Term part_record_test(const Term &call, const PartScope &scope);
    Term part_record_field(const Term &node, const PartScope &scope);
    Term part_record_index(const Term &node, const PartScope &scope);
    Term part_record(const Term &node, const PartScope &scope);
    GivenFields construction_fields(const Term &node, const PartScope &scope);
    Term part_record_update(const Term &node, const PartScope &scope);
    Term part_bin_element(const Term &node, const PartScope &scope);
    Term part_bin(const Term &node, const PartScope &scope);
    Term part_map_field(const Term &node, const PartScope &scope);
    Term part_other(const Term &node, const PartScope &scope);

    // Throw MsError with `reason` at `anno`; fail_part adds 100 to a body's code and the details as a tuple.
    [[noreturn]] void fail(const Term &anno, const Term &reason) const;
    [[noreturn]] void fail_part(const Term &anno, std::int64_t code, const PartScope &scope,
                                std::span<const Term> details) const;

    Builder build_;
    // The record definitions in definition order (the newest last).
    std::vector<Record> records_;
    std::vector<Term> warnings_;
    std::vector<Term> used_;
};

// The value of `field` among the given ones (the last one written), else `fallback`.
Term field_value(const GivenFields &given, const Field &field, const Term &fallback);

// The error codes of check_fields/6: a field named twice and a field the record does not have.
struct FieldCodes {
    std::int64_t multi;
    std::int64_t bad;
};

// check_fields/6: a field named twice (the newest first), then a field the record does not have, is an MsError.
void check_fields(const Builder &build, const Term &anno, const Record &record, const GivenFields &given,
                  FieldCodes codes);
// split_default/2 and its check: the value of the last `_ = Value` field written, none without one; MsError
// {Multi, Name, '_'} when there are two.
std::optional<Term> split_default(const Builder &build, const Term &anno, const Term &name, const GivenFields &given,
                                  std::int64_t multi);
// fold_constant/1: an operator over constants as its integer, float or atom value; none when it is not one.
std::optional<Term> fold_constant(ProcessContext &context, const Term &expr);

// The abstract match specification of `function`'s fun2ms call when the forms around it translate, with the module's
// record definitions; what qlc_pt's try_ms/1 asks of ms_transform:parse_transform/2.
struct ModuleRun {
    // The translated forms in order, or none after an MsError.
    std::vector<Term> forms;
    // The file of the last file attribute ("TOP_LEVEL" before one).
    Term file;
    // The error's annotation and reason, when translation stopped.
    std::optional<MsError> error;
    std::vector<Term> warnings;
    std::vector<Term> used;
};

// transform_forms/3: every ets:fun2ms/1 and dbg:fun2ms/1 of a literal fun in the forms replaced by its match
// specification, the file and record attributes updating the state; stops at the first fun that does not translate.
ModuleRun transform_forms(ProcessContext &context, const Term &forms);
} // namespace clause::runtime::builtins::front_end::ms
