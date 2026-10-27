#include "../front_end/front_end.hpp"
#include "../lists/list_walk.hpp"
#include "syntax.hpp"

// erl_syntax's revert/1 and revert_forms/1 (plan 21 LIB77E2): trees back to erl_parse forms, subtrees first, each
// node then by its type; a node that cannot be reverted out of context stays a tree.
namespace clause::runtime::builtins::syntax {
namespace {
// The context every revert function takes, with the position of the node being reverted.
struct Reverting {
    ProcessContext &context;
    Trees trees;
    Term node;
    Term pos;
};

using Revert = Term (*)(const Reverting &at);

// A field of the node being reverted.
Term of(const Reverting &at, FieldName name) { return get(at.context, at.node, name); }

std::string_view type(const Reverting &at, const Term &node) { return type_of(at.context, node); }

// revert_clause/1: the guard tree as erl_parse's list of lists of tests.
Term revert_guard(const Reverting &at, const Term &guard) {
    if (is_none(guard)) {
        return at.trees.nil();
    }
    const auto kind = type(at, guard);
    if (kind == "conjunction") {
        return at.trees.list(std::array{get(at.context, guard, "conjunction_body")});
    }
    if (kind != "disjunction") {
        return at.trees.list(std::array{at.trees.list(std::array{guard})});
    }
    Items alternatives;
    for (const auto &test : elements(get(at.context, guard, "disjunction_body"))) {
        alternatives.push_back(type(at, test) == "conjunction" ? get(at.context, test, "conjunction_body")
                                                               : at.trees.list(std::array{test}));
    }
    return at.trees.list(alternatives);
}

Term revert_clause(ProcessContext &context, const Term &clause) {
    const Reverting at{context, Trees(context), clause, get_pos(context, clause)};
    return at.trees.node("clause"_atom, at.pos, of(at, "clause_patterns"),
                         revert_guard(at, clause_guard(context, clause)), of(at, "clause_body"));
}

// [revert_clause(C) || C <- Clauses].
Term revert_clauses(const Reverting &at, const Term &clauses) {
    Items reverted;
    for (const auto &clause : elements(clauses)) {
        reverted.push_back(revert_clause(at.context, clause));
    }
    return at.trees.list(reverted);
}

// revert_try_clause/1: a handler's pattern as erl_parse's {Class, Reason, Stacktrace} tuple.
Term revert_try_clause(const Reverting &at, const Term &clause) {
    const auto reverted = revert_clause(at.context, clause);
    const auto patterns = element(reverted, 2);
    if (!list_of(patterns, 1)) {
        lists::no_clause();
    }
    const auto pattern = patterns.head_unchecked();
    const auto pos = element(reverted, 1);
    Items parts;
    if (type(at, pattern) == "class_qualifier") {
        parts = {get(at.context, pattern, "class_qualifier_argument"), get(at.context, pattern, "class_qualifier_body"),
                 get(at.context, pattern, "class_qualifier_stacktrace")};
    } else {
        parts = {at.trees.node("atom"_atom, pos, at.trees.atom("throw"_atom)), pattern,
                 at.trees.node("var"_atom, pos, at.trees.atom("_"_atom))};
    }
    const auto tuple = at.trees.node("tuple"_atom, pos, at.trees.list(parts));
    return at.trees.node("clause"_atom, pos, at.trees.list(std::array{tuple}), element(reverted, 3),
                         element(reverted, 4));
}

// {type, Pos, Name, Arguments}.
Term type_node(const Reverting &at, AtomName name, const Term &arguments) {
    return at.trees.node("type"_atom, at.pos, at.trees.atom(name), arguments);
}

Term type_pair(const Reverting &at, AtomName name, FieldName first, FieldName second) {
    return type_node(at, name, at.trees.list(std::array{of(at, first), of(at, second)}));
}

// The revert functions of the node types.
Term revert_annotated_type(const Reverting &at) {
    const std::array parts{of(at, "annotated_type_name"), of(at, "annotated_type_body")};
    return at.trees.node("ann_type"_atom, at.pos, at.trees.list(parts));
}

Term revert_application(const Reverting &at) {
    return at.trees.node("call"_atom, at.pos, of(at, "application_operator"), of(at, "application_arguments"));
}

Term revert_atom(const Reverting &at) { return at.trees.node("atom"_atom, at.pos, of(at, "atom_value")); }

Term revert_binary(const Reverting &at) { return at.trees.node("bin"_atom, at.pos, of(at, "binary_fields")); }

Term revert_binary_comp(const Reverting &at) {
    return at.trees.node("bc"_atom, at.pos, of(at, "binary_comp_template"), of(at, "binary_comp_body"));
}

Term revert_binary_field(const Reverting &at) {
    const auto body = binary_field_body(at.context, at.node);
    const auto sized = type(at, body) == "size_qualifier";
    const auto expression = sized ? get(at.context, body, "size_qualifier_body") : body;
    const auto size = sized ? get(at.context, body, "size_qualifier_argument") : at.trees.atom("default"_atom);
    const auto types = binary_field_types(at.context, at.node);
    const auto folded = types.is_nil() ? at.trees.atom("default"_atom) : fold_binary_field_types(at.context, types);
    return at.trees.node("bin_element"_atom, at.pos, expression, size, folded);
}

Term revert_generator_like(const Reverting &at, AtomName tag, FieldName pattern, FieldName body) {
    return at.trees.node(tag, at.pos, of(at, pattern), of(at, body));
}

Term revert_binary_generator(const Reverting &at) {
    return revert_generator_like(at, "b_generate"_atom, "binary_generator_pattern", "binary_generator_body");
}

Term revert_strict_binary_generator(const Reverting &at) {
    return revert_generator_like(at, "b_generate_strict"_atom, "strict_binary_generator_pattern",
                                 "strict_binary_generator_body");
}

Term revert_generator(const Reverting &at) {
    return revert_generator_like(at, "generate"_atom, "generator_pattern", "generator_body");
}

Term revert_strict_generator(const Reverting &at) {
    return revert_generator_like(at, "generate_strict"_atom, "strict_generator_pattern", "strict_generator_body");
}

Term revert_map_generator(const Reverting &at) {
    return revert_generator_like(at, "m_generate"_atom, "map_generator_pattern", "map_generator_body");
}

Term revert_strict_map_generator(const Reverting &at) {
    return revert_generator_like(at, "m_generate_strict"_atom, "strict_map_generator_pattern",
                                 "strict_map_generator_body");
}

Term revert_bitstring_type(const Reverting &at) {
    return type_pair(at, "binary"_atom, "bitstring_type_m", "bitstring_type_n");
}

Term revert_block_expr(const Reverting &at) { return at.trees.node("block"_atom, at.pos, of(at, "block_expr_body")); }

Term revert_case_expr(const Reverting &at) {
    return at.trees.node("case"_atom, at.pos, of(at, "case_expr_argument"),
                         revert_clauses(at, of(at, "case_expr_clauses")));
}

Term revert_catch_expr(const Reverting &at) { return at.trees.node("catch"_atom, at.pos, of(at, "catch_expr_body")); }

Term revert_char(const Reverting &at) { return at.trees.node("char"_atom, at.pos, of(at, "char_value")); }

Term revert_clause_node(const Reverting &at) { return revert_clause(at.context, at.node); }

Term revert_constrained_function_type(const Reverting &at) {
    const auto constraint =
        get(at.context, constrained_function_type_argument(at.context, at.node), "conjunction_body");
    const std::array parts{of(at, "constrained_function_type_body"), constraint};
    return type_node(at, "bounded_fun"_atom, at.trees.list(parts));
}

Term revert_constraint(const Reverting &at) {
    return type_pair(at, "constraint"_atom, "constraint_argument", "constraint_body");
}

Term revert_else_expr(const Reverting &at) { return at.trees.node("else"_atom, at.pos, of(at, "else_expr_clauses")); }

Term revert_eof_marker(const Reverting &at) { return at.trees.node("eof"_atom, at.pos); }

Term revert_error_marker(const Reverting &at) {
    return at.trees.tuple(std::array{at.trees.atom("error"_atom), of(at, "error_marker_info")});
}

Term revert_warning_marker(const Reverting &at) {
    return at.trees.tuple(std::array{at.trees.atom("warning"_atom), of(at, "warning_marker_info")});
}

Term revert_float(const Reverting &at) { return at.trees.node("float"_atom, at.pos, of(at, "float_value")); }

Term revert_fun_expr(const Reverting &at) {
    const auto clauses = revert_clauses(at, fun_expr_clauses(at.context, at.node));
    return at.trees.node("fun"_atom, at.pos, at.trees.tuple(std::array{at.trees.atom("clauses"_atom), clauses}));
}

Term revert_fun_type(const Reverting &at) { return type_node(at, "fun"_atom, at.trees.nil()); }

Term revert_function(const Reverting &at) {
    const auto name = function_name(at.context, at.node);
    const auto clauses = revert_clauses(at, of(at, "function_clauses"));
    if (type(at, name) != "atom") {
        return at.node;
    }
    return at.trees.node("function"_atom, at.pos, concrete(at.context, name), function_arity(at.context, at.node),
                         clauses);
}

Term revert_function_type(const Reverting &at) {
    const auto arguments = function_type_arguments(at.context, at.node);
    const auto any = is_atom(arguments, "any_arity");
    const auto domain =
        any ? at.trees.node("type"_atom, at.pos, at.trees.atom("any"_atom)) : type_node(at, "product"_atom, arguments);
    return type_node(at, "fun"_atom, at.trees.list(std::array{domain, of(at, "function_type_return")}));
}

Term revert_if_expr(const Reverting &at) {
    return at.trees.node("if"_atom, at.pos, revert_clauses(at, of(at, "if_expr_clauses")));
}

// revert_implicit_fun/1: fun F/A with literal name and arity, fun M:F/A with any parts.
Term revert_implicit_fun(const Reverting &at) {
    const auto name = implicit_fun_name(at.context, at.node);
    const auto kind = type(at, name);
    if (kind == "arity_qualifier") {
        const auto function = get(at.context, name, "arity_qualifier_body");
        const auto arity = get(at.context, name, "arity_qualifier_argument");
        if (type(at, function) != "atom" || type(at, arity) != "integer") {
            return at.node;
        }
        const std::array parts{at.trees.atom("function"_atom), concrete(at.context, function),
                               concrete(at.context, arity)};
        return at.trees.node("fun"_atom, at.pos, at.trees.tuple(parts));
    }
    if (kind != "module_qualifier") {
        return at.node;
    }
    const auto body = get(at.context, name, "module_qualifier_body");
    if (type(at, body) != "arity_qualifier") {
        return at.node;
    }
    const std::array parts{at.trees.atom("function"_atom), get(at.context, name, "module_qualifier_argument"),
                           get(at.context, body, "arity_qualifier_body"),
                           get(at.context, body, "arity_qualifier_argument")};
    return at.trees.node("fun"_atom, at.pos, at.trees.tuple(parts));
}

// The operator of an infix or prefix expression, unless it is no operator node (then the node stays a tree).
std::optional<Term> operator_of(const Reverting &at, const Term &op) {
    return type(at, op) == "operator" ? std::optional{get(at.context, op, "operator_name")} : std::nullopt;
}

Term revert_infix_expr(const Reverting &at) {
    const auto op = operator_of(at, infix_expr_operator(at.context, at.node));
    return op ? at.trees.node("op"_atom, at.pos, *op, of(at, "infix_expr_left"), of(at, "infix_expr_right")) : at.node;
}

Term revert_prefix_expr(const Reverting &at) {
    const auto op = operator_of(at, prefix_expr_operator(at.context, at.node));
    return op ? at.trees.node("op"_atom, at.pos, *op, of(at, "prefix_expr_argument")) : at.node;
}

Term revert_integer(const Reverting &at) { return at.trees.node("integer"_atom, at.pos, of(at, "integer_value")); }

Term revert_integer_range_type(const Reverting &at) {
    return type_pair(at, "range"_atom, "integer_range_type_low", "integer_range_type_high");
}

// {nil, Anno} ending a reverted list: at the list's end location, else at its last element's, else at 0.
Term list_end(const Reverting &at, const Items &prefix) {
    auto end = front_end::anno_end_location(at.context, at.pos);
    if (is_atom(end, "undefined")) {
        end = front_end::anno_end_location(at.context, get_pos(at.context, prefix.back()));
    }
    if (is_atom(end, "undefined")) {
        return at.trees.node("nil"_atom, at.trees.integer(0));
    }
    return at.trees.node("nil"_atom, front_end::anno_set(at.context, "end_location", end, end));
}

Term revert_list(const Reverting &at) {
    const auto prefix = elements(list_prefix(at.context, at.node));
    const auto suffix = list_suffix(at.context, at.node);
    auto result = is_none(suffix) ? list_end(at, prefix) : suffix;
    for (auto head = prefix.rbegin(); head != prefix.rend(); ++head) {
        const auto location = front_end::anno_location(at.context, get_pos(at.context, *head));
        result = at.trees.node("cons"_atom, location, *head, result);
    }
    return front_end::forms::with_element(front_end::forms::Builder(at.context), result, 1, at.pos);
}

Term revert_list_comp(const Reverting &at) {
    return at.trees.node("lc"_atom, at.pos, of(at, "list_comp_template"), of(at, "list_comp_body"));
}

Term revert_map_comp(const Reverting &at) {
    return at.trees.node("mc"_atom, at.pos, of(at, "map_comp_template"), of(at, "map_comp_body"));
}

Term revert_map_expr(const Reverting &at) {
    const auto argument = map_expr_argument(at.context, at.node);
    const auto fields = map_expr_fields(at.context, at.node);
    return is_none(argument) ? at.trees.node("map"_atom, at.pos, fields)
                             : at.trees.node("map"_atom, at.pos, argument, fields);
}

Term revert_map_field_assoc(const Reverting &at) {
    return at.trees.node("map_field_assoc"_atom, at.pos, of(at, "map_field_assoc_name"),
                         of(at, "map_field_assoc_value"));
}

Term revert_map_field_exact(const Reverting &at) {
    return at.trees.node("map_field_exact"_atom, at.pos, of(at, "map_field_exact_name"),
                         of(at, "map_field_exact_value"));
}

// map_type/0 and tuple_type/0's any_size as `any`.
Term sized_type(const Reverting &at, AtomName name, const Term &parts) {
    return type_node(at, name, is_atom(parts, "any_size") ? at.trees.atom("any"_atom) : parts);
}

Term revert_map_type(const Reverting &at) { return sized_type(at, "map"_atom, map_type_fields(at.context, at.node)); }

Term revert_tuple_type(const Reverting &at) {
    return sized_type(at, "tuple"_atom, tuple_type_elements(at.context, at.node));
}

Term revert_map_type_assoc(const Reverting &at) {
    return type_pair(at, "map_field_assoc"_atom, "map_type_assoc_name", "map_type_assoc_value");
}

Term revert_map_type_exact(const Reverting &at) {
    return type_pair(at, "map_field_exact"_atom, "map_type_exact_name", "map_type_exact_value");
}

Term revert_match_expr(const Reverting &at) {
    return at.trees.node("match"_atom, at.pos, of(at, "match_expr_pattern"), of(at, "match_expr_body"));
}

Term revert_maybe_match_expr(const Reverting &at) {
    return at.trees.node("maybe_match"_atom, at.pos, of(at, "maybe_match_expr_pattern"),
                         of(at, "maybe_match_expr_body"));
}

Term revert_maybe_expr(const Reverting &at) {
    const auto body = maybe_expr_body(at.context, at.node);
    const auto otherwise = maybe_expr_else(at.context, at.node);
    return is_none(otherwise) ? at.trees.node("maybe"_atom, at.pos, body)
                              : at.trees.node("maybe"_atom, at.pos, body, otherwise);
}

Term revert_module_qualifier(const Reverting &at) {
    return at.trees.node("remote"_atom, at.pos, of(at, "module_qualifier_argument"), of(at, "module_qualifier_body"));
}

Term revert_named_fun_expr(const Reverting &at) {
    const auto name = named_fun_expr_name(at.context, at.node);
    const auto clauses = revert_clauses(at, of(at, "named_fun_expr_clauses"));
    if (type(at, name) != "variable") {
        return at.node;
    }
    return at.trees.node("named_fun"_atom, at.pos, get(at.context, name, "variable_name"), clauses);
}

Term revert_nil(const Reverting &at) { return at.trees.node("nil"_atom, at.pos); }

Term revert_parentheses(const Reverting &at) { return of(at, "parentheses_body"); }

Term revert_receive_expr(const Reverting &at) {
    const auto clauses = revert_clauses(at, receive_expr_clauses(at.context, at.node));
    const auto timeout = receive_expr_timeout(at.context, at.node);
    const auto action = receive_expr_action(at.context, at.node);
    return is_none(timeout) ? at.trees.node("receive"_atom, at.pos, clauses)
                            : at.trees.node("receive"_atom, at.pos, clauses, timeout, action);
}

// fold_record_field/1: a record_field tree, typed or not, as erl_parse's record_field form.
Term fold_record_field_1(const Reverting &at, const Term &field) {
    const auto pos = get_pos(at.context, field);
    const auto name = get(at.context, field, "record_field_name");
    const auto value = get(at.context, field, "record_field_value");
    return is_none(value) ? at.trees.node("record_field"_atom, pos, name)
                          : at.trees.node("record_field"_atom, pos, name, value);
}

Term fold_record_fields(const Reverting &at, const Term &fields) {
    Items folded;
    for (const auto &field : elements(fields)) {
        const auto kind = type(at, field);
        if (kind == "typed_record_field") {
            const std::array parts{at.trees.atom("typed_record_field"_atom),
                                   fold_record_field_1(at, get(at.context, field, "typed_record_field_body")),
                                   get(at.context, field, "typed_record_field_type")};
            folded.push_back(at.trees.tuple(parts));
        } else if (kind == "record_field") {
            folded.push_back(fold_record_field_1(at, field));
        } else {
            case_clause(at.context, at.trees.atom_of(kind));
        }
    }
    return at.trees.list(folded);
}

// The record name of a record access or expression: [] for '_', {Module, Name} for a qualified name; none when the
// name cannot be reverted.
std::optional<Term> record_name(const Reverting &at, const Term &name) {
    const auto kind = type(at, name);
    if (kind == "atom") {
        const auto value = concrete(at.context, name);
        return is_atom(value, "_") ? at.trees.nil() : value;
    }
    if (kind != "list") {
        return std::nullopt;
    }
    const auto parts = list_elements(at.context, name);
    if (!list_of(parts, 2)) {
        string::case_clause(parts);
    }
    const auto items = elements(parts);
    return at.trees.tuple(std::array{concrete(at.context, items[0]), concrete(at.context, items[1])});
}

Term revert_record_access(const Reverting &at) {
    const auto name = record_name(at, record_access_type(at.context, at.node));
    return name ? at.trees.node("record_field"_atom, at.pos, of(at, "record_access_argument"), *name,
                                of(at, "record_access_field"))
                : at.node;
}

Term revert_record_expr(const Reverting &at) {
    const auto argument = record_expr_argument(at.context, at.node);
    const auto type_name = record_expr_type(at.context, at.node);
    const auto fields = fold_record_fields(at, record_expr_fields(at.context, at.node));
    const auto name = record_name(at, type_name);
    if (!name) {
        return at.node;
    }
    return is_none(argument) ? at.trees.node("record"_atom, at.pos, *name, fields)
                             : at.trees.node("record"_atom, at.pos, argument, *name, fields);
}

Term revert_record_index_expr(const Reverting &at) {
    const auto type_name = record_index_expr_type(at.context, at.node);
    if (type(at, type_name) != "atom") {
        return at.node;
    }
    return at.trees.node("record_index"_atom, at.pos, concrete(at.context, type_name),
                         of(at, "record_index_expr_field"));
}

Term revert_record_type(const Reverting &at) {
    return type_node(
        at, "record"_atom,
        at.trees.list(std::array{record_type_name(at.context, at.node)}, record_type_fields(at.context, at.node)));
}

Term revert_record_type_field(const Reverting &at) {
    return type_pair(at, "field_type"_atom, "record_type_field_name", "record_type_field_type");
}

Term revert_type_application(const Reverting &at) {
    const auto name = type_application_name(at.context, at.node);
    const auto arguments = type_application_arguments(at.context, at.node);
    const auto kind = type(at, name);
    if (kind == "module_qualifier") {
        const std::array parts{get(at.context, name, "module_qualifier_argument"),
                               get(at.context, name, "module_qualifier_body"), arguments};
        return at.trees.node("remote_type"_atom, at.pos, at.trees.list(parts));
    }
    if (kind != "atom") {
        case_clause(at.context, at.trees.atom_of(kind));
    }
    return at.trees.node("type"_atom, at.pos, get(at.context, name, "atom_value"), arguments);
}

Term revert_type_union(const Reverting &at) {
    return type_node(at, "union"_atom, type_union_types(at.context, at.node));
}

Term revert_string(const Reverting &at) { return at.trees.node("string"_atom, at.pos, of(at, "string_value")); }

Term revert_try_expr(const Reverting &at) {
    Items handlers;
    for (const auto &clause : elements(try_expr_handlers(at.context, at.node))) {
        handlers.push_back(revert_try_clause(at, clause));
    }
    return at.trees.node("try"_atom, at.pos, of(at, "try_expr_body"), revert_clauses(at, of(at, "try_expr_clauses")),
                         at.trees.list(handlers), of(at, "try_expr_after"));
}

Term revert_tuple(const Reverting &at) { return at.trees.node("tuple"_atom, at.pos, of(at, "tuple_elements")); }

Term revert_underscore(const Reverting &at) { return at.trees.node("var"_atom, at.pos, at.trees.atom("_"_atom)); }

Term revert_user_type_application(const Reverting &at) {
    const auto name = get(at.context, user_type_application_name(at.context, at.node), "atom_value");
    return at.trees.node("user_type"_atom, at.pos, name, of(at, "user_type_application_arguments"));
}

Term revert_variable(const Reverting &at) { return at.trees.node("var"_atom, at.pos, of(at, "variable_name")); }

Term revert_zip_generator(const Reverting &at) {
    return at.trees.node("zip"_atom, at.pos, of(at, "zip_generator_body"));
}

// A module name of a reverted attribute: the atom, or none when it is no atom node.
std::optional<Term> module_name(const Reverting &at, const Term &name) {
    return type(at, name) == "atom" ? std::optional{concrete(at.context, name)} : std::nullopt;
}

// The elements of a proper list skeleton; none for anything else.
std::optional<Items> skeleton_elements(const Reverting &at, const Term &list) {
    if (!is_list_skeleton(at.context, list) || !is_proper_list(at.context, list)) {
        return std::nullopt;
    }
    return elements(list_elements(at.context, list));
}

// fold_function_names/1: {Name, Arity} of each arity qualifier with literal parts; badmatch otherwise.
Term fold_function_names(const Reverting &at, const Items &names) {
    Items folded;
    for (const auto &name : names) {
        const auto function = get(at.context, name, "arity_qualifier_body");
        const auto arity = get(at.context, name, "arity_qualifier_argument");
        if (type(at, function) != "atom" || type(at, arity) != "integer") {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch, .payload = at.trees.boolean(false).word()};
        }
        folded.push_back(at.trees.tuple(std::array{concrete(at.context, function), concrete(at.context, arity)}));
    }
    return at.trees.list(folded);
}

Term attribute_form(const Reverting &at, const Term &name, const Term &data) {
    return at.trees.node("attribute"_atom, at.pos, name, data);
}

// The module attribute: -module(M) or -module(M, [Variables]) (a non-list of variables keeps the node there).
Term revert_module_attribute(const Reverting &at, const Term &name, const Items &arguments) {
    const auto module = module_name(at, arguments[0]);
    if (arguments.size() == 1) {
        return module ? attribute_form(at, name, *module) : at.node;
    }
    Term variables = at.node;
    if (const auto items = skeleton_elements(at, arguments[1])) {
        Items names;
        for (const auto &variable : *items) {
            names.push_back(get(at.context, variable, "variable_name"));
        }
        variables = at.trees.list(names);
    }
    return module ? attribute_form(at, name, at.trees.tuple(std::array{*module, variables})) : at.node;
}

// -import(M) and -import(M, Functions), -import_record(M, Names).
Term revert_import_attribute(const Reverting &at, const Term &name, const Items &arguments) {
    const auto module = module_name(at, arguments[0]);
    if (arguments.size() == 1) {
        return module ? attribute_form(at, name, *module) : at.node;
    }
    const auto items = module ? skeleton_elements(at, arguments[1]) : std::nullopt;
    if (!items) {
        return at.node;
    }
    Term imported;
    if (is_atom(name, "import")) {
        imported = fold_function_names(at, *items);
    } else {
        Items names;
        for (const auto &item : *items) {
            names.push_back(concrete(at.context, item));
        }
        imported = at.trees.list(names);
    }
    return attribute_form(at, name, at.trees.tuple(std::array{*module, imported}));
}

// -record(Name, {Fields}) and -native_record(Name, {Fields}).
Term revert_record_attribute(const Reverting &at, const Term &name, const Items &arguments) {
    if (type(at, arguments[0]) != "atom" || (is_atom(name, "record") && type(at, arguments[1]) != "tuple")) {
        return at.node;
    }
    const auto fields = fold_record_fields(at, get(at.context, arguments[1], "tuple_elements"));
    return attribute_form(at, name, at.trees.tuple(std::array{concrete(at.context, arguments[0]), fields}));
}

// -export(Functions).
Term revert_export_attribute(const Reverting &at, const Term &name, const Items &arguments) {
    const auto items = skeleton_elements(at, arguments[0]);
    return items ? attribute_form(at, name, fold_function_names(at, *items)) : at.node;
}

// -file(File, Line) with literal parts.
Term revert_file_attribute(const Reverting &at, const Term &name, const Items &arguments) {
    if (type(at, arguments[0]) != "string" || type(at, arguments[1]) != "integer") {
        return at.node;
    }
    const std::array file{concrete(at.context, arguments[0]), concrete(at.context, arguments[1])};
    return attribute_form(at, name, at.trees.tuple(file));
}

// One clause of revert_attribute_1/4: the attribute name, its argument counts and how it reverts.
struct AttributeClause {
    std::string_view name;
    std::size_t fewest;
    std::size_t most;
    Term (*revert)(const Reverting &at, const Term &name, const Items &arguments);
};

constexpr auto ATTRIBUTE_CLAUSES = std::to_array<AttributeClause>({
    {"module", 1, 2, revert_module_attribute},
    {"export", 1, 1, revert_export_attribute},
    {"import", 1, 2, revert_import_attribute},
    {"file", 2, 2, revert_file_attribute},
    {"record", 2, 2, revert_record_attribute},
    {"native_record", 2, 2, revert_record_attribute},
    {"import_record", 2, 2, revert_import_attribute},
});

// revert_attribute_1/4 by attribute name and argument count: a single argument as a concrete term otherwise, and the
// node itself when no clause reverts it.
Term revert_named_attribute(const Reverting &at, const Term &name, const Items &arguments) {
    const auto spelling = name.atom_spelling_unchecked();
    const auto count = arguments.size();
    for (const auto &clause : ATTRIBUTE_CLAUSES) {
        if (clause.name == spelling && count >= clause.fewest && count <= clause.most) {
            return clause.revert(at, name, arguments);
        }
    }
    return count == 1 ? attribute_form(at, name, concrete(at.context, arguments[0])) : at.node;
}

Term revert_attribute(const Reverting &at) {
    const auto name = attribute_name(at.context, at.node);
    const auto arguments = attribute_arguments(at.context, at.node);
    if (type(at, name) != "atom") {
        return at.node;
    }
    const auto value = get(at.context, name, "atom_value");
    if (!arguments.is_list()) {
        return at.node;
    }
    return revert_named_attribute(at, value, elements(arguments));
}

struct Reverter {
    std::string_view type;
    Revert revert;
};

constexpr auto REVERTERS = std::to_array<Reverter>({
    {"annotated_type", revert_annotated_type},
    {"application", revert_application},
    {"atom", revert_atom},
    {"attribute", revert_attribute},
    {"binary", revert_binary},
    {"binary_comp", revert_binary_comp},
    {"binary_field", revert_binary_field},
    {"binary_generator", revert_binary_generator},
    {"bitstring_type", revert_bitstring_type},
    {"block_expr", revert_block_expr},
    {"case_expr", revert_case_expr},
    {"catch_expr", revert_catch_expr},
    {"char", revert_char},
    {"clause", revert_clause_node},
    {"constrained_function_type", revert_constrained_function_type},
    {"constraint", revert_constraint},
    {"else_expr", revert_else_expr},
    {"eof_marker", revert_eof_marker},
    {"error_marker", revert_error_marker},
    {"float", revert_float},
    {"fun_expr", revert_fun_expr},
    {"fun_type", revert_fun_type},
    {"function", revert_function},
    {"function_type", revert_function_type},
    {"generator", revert_generator},
    {"if_expr", revert_if_expr},
    {"implicit_fun", revert_implicit_fun},
    {"infix_expr", revert_infix_expr},
    {"integer", revert_integer},
    {"integer_range_type", revert_integer_range_type},
    {"list", revert_list},
    {"list_comp", revert_list_comp},
    {"map_comp", revert_map_comp},
    {"map_expr", revert_map_expr},
    {"map_field_assoc", revert_map_field_assoc},
    {"map_field_exact", revert_map_field_exact},
    {"map_generator", revert_map_generator},
    {"map_type", revert_map_type},
    {"map_type_assoc", revert_map_type_assoc},
    {"map_type_exact", revert_map_type_exact},
    {"match_expr", revert_match_expr},
    {"maybe_match_expr", revert_maybe_match_expr},
    {"maybe_expr", revert_maybe_expr},
    {"module_qualifier", revert_module_qualifier},
    {"named_fun_expr", revert_named_fun_expr},
    {"nil", revert_nil},
    {"strict_binary_generator", revert_strict_binary_generator},
    {"strict_generator", revert_strict_generator},
    {"strict_map_generator", revert_strict_map_generator},
    {"parentheses", revert_parentheses},
    {"prefix_expr", revert_prefix_expr},
    {"receive_expr", revert_receive_expr},
    {"record_access", revert_record_access},
    {"record_expr", revert_record_expr},
    {"record_index_expr", revert_record_index_expr},
    {"record_type", revert_record_type},
    {"record_type_field", revert_record_type_field},
    {"type_application", revert_type_application},
    {"type_union", revert_type_union},
    {"string", revert_string},
    {"try_expr", revert_try_expr},
    {"tuple", revert_tuple},
    {"tuple_type", revert_tuple_type},
    {"underscore", revert_underscore},
    {"user_type_application", revert_user_type_application},
    {"variable", revert_variable},
    {"warning_marker", revert_warning_marker},
    {"zip_generator", revert_zip_generator},
});

// revert_root/1: the node by its type's revert function; a type with none stays a tree.
Term revert_root(ProcessContext &context, const Term &node) {
    const auto kind = type_of(context, node);
    for (const auto &reverter : REVERTERS) {
        if (reverter.type == kind) {
            return reverter.revert(Reverting{context, Trees(context), node, get_pos(context, node)});
        }
    }
    return node;
}

// Each subtree group of a tree reverted.
Term reverted_groups(ProcessContext &context, const Term &node) {
    const Trees trees(context);
    Items groups;
    for (const auto &group : elements(subtrees(context, node))) {
        Items reverted;
        for (const auto &child : elements(group)) {
            reverted.push_back(revert(context, child));
        }
        groups.push_back(trees.list(reverted));
    }
    return trees.list(groups);
}

// revert_forms_1/1: the forms reverted, comments dropped; error({error, Tree}) at a form that stays a tree.
Term revert_form_list(ProcessContext &context, const Term &forms) {
    const Trees trees(context);
    Items reverted;
    for (const auto &form : elements(forms)) {
        if (type_of(context, form) == "comment") {
            continue;
        }
        const auto plain = revert(context, form);
        if (is_tree(plain)) {
            const std::array reason{trees.atom("error"_atom), plain};
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = trees.tuple(reason).word()};
        }
        reverted.push_back(plain);
    }
    return trees.list(reverted);
}

