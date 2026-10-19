#pragma once
#include "../builtins/support.hpp"
#include "program.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

// The compiler of match specifications (plan 21 step LIB11): one clause at a time, heads into patterns and guards
// and bodies into expressions, with OTP's checks and messages (erl_db_util.c's db_match_compile). Shared by the
// pattern and expression translation units.
namespace clause::runtime::detail::match_spec {
// Term access failures while compiling are the running builtin's failures.
using builtins::need;

// The number of a match variable '$N' ('$0', or digits without a leading zero), or none for any other term.
std::optional<std::uint64_t> variable_number(const Term &term);
// Whether a term is the atom `name`.
bool is_atom(const Term &term, std::string_view name);

// Raised when compiling stops: at the first error unless linting, and at nesting past the limit.
struct Stop {};

// A clause as the compiler takes it: the head (a trace head's list already a tuple), guards, body and whether the
// head ends in '_'.
struct ClauseTerms {
    Term head;
    Term guards;
    Term body;
    bool prefix = false;
};

class Compiler final {
  public:
    // Nesting of guard and body expressions, and of heads, past which compiling stops with system_limit.
    static constexpr std::size_t DEPTH_LIMIT = 1000;

    Compiler(ProcessContext &context, Program &program, std::vector<Message> &messages, bool lint)
        : context_(context), program_(program), messages_(messages), lint_(lint) {}

    // Compile one {Head, Guards, Body} (a trace head already as a tuple, or '$N'/'_' for the whole list); false when
    // compiling must stop.
    bool clause(const ClauseTerms &terms);

    bool system_limit() const noexcept { return system_limit_; }

    // Record an error (stopping unless linting); the text is cut as OTP cuts it.
    void error(std::string text);
    // An error whose text names a term, printed as erlang:display/1 prints it.
    void error(std::string_view before, const Term &term, std::string_view after);
    // The text of a term as OTP's %T prints it.
    std::string printed(const Term &term) const;

    ProcessContext &context() noexcept { return context_; }

    Program &program() noexcept { return program_; }

    // A constant's word kept in the program.
    Word keep(const Term &term);

  private:
    // An expression: its constant value when it is one, else the node computing it.
    struct Operand {
        std::optional<Term> constant;
        std::size_t node = 0;
    };

    // The slot of variable number `number`, made on first use.
    std::size_t slot(std::uint64_t number);

    // Heads (patterns.cpp).
    std::size_t pattern(const Term &term, std::size_t depth);
    std::size_t pattern_variable(std::uint64_t number);
    std::size_t pattern_tuple(const Term &term, std::size_t depth);
    std::size_t pattern_list(const Term &term, std::size_t depth);
    std::size_t pattern_map(const Term &term, std::size_t depth);
    std::size_t add(Pattern pattern);

    // Guards and bodies (expressions.cpp).
    void expressions(const Term &list, std::vector<std::size_t> &out);
    Operand expression(const Term &term, std::size_t depth);
    Operand atom_expression(const Term &term);
    Operand tuple_expression(const Term &term, std::size_t depth);
    Operand list_expression(const Term &term, std::size_t depth);
    Operand map_expression(const Term &term, std::size_t depth);
    Operand call(const Term &term, std::size_t depth);
    Operand special_form(const Term &term, std::string_view name);
    Operand logical(const Term &term, std::string_view name, std::size_t depth);
    Operand action(const Term &term, std::string_view name, std::size_t depth);
    bool stacktrace_depth(const Term &term);
    Operand function_call(const Term &term, std::string_view name, std::size_t depth);
    // Operands of a tuple's elements from `first`, compiled last to first as OTP does; their constants when every
    // one is constant.
    std::vector<Operand> operands(const Term &tuple, std::size_t first, std::size_t depth, bool reversed);
    std::size_t node_of(const Operand &operand);
    std::size_t add(Expression expression);
    // Whether a trace special form may be used here; records the error otherwise.
    bool trace_form(std::string_view name, bool in_guard);

    ProcessContext &context_;
    Program &program_;
    std::vector<Message> &messages_;
    bool lint_;
    bool system_limit_ = false;
    // Variable numbers by slot over the whole program; the ones the current clause's head binds.
    std::map<std::uint64_t, std::size_t> slots_;
    std::set<std::uint64_t> bound_;
    // Whether guards are being compiled, and whether the current clause recorded an error.
    bool in_guard_ = false;
    bool failed_ = false;
};
} // namespace clause::runtime::detail::match_spec
