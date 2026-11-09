#pragma once
#include "declarations.hpp"

namespace clause::semantic {
// Index included declarations and validate closed defaults before function binding analysis.
void index_records(Module &module, const Reporter &out);
// Charge expanded fields at every source use, including omitted undefined/default fields.
bool record_budget(struct BindingAnalysis &state, const ast::ExprId &id);
// Resolve only visible ordinary record names; native/qualified/inferred forms keep separate capability gates.
const RecordLayout *record_layout(const Module &module, const ast::RecordIdentity &identity);
const RecordLayout *record_layout(const Module &module, const ast::Atom &name, const ast::NodeSource &source);
// Field positions are zero-based after the tuple tag, independently of initializer source order.
std::optional<std::size_t> record_field(const RecordLayout &layout, const ast::Atom &name);
// Select explicit/wildcard/default fields in declaration order; omitted pattern fields have no constraint.
std::vector<std::optional<ast::ExprId>> record_values(const Module &module, const ast::RecordExpression &record,
                                                      bool pattern);
// A construction's `_ = Value` when every field is named: it initializes nothing and is never evaluated, but its
// variables are still checked (record wildcards).
std::optional<ast::ExprId> unused_wildcard(const Module &module, const ast::RecordExpression &record);
// Positions of record_values in evaluation order: declaration order for tuple records; for native records the
// explicit fields in source order, then the defaulted ones (OTP v3_core).
std::vector<std::size_t> record_order(const Module &module, const ast::RecordExpression &record);

// A native record named by its defining module, as qualified (#m:r) and imported forms name it.
struct RecordName {
    std::u32string module;
    std::u32string name;
};

// The record a qualified identity or an imported name refers to; nullopt for local and anonymous identities.
std::optional<RecordName> external_record(const Module &module, const ast::RecordIdentity &identity);
// Whether an identity is the anonymous #_ form, which reads any native record's captured definition.
bool anonymous_record(const ast::RecordIdentity &identity);
// The module an -import_record attribute imports a record name from, if any.
const std::u32string *imported_module(const Module &module, const std::u32string &name);
// The definition external construction uses: an exported native record of a batch module; null otherwise.
const RecordLayout *external_layout(const Module &module, const RecordName &record);
// Native record definitions of a module in descriptor order (record name order).
std::vector<const RecordLayout *> native_layouts(const Module &module);
// Values a record pattern matches: explicit native fields in source order (they may name fields the record lacks),
// or the selected tuple-record fields in declaration order.
std::vector<ast::ExprId> pattern_fields(const Module &module, const ast::RecordExpression &record);
// Diagnose missing declarations, duplicate/unknown fields and invalid wildcard initializers at original sites;
// patterns, like updates, may name fields a native record does not declare.
void validate_record(const Module &module, const ast::Expression &expression, const Reporter &out,
                     bool pattern = false);
// An unqualified record_info/2 call is the compile-time pseudo-function, never a local or builtin call.
bool record_info_call(const ast::Module &syntax, const ast::ExprValue &value);

struct RecordInfo {
    // The visible tuple record whose declaration answers the query.
    const RecordLayout &layout;
    // record_info(fields, R) lists the field names; record_info(size, R) is the tuple arity.
    bool fields;
};

// Resolve a valid record_info/2 call; invalid calls are diagnosed by validate_record.
std::optional<RecordInfo> record_info(const Module &module, const ast::Expression &expression);
// Enforce record test literal rules separately from ordinary body BIF argument errors.
void validate_record_test(const Module &module, const ast::Expression &expression, const ast::CallExpression &call,
                          bool guard, const Reporter &out);
} // namespace clause::semantic
