#pragma once
#include "../memory/detached_term.hpp"
#include <clause/runtime/process_context.hpp>
#include <clause/runtime/terms.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Match specifications (plan 21 step LIB11, docs/runtime.md#match-specifications): a match specification compiled
// into a Program of clauses, each a head pattern, guard expressions and body expressions over numbered variable
// slots, run against objects of ETS tables, terms of the caller's heap or trace argument lists as OTP's db_prog_match
// runs them. A program and its constants belong to no process, so any process may run it and tables may hold it.
namespace clause::runtime::detail::match_spec {
// Which match specifications a program is: ets's (each body gives a result) or the tracing ones of
// erlang:match_spec_test/3 (heads of argument lists, trace actions in bodies).
enum class Dialect : std::uint8_t { table, trace };

// A node of a head pattern; nodes are kept in Program::patterns, children by index.
struct Pattern {
    enum class Kind : std::uint8_t { skip, bind, compare, constant, tuple, cons, map };
    Kind kind = Kind::skip;
    // The variable slot of bind and compare.
    std::size_t slot = 0;
    // The constant word of constant, and the keys of map ([K1, ..., Kn] in a constant), in Program::constants.
    Word word = 0;
    // Tuple elements, cons head and tail, map values in the order of the keys.
    std::vector<std::size_t> children;
};

// The functions a guard or body may call (OTP's guard BIF table), with the immediate service's operation where one
// implements them.
enum class Function : std::uint8_t {
    immediate,
    is_integer3,
    is_record3,
    map_size,
    map_get,
    is_map_key,
    binary_part2,
    binary_part3,
    get_tcw,
    set_tcw,
    set_tcw_fake
};

// The trace actions of trace bodies (DCOMP_TRACE special forms), run as erlang:match_spec_test/3 runs them.
enum class Action : std::uint8_t {
    message,
    return_trace,
    exception_trace,
    display,
    process_dump,
    caller,
    caller_line,
    current_stacktrace,
    is_seq_trace,
    get_seq_token,
    set_seq_token,
    enable_trace,
    disable_trace,
    trace,
    silent
};

// A node of a guard or body expression; nodes are kept in Program::expressions, operands by index.
struct Expression {
    enum class Kind : std::uint8_t {
        constant,
        variable,
        whole,
        bindings,
        tuple,
        cons,
        map,
        call,
        and_all,
        or_all,
        and_also,
        or_else,
        self,
        action
    };
    Kind kind = Kind::constant;
    // The constant word (in Program::constants or an immediate) and the variable slot.
    Word word = 0;
    std::size_t slot = 0;
    // A call's function and its immediate operation; an action.
    Function function = Function::immediate;
    std::uint8_t operation = 0;
    Action action = Action::message;
    // Operands in source order: tuple elements, cons head and tail, map keys then values, call arguments.
    std::vector<std::size_t> operands;
};

// One clause of a match specification.
struct Clause {
    std::size_t head = 0;
    std::vector<std::size_t> guards;
    std::vector<std::size_t> body;
    // The variable slots the head binds, in variable number order ('$$').
    std::vector<std::size_t> bindings;
    // Trace dialect: the head matches the argument list as one term ('$1' or '_'), or a prefix of it (a list head
    // ending in '_').
    bool whole_list = false;
    bool prefix = false;
};

class Program final {
  public:
    Dialect dialect = Dialect::table;
    std::vector<Clause> clauses;
    std::vector<Pattern> patterns;
    std::vector<Expression> expressions;
    // The variable slots of the whole program.
    std::size_t slots = 0;
    // Copies of the constants the patterns and expressions name.
    std::vector<DetachedTerm> constants;
    // The specification's own copy, which the program was compiled from.
    DetachedTerm source;
};

// A compile message: an error or a warning and its text, as erlang:match_spec_test/3 lists them.
struct Message {
    bool warning = false;
    std::string text;
};

// The result of compiling: the program, or none and the messages; system_limit for nesting past the limit.
struct Compiled {
    std::shared_ptr<const Program> program;
    std::vector<Message> messages;
    bool system_limit = false;
};

// Compile `spec` in `dialect`; `lint` collects every message OTP's lint would give instead of stopping at the first.
Compiled compile(ProcessContext &context, const Term &spec, Dialect dialect, bool lint);

// Where a program runs: the object is a stored term outside the caller's heap (an ETS object), so values the body
// gives are copied into the heap, or a term of the caller's heap.
enum class Source : std::uint8_t { stored, heap };

// What a run of a trace program set (erlang:match_spec_test/3's flags).
struct TraceFlags {
    bool return_trace = false;
    bool exception_trace = false;
};

// The result of the first clause that matches `object` (table dialect), or none when no clause matches. A body
// expression that raises gives 'EXIT' in its place.
std::optional<Term> run(ProcessContext &context, const Program &program, Word object, Source source);
// The result of a trace program on an argument list: true or the last message's term, or none when no clause
// matches; `flags` collects the trace flags its actions set.
std::optional<Term> run_trace(ProcessContext &context, const Program &program, const Term &arguments,
                              TraceFlags &flags);

// The compiled match specification a term is (a handle reference made by handle()), or null.
std::shared_ptr<const Program> program_of(const Term &term);
// A handle reference holding a compiled program, as ets:match_spec_compile/1 gives it.
TermResult<Term> handle(ProcessContext &context, std::shared_ptr<const Program> program);
} // namespace clause::runtime::detail::match_spec
