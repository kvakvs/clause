#include "../lists/list_walk.hpp"
#include "../text.hpp"
#include "syntax.hpp"
#include <utility>

// erl_syntax's constructors (plan 21 LIB77E2): the simple ones from the SIMPLE table, and those that normalize their
// arguments or build nested nodes.
namespace clause::runtime::builtins::syntax {
namespace {
// list_to_atom/1 of a character list, as atom/1, variable/1 and operator/1 take a name that is not an atom.
Term name_atom(ProcessContext &context, const Term &name) {
    if (name.is_atom()) {
        return name;
    }
    std::u32string codes;
    for (const auto &code : elements(name)) {
        const auto value = small(code.word());
        if (!value || *value < 0 || *value > 0x10FFFF) {
            bad_argument();
        }
        codes.push_back(char32_t(*value));
    }
    std::string spelling;
    for (const auto code : codes) {
        encode(code, spelling);
    }
    return Trees(context).atom_of(spelling);
}

Term none(ProcessContext &context) { return Trees(context).atom("none"_atom); }

// Whether a term is a proper list of any length.
bool list_of_any(const Term &term) {
    auto rest = term;
    while (rest.is_cons()) {
        rest = rest.tail_unchecked();
    }
    return rest.is_nil();
}
} // namespace

Term build(ProcessContext &context, const Simple &simple, Parts arguments) {
    const Trees trees(context);
    if (!simple.record.empty()) {
        return trees.tree(trees.atom_of(simple.type), trees.record(simple.record, arguments));
    }
    return trees.tree(trees.atom_of(simple.type), arguments.empty() ? trees.nil() : arguments[0]);
}

Term atom(ProcessContext &context, const Term &name) {
    return Trees(context).tree("atom"_atom, name_atom(context, name));
}

Term variable(ProcessContext &context, const Term &name) {
    return Trees(context).tree("variable"_atom, name_atom(context, name));
}

Term operator_node(ProcessContext &context, const Term &name) {
    return Trees(context).tree("operator"_atom, name_atom(context, name));
}

Term list(ProcessContext &context, const Term &elements, const Term &tail) {
    if (elements.is_nil() && is_none(tail)) {
        return make(context, "nil", {});
    }
    if (elements.is_nil()) {
        lists::no_clause();
    }
    const Trees trees(context);
    return trees.tree("list"_atom, trees.record("list"_atom, std::array{elements, tail}));
}

Term clause(ProcessContext &context, Parts patterns_guard_body) {
    const auto &guard = patterns_guard_body[1];
    auto normalized = guard;
    if (guard.is_nil()) {
        normalized = none(context);
    } else if (guard.is_cons() && guard.head_unchecked().is_list()) {
        Items conjunctions;
        for (const auto &tests : lists::proper(guard)) {
            conjunctions.push_back(conjunction(context, tests));
        }
        normalized = make(context, "disjunction", std::array{Trees(context).list(conjunctions)});
    } else if (guard.is_cons()) {
        normalized = conjunction(context, guard);
    }
    const std::array fields{patterns_guard_body[0], normalized, patterns_guard_body[2]};
    const Trees trees(context);
    return trees.tree("clause"_atom, trees.record("clause"_atom, fields));
}

Term conjunction(ProcessContext &context, const Term &tests) { return make(context, "conjunction", std::array{tests}); }

Term record_expr(ProcessContext &context, Parts argument_type_fields) {
    const auto &type = argument_type_fields[1];
    const auto named = list_of(type, 2) ? list(context, type, none(context)) : type;
    const std::array fields{argument_type_fields[0], named, argument_type_fields[2]};
    const Trees trees(context);
    return trees.tree("record_expr"_atom, trees.record("record_expr"_atom, fields));
}

Term receive_expr(ProcessContext &context, Parts clauses_timeout_action) {
    const auto &timeout = clauses_timeout_action[1];
    const auto action = is_none(timeout) ? Trees(context).nil() : clauses_timeout_action[2];
    const std::array fields{clauses_timeout_action[0], timeout, action};
    const Trees trees(context);
    return trees.tree("receive_expr"_atom, trees.record("receive_expr"_atom, fields));
}

Term cons(ProcessContext &context, const Term &head, const Term &tail) {
    const Trees trees(context);
    const auto type = type_of(context, tail);
    if (type == "list") {
        const auto prefix = trees.list(std::array{head}, list_prefix(context, tail));
        return copy_comments(context, tail, {list(context, prefix, list_suffix(context, tail))});
    }
    if (type == "nil") {
        return copy_comments(context, tail, {list(context, trees.list(std::array{head}), none(context))});
    }
    return list(context, trees.list(std::array{head}), tail);
}

namespace {
// The constructors' entry bodies over Parts: each fills in its defaults or normalizes its arguments.
Term atom1(ProcessContext &context, Parts a) { return atom(context, a[0]); }

Term variable1(ProcessContext &context, Parts a) { return variable(context, a[0]); }

Term operator1(ProcessContext &context, Parts a) { return operator_node(context, a[0]); }

Term comment1(ProcessContext &context, Parts a) { return make(context, "comment", std::array{none(context), a[0]}); }

Term map_expr1(ProcessContext &context, Parts a) { return make(context, "map_expr", std::array{none(context), a[0]}); }

Term list1(ProcessContext &context, Parts a) { return list(context, a[0], none(context)); }

Term list2(ProcessContext &context, Parts a) { return list(context, a[0], a[1]); }

Term binary_field1(ProcessContext &context, Parts a) {
    return make(context, "binary_field", std::array{a[0], Trees(context).nil()});
}

Term binary_field3(ProcessContext &context, Parts a) {
    const auto body = is_none(a[1]) ? a[0] : make(context, "size_qualifier", std::array{a[0], a[1]});
    return make(context, "binary_field", std::array{body, a[2]});
}

Term attribute1(ProcessContext &context, Parts a) {
    return make(context, "attribute", std::array{a[0], none(context)});
}

Term clause2(ProcessContext &context, Parts a) { return clause(context, std::array{Trees(context).nil(), a[0], a[1]}); }

Term clause_unguarded(ProcessContext &context, Parts a) {
    return clause(context, std::array{a[0], none(context), a[1]});
}

Term record_field1(ProcessContext &context, Parts a) {
    return make(context, "record_field", std::array{a[0], none(context)});
}

Term record_expr2(ProcessContext &context, Parts a) {
    return record_expr(context, std::array{none(context), a[0], a[1]});
}

// application/3 and type_application/3: a module-qualified name unless the module is none.
template <SimpleName Two> Term qualified3(ProcessContext &context, Parts a) {
    const auto name = is_none(a[0]) ? a[1] : make(context, "module_qualifier", std::array{a[0], a[1]});
    return make(context, Two, std::array{name, a[2]});
}

Term constrained_function_type2(ProcessContext &context, Parts a) {
    const std::array fields{a[0], conjunction(context, a[1])};
    const Trees trees(context);
    return trees.tree("constrained_function_type"_atom, trees.record("constrained_function_type"_atom, fields));
}

Term function_type1(ProcessContext &context, Parts a) {
    return make(context, "function_type", std::array{Trees(context).atom("any_arity"_atom), a[0]});
}

Term map_type0(ProcessContext &context, Parts) {
    return make(context, "map_type", std::array{Trees(context).atom("any_size"_atom)});
}

Term tuple_type0(ProcessContext &context, Parts) {
    return make(context, "tuple_type", std::array{Trees(context).atom("any_size"_atom)});
}

Term maybe_expr1(ProcessContext &context, Parts a) {
    return make(context, "maybe_expr", std::array{a[0], none(context)});
}

Term receive_expr1(ProcessContext &context, Parts a) {
    return receive_expr(context, std::array{a[0], none(context), Trees(context).nil()});
}

Term try_expr2(ProcessContext &context, Parts a) {
    const auto nil = Trees(context).nil();
    return make(context, "try_expr", std::array{a[0], nil, a[1], nil});
}

Term try_expr3(ProcessContext &context, Parts a) {
    return make(context, "try_expr", std::array{a[0], a[1], a[2], Trees(context).nil()});
}

Term try_after_expr2(ProcessContext &context, Parts a) {
    const auto nil = Trees(context).nil();
    return make(context, "try_expr", std::array{a[0], nil, nil, a[1]});
}

Term class_qualifier2(ProcessContext &context, Parts a) {
    const Trees trees(context);
    const auto underscore = trees.node("var"_atom, get_pos(context, a[1]), trees.atom("_"_atom));
    return make(context, "class_qualifier", std::array{a[0], a[1], underscore});
}

Term implicit_fun2(ProcessContext &context, Parts a) {
    const auto name = is_none(a[1]) ? a[0] : make(context, "arity_qualifier", std::array{a[0], a[1]});
    return make(context, "implicit_fun", std::array{name});
}

Term implicit_fun3(ProcessContext &context, Parts a) {
    if (is_none(a[0])) {
        return implicit_fun2(context, a.subspan(1));
    }
    const auto name = make(context, "arity_qualifier", std::array{a[1], a[2]});
    return make(context, "implicit_fun", std::array{make(context, "module_qualifier", std::array{a[0], name})});
}

Term macro1(ProcessContext &context, Parts a) { return make(context, "macro", std::array{a[0], none(context)}); }

Term infix_expr3(ProcessContext &context, Parts a) {
    const std::array fields{a[1], a[0], a[2]};
    const Trees trees(context);
    return trees.tree("infix_expr"_atom, trees.record("infix_expr"_atom, fields));
}

Term cons2(ProcessContext &context, Parts a) { return cons(context, a[0], a[1]); }

Term tree1(ProcessContext &context, Parts a) { return Trees(context).tree(a[0], Trees(context).nil()); }

Term tree2(ProcessContext &context, Parts a) { return Trees(context).tree(a[0], a[1]); }

// A SIMPLE constructor by name, as a Make.
template <SimpleName Name> Term simple(ProcessContext &context, Parts arguments) {
    return build(context, SIMPLE[Name.index], arguments);
}

// One clause of make_tree/2: the type, the shape of the groups ('o' a group [X] passing X, 'p' a group [M, D] passed
// as it is, 'l' any group passed as it is) and the constructor the arguments go to.
struct Assembly {
    std::string_view type;
    std::string_view shape;
    Make make;
};

constexpr auto ASSEMBLIES = std::to_array<Assembly>({
    {"annotated_type", "oo", simple<"annotated_type">},
    {"application", "ol", simple<"application">},
    {"arity_qualifier", "oo", simple<"arity_qualifier">},
    {"attribute", "o", attribute1},
    {"attribute", "ol", simple<"attribute">},
    {"binary", "l", simple<"binary">},
    {"binary_comp", "ol", simple<"binary_comp">},
    {"binary_field", "o", binary_field1},
    {"binary_field", "ol", simple<"binary_field">},
    {"binary_generator", "oo", simple<"binary_generator">},
    {"bitstring_type", "oo", simple<"bitstring_type">},
    {"block_expr", "l", simple<"block_expr">},
    {"case_expr", "ol", simple<"case_expr">},
    {"catch_expr", "o", simple<"catch_expr">},
    {"class_qualifier", "oo", class_qualifier2},
    {"class_qualifier", "ooo", simple<"class_qualifier">},
    {"clause", "ll", clause_unguarded},
    {"clause", "lol", clause},
    {"conjunction", "l", simple<"conjunction">},
    {"constrained_function_type", "ol", constrained_function_type2},
    {"constraint", "ol", simple<"constraint">},
    {"disjunction", "l", simple<"disjunction">},
    {"else_expr", "l", simple<"else_expr">},
    {"form_list", "l", simple<"form_list">},
    {"fun_expr", "l", simple<"fun_expr">},
    {"function", "ol", simple<"function">},
    {"function_type", "o", function_type1},
    {"function_type", "lo", simple<"function_type">},
    {"generator", "oo", simple<"generator">},
    {"if_expr", "l", simple<"if_expr">},
    {"implicit_fun", "o", simple<"implicit_fun">},
    {"infix_expr", "ooo", infix_expr3},
    {"integer_range_type", "oo", simple<"integer_range_type">},
    {"list", "l", list1},
    {"list", "lo", list2},
    {"list_comp", "ol", simple<"list_comp">},
    {"list_comp", "ll", simple<"list_comp">},
    {"macro", "o", macro1},
    {"macro", "ol", simple<"macro">},
    {"map_comp", "ol", simple<"map_comp">},
    {"map_comp", "ll", simple<"map_comp">},
    {"map_expr", "l", map_expr1},
    {"map_expr", "ol", simple<"map_expr">},
    {"map_field_assoc", "oo", simple<"map_field_assoc">},
    {"map_field_exact", "oo", simple<"map_field_exact">},
    {"map_generator", "oo", simple<"map_generator">},
    {"map_type", "l", simple<"map_type">},
    {"map_type_assoc", "oo", simple<"map_type_assoc">},
    {"map_type_exact", "oo", simple<"map_type_exact">},
    {"match_expr", "oo", simple<"match_expr">},
    {"maybe_expr", "l", maybe_expr1},
    {"maybe_expr", "lo", simple<"maybe_expr">},
    {"maybe_match_expr", "oo", simple<"maybe_match_expr">},
    {"named_fun_expr", "ol", simple<"named_fun_expr">},
    {"module_qualifier", "oo", simple<"module_qualifier">},
    {"strict_binary_generator", "oo", simple<"strict_binary_generator">},
    {"strict_generator", "oo", simple<"strict_generator">},
    {"strict_map_generator", "oo", simple<"strict_map_generator">},
    {"parentheses", "o", simple<"parentheses">},
    {"prefix_expr", "oo", simple<"prefix_expr">},
    {"receive_expr", "l", receive_expr1},
    {"receive_expr", "lol", receive_expr},
    {"record_access", "ooo", simple<"record_access">},
    {"record_expr", "ol", record_expr2},
    {"record_expr", "pl", record_expr2},
    {"record_expr", "ool", record_expr},
    {"record_expr", "opl", record_expr},
    {"record_field", "o", record_field1},
    {"record_field", "oo", simple<"record_field">},
    {"record_index_expr", "oo", simple<"record_index_expr">},
    {"record_type", "ol", simple<"record_type">},
    {"record_type_field", "oo", simple<"record_type_field">},
    {"size_qualifier", "oo", simple<"size_qualifier">},
    {"try_expr", "llll", simple<"try_expr">},
    {"tuple", "l", simple<"tuple">},
    {"tuple_type", "l", simple<"tuple_type">},
    {"type_application", "ol", simple<"type_application">},
    {"type_union", "l", simple<"type_union">},
    {"typed_record_field", "oo", simple<"typed_record_field">},
    {"user_type_application", "ol", simple<"user_type_application">},
    {"zip_generator", "l", simple<"zip_generator">},
});

// The constructor arguments of `groups` in `shape`; none when the groups do not have that shape.
std::optional<Items> arguments_of(const Items &groups, std::string_view shape) {
    if (groups.size() != shape.size()) {
        return std::nullopt;
    }
    Items arguments;
    for (std::size_t at = 0; at < shape.size(); ++at) {
        const auto &group = groups[at];
        if (shape[at] == 'l' || (shape[at] == 'p' && list_of(group, 2))) {
            arguments.push_back(group);
        } else if (shape[at] == 'o' && list_of(group, 1)) {
            arguments.push_back(group.head_unchecked());
        } else {
            return std::nullopt;
        }
    }
    return arguments;
}

// The body of the simple constructor SIMPLE[Index].
template <std::size_t Index> Term simple_body(ProcessContext &context, Parts arguments) {
    return build(context, SIMPLE[Index], arguments);
}

template <std::size_t... Index> constexpr auto simple_entries(std::index_sequence<Index...>) {
    return std::array{made_entry<&simple_body<Index>>(SIMPLE[Index].function, SIMPLE[Index].arity)...};
}

constexpr auto SIMPLE_ENTRIES = simple_entries(std::make_index_sequence<SIMPLE.size()>{});

constexpr std::array CONSTRUCTORS{
    made_entry<atom1>("atom", 1),
    made_entry<variable1>("variable", 1),
    made_entry<operator1>("operator", 1),
    made_entry<comment1>("comment", 1),
    made_entry<map_expr1>("map_expr", 1),
    made_entry<list1>("list", 1),
    made_entry<list2>("list", 2),
    made_entry<binary_field1>("binary_field", 1),
    made_entry<binary_field3>("binary_field", 3),
    made_entry<attribute1>("attribute", 1),
    made_entry<clause2>("clause", 2),
    made_entry<clause>("clause", 3),
    made_entry<record_field1>("record_field", 1),
    made_entry<record_expr2>("record_expr", 2),
    made_entry<record_expr>("record_expr", 3),
    made_entry<qualified3<"application">>("application", 3),
    made_entry<qualified3<"type_application">>("type_application", 3),
    made_entry<constrained_function_type2>("constrained_function_type", 2),
    made_entry<function_type1>("function_type", 1),
    made_entry<map_type0>("map_type", 0),
    made_entry<tuple_type0>("tuple_type", 0),
    made_entry<maybe_expr1>("maybe_expr", 1),
    made_entry<receive_expr1>("receive_expr", 1),
    made_entry<receive_expr>("receive_expr", 3),
    made_entry<try_expr2>("try_expr", 2),
    made_entry<try_expr3>("try_expr", 3),
    made_entry<try_after_expr2>("try_after_expr", 2),
    made_entry<class_qualifier2>("class_qualifier", 2),
    made_entry<implicit_fun2>("implicit_fun", 2),
    made_entry<implicit_fun3>("implicit_fun", 3),
    made_entry<macro1>("macro", 1),
    made_entry<infix_expr3>("infix_expr", 3),
    made_entry<cons2>("cons", 2),
    made_entry<tree1>("tree", 1),
    made_entry<tree2>("tree", 2),
};

Term make_tree2(ProcessContext &context, Parts a) { return make_tree(context, a[0], {a[1]}); }

Term update_tree2(ProcessContext &context, Parts a) { return update_tree(context, a[0], a[1]); }

constexpr std::array ASSEMBLY_ENTRIES{
    made_entry<make_tree2>("make_tree", 2),
    made_entry<update_tree2>("update_tree", 2),
};
} // namespace

Term make_tree(ProcessContext &context, const Term &type, const Groups &groups) {
    const auto spelling = type.is_atom() ? type.atom_spelling_unchecked() : std::string_view{};
    const auto proper = list_of_any(groups.term);
    const auto parts = proper ? elements(groups.term) : Items{};
    for (const auto &assembly : ASSEMBLIES) {
        if (assembly.type != spelling || !proper) {
            continue;
        }
        if (const auto arguments = arguments_of(parts, assembly.shape)) {
            return assembly.make(context, *arguments);
        }
    }
    lists::no_clause();
}

Term update_tree(ProcessContext &context, const Term &node, const Term &groups) {
    const auto type = Trees(context).atom_of(type_of(context, node));
    return copy_attrs(context, node, make_tree(context, type, Groups{groups}));
}
} // namespace clause::runtime::builtins::syntax

namespace clause::runtime {
// Publish erl_syntax's constructors.
std::span<const BuiltinEntry> erl_syntax_simple_builtins() noexcept { return builtins::syntax::SIMPLE_ENTRIES; }

std::span<const BuiltinEntry> erl_syntax_constructor_builtins() noexcept { return builtins::syntax::CONSTRUCTORS; }

std::span<const BuiltinEntry> erl_syntax_assembly_builtins() noexcept { return builtins::syntax::ASSEMBLY_ENTRIES; }
} // namespace clause::runtime
