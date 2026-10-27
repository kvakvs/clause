#include "../collections/collections.hpp"
#include "../lists/list_walk.hpp"
#include "syntax.hpp"
#include <utility>

// erl_syntax's accessors (plan 21 LIB77E2): the simple ones from the FIELDS table, and those reading several
// erl_parse forms or unfolding erl_parse parts into trees.
namespace clause::runtime::builtins::syntax {
namespace {
// Whether a form has a field where `plain` says it does.
bool holds(const Term &node, const Plain &plain) {
    if (plain.size == 0 || !is_node(node, plain.tag, plain.size)) {
        return false;
    }
    if (!plain.name.empty() && !is_atom(element(node, 2), plain.name)) {
        return false;
    }
    return plain.items == 0 || list_of(element(node, plain.element), plain.items);
}

// The field `holds` found.
Term read(const Term &node, const Plain &plain) {
    const auto value = element(node, plain.element);
    return plain.items == 0 ? value : elements(value)[plain.item];
}

// error({badmatch, Term}).
[[noreturn]] void badmatch(ProcessContext &context, const Term &term) {
    static_cast<void>(context);
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch, .payload = term.word()};
}

// The two elements of a 2-tuple; badmatch for anything else ({A, B} = Term).
std::pair<Term, Term> pair_of(ProcessContext &context, const Term &term) {
    if (!term.is_tuple() || term.tuple_size_unchecked() != 2) {
        badmatch(context, term);
    }
    return {element(term, 0), element(term, 1)};
}

// set_pos(atom(Name), Pos).
Term atom_at(ProcessContext &context, const Term &name, const Term &pos) {
    return set_pos(context, atom(context, name), {pos});
}

// list([set_pos(atom(Module), Pos), set_pos(atom(Name), Pos)]) for a {Module, Name} record name.
Term qualified_name(ProcessContext &context, const Term &qualified, const Term &pos) {
    const std::array names{atom_at(context, element(qualified, 0), pos), atom_at(context, element(qualified, 1), pos)};
    return list(context, Trees(context).list(names), Trees(context).atom("none"_atom));
}

// A record name of record_access_type/1 and record_expr_type/1: {Module, Name}, [] for '_' or an atom.
Term record_name(ProcessContext &context, const Term &name, const Term &pos) {
    if (name.is_tuple() && name.tuple_size_unchecked() == 2) {
        return qualified_name(context, name, pos);
    }
    return atom_at(context, name.is_nil() ? Trees(context).atom("_"_atom) : name, pos);
}

// Whether a form is {type, _, Name, Args} for the type name `name`.
bool type_named(const Term &node, std::string_view name) {
    return is_node(node, "type", 4) && is_atom(element(node, 2), name);
}

// set_pos(arity_qualifier(atom(Atom), integer(Arity)), Pos) of each {Atom, Arity}.
Term unfold_function_names(ProcessContext &context, const Term &names, const Pos &at) {
    const auto &pos = at.term;
    Items unfolded;
    for (const auto &name : elements(names)) {
        if (!name.is_tuple() || name.tuple_size_unchecked() != 2) {
            lists::no_clause();
        }
        const std::array parts{atom(context, element(name, 0)), make(context, "integer", std::array{element(name, 1)})};
        unfolded.push_back(set_pos(context, make(context, "arity_qualifier", parts), {pos}));
    }
    return Trees(context).list(unfolded);
}

// Each element of `items` built by `unfold`, as a list tree at `pos`.
template <typename Unfold> Term list_at(ProcessContext &context, const Term &items, const Pos &pos, Unfold unfold) {
    Items unfolded;
    for (const auto &item : elements(items)) {
        unfolded.push_back(unfold(item));
    }
    const Trees trees(context);
    return set_pos(context, list(context, trees.list(unfolded), trees.atom("none"_atom)), pos);
}

// attribute_arguments/1 of the module attribute: [M] or [M, list(Variables)].
Term module_arguments(ProcessContext &context, const Term &data, const Pos &at) {
    const auto &pos = at.term;
    if (!data.is_tuple() || data.tuple_size_unchecked() != 2) {
        return Trees(context).list(std::array{atom_at(context, data, pos)});
    }
    const auto variables = list_at(context, element(data, 1), {pos},
                                   [&](const Term &name) { return set_pos(context, variable(context, name), {pos}); });
    return Trees(context).list(std::array{atom_at(context, element(data, 0), pos), variables});
}

// attribute_arguments/1 of an erl_parse attribute {attribute, Pos, Name, Data}.
Term plain_arguments(ProcessContext &context, const Term &attribute) {
    const auto pos = element(attribute, 1);
    const auto name = element(attribute, 2);
    const auto data = element(attribute, 3);
    const Trees trees(context);
    const auto spelling = name.is_atom() ? name.atom_spelling_unchecked() : std::string_view{};
    if (spelling == "module") {
        return module_arguments(context, data, {pos});
    }
    if (spelling == "export") {
        const auto names = unfold_function_names(context, data, {pos});
        return trees.list(std::array{set_pos(context, list(context, names, trees.atom("none"_atom)), {pos})});
    }
    if (spelling == "import" || spelling == "import_record") {
        const auto [module, imports] = pair_of(context, data);
        const auto unfolded =
            spelling == "import"
                ? set_pos(context,
                          list(context, unfold_function_names(context, imports, {pos}), trees.atom("none"_atom)), {pos})
                : list_at(context, imports, {pos}, [&](const Term &item) { return atom_at(context, item, pos); });
        return trees.list(std::array{atom_at(context, module, pos), unfolded});
    }
    if (spelling == "file") {
        const auto [file, line] = pair_of(context, data);
        return trees.list(std::array{set_pos(context, make(context, "string", std::array{file}), {pos}),
                                     set_pos(context, make(context, "integer", std::array{line}), {pos})});
    }
    if (spelling == "record" || spelling == "native_record") {
        const auto [type, entries] = pair_of(context, data);
        const auto fields = make(context, "tuple", std::array{unfold_record_fields(context, entries)});
        return trees.list(std::array{atom_at(context, type, pos), set_pos(context, fields, {pos})});
    }
    return trees.list(std::array{set_pos(context, abstract(context, data), {pos})});
}

// The {Class, Reason, Stacktrace} parts of a try handler's one tuple pattern; function_clause for another clause.
Items handler_parts(const Term &clause) {
    if (!is_node(clause, "clause", 5) || !list_of(element(clause, 2), 1)) {
        lists::no_clause();
    }
    const auto pattern = element(clause, 2).head_unchecked();
    if (!is_node(pattern, "tuple", 3) || !list_of(element(pattern, 2), 3)) {
        lists::no_clause();
    }
    return elements(element(pattern, 2));
}

// One handler of an erl_parse try: {throw, V, _} as V, {C, V, S} as class_qualifier(C, V, S).
Term unfold_try_clause(ProcessContext &context, const Term &clause) {
    const auto parts = handler_parts(clause);
    const auto thrown = is_node(parts[0], "atom", 3) && is_atom(element(parts[0], 2), "throw") &&
                        is_node(parts[2], "var", 3) && is_atom(element(parts[2], 2), "_");
    const Trees trees(context);
    const auto unfolded = thrown ? parts[1] : make(context, "class_qualifier", parts);
    return trees.tuple(std::array{element(clause, 0), element(clause, 1), trees.list(std::array{unfolded}),
                                  element(clause, 3), element(clause, 4)});
}

// set_pos(size_qualifier(atom(Type), integer(Size)), Pos) for {Type, Size}, set_pos(atom(Type), Pos) otherwise.
Term unfold_binary_field_types(ProcessContext &context, const Term &types, const Pos &at) {
    const auto &pos = at.term;
    Items unfolded;
    for (const auto &type : elements(types)) {
        if (type.is_tuple() && type.tuple_size_unchecked() == 2) {
            const std::array parts{atom(context, element(type, 0)),
                                   make(context, "integer", std::array{element(type, 1)})};
            unfolded.push_back(set_pos(context, make(context, "size_qualifier", parts), {pos}));
        } else {
            unfolded.push_back(atom_at(context, type, pos));
        }
    }
    return Trees(context).list(unfolded);
}

// One record field of an erl_parse record declaration or expression as a record_field tree at its position.
Term unfold_record_field(ProcessContext &context, const Term &field) {
    const auto none = Trees(context).atom("none"_atom);
    if (is_node(field, "record_field", 3) || is_node(field, "record_field", 4)) {
        const auto value = field.tuple_size_unchecked() == 4 ? element(field, 3) : none;
        return set_pos(context, make(context, "record_field", std::array{element(field, 2), value}),
                       {element(field, 1)});
    }
    lists::no_clause();
}

// clause_guard/1 of an erl_parse guard: none, a disjunction of conjunctions, or a conjunction of old-style tests.
Term plain_guard(ProcessContext &context, const Term &guard) {
    if (guard.is_nil()) {
        return Trees(context).atom("none"_atom);
    }
    if (!guard.is_cons()) {
        case_clause(context, guard);
    }
    if (!guard.head_unchecked().is_list()) {
        return conjunction(context, guard);
    }
    Items conjunctions;
    for (const auto &tests : lists::proper(guard)) {
        conjunctions.push_back(conjunction(context, tests));
    }
    return make(context, "disjunction", std::array{Trees(context).list(conjunctions)});
}
} // namespace

