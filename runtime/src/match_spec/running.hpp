#pragma once
#include "program.hpp"
#include <optional>
#include <span>
#include <vector>

// Running match specifications (plan 21 step LIB11): one run of a program on one object, as OTP's db_prog_match runs
// it. Shared by the interpreter and the trace actions.
namespace clause::runtime::detail::match_spec {
class Run final {
  public:
    Run(ProcessContext &context, const Program &program, Source source, TraceFlags *flags = nullptr)
        : context_(context), program_(program), source_(source), flags_(flags), values_(program.slots, 0),
          copies_(program.slots, 0) {}

    // The result of the first clause matching `object` (table dialect).
    std::optional<Term> object(Word object);
    // The result of the first clause matching an argument list (trace dialect).
    std::optional<Term> arguments(const Term &list);

    ProcessContext &context() noexcept { return context_; }

    // The value of the expression at `index`: none when it raises in a guard; a body's raising call is 'EXIT'.
    std::optional<Word> evaluate(std::size_t index);
    // Values of the operands of a node in order; none when one raises.
    std::optional<std::vector<Word>> operands(const Expression &node);

    // The trace run's result, set by message, and its flags.
    void set_result(Word result) noexcept { result_ = result; }

    TraceFlags &flags() noexcept { return *flags_; }

    // A term built here, as a word of the caller's heap.
    Word built(const TermResult<Term> &term);

  private:
    // Matching (patterns of heads).
    bool match(std::size_t index, const Term &value);
    bool match_node(const Pattern &node, Word value);
    bool match_tuple(const Pattern &node, Word value);
    bool match_map(const Pattern &node, Word value);
    bool match_arguments(const Clause &clause, std::span<const Word> arguments);

    // A clause's guards, all true; its body's result.
    bool guards_hold(const Clause &clause);
    Word body(const Clause &clause);
    void start(const Clause &clause);

    // Leaves of expressions: constants, variables, '$_', '$$', self().
    Word leaf(const Expression &node);
    std::optional<Word> compound(const Expression &node);
    Word constant(Word word);
    Word variable(std::size_t slot);
    Word whole();
    Word bindings();
    // Containers built from operand values.
    std::optional<Word> construct(const Expression &node);
    std::optional<Word> logic(const Expression &node);
    std::optional<Word> sequence(const Expression &node);
    std::optional<Word> call(const Expression &node);

    // A stored word copied into the caller's heap (every value a body gives is the caller's).
    Word copied(Word word);

    ProcessContext &context_;
    const Program &program_;
    Source source_;
    TraceFlags *flags_;
    // Variable values by slot, and their copies in the caller's heap once a body used them.
    std::vector<Word> values_;
    std::vector<Word> copies_;
    // The clause running, the object (or argument list) and its copy.
    const Clause *clause_ = nullptr;
    Word object_ = 0;
    Word whole_copy_ = 0;
    // Whether guards are being evaluated: raising fails the clause there, and values may stay stored words.
    bool in_guard_ = false;
    // The trace run's result: true, or the last message's term.
    Word result_ = 0;
};

// Run a trace action (actions.cpp): its value, or none when it raises.
std::optional<Word> run_action(Run &run, const Expression &node);
} // namespace clause::runtime::detail::match_spec
