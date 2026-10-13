#pragma once
#include "declarations.hpp"
#include <map>
#include <set>

namespace clause::semantic {
struct FunctionRef {
    // Borrow stable batch-owned declarations; no syntax or LLVM ownership is transferred.
    Module *module;
    Function *function;
    bool operator==(const FunctionRef &) const = default;
};

struct Call {
    // Associate each direct call expression with its resolved declaration.
    ast::ExprId expression;
    FunctionRef caller;
    FunctionRef callee;
};

struct Component {
    // Functions that reach each other through direct calls; recursive when a member can reach itself.
    std::vector<FunctionRef> members;
    bool recursive = false;
};

struct CallGraph {
    // Retain resolution, strongly connected components with callees first, and their flattened order.
    std::vector<Call> calls;
    std::vector<Component> components;
    std::vector<FunctionRef> order;
};

// Resolve only within one compilation batch; recursive components are ordered, never rejected.
CallGraph resolve_calls(std::span<const std::unique_ptr<Module>> modules, const Reporter &out);
// The name a module declares with -module, if any.
std::optional<std::u32string> declared_module(const ast::Module &syntax);
// The modules a module names with a literal atom (M:F(...), fun M:F/A, apply(M, F, Args), -behaviour(M) and
// -import(M, ...)), each with its earliest naming site (sites added for --print-inputs).
std::map<std::u32string, ast::NodeSource> referenced_modules(const ast::Module &syntax);
// The functions a module names in other modules with literal atoms (M:F(...), fun M:F/A, apply(M, F, [...]) and
// -import(M, ...)), by module. Added for ignored modules.
std::map<std::u32string, std::set<FunctionKey>> referenced_functions(const ast::Module &syntax);
// Every atom the module's expressions and patterns spell and every module it names (calls, funs, imports, behaviours,
// native records): the modules whose functions or records its inferred facts can reach. Added for the inference
// cache (58W).
std::set<std::u32string> named_atoms(const ast::Module &syntax);
} // namespace clause::semantic
