#pragma once
#include "../calls.hpp"
#include "domain.hpp"
#include "profile.hpp"
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>

namespace clause::semantic::types {
struct Fact {
    // A top-valued parameter may still carry an exact input/result relation.
    Id type;
    std::optional<std::size_t> argument = {};
    // The clauses of a case or if the value depends on (Inference::dependents); `type` is their erased join.
    std::optional<std::size_t> dependent = {};
    auto operator<=>(const Fact &) const = default;
};

// The facts of a walked function's variables, one slot per definition, clause by clause in BindingId order: scopes
// save and restore them as flat copies. Added for walk containers (OPTC.D).
class BindingValues {
  public:
    BindingValues() = default;

    // Slots from the first slot of each clause (and the slot count last), kept by the caller, starting as `facts`.
    BindingValues(const std::vector<std::size_t> &offsets, std::vector<Fact> facts)
        : offsets_(&offsets), facts_(std::move(facts)) {}

    // The fact of a variable of the function; null for an identity it does not define.
    const Fact *find(const BindingId identity) const {
        const auto slot = slot_of(identity);
        return slot ? &facts_[*slot] : nullptr;
    }

    Fact *find(const BindingId identity) {
        const auto slot = slot_of(identity);
        return slot ? &facts_[*slot] : nullptr;
    }

    // The fact of a variable the function defines.
    Fact &at(const BindingId identity) {
        auto *found = find(identity);
        if (!found) {
            throw std::out_of_range("a variable the walked function does not define");
        }
        return *found;
    }

    // Every slot's fact, in BindingId order.
    std::vector<Fact> &slots() { return facts_; }

    const std::vector<Fact> &slots() const { return facts_; }

  private:
    std::optional<std::size_t> slot_of(const BindingId identity) const {
        if (!offsets_ || identity.clause + 1 >= offsets_->size()) {
            return std::nullopt;
        }
        const auto slot = (*offsets_)[identity.clause] + identity.local;
        return slot < (*offsets_)[identity.clause + 1] ? std::optional{slot} : std::nullopt;
    }

    // The first slot of each clause and the slot count, and the facts by slot.
    const std::vector<std::size_t> *offsets_ = nullptr;
    std::vector<Fact> facts_;
};

// What every walk of a function starts from, built at its first walk: its binding events by expression, the names
// several clauses of a construct define, its value slots per clause and their first facts. Added for walk containers
// (OPTC.D).
struct BindingIndex {
    std::unordered_map<const ast::Expression *, const Binding *> events;
    std::set<BindingId> shared;
    std::vector<std::size_t> offsets;
    std::vector<Fact> initial;
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
    bool operator==(const Summary &) const = default;
};

// What a component's walk read of other functions: summaries (by calls, fun F/A and apply), inputs (of callees it
// evaluated again) and the fun F/A facts it noted. Added for incremental passes (CONV.2).
struct WalkReads {
    std::set<const Function *> summaries;
    std::set<const Function *> inputs;
    std::map<const Function *, Id> funs;
};

// A component's last walk: the versions of what it read, its fun F/A facts, the work it spent, whether the pass
// budget cut it short, and how the re-analysis budget answered it: the pass's re-analysis work at its start, how many
// re-analyses it asked for and how many the budget refused. A later pass skips the component while all of it holds.
// Added for incremental passes (CONV.2).
struct ComponentWalk {
    bool walked = false;
    std::vector<std::pair<const Function *, std::size_t>> summaries;
    std::vector<std::pair<const Function *, std::size_t>> inputs;
    std::map<const Function *, Id> funs;
    std::size_t work = 0;
    bool exhausted = false;
    std::size_t reanalysis_start = 0;
    std::size_t reanalysis_work = 0;
    std::size_t reanalysis_checks = 0;
    std::size_t reanalysis_refusals = 0;
};

// A function body's result and the entry domain of its arguments.
struct Body {
    Fact result;
    // The inputs a summary shows (the success domain, or the entry domain of a function that never returns) and the
    // entry domain.
    std::vector<Id> domain;
    std::vector<Id> entry;
    // The function types of its possible clauses, merged within their budget.
    std::vector<FunctionType> types = {};
};

// The last walk of a recursive component's member, kept across solves: what it read outside the component (versions,
// like a component's walk), the members it read with their results and function types then, its body, the expression
// facts it recorded and the lattice joins it ran. A solve takes its body again instead of walking while nothing it
// read changed. Added for member-granular warm solves (CONV.8).
struct MemberWalk {
    ComponentWalk outside;
    std::vector<std::tuple<const Function *, Fact, std::vector<FunctionType>>> members;
    Body body = {};
    std::vector<const ast::Expression *> recorded;
    std::size_t joins = 0;
};

struct Inference {
    // Own implementation facts separately from declared contracts and their budgets.
    explicit Inference(Limits limits) : graph(limits) {}

