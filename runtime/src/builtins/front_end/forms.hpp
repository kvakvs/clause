#pragma once
#include "../lists/list_walk.hpp"
#include "../string/string.hpp"
#include <array>
#include <initializer_list>
#include <string_view>
#include <vector>

// Reading and building abstract-format nodes for the front-end walks written in C++ (plan 21 step LIB34C):
// erl_expand_records, the guard tests of erl_lint and the modules after them.
namespace clause::runtime::builtins::front_end::forms {
inline bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

inline Term element(const Term &tuple, std::size_t index) { return tuple.tuple_element_unchecked(index); }

// The spelling of a node's tag (its first element when that is an atom), empty for anything else.
inline std::string_view tag_of(const Term &node) {
    if (!node.is_tuple() || node.tuple_size_unchecked() == 0 || !element(node, 0).is_atom()) {
        return {};
    }
    return element(node, 0).atom_spelling_unchecked();
}

// A tuple of `size` elements tagged `tag`.
inline bool is_node(const Term &term, std::string_view tag, std::size_t size) {
    return term.is_tuple() && term.tuple_size_unchecked() == size && is_atom(element(term, 0), tag);
}

inline bool is_one_of(std::string_view name, std::initializer_list<std::string_view> names) {
    for (const auto known : names) {
        if (known == name) {
            return true;
        }
    }
    return false;
}

// The elements of a proper list; function_clause for anything else, as OTP's recursions raise.
inline std::vector<Term> elements(const Term &list) { return lists::proper(list); }

// Node building over one process.
class Builder final {
  public:
    explicit Builder(ProcessContext &context) : terms_(context) {}

    ProcessContext &context() const { return terms_.context(); }

    const string::Terms &terms() const { return terms_; }

    Term atom(AtomName name) const { return terms_.atom(name); }

    Term integer(std::int64_t value) const { return terms_.integer(value); }

    Term nil() const { return terms_.nil(); }

    Term list(std::span<const Term> items) const { return terms_.list(items); }

    Term tuple(std::span<const Term> items) const { return terms_.tuple(items); }

    // {Tag, Anno, Parts...}.
    template <typename... Parts> Term node(AtomName tag, const Term &anno, const Parts &...parts) const {
        const std::array<Term, sizeof...(Parts) + 2> items{atom(tag), anno, parts...};
        return terms_.tuple(items);
    }

    // {atom, Anno, Name}.
    Term atom_node(const Term &anno, AtomName name) const { return node("atom"_atom, anno, atom(name)); }

    Term atom_node(const Term &anno, const Term &name) const { return node("atom"_atom, anno, name); }

    Term integer_node(const Term &anno, std::int64_t value) const { return node("integer"_atom, anno, integer(value)); }

    Term var_node(const Term &anno, AtomName name) const { return node("var"_atom, anno, atom(name)); }

    // {call, Anno, {remote, Anno, {atom, Anno, erlang}, {atom, Anno, Name}}, Args}.
    Term erlang_call(const Term &anno, AtomName name, std::span<const Term> args) const {
        const auto callee = node("remote"_atom, anno, atom_node(anno, "erlang"_atom), atom_node(anno, name));
        return node("call"_atom, anno, callee, list(args));
    }

  private:
    string::Terms terms_;
};

// A copy of `tuple` with element `index` replaced.
inline Term with_element(const Builder &build, const Term &tuple, std::size_t index, const Term &value) {
    std::vector<Term> items;
    items.reserve(tuple.tuple_size_unchecked());
    for (std::size_t at = 0; at < tuple.tuple_size_unchecked(); ++at) {
        items.push_back(at == index ? value : element(tuple, at));
    }
    return build.tuple(items);
}
} // namespace clause::runtime::builtins::front_end::forms
