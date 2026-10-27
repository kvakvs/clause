#include "epp_dodger.hpp"

// epp_dodger's rewriting of parsed forms as builtins (plan 21 LIB77E4c, docs/builtins.md#epp-dodger): the
// '?preprocessor declaration?' functions back into attributes, and the atoms '? NAME' and '?,NAME' and the tuples
// tagged '? <macro> (' back into erl_syntax macros.
namespace clause::runtime::builtins::epp_dodger {
namespace {
Term rewrite(ProcessContext &context, const Term &node);

Term rewrite_list(ProcessContext &context, const Term &nodes) {
    Items rewritten;
    for (const auto &node : syntax::elements(nodes)) {
        rewritten.push_back(rewrite(context, node));
    }
    return Trees(context).list(rewritten);
}

// erl_syntax:copy_pos(From, To).
Term copy_pos(ProcessContext &context, const Term &from, const Term &to) {
    return syntax::set_pos(context, to, {syntax::get_pos(context, from)});
}

// A macro atom ('? NAME' or '?,NAME') as the macro ?NAME; none for any other atom.
std::optional<Term> macro_of_atom(ProcessContext &context, const Term &node) {
    const auto spelling = syntax::get(context, node, "atom_value").atom_spelling_unchecked();
    const auto variable = spelling.starts_with("?,");
    if (!variable && !spelling.starts_with("? ")) {
        return std::nullopt;
    }
    const auto name = Trees(context).atom_of(spelling.substr(2));
    const auto made = variable ? syntax::variable(context, name) : syntax::atom(context, name);
    const auto macro =
        syntax::make(context, "macro", std::array{copy_pos(context, node, made), Trees(context).atom("none"_atom)});
    return copy_pos(context, node, macro);
}

// A tuple {'? <macro> (', Name, Arguments...} as the macro ?Name(Arguments...); none for any other tuple.
std::optional<Term> macro_of_tuple(ProcessContext &context, const Term &node) {
    const auto elements = syntax::elements(syntax::get(context, node, "tuple_elements"));
    if (elements.size() < 2 || syntax::type_of(context, elements[0]) != "atom" ||
        !is_atom(syntax::get(context, elements[0], "atom_value"), MACRO_CALL)) {
        return std::nullopt;
    }
    Items arguments;
    for (auto at = elements.begin() + 2; at != elements.end(); ++at) {
        arguments.push_back(rewrite(context, *at));
    }
    const auto macro = syntax::make(context, "macro", std::array{elements[1], Trees(context).list(arguments)});
    return copy_pos(context, node, macro);
}

// rewrite_1/1: the subtrees rewritten, in a node of the same type with the same position.
Term rewrite_subtrees(ProcessContext &context, const Term &node) {
    const auto groups = syntax::subtrees(context, node);
    if (groups.is_nil()) {
        return node;
    }
    const Trees trees(context);
    Items rewritten;
    for (const auto &group : syntax::elements(groups)) {
        rewritten.push_back(rewrite_list(context, group));
    }
    const auto made = syntax::make_tree(context, syntax::node_type(context, node), {trees.list(rewritten)});
    return copy_pos(context, node, made);
}

Term rewrite(ProcessContext &context, const Term &node) {
    const auto type = syntax::type_of(context, node);
    std::optional<Term> macro;
    if (type == "atom") {
        return macro_of_atom(context, node).value_or(node);
    }
    if (type == "tuple") {
        macro = macro_of_tuple(context, node);
    }
    return macro ? *macro : rewrite_subtrees(context, node);
}

// The body of a '?preprocessor declaration?'() -> Body function form with one clause; none for any other form.
std::optional<Term> declaration_body(const Term &form) {
    if (!is_token(form, "function", 5) || !is_atom(element(form, 2), PP_FORM)) {
        return std::nullopt;
    }
    const auto clauses = element(form, 4);
    if (!syntax::list_of(clauses, 1)) {
        return std::nullopt;
    }
    const auto clause = clauses.head_unchecked();
    if (!is_token(clause, "clause", 5) || !element(clause, 2).is_nil() || !element(clause, 3).is_nil() ||
        !syntax::list_of(element(clause, 4), 1)) {
        return std::nullopt;
    }
    return element(clause, 4).head_unchecked();
}

// clause_epp_dodger:rewrite_form(Form): a directive's function back into its attribute, any other form rewritten.
Term rewrite_form(ProcessContext &context, const Term &form) {
    const auto body = declaration_body(form);
    if (!body) {
        return rewrite(context, form);
    }
    const auto call = is_token(*body, "call", 4);
    const auto name = call ? element(*body, 2) : *body;
    const auto arguments = call ? rewrite_list(context, element(*body, 3)) : Trees(context).atom("none"_atom);
    const auto attribute = syntax::make(context, "attribute", std::array{name, arguments});
    return syntax::set_pos(context, attribute, {element(form, 1)});
}

constexpr std::array REWRITE{
    typed_entry<rewrite_form>("clause_epp_dodger", "rewrite_form"),
};
} // namespace
} // namespace clause::runtime::builtins::epp_dodger

namespace clause::runtime {
// Publish clause_epp_dodger's form rewriting.
std::span<const BuiltinEntry> epp_dodger_rewrite_builtins() noexcept { return builtins::epp_dodger::REWRITE; }
} // namespace clause::runtime