// flatten_form_list/1's forms: those of nested form lists in place of the lists, in order.
void flatten_forms(ProcessContext &context, const Term &forms, Items &into) {
    for (const auto &form : elements(forms)) {
        if (type_of(context, form) == "form_list") {
            flatten_forms(context, get(context, form, "form_list_elements"), into);
        } else {
            into.push_back(form);
        }
    }
}

Term revert_forms(ProcessContext &context, const Term &forms) {
    const auto list = forms.is_list() ? make(context, "form_list", std::array{forms}) : forms;
    if (type_of(context, list) != "form_list") {
        badarg_of(context, list);
    }
    Items flat;
    flatten_forms(context, get(context, list, "form_list_elements"), flat);
    return revert_form_list(context, Trees(context).list(flat));
}

Term revert1(ProcessContext &context, const Term &node) { return revert(context, node); }

constexpr std::array REVERT_ENTRIES{
    typed_entry<revert1>("erl_syntax", "revert"),
    typed_entry<revert_forms>("erl_syntax", "revert_forms"),
};
} // namespace

Term fold_binary_field_types(ProcessContext &context, const Term &types) {
    const Trees trees(context);
    Items folded;
    for (const auto &item : elements(types)) {
        if (type_of(context, item) == "size_qualifier") {
            const std::array pair{concrete(context, get(context, item, "size_qualifier_body")),
                                  concrete(context, get(context, item, "size_qualifier_argument"))};
            folded.push_back(trees.tuple(pair));
        } else {
            folded.push_back(concrete(context, item));
        }
    }
    return trees.list(folded);
}

Term revert(ProcessContext &context, const Term &node) {
    if (is_wrapper(node)) {
        const auto form = element(node, 3);
        if (is_node(form, "error", 2) || is_node(form, "warning", 2)) {
            return form;
        }
        const auto pos = get_pos(context, node);
        return front_end::forms::with_element(front_end::forms::Builder(context), form, 1, pos);
    }
    if (!is_tree(node)) {
        return node;
    }
    if (is_leaf(context, node)) {
        return revert_root(context, node);
    }
    return revert_root(context, update_tree(context, node, reverted_groups(context, node)));
}
} // namespace clause::runtime::builtins::syntax

namespace clause::runtime {
// Publish erl_syntax's revert functions.
std::span<const BuiltinEntry> erl_syntax_revert_builtins() noexcept { return builtins::syntax::REVERT_ENTRIES; }
} // namespace clause::runtime