Term field(ProcessContext &context, const Term &node, const Field &field) {
    const auto plain = unwrap(node);
    return holds(plain, field.plain) ? read(plain, field.plain)
                                     : record_field(context, plain, field.record, field.field);
}

Term function_name(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "function", 5)) {
        return atom_at(context, element(plain, 2), element(plain, 1));
    }
    return record_field(context, plain, {"func", 3}, 0);
}

Term function_arity(ProcessContext &context, const Term &node) {
    const auto clause = first(get(context, node, "function_clauses"));
    return Trees(context).integer(std::int64_t(length(get(context, clause, "clause_patterns"))));
}

Term clause_guard(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    return is_node(plain, "clause", 5) ? plain_guard(context, element(plain, 3))
                                       : record_field(context, plain, {"clause", 4}, 1);
}

Term infix_expr_operator(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "op", 5)) {
        return set_pos(context, operator_node(context, element(plain, 2)), {element(plain, 1)});
    }
    return record_field(context, plain, {"infix_expr", 4}, 0);
}

Term prefix_expr_operator(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "op", 4)) {
        return set_pos(context, operator_node(context, element(plain, 2)), {element(plain, 1)});
    }
    return record_field(context, plain, {"prefix_expr", 3}, 0);
}

Term record_index_expr_type(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "record_index", 4)) {
        return atom_at(context, element(plain, 2), element(plain, 1));
    }
    return record_field(context, plain, {"record_index_expr", 3}, 0);
}

