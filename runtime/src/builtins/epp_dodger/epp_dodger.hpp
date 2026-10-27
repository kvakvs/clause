#pragma once
#include "../syntax/syntax.hpp"
#include <deque>

// epp_dodger's rewriting in C++ (plan 21 LIB77E4c, docs/builtins.md#epp-dodger): tokens of a form with directives and
// macros made parseable (clause_epp_dodger:scan/2, quickscan/1) and the parsed form's magic names turned back into
// attributes and macros (rewrite_form/1). Reading, parsing and the fixes of failed parses stay Erlang.
namespace clause::runtime::builtins::epp_dodger {
using front_end::forms::element;
using front_end::forms::is_atom;
using syntax::Items;
using syntax::Trees;

// The name of the function a directive becomes, and the tag of a macro call tuple.
inline constexpr std::string_view PP_FORM = "?preprocessor declaration?";
inline constexpr std::string_view MACRO_CALL = "? <macro> (";

// The tokens still to scan, the next one first.
using Input = std::deque<Term>;

// A token's category (its first element's spelling); empty for anything else.
inline std::string_view category(const Term &token) { return front_end::forms::tag_of(token); }

// Whether a token is a tuple of `size` elements of category `name`.
inline bool is_token(const Term &token, std::string_view name, std::size_t size) {
    return front_end::forms::is_node(token, name, size);
}

// Whether input[at] and input[at + 1] are a macro use: `?` and an atom or variable token.
inline bool is_macro_at(const Input &input, std::size_t at) {
    return at + 1 < input.size() && is_token(input[at], "?", 2) &&
           (is_token(input[at + 1], "atom", 3) || is_token(input[at + 1], "var", 3));
}

// Token building over one process.
class Build final {
  public:
    explicit Build(ProcessContext &context) : trees_(context) {}

    const Trees &trees() const { return trees_; }

    // {Category, Anno} and {Category, Anno, Value}.
    Term token(std::string_view name, const Term &anno) const {
        return trees_.tuple(std::array{trees_.atom_of(name), anno});
    }

    Term token(std::string_view name, const Term &anno, const Term &value) const {
        return trees_.tuple(std::array{trees_.atom_of(name), anno, value});
    }

    Term atom(std::string_view spelling) const { return trees_.atom_of(spelling); }

  private:
    // Builds the tokens.
    Trees trees_;
};

// skip_macro_args/1 of input starting with `(`: the number of tokens up to the matching `)`; throws
// {error, macro_args} when the input ends first.
std::size_t skip_macro_args(ProcessContext &context, const Input &input, std::size_t from);
// The elements of a token list.
Input token_input(const Term &tokens);
} // namespace clause::runtime::builtins::epp_dodger
