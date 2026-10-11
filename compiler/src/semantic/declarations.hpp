#pragma once
#include "patterns.hpp"
#include <array>
#include <clause/abi/immediate_services.hpp>
#include <clause/compiler/ast/module.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>

namespace clause::semantic {
// Compare decoded names and arities without host-dependent hashes.
struct FunctionKey {
    // Preserve exact Erlang identity, including quoted and Unicode names.
    std::u32string name;
    std::size_t arity;
    auto operator<=>(const FunctionKey &) const = default;
};

struct BindingId {
    // Source-ordered clause and local definition indices never depend on spelling or addresses.
    std::size_t clause;
    std::size_t local;
    auto operator<=>(const BindingId &) const = default;
};

enum class BindingUse : std::uint8_t { read, definition, exact_check };
enum class BindingContext : std::uint8_t { head, guard, body };

struct Binding {
    // Locate each read, definition or exact-equality obligation in the original owned syntax.
    ast::ExprId expression;
    BindingId identity;
    BindingUse use;
    BindingContext context;
};

struct BindingDefinition {
    // Retain declaration spelling/source separately from clause-local identity.
    std::u32string name;
    ast::ExprId expression;
    // Only a whole original argument has a projection fact; extracted/new values remain unknown.
    std::optional<std::size_t> argument;
};

struct ClauseBindings {
    // Stable local slots include head and body definitions, never definitions from another clause.
    std::vector<BindingDefinition> definitions;
};

struct ServiceResolution {
    // A resolved erlang signature is semantic authorization, independent of runtime builtin registrations.
    FunctionKey identity;
    bool guard_legal;
    bool legacy_test;
    // Missing operations denote legal signatures whose numeric/container/process owner is still deferred.
    std::optional<abi::v1::ImmediateOperation> operation;
    // A body builtin without an inline operation calls this bridge builtin (abi::v1::bridge_builtins index).
    std::optional<std::size_t> builtin = {};