Term record_access_type(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "record_field", 5)) {
        return record_name(context, element(plain, 3), element(plain, 1));
    }
    return record_field(context, plain, {"record_access", 4}, 1);
}

Term record_expr_argument(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "record", 4)) {
        return Trees(context).atom("none"_atom);
    }
    return is_node(plain, "record", 5) ? element(plain, 2) : record_field(context, plain, {"record_expr", 4}, 0);
}

Term record_expr_type(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "record", 4)) {
        return record_name(context, element(plain, 2), element(plain, 1));
    }
    if (is_node(plain, "record_field", 5)) {
        return atom_at(context, element(plain, 3), element(plain, 1));
    }
    if (is_node(plain, "record", 5)) {
        return record_name(context, element(plain, 3), element(plain, 1));
    }
    return record_field(context, plain, {"record_expr", 4}, 1);
}

Term record_expr_fields(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "record", 4) || is_node(plain, "record", 5)) {
        return unfold_record_fields(context, element(plain, plain.tuple_size_unchecked() - 1));
    }
    return record_field(context, plain, {"record_expr", 4}, 2);
}

Term constrained_function_type_argument(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (type_named(plain, "bounded_fun") && list_of(element(plain, 3), 2)) {
        return conjunction(context, elements(element(plain, 3))[1]);
    }
    return record_field(context, plain, {"constrained_function_type", 3}, 1);
}

Term function_type_arguments(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (type_named(plain, "fun") && list_of(element(plain, 3), 2)) {
        const auto arguments = elements(element(plain, 3))[0];
        if (is_node(arguments, "type", 3) && is_atom(element(arguments, 2), "any")) {
            return Trees(context).atom("any_arity"_atom);
        }
        if (type_named(arguments, "product")) {
            return element(arguments, 3);
        }
    }
    return record_field(context, plain, {"function_type", 3}, 0);
}

Term type_application_name(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "remote_type", 3) && list_of(element(plain, 2), 3)) {
        const auto parts = elements(element(plain, 2));
        return make(context, "module_qualifier", std::array{parts[0], parts[1]});
    }
    if (is_node(plain, "type", 4)) {
        return atom_at(context, element(plain, 2), element(plain, 1));
    }
    return record_field(context, plain, {"type_application", 3}, 0);
}

