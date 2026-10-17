#pragma once
#include "../lists/list_walk.hpp"
#include <optional>
#include <terms.hpp>
#include <utility>
#include <vector>

// Helpers of the ordered-list collection modules (plan 21 step LIB1, docs/builtins.md#collections): ordsets, orddict
// and proplists are Erlang in OTP, so an argument they do not accept raises function_clause, not badarg.
namespace clause::runtime::builtins::collections {
using lists::no_clause;

// The roles of the collection builtins' arguments (typed.hpp's Named).
struct ElementRole;
struct KeyRole;
struct ValueRole;
struct ListRole;
// A set element or property list entry.
using Element = Named<ElementRole>;
// A dictionary or property key.
using Key = Named<KeyRole>;
// A value to store, append, add or return by default.
using Value = Named<ValueRole>;
// A set, dictionary or list argument.
using List = Named<ListRole>;

// -1, 0 or 1 as `left` is <, == or > `right` in Erlang's term order, numbers by value.
int compare(const Term &left, const Term &right);

// Whether `left` and `right` are the same term (=:=).
bool same(const Term &left, const Term &right);

// `list ++ tail`: badarg unless `list` is a proper list.
Term append(ProcessContext &context, const List &list, const Term &tail);

// `left + right`: badarith unless both are numbers.
Term add(ProcessContext &context, const Term &left, const Term &right);

// The key and value of a two-element tuple; none for anything else.
std::optional<std::pair<Term, Term>> pair_of(const Term &term);

// The first element of a non-empty tuple; none for anything else (OTP's `tuple_size(P) >= 1, element(1, P)`).
std::optional<Term> first_element(const Term &term);

// The elements a walk has passed, put in front of the rest it ends with.
class Prefix final {
  public:
    // Pass one element.
    void push(const Term &element) { elements_.push_back(element); }

    // The passed elements followed by `rest`.
    Term then(ProcessContext &context, const Term &rest) const {
        return need(TermFactory(context).list(elements_, rest));
    }

    // The passed elements as a proper list.
    Term done(ProcessContext &context) const { return need(TermFactory(context).list(elements_)); }

  private:
    // The elements in order.
    std::vector<Term> elements_;
};
} // namespace clause::runtime::builtins::collections
