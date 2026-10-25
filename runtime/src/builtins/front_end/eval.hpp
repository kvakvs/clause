#pragma once
#include "forms.hpp"
#include <exception>
#include <optional>
#include <span>
#include <utility>

// erl_eval's parts in C++ (plan 21 step LIB34C, docs/builtins.md#erl-eval): bindings as erl_eval.erl keeps them, the
// guard expressions patterns and -if conditions evaluate without function handlers, and the bit segment helpers of
// clause_erl_eval's builtins.
namespace clause::runtime::builtins::front_end::eval {
// An evaluation error: a guard expression raised.
struct Failed {};

// A guard expression read a variable the bindings do not have (erl_eval's {unbound, Name}).
struct Unbound {
    Term name;
};

// erl_eval bindings, an orddict or a map, read and extended as erl_eval.erl's binding/2 and add_binding/3 do.
class Bindings final {
  public:
    Bindings(const forms::Builder &build, const Term &term) : build_(&build), term_(term) {}

    const Term &term() const { return term_; }

    std::optional<Term> find(const Term &name) const;
    // add_binding/3: orddict:store/3 keeps the list sorted, a map takes the new association.
    void add(const Term &name, const Term &value);

  private:
    // The builder of the process the bindings live in.
    const forms::Builder *build_;
    // The orddict or map.
    Term term_;
};

// A guard expression's value as erl_eval evaluates it with no function handlers: literals, lists, tuples, maps,
// binaries, operators and guard BIF calls; Failed on any error, Unbound for a variable `bindings` lacks.
class GuardEval final {
  public:
    GuardEval(ProcessContext &context, const Bindings *bindings) : build_(context), bindings_(bindings) {}

    Term value(const Term &node);

  private:
    Term compound(const Term &node, std::string_view tag);
    Term variable(const Term &node) const;
    std::vector<Term> values(const Term &list);
    Term unary(const Term &node);
    Term binary(const Term &node);
    Term short_circuit(const Term &node);
    Term logical(bool conjunction, const Term &left, const Term &right) const;
    Term call(const Term &callee, const std::vector<Term> &args);
    Term guard_call(const Term &name, const std::vector<Term> &args);
    Term is_record(const Term &term, const Term &tag) const;
    Term map_of(std::vector<std::pair<Term, Term>> entries, const Term &fields);
    Term map_update(const Term &map, const Term &fields);
    Term bits(const Term &segments);
    void add_segment(const Term &segment, std::vector<Term> &parts);

    forms::Builder build_;
    // The variables the expression may read; none for -if conditions.
    const Bindings *bindings_;
};

// clause_erl_eval:bit_type/2 of {Size, Types}: {Size1, {Type, Unit, Sign, Endian}}; erl_bits' errors raise.
Term bit_type(ProcessContext &context, std::span<const Term, 2> size_and_types);
// clause_erl_eval:bit_segment/3: the segment's bits, or the atom badarg or system_limit.
Term bit_segment(ProcessContext &context, const Term &value, std::span<const Term, 2> size_and_spec);
// clause_erl_eval:bit_take/3: {Value, Rest} for the segment at the start of the bits, or nomatch.
Term bit_take(ProcessContext &context, const Term &bits, std::span<const Term, 2> size_and_spec);

// Whether `failure` is a guard expression failing: Failed, Unbound or a raising builtin (others propagate).
bool evaluation_failure(const std::exception_ptr &failure);

// A registered builtin by name.
struct Callee {
    std::string_view module;
    std::string_view function;
};

// A registered builtin's result; Failed when it raises.
Term call_registered(ProcessContext &context, const Callee &callee, std::span<const Term> args);
} // namespace clause::runtime::builtins::front_end::eval