Term type_application_arguments(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "remote_type", 3) && list_of(element(plain, 2), 3)) {
        return elements(element(plain, 2))[2];
    }
    if (is_node(plain, "type", 4)) {
        return element(plain, 3);
    }
    return record_field(context, plain, {"type_application", 3}, 1);
}

Term record_type_name(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (type_named(plain, "record") && element(plain, 3).is_cons()) {
        return element(plain, 3).head_unchecked();
    }
    return record_field(context, plain, {"record_type", 3}, 0);
}

Term record_type_fields(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (type_named(plain, "record") && element(plain, 3).is_cons()) {
        return element(plain, 3).tail_unchecked();
    }
    return record_field(context, plain, {"record_type", 3}, 1);
}

Term user_type_application_name(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "user_type", 4)) {
        return atom_at(context, element(plain, 2), element(plain, 1));
    }
    return record_field(context, plain, {"user_type_application", 3}, 0);
}

namespace {
// map_type_fields/1 and tuple_type_elements/1: the list of a type form, any_size for `any`.
Term sized_type_parts(ProcessContext &context, const Term &node, std::string_view name) {
    const auto plain = unwrap(node);
    if (type_named(plain, name) && element(plain, 3).is_list()) {
        return element(plain, 3);
    }
    if (type_named(plain, name) && is_atom(element(plain, 3), "any")) {
        return Trees(context).atom("any_size"_atom);
    }
    return data(context, plain);
}
} // namespace

Term map_type_fields(ProcessContext &context, const Term &node) { return sized_type_parts(context, node, "map"); }

Term tuple_type_elements(ProcessContext &context, const Term &node) { return sized_type_parts(context, node, "tuple"); }

Term type_union_types(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    return type_named(plain, "union") && element(plain, 3).is_list() ? element(plain, 3) : data(context, plain);
}

Term map_expr_argument(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "map", 3)) {
        return Trees(context).atom("none"_atom);
    }
    return is_node(plain, "map", 4) ? element(plain, 2) : record_field(context, plain, {"map_expr", 3}, 0);
}

Term map_expr_fields(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "map", 3) || is_node(plain, "map", 4)) {
        return element(plain, plain.tuple_size_unchecked() - 1);
    }
    return record_field(context, plain, {"map_expr", 3}, 1);
}

Term binary_field_body(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (!is_node(plain, "bin_element", 5)) {
        return record_field(context, plain, {"binary_field", 3}, 0);
    }
    const auto size = element(plain, 3);
    return is_atom(size, "default") ? element(plain, 2)
                                    : make(context, "size_qualifier", std::array{element(plain, 2), size});
}

Term binary_field_types(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (!is_node(plain, "bin_element", 5)) {
        return record_field(context, plain, {"binary_field", 3}, 1);
    }
    const auto types = element(plain, 4);
    return is_atom(types, "default") ? Trees(context).nil()
                                     : unfold_binary_field_types(context, types, {element(plain, 1)});
}

namespace {
// binary_field_size/1: the size of a bin_element (none for default), or the argument of a size-qualified body.
Term binary_field_size(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    const auto none = Trees(context).atom("none"_atom);
    if (is_node(plain, "bin_element", 5)) {
        return is_atom(element(plain, 3), "default") ? none : element(plain, 3);
    }
    const auto body = record_field(context, plain, {"binary_field", 3}, 0);
    return type_of(context, body) == "size_qualifier" ? get(context, body, "size_qualifier_argument") : none;
}
} // namespace

Term attribute_name(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "attribute", 4)) {
        return atom_at(context, element(plain, 2), element(plain, 1));
    }
    return record_field(context, plain, {"attribute", 3}, 0);
}

Term attribute_arguments(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "attribute", 4)) {
        return plain_arguments(context, plain);
    }
    return record_field(context, plain, {"attribute", 3}, 1);
}

Term maybe_expr_body(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "maybe", 3) || is_node(plain, "maybe", 4)) {
        return element(plain, 2);
    }
    return record_field(context, plain, {"maybe_expr", 3}, 0);
}

Term maybe_expr_else(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "maybe", 3)) {
        return Trees(context).atom("none"_atom);
    }
    return is_node(plain, "maybe", 4) ? element(plain, 3) : record_field(context, plain, {"maybe_expr", 3}, 1);
}

Term receive_expr_clauses(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "receive", 3) || is_node(plain, "receive", 5)) {
        return element(plain, 2);
    }
    return record_field(context, plain, {"receive_expr", 4}, 0);
}