    // Whether this is apply/2,3: a dynamic call, lowered as a transfer rather than an immediate operation.
    [[nodiscard]] bool apply() const { return identity.name == U"apply"; }
};

struct Function {
    // Retain declaration identity beside the immutable AST.
    FunctionKey key;
    ast::FormId form;
    // Only explicitly declared exports permit remote calls.
    bool exported = false;
    // Stable private ABI name is independent of addresses and table order.
    std::string symbol;
    // Whether the executable can reach the function; export liveness (62C) clears it, and codegen then emits no
    // code for it and keeps only the name of an export.
    bool live_ = true;
    // Preserve explicit binding operations and their clause-owned definitions outside syntax.
    std::vector<Binding> bindings = {};
    std::vector<ClauseBindings> clause_bindings = {};
    // Each case or if expression lists the identities bound by all of its clauses; lowering joins them after the case.
    std::map<const ast::Expression *, std::vector<BindingId>> exports = {};
    // Validated head/body patterns retain source identities for later match planning, without enabling execution.
    std::vector<NormalizedPattern> patterns = {};
    // Only semantically resolved service calls may reach lowering; keys borrow immutable owned syntax.
    std::map<const ast::Expression *, ServiceResolution> services = {};
    // Comprehension filters that are guard tests: they reject the element on failure instead of raising.
    std::set<const ast::Expression *> guard_filters = {};
    // Local funs F/A naming auto-imported bridge builtins, found with services; the module makes them erlang:F/A.
    std::map<const ast::Expression *, FunctionKey> builtin_funs = {};
    // The definitions each anonymous fun uses from outside itself (its captured values), in definition order.
    std::map<const ast::Expression *, std::vector<BindingId>> captures = {};
    // The binding each named fun's clauses see for its own name.
    std::map<const ast::Expression *, BindingId> fun_names = {};
    // The definitions fun M:F/A with variables reads for its module, function and arity (none for a literal part).
    std::map<const ast::Expression *, std::array<std::optional<BindingId>, 3>> fun_operands = {};
};

struct RecordLayout {
    // Preserve the source tag independently of runtime atom identities.
    const ast::Atom &name;
    // Distinguish future native representations from ordinary tuple records.
    bool native;
    // Borrow declaration order, source locations and owned default syntax.
    const std::vector<ast::RecordDeclarationField> &fields;
    // Resolve field names without repeatedly scanning wide declarations.
    std::map<std::u32string, std::size_t> positions;
    // Build positions once; duplicate declarations/fields are diagnosed by the module indexer.
    explicit RecordLayout(const ast::RecordDeclaration &declaration);
};

struct FunEntry {
    // An external fun M:F/A finds its code by name at run time; a local fun F/A enters a function of this module.
    bool external = false;
    // The module and function the value names, and its Erlang arity.
    std::u32string module;
    std::u32string function;
    std::size_t arity = 0;
    // A local fun's index among the module's local funs in source order (OTP numbers them differently).
    std::size_t index = 0;
    // The native symbol of a local fun's code; empty for an external fun, and for an anonymous fun whose arguments
    // and captured values exceed 255.
    std::string symbol;
    // An anonymous fun: its syntax, the function it appears in and the definitions it captures, in capture order.
    const ast::Expression *expression = nullptr;
    const Function *owner = nullptr;
    std::vector<BindingId> captures = {};
    // A named fun's own name, read inside its clauses as the fun itself.
    std::optional<BindingId> self = {};
};

struct Module {
    // Borrow the batch-owned immutable AST for the duration of semantic analysis.
    const ast::Module *syntax = nullptr;
    // Preserve physical input identity even when the AST has no forms.
    std::string file;
    // Decoded module identity is established before function indexing.
    std::u32string name;
    std::optional<ast::FormId> declaration;
    // Source-ordered declarations and deterministic lookup live outside syntax.
    std::vector<Function> functions;
    std::map<FunctionKey, std::size_t> lookup;
    // Borrow source-ordered tuple record declarations and their owned default syntax.
    std::map<std::u32string, RecordLayout> records = {};
    // Origin-table identity preserves declaration-before-use across macro and include boundaries.
    std::map<const ast::TokenOrigin *, std::size_t> source_order = {};
    std::map<std::u32string, std::size_t> record_order = {};
    // Native records listed in -export_record, and record names -import_record maps to their modules.
    std::set<std::u32string> exported_records = {};
    std::map<std::u32string, std::u32string> imported_records = {};
    // The functions -import makes callable without their module, mapped to that module.
    std::map<FunctionKey, std::u32string> imports_ = {};
    // The function values this module creates, one entry per distinct fun F/A or fun M:F/A, and the entry each
    // fun expression creates.
    std::vector<FunEntry> funs = {};
    std::map<const ast::Expression *, std::size_t> fun_entries = {};
    // Every module of the compilation batch by name, set once calls are resolved; external records read it.
    std::map<std::u32string, const Module *> peers = {};
    // Escript sources implicitly export main/1, accept -mode and use escript exit semantics.
    bool escript = false;
    // The number of trailing forms the frontend generated: OTP's predefined functions, always exported.
    std::size_t predefined_ = 0;
    // behaviour_info/1 is among them, generated from the -callback declarations.
    bool behaviour_info_ = false;
};

struct IndexOptions {
    // The source began with an escript header.
    bool escript_ = false;
    // The number of trailing forms the frontend generated (OTP's predefined functions).
    std::size_t predefined_ = 0;
};

using Reporter = std::function<void(const Diagnostic &)>;
// Attach logical and physical/macro/include provenance to semantic diagnostics.
void report(const Module &module, const ast::NodeSource *source, std::string message, const Reporter &reporter,
            Severity severity = Severity::error);
// Report at one token, such as an attribute's name, where erl_lint locates attribute diagnostics.
void report(const ast::TokenOrigin &origin, std::string message, const Reporter &reporter,
            Severity severity = Severity::error);
// Index declarations and validate the whole module before any lowering occurs.
std::unique_ptr<Module> index(const ast::Module &syntax, std::string file, const Reporter &reporter,
                              IndexOptions options = {});
// Whether the form is one of OTP's predefined functions the frontend generated.
bool predefined_form(const Module &module, const ast::FormId &id);
// An attribute's name token, where erl_lint locates attribute diagnostics: `export` of -export([...]).
const ast::TokenOrigin &attribute_name(const ast::Module &syntax, const ast::Form &form);
// The options of every -compile attribute in source order, nested lists flattened as compile's options are.
std::vector<ast::TermId> compile_options(const ast::Module &syntax);
// The warning categories -compile(nowarn_Category) turns off; Clause admits no option turning one back on.
std::set<std::u32string> disabled_warnings(const ast::Module &syntax);
// Whether the module's syntax defines the function itself.
bool defines_function(const ast::Module &syntax, const FunctionKey &key);
// The index of a function the module's own source may name; OTP's predefined behaviour_info/1 is not one.
std::optional<std::size_t> source_function(const Module &module, const FunctionKey &key);
// Parse an Erlang declaration arity without narrowing arbitrary precision integers.
std::optional<std::size_t> arity(const Integer &value);
} // namespace clause::semantic