    Graph graph;
    std::map<const Function *, Summary> functions;
    // Borrow stable AST nodes for location-aware inspection without mutating syntax.
    std::unordered_map<const ast::Expression *, Fact> expressions;
    // Preserve validated call targets for fresh instantiation at each expression.
    std::unordered_map<const ast::Expression *, FunctionRef> callees;
    // The inputs of functions entered only by direct calls of the batch, joined from those calls.
    std::map<const Function *, std::vector<Id>> inputs;
    // How often each of those inputs changed over the passes, which picks how its next change widens. Added for the
    // input widening ladder (CONV.1).
    std::map<const Function *, std::vector<std::size_t>> input_changes;
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
    // The lattice joins each recursive component's solve spent in the current pass, the most joins one pass spent so
    // far, and the components whose solve cost more than a share of that: when their members' local inputs change
    // again, those become term(). Added for cost-aware give-up (CONV.4).
    std::map<const Component *, std::size_t> solve_joins;
    std::size_t pass_joins = 0;
    std::set<const Component *> costly;
    // How many callee re-analyses for a call are nested now, and the work they spent in the current pass.
    std::size_t reanalyses = 0;
    std::size_t reanalysis_work = 0;
    // Dependent facts by the index facts carry, each kept once.
    std::vector<Dependent> dependents;
    std::map<Dependent, std::size_t> dependent_indices;
    // Where passes and components spend time, when the caller asks; it outlives the inference. Added for
    // --print-stage-times.
    InferenceProfile *profile = nullptr;
    // Functions whose results a build cache kept, and the expression facts of their modules: passes keep their
    // summaries, local inputs and facts instead of walking them. Added for the inference cache (58V).
    std::set<const Function *> reused;
    std::unordered_map<const ast::Expression *, Fact> reused_expressions;
    // What the walk of the current component reads; each component's last walk; how often each summary changed; and
    // how often re-analyses asked the pass budget and were refused by it. Added for incremental passes (CONV.2).
    WalkReads *walk_reads = nullptr;
    std::vector<ComponentWalk> walks;
    std::map<const Function *, std::size_t> summary_versions;
    std::size_t reanalysis_checks = 0;
    std::size_t reanalysis_refusals = 0;
    // Each recursive component's member walks by member index, kept across the solves of incremental passes, and the
    // lattice joins of the walks solves took again, which cost-aware give-up still counts. Added for member-granular
    // warm solves (CONV.8).
    std::map<const Component *, std::vector<MemberWalk>> member_walks;
    std::size_t replayed_joins = 0;
    // Each walked function's binding index, built once (elements never move). Added for walk containers (OPTC.D).
    std::unordered_map<const Function *, BindingIndex> binding_indices;
};

// A function's summary, noting the read for the solve of its component (step 58R) and for incremental passes.
inline const Summary &summary_of(const Inference &inference, const Function *function) {
    if (inference.walk_reads) {
        inference.walk_reads->summaries.insert(function);
    }
    return inference.functions.at(function);
}

// Infer supported bodies iteratively; recursive components iterate from bottom to a fixed point and widen
// to top after a bounded number of rounds. Budget exhaustion conservatively loses precision. A `profile` records
// pass and component costs (added for --print-stage-times).
std::unique_ptr<Inference> infer(const CallGraph &calls, Limits limits = {}, InferenceProfile *profile = nullptr);
// Infer into `inference`, keeping the summaries, local inputs and expression facts of the functions it reuses: a
// component whose members are all reused is never walked. Added for the inference cache (58W).
void infer(Inference &inference, const CallGraph &calls);
} // namespace clause::semantic::types