Term receive_expr_timeout(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "receive", 3)) {
        return Trees(context).atom("none"_atom);
    }
    return is_node(plain, "receive", 5) ? element(plain, 3) : record_field(context, plain, {"receive_expr", 4}, 1);
}

Term receive_expr_action(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "receive", 3)) {
        return Trees(context).nil();
    }
    return is_node(plain, "receive", 5) ? element(plain, 4) : record_field(context, plain, {"receive_expr", 4}, 2);
}

Term try_expr_handlers(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (!is_node(plain, "try", 6)) {
        return record_field(context, plain, {"try_expr", 5}, 2);
    }
    Items handlers;
    for (const auto &clause : elements(element(plain, 4))) {
        handlers.push_back(unfold_try_clause(context, clause));
    }
    return Trees(context).list(handlers);
}

Term implicit_fun_name(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    const auto name = is_node(plain, "fun", 3) ? element(plain, 2) : Term{};
    if (is_node(plain, "fun", 3) && is_node(name, "function", 3)) {
        const auto pos = element(plain, 1);
        const auto arity = set_pos(context, make(context, "integer", std::array{element(name, 2)}), {pos});
        return make(context, "arity_qualifier", std::array{atom_at(context, element(name, 1), pos), arity});
    }
    if (is_node(plain, "fun", 3) && is_node(name, "function", 4)) {
        const auto qualified = make(context, "arity_qualifier", std::array{element(name, 2), element(name, 3)});
        return make(context, "module_qualifier", std::array{element(name, 1), qualified});
    }
    return data(context, plain);
}

Term fun_expr_clauses(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "fun", 3) && is_node(element(plain, 2), "clauses", 2)) {
        return element(element(plain, 2), 1);
    }
    return data(context, plain);
}

Term named_fun_expr_name(ProcessContext &context, const Term &node) {
    const auto plain = unwrap(node);
    if (is_node(plain, "named_fun", 4)) {
        return set_pos(context, variable(context, element(plain, 2)), {element(plain, 1)});
    }
    return record_field(context, plain, {"named_fun_expr", 3}, 0);
}

Term unfold_record_fields(ProcessContext &context, const Term &fields) {
    Items unfolded;
    for (const auto &field : elements(fields)) {
        if (!is_node(field, "typed_record_field", 3)) {
            unfolded.push_back(unfold_record_field(context, field));
            continue;
        }
        const auto body = unfold_record_field(context, element(field, 1));
        const auto typed = make(context, "typed_record_field", std::array{body, element(field, 2)});
        unfolded.push_back(set_pos(context, typed, {get_pos(context, body)}));
    }
    return Trees(context).list(unfolded);
}

