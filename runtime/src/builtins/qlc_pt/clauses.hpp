#pragma once
#include "qlc_pt.hpp"
#include <initializer_list>
#include <variant>

// The generated QLC fun's clauses (plan 21 step LIB73, clauses.cpp): abstract code built with one annotation, as
// qlc_pt.erl's ?V, ?A and ?I macros do.
namespace clause::runtime::builtins::qlc_pt {
// Abstract code with one annotation.
class Code final {
  public:
    Code(Transformer &transformer, const Term &anno) : t_(transformer), anno_(anno) {}

    Transformer &t() const { return t_; }

    const Term &anno() const { return anno_; }

    // ?V(Name), ?A(Name), ?I(Value) and {nil, Anno}.
    Term v(const Term &name) const;
    Term a(AtomName name) const;
    Term i(std::int64_t value) const;
    Term nil() const;
    Term call(const Term &callee, std::span<const Term> args) const;
    Term clause(std::span<const Term> head, const Term &guards, std::span<const Term> body) const;
    Term fun(std::span<const Term> clauses) const;
    // closure/2: fun() -> Code end.
    Term closure(const Term &code) const;
    Term cons(const Term &head, const Term &tail) const;
    // abst_vars/2: each name as a variable, the atom nil as {nil, Anno}.
    TermList vars(std::span<const Term> names) const;
    // next/3: element(State, Go); is_list_c/2: is_list(Name).
    Term next(const Term &go, std::int64_t state) const;
    Term is_list(const Term &name) const;
    // split_args/3: arguments beyond the evaluator's limit go into a last tuple; pack_args/3 appends that tuple.
    std::variant<TermList, std::pair<TermList, Term>> split_args(const TermList &args) const;
    TermList pack_args(const TermList &args) const;

  private:
    Transformer &t_;
    Term anno_;
};

// The auxiliary variable names of one QLC and its introduced variables (AllIVs).
struct Lc {
    Term rl;
    Term fun;
    Term go;
    Term ngv;
    Term at;
    Term err;
    TermList ivs;
};

// One qualifier's clause data: generator (pattern, generator variable) or filter (its expression as `pattern`).
struct Qual {
    bool generator = false;
    std::int64_t go = 0;
    std::int64_t state = 0;
    TermList ivs;
    Term gv;
    Term pattern;
    Term anno;
};

// replace/3: every item that is a member of `those` replaced by `with`.
TermList replace(const TermList &those, const Term &with, const TermList &items);
TermList concat(std::initializer_list<TermList> parts);
// Fin ++ Template ++ Cs0 of transform/2's generated fun.
TermList fun_clauses(Transformer &t, const Lc &lc, const Term &expr, std::span<const Qual> quals, const Term &anno);
} // namespace clause::runtime::builtins::qlc_pt
