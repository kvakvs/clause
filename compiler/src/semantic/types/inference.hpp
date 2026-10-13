#pragma once
#include "../calls.hpp"
#include "domain.hpp"
#include "profile.hpp"
#include <set>
#include <string>
#include <string_view>
#include <tuple>

namespace clause::semantic::types {
struct Fact {
    // A top-valued parameter may still carry an exact input/result relation.
    Id type;
    std::optional<std::size_t> argument = {};
    // The clauses of a case or if the value depends on (Inference::dependents); `type` is their erased join.
    std::optional<std::size_t> dependent = {};
    auto operator<=>(const Fact &) const = default;
};

struct FunctionType {
    // One possible clause (or branch of a case or if ending it): the arguments' facts after its head and guard, and
    // its result.
    std::vector<Id> inputs;
    Fact result;
    // Whether every argument value within `inputs` that reaches the clause enters it (exact patterns, type tests):
    // a call whose arguments are within them can enter no later function type.
    bool exact = false;
    auto operator<=>(const FunctionType &) const = default;
};

// The construct a dependent fact comes from, printed as its function name: $case_of_operator, ...
enum class Operator : std::uint8_t { case_operator, if_operator, try_of_operator };

// The function name a dependent fact of `construct` prints with.
inline std::string_view operator_name(const Operator construct) {
    switch (construct) {
    case Operator::case_operator:
        return "$case_of_operator";
    case Operator::if_operator:
        return "$if_operator";
    case Operator::try_of_operator:
        return "$try_of_operator";
    }
    return "$operator";
}

// A value that depends on variables narrowed by the clauses of a case, if or try ... of
// (docs/semantic.md#dependent-facts): like a fun applied to them, one function type per possible clause.
struct Dependent {
    Operator construct;
    // The variables of the walked function the clauses narrow, and their names for printing.
    std::vector<BindingId> parameters;
    std::vector<std::string> names;
    // Per clause: the parameters' facts entering it, and its value (never dependent itself).
    std::vector<FunctionType> types;
    auto operator<=>(const Dependent &) const = default;
};

// Function types a function or fun keeps at most; past it the last ones merge into one.
inline constexpr std::size_t FUNCTION_TYPES = 32;

struct Summary {
    // Inputs are term() for exported functions and functions fun F/A names, else their callers' joined arguments.
    std::vector<Id> inputs;
    Fact result;
    // The entry domain: each argument's join over the possible clauses of its fact after the head and guard.
    std::vector<Id> entry = {};
    // The function types of the possible clauses, in source order; none when a budget or widening gave them up.
    std::vector<FunctionType> types = {};
};

struct Inference {
    // Own implementation facts separately from declared contracts and their budgets.
    explicit Inference(Limits limits) : graph(limits) {}

    Graph graph;
    std::map<const Function *, Summary> functions;
    // Borrow stable AST nodes for location-aware inspection without mutating syntax.
    std::map<const ast::Expression *, Fact> expressions;
    // Preserve validated call targets for fresh instantiation at each expression.
    std::map<const ast::Expression *, FunctionRef> callees;
    // The inputs of functions entered only by direct calls of the batch, joined from those calls.
    std::map<const Function *, std::vector<Id>> inputs;
    // The facts fun F/A read in the current pass, by function, to check they were final.
    std::map<const Function *, Id> fun_reads;
    // The members of the recursive component being solved: their domains are not final yet.
    std::set<const Function *> solving;
    // Set for a last pass in which fun F/A has an unknown result, when passes did not settle.
    bool opaque_funs = false;
    // The exported functions of the batch by module, name and arity (UTF-8), which apply(M, F, Args) can call.
    std::map<std::tuple<std::string, std::string, std::size_t>, const Function *> exported;
    // Members of recursive components, whose calls are never re-analysed.
    std::set<const Function *> recursive;
    // Recursive components that reached their round cap or work share in an earlier pass: later passes give their
    // members term() at once instead of iterating again. Added for step 58P.
    std::set<const Component *> failed;
    // How many callee re-analyses for a call are nested now, and the work they spent in the current pass.
    std::size_t reanalyses = 0;
    std::size_t reanalysis_work = 0;
    // Dependent facts by the index facts carry, each kept once.
    std::vector<Dependent> dependents;
    std::map<Dependent, std::size_t> dependent_indices;
    // Where passes and components spend time, when the caller asks; it outlives the inference. Added for
    // --print-stage-times.
    InferenceProfile *profile = nullptr;
};

// Infer supported bodies iteratively; recursive components iterate from bottom to a fixed point and widen
// to top after a bounded number of rounds. Budget exhaustion conservatively loses precision. A `profile` records
// pass and component costs (added for --print-stage-times).
std::unique_ptr<Inference> infer(const CallGraph &calls, Limits limits = {}, InferenceProfile *profile = nullptr);
} // namespace clause::semantic::types