namespace {
// The body of the simple accessor FIELDS[Index].
template <std::size_t Index> Term field_body(ProcessContext &context, const Term &node) {
    return field(context, node, FIELDS[Index]);
}

template <std::size_t... Index> constexpr auto field_entries(std::index_sequence<Index...>) {
    return std::array{typed_entry<&field_body<Index>>("erl_syntax", FIELDS[Index].function)...};
}

constexpr auto FIELD_ENTRIES = field_entries(std::make_index_sequence<FIELDS.size()>{});

// The arity functions: length(clause_patterns(hd(Clauses))).
Term fun_expr_arity(ProcessContext &context, const Term &node) {
    const auto clause = first(fun_expr_clauses(context, node));
    return Trees(context).integer(std::int64_t(length(get(context, clause, "clause_patterns"))));
}

Term named_fun_expr_arity(ProcessContext &context, const Term &node) {
    const auto clause = first(get(context, node, "named_fun_expr_clauses"));
    return Trees(context).integer(std::int64_t(length(get(context, clause, "clause_patterns"))));
}

Term tuple_size(ProcessContext &context, const Term &node) {
    return Trees(context).integer(std::int64_t(length(get(context, node, "tuple_elements"))));
}

// is_atom/2, is_char/2, is_integer/2 and is_string/2: whether a leaf of the type (also its erl_parse tag) holds
// exactly the value.
bool holds_value(const Term &node, std::string_view tag, const Term &value) {
    const auto plain = unwrap(node);
    if (is_node(plain, tag, 3)) {
        return collections::same(element(plain, 2), value);
    }
    return is_tree(plain) && is_atom(element(plain, 1), tag) && collections::same(element(plain, 3), value);
}

Term is_atom2(ProcessContext &context, Parts a) { return Trees(context).boolean(holds_value(a[0], "atom", a[1])); }

Term is_char2(ProcessContext &context, Parts a) { return Trees(context).boolean(holds_value(a[0], "char", a[1])); }

Term is_integer2(ProcessContext &context, Parts a) {
    return Trees(context).boolean(holds_value(a[0], "integer", a[1]));
}

Term is_string2(ProcessContext &context, Parts a) { return Trees(context).boolean(holds_value(a[0], "string", a[1])); }

constexpr std::array ACCESSORS{
    made_entry<is_atom2>("is_atom", 2),
    made_entry<is_char2>("is_char", 2),
    made_entry<is_integer2>("is_integer", 2),
    made_entry<is_string2>("is_string", 2),
    typed_entry<function_name>("erl_syntax", "function_name"),
    typed_entry<function_arity>("erl_syntax", "function_arity"),
    typed_entry<clause_guard>("erl_syntax", "clause_guard"),
    typed_entry<infix_expr_operator>("erl_syntax", "infix_expr_operator"),
    typed_entry<prefix_expr_operator>("erl_syntax", "prefix_expr_operator"),
    typed_entry<record_index_expr_type>("erl_syntax", "record_index_expr_type"),
    typed_entry<record_access_type>("erl_syntax", "record_access_type"),
    typed_entry<record_expr_argument>("erl_syntax", "record_expr_argument"),
    typed_entry<record_expr_type>("erl_syntax", "record_expr_type"),
    typed_entry<record_expr_fields>("erl_syntax", "record_expr_fields"),
    typed_entry<constrained_function_type_argument>("erl_syntax", "constrained_function_type_argument"),
    typed_entry<function_type_arguments>("erl_syntax", "function_type_arguments"),
    typed_entry<type_application_name>("erl_syntax", "type_application_name"),
    typed_entry<type_application_arguments>("erl_syntax", "type_application_arguments"),
    typed_entry<record_type_name>("erl_syntax", "record_type_name"),
    typed_entry<record_type_fields>("erl_syntax", "record_type_fields"),
    typed_entry<user_type_application_name>("erl_syntax", "user_type_application_name"),
    typed_entry<map_type_fields>("erl_syntax", "map_type_fields"),
    typed_entry<tuple_type_elements>("erl_syntax", "tuple_type_elements"),
    typed_entry<type_union_types>("erl_syntax", "type_union_types"),
    typed_entry<map_expr_argument>("erl_syntax", "map_expr_argument"),
    typed_entry<map_expr_fields>("erl_syntax", "map_expr_fields"),
    typed_entry<binary_field_body>("erl_syntax", "binary_field_body"),
    typed_entry<binary_field_types>("erl_syntax", "binary_field_types"),
    typed_entry<binary_field_size>("erl_syntax", "binary_field_size"),
    typed_entry<attribute_name>("erl_syntax", "attribute_name"),
    typed_entry<attribute_arguments>("erl_syntax", "attribute_arguments"),
    typed_entry<maybe_expr_body>("erl_syntax", "maybe_expr_body"),
    typed_entry<maybe_expr_else>("erl_syntax", "maybe_expr_else"),
    typed_entry<receive_expr_clauses>("erl_syntax", "receive_expr_clauses"),
    typed_entry<receive_expr_timeout>("erl_syntax", "receive_expr_timeout"),
    typed_entry<receive_expr_action>("erl_syntax", "receive_expr_action"),
    typed_entry<try_expr_handlers>("erl_syntax", "try_expr_handlers"),
    typed_entry<implicit_fun_name>("erl_syntax", "implicit_fun_name"),
    typed_entry<fun_expr_clauses>("erl_syntax", "fun_expr_clauses"),
    typed_entry<fun_expr_arity>("erl_syntax", "fun_expr_arity"),
    typed_entry<named_fun_expr_name>("erl_syntax", "named_fun_expr_name"),
    typed_entry<named_fun_expr_arity>("erl_syntax", "named_fun_expr_arity"),
    typed_entry<tuple_size>("erl_syntax", "tuple_size"),
};
} // namespace
} // namespace clause::runtime::builtins::syntax

namespace clause::runtime {
// Publish erl_syntax's simple accessors and those with their own rules.
std::span<const BuiltinEntry> erl_syntax_field_builtins() noexcept { return builtins::syntax::FIELD_ENTRIES; }

std::span<const BuiltinEntry> erl_syntax_accessor_builtins() noexcept { return builtins::syntax::ACCESSORS; }
} // namespace clause::runtime
