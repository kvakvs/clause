#pragma once
#include "inference.hpp"
#include <set>

namespace clause::semantic::types {
// What a waiting pattern matches of the value it waits for: all of it, each element of a list, or each key or value
// of a map.
enum class Part : std::uint8_t { whole, elements, keys, values };

struct Waiting {
    ast::ExprId pattern;
    Part part;
};

struct BindingFacts {
    // Borrow one function's semantic identities; each clause has disjoint local slots.
    FunctionRef function;
    Inference &inference;
    // Index syntax events once rather than scanning the complete binding table for every read (kept by the inference
    // across walks since OPTC.D).
    const std::unordered_map<const ast::Expression *, const Binding *> &events;
    // Track the facts of successful definitions, parts of matched values included; unproved values remain top.
    BindingValues values;
    // Identities defined by several clauses of a case, if, receive, try or maybe: their facts after it are the join
    // of the completed clauses' (or dependent on a case's or if's parameters).
    const std::set<BindingId> &shared;

    // Index validated bindings within the same batch inference ceiling.
    BindingFacts(FunctionRef function, Inference &inference, std::size_t &work);
    // Missing facts and nonread events are conservative, never argument-index lookup failures.
    Fact read(const ast::ExprId &id) const;
    // Publish the definitions of a pattern with the facts of the values they match, after its successful match.
    void publish(const ast::ExprId &pattern, Fact fact, std::size_t &work);
    // Register the patterns that match an expression's operand once its value is known: generator patterns wait for
    // their input.
    void expect(const ast::ExprValue &value);
    // Publish the patterns waiting for `expression`, whose value has `fact`.
    void matched(const ast::Expression &expression, Fact fact, std::size_t &work);

    // Register a generator's patterns with its input.
    void expect_generator(const ast::Qualifier &qualifier);

    // Remember an anonymous fun bound whole to a variable, so calls of the variable can evaluate it, and a variable
    // bound to another one's value.
    void bind_lambda(const ast::MatchExpression &match);
    // The anonymous fun bound to the variable a read names, if any.
    std::optional<ast::ExprId> lambda(const ast::ExprId &read) const;

    // The anonymous funs bound to variables, and how many of them are being evaluated for a call.
    std::map<BindingId, ast::ExprId> lambdas;
    std::size_t depth = 0;
    // The argument facts of the anonymous funs being evaluated for a call, which their clauses' patterns match.
    std::map<const ast::Expression *, std::vector<Fact>> instances;
    // Narrow the variable a read names, and the names bound to the same value, to their meet with `fact`.
    void narrow(const ast::ExprId &read, Id fact);
    // Narrow a name and the names bound to the same value to their meet with `fact`, in `facts` (the current values or
    // a copy of them), and the parameters of a dependent one to what that proves, `depth` levels deep at most; the
    // name's new fact.
    Id narrow_identity(BindingValues &facts, BindingId identity, Id fact, std::size_t depth = 0) const;
    // Record that two names are bound to the same value.
    void link(BindingId first, BindingId second);

    // Names bound to the same value as each name.
    std::map<BindingId, std::set<BindingId>> aliases;
    // For each case, if and receive being walked, the join of the facts at the end of each clause that completed.
    std::vector<std::optional<BindingValues>> merged;
    // Each argument's join of its facts at the normal return of every clause: the success domain.
    std::vector<Id> success;

    // Narrowing scopes: the facts saved when a clause, andalso operand, catch, try or comprehension began.
    std::vector<BindingValues> saved;
    // Clause bodies that can never run: their patterns or guard contradict the facts.
    std::set<const std::vector<ast::ExprId> *> impossible;
    // A function's inputs, each argument's fact at the current clause's entry, and the entry domain.
    std::vector<Id> inputs;
    std::vector<Id> entry;
    std::vector<Id> domain;

    // The function clause being walked, its arguments' facts after its head and guard, and whether every value of
    // them enters it.
    std::size_t head = 0;
    std::vector<Id> arguments;
    bool exact = false;
    // The parameters' facts entering each possible clause of a case or if (results still unknown), by construct and
    // clause, and each construct's parameters (docs/semantic.md#dependent-facts).
    std::map<std::pair<const ast::Expression *, std::size_t>, FunctionType> keys;
    std::map<const ast::Expression *, std::vector<BindingId>> parameters;
    // The facts of the variables every clause of a case or if binds, at the end of each clause that completed.
    std::map<std::pair<const ast::Expression *, std::size_t>, std::map<BindingId, Fact>> exits;
    // The function types of the clauses walked so far, in source order.
    std::vector<FunctionType> types;
    // The function types (results still unknown) of each anonymous fun clause, by fun and clause.
    std::map<std::pair<const ast::Expression *, std::size_t>, FunctionType> fun_inputs;
    // The patterns waiting for each expression's value.
    std::map<const ast::Expression *, std::vector<Waiting>> waiting;

    // What a ?= match of a maybe let through and the values it failed on, in the latest walk of the maybe.
    struct Conditional {
        Id matched;
        Id failed;
    };

    // The ?= matches walked, by maybe and position in its body.
    std::map<std::pair<const ast::Expression *, std::size_t>, Conditional> conditionals;
    // Whether a ?= match of a maybe can never succeed, so its body never completes.
    bool stopped(const ast::Expression &maybe) const;
    // The join of the values the ?= matches of a maybe can fail on, up to the first that can never succeed.
    Id failures(const ast::Expression &maybe) const;

  private:
    // Start from the function's kept binding index. Added for walk containers (OPTC.D).
    BindingFacts(FunctionRef function, Inference &inference, const BindingIndex &index, std::size_t &work);
};
} // namespace clause::semantic::types
