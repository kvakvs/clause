#include "syntax.hpp"

// erl_syntax's node shapes (plan 21 LIB77E2): type/1, the leaf and form tests, subtrees/1 by table and
// flatten_form_list/1.
namespace clause::runtime::builtins::syntax {
namespace {
using Read = Term (*)(ProcessContext &context, const Term &node);

// How subtrees/1 puts an accessor's result in a group: [X], the list itself, or nothing for an absent part.
enum class Kind : std::uint8_t {
    one,
    list,
    one_unless_none,
    list_unless_none,
    list_unless_empty,
    list_unless_any_arity,
    one_or_list
};

struct Group {
    Read read = nullptr;
    Kind kind = Kind::one;
};

// The groups of one node type, in subtrees/1's order.
struct Shape {
    std::string_view type;
    std::array<Group, 4> groups;
};

// A FIELDS accessor as a Read.
template <FieldName Name> Term read(ProcessContext &context, const Term &node) { return get(context, node, Name); }

// The constraints of a constrained function type: the body of its conjunction.
Term constraint_tests(ProcessContext &context, const Term &node) {
    return get(context, constrained_function_type_argument(context, node), "conjunction_body");
}

constexpr auto ONE = Kind::one;
constexpr auto LIST = Kind::list;

constexpr auto SHAPES = std::to_array<Shape>({
    {"annotated_type", {{{read<"annotated_type_name">, ONE}, {read<"annotated_type_body">, ONE}}}},
    {"application", {{{read<"application_operator">, ONE}, {read<"application_arguments">, LIST}}}},
    {"arity_qualifier", {{{read<"arity_qualifier_body">, ONE}, {read<"arity_qualifier_argument">, ONE}}}},
    {"attribute", {{{attribute_name, ONE}, {attribute_arguments, Kind::list_unless_none}}}},
    {"binary", {{{read<"binary_fields">, LIST}}}},
    {"binary_comp", {{{read<"binary_comp_template">, ONE}, {read<"binary_comp_body">, LIST}}}},
    {"binary_field", {{{binary_field_body, ONE}, {binary_field_types, Kind::list_unless_empty}}}},
    {"binary_generator", {{{read<"binary_generator_pattern">, ONE}, {read<"binary_generator_body">, ONE}}}},
    {"bitstring_type", {{{read<"bitstring_type_m">, ONE}, {read<"bitstring_type_n">, ONE}}}},
    {"block_expr", {{{read<"block_expr_body">, LIST}}}},
    {"case_expr", {{{read<"case_expr_argument">, ONE}, {read<"case_expr_clauses">, LIST}}}},
    {"catch_expr", {{{read<"catch_expr_body">, ONE}}}},
    {"class_qualifier",
     {{{read<"class_qualifier_argument">, ONE},
       {read<"class_qualifier_body">, ONE},
       {read<"class_qualifier_stacktrace">, ONE}}}},
    {"clause", {{{read<"clause_patterns">, LIST}, {clause_guard, Kind::one_unless_none}, {read<"clause_body">, LIST}}}},
    {"conjunction", {{{read<"conjunction_body">, LIST}}}},
    {"constrained_function_type", {{{read<"constrained_function_type_body">, ONE}, {constraint_tests, LIST}}}},
    {"constraint", {{{read<"constraint_argument">, ONE}, {read<"constraint_body">, LIST}}}},
    {"disjunction", {{{read<"disjunction_body">, LIST}}}},
    {"else_expr", {{{read<"else_expr_clauses">, LIST}}}},
    {"form_list", {{{read<"form_list_elements">, LIST}}}},
    {"fun_expr", {{{fun_expr_clauses, LIST}}}},
    {"fun_type", {}},
    {"function", {{{function_name, ONE}, {read<"function_clauses">, LIST}}}},
    {"function_type", {{{function_type_arguments, Kind::list_unless_any_arity}, {read<"function_type_return">, ONE}}}},
    {"generator", {{{read<"generator_pattern">, ONE}, {read<"generator_body">, ONE}}}},
    {"if_expr", {{{read<"if_expr_clauses">, LIST}}}},
    {"implicit_fun", {{{implicit_fun_name, ONE}}}},
    {"infix_expr", {{{read<"infix_expr_left">, ONE}, {infix_expr_operator, ONE}, {read<"infix_expr_right">, ONE}}}},
    {"integer_range_type", {{{read<"integer_range_type_low">, ONE}, {read<"integer_range_type_high">, ONE}}}},
    {"list", {{{list_prefix, LIST}, {list_suffix, Kind::one_unless_none}}}},
    {"list_comp", {{{read<"list_comp_template">, Kind::one_or_list}, {read<"list_comp_body">, LIST}}}},
    {"macro", {{{read<"macro_name">, ONE}, {read<"macro_arguments">, Kind::list_unless_none}}}},
    {"map_comp", {{{read<"map_comp_template">, Kind::one_or_list}, {read<"map_comp_body">, LIST}}}},
    {"map_expr", {{{map_expr_argument, Kind::one_unless_none}, {map_expr_fields, LIST}}}},
    {"map_field_assoc", {{{read<"map_field_assoc_name">, ONE}, {read<"map_field_assoc_value">, ONE}}}},
    {"map_field_exact", {{{read<"map_field_exact_name">, ONE}, {read<"map_field_exact_value">, ONE}}}},
    {"map_generator", {{{read<"map_generator_pattern">, ONE}, {read<"map_generator_body">, ONE}}}},
    {"map_type", {{{map_type_fields, LIST}}}},
    {"map_type_assoc", {{{read<"map_type_assoc_name">, ONE}, {read<"map_type_assoc_value">, ONE}}}},
    {"map_type_exact", {{{read<"map_type_exact_name">, ONE}, {read<"map_type_exact_value">, ONE}}}},
    {"match_expr", {{{read<"match_expr_pattern">, ONE}, {read<"match_expr_body">, ONE}}}},
    {"maybe_expr", {{{maybe_expr_body, LIST}, {maybe_expr_else, Kind::one_unless_none}}}},
    {"maybe_match_expr", {{{read<"maybe_match_expr_pattern">, ONE}, {read<"maybe_match_expr_body">, ONE}}}},
    {"module_qualifier", {{{read<"module_qualifier_argument">, ONE}, {read<"module_qualifier_body">, ONE}}}},
    {"named_fun_expr", {{{named_fun_expr_name, ONE}, {read<"named_fun_expr_clauses">, LIST}}}},
    {"strict_binary_generator",
     {{{read<"strict_binary_generator_pattern">, ONE}, {read<"strict_binary_generator_body">, ONE}}}},
    {"strict_generator", {{{read<"strict_generator_pattern">, ONE}, {read<"strict_generator_body">, ONE}}}},
    {"strict_map_generator", {{{read<"strict_map_generator_pattern">, ONE}, {read<"strict_map_generator_body">, ONE}}}},
    {"parentheses", {{{read<"parentheses_body">, ONE}}}},
    {"prefix_expr", {{{prefix_expr_operator, ONE}, {read<"prefix_expr_argument">, ONE}}}},
    {"record_access",
     {{{read<"record_access_argument">, ONE}, {record_access_type, ONE}, {read<"record_access_field">, ONE}}}},
    {"record_field", {{{read<"record_field_name">, ONE}, {read<"record_field_value">, Kind::one_unless_none}}}},
    {"record_index_expr", {{{record_index_expr_type, ONE}, {read<"record_index_expr_field">, ONE}}}},
    {"record_type", {{{record_type_name, ONE}, {record_type_fields, LIST}}}},
    {"record_type_field", {{{read<"record_type_field_name">, ONE}, {read<"record_type_field_type">, ONE}}}},
    {"size_qualifier", {{{read<"size_qualifier_body">, ONE}, {read<"size_qualifier_argument">, ONE}}}},
    {"try_expr",
     {{{read<"try_expr_body">, LIST},
       {read<"try_expr_clauses">, LIST},
       {try_expr_handlers, LIST},
       {read<"try_expr_after">, LIST}}}},
    {"tuple", {{{read<"tuple_elements">, LIST}}}},
    {"tuple_type", {{{tuple_type_elements, LIST}}}},
    {"type_application", {{{type_application_name, ONE}, {type_application_arguments, LIST}}}},
    {"type_union", {{{type_union_types, LIST}}}},
    {"typed_record_field", {{{read<"typed_record_field_body">, ONE}, {read<"typed_record_field_type">, ONE}}}},
    {"user_type_application", {{{user_type_application_name, ONE}, {read<"user_type_application_arguments">, LIST}}}},
    {"zip_generator", {{{read<"zip_generator_body">, LIST}}}},
});

// Whether an optional part is absent, so subtrees/1 leaves its group out.
bool absent(Kind kind, const Term &value) {
    switch (kind) {
    case Kind::one_unless_none:
    case Kind::list_unless_none:
        return is_none(value);
    case Kind::list_unless_empty:
        return value.is_nil();
    case Kind::list_unless_any_arity:
        return is_atom(value, "any_arity");
    default:
        return false;
    }
}

// Whether a part is one subtree, put in a group of its own.
bool single(Kind kind, const Term &value) {
    return kind == Kind::one || kind == Kind::one_unless_none || (kind == Kind::one_or_list && !value.is_list());
}

// One group of subtrees/1, or none for an absent optional part.
std::optional<Term> group_of(const Trees &trees, const Group &group, const Term &value) {
    if (absent(group.kind, value)) {
        return std::nullopt;
    }
    return single(group.kind, value) ? trees.list(std::array{value}) : value;
}

// The groups of a node by the accessors of its shape.
Items shape_groups(ProcessContext &context, const Term &node, const Shape &shape) {
    const Trees trees(context);
    Items groups;
    for (const auto &group : shape.groups) {
        if (group.read == nullptr) {
            break;
        }
        if (const auto made = group_of(trees, group, group.read(context, node))) {
            groups.push_back(*made);
        }
    }
    return groups;
}

// receive_expr's groups: [Clauses] without a timeout, else [Clauses, [Timeout], Action].
Items receive_groups(ProcessContext &context, const Term &node) {
    const Trees trees(context);
    const auto timeout = receive_expr_timeout(context, node);
    if (is_none(timeout)) {
        return {receive_expr_clauses(context, node)};
    }
    return {receive_expr_clauses(context, node), trees.list(std::array{timeout}), receive_expr_action(context, node)};
}

// record_expr's groups: [Argument] unless none, the type (both names of a qualified one) and the fields.
Items record_groups(ProcessContext &context, const Term &node) {
    const Trees trees(context);
    const auto type = record_expr_type(context, node);
    const auto names = type_of(context, type) == "list" ? list_elements(context, type) : trees.list(std::array{type});
    const auto argument = record_expr_argument(context, node);
    if (is_none(argument)) {
        return {names, record_expr_fields(context, node)};
    }
    return {trees.list(std::array{argument}), names, record_expr_fields(context, node)};
}

// The groups of a node by its type's Shape; error({case_clause, Type}) for a type subtrees/1 does not know.
Items groups_of(ProcessContext &context, const Term &node, std::string_view type) {
    if (type == "receive_expr") {
        return receive_groups(context, node);
    }
    if (type == "record_expr") {
        return record_groups(context, node);
    }
    const auto shape = std::ranges::find(SHAPES, type, &Shape::type);
    if (shape != SHAPES.end()) {
        return shape_groups(context, node, *shape);
    }
    case_clause(context, is_tree(node) || is_wrapper(node) ? element(node, 1) : Trees(context).atom_of(type));
}

// The leaf types (always leaves).
constexpr auto LEAVES = std::to_array<std::string_view>({"atom", "char", "comment", "eof_marker", "error_marker",
                                                         "float", "fun_type", "integer", "nil", "operator", "string",
                                                         "text", "underscore", "variable", "warning_marker"});

constexpr auto FORM_TYPES = std::to_array<std::string_view>(
    {"attribute", "comment", "function", "eof_marker", "error_marker", "form_list", "warning_marker", "text"});

bool one_of(std::string_view name, std::span<const std::string_view> names) {
    return std::ranges::find(names, name) != names.end();
}

Term type1(ProcessContext &context, const Term &node) {
    if (is_tree(node) || is_wrapper(node)) {
        return element(node, 1);
    }
    return Trees(context).atom_of(type_of(context, node));
}

Term is_leaf1(ProcessContext &context, const Term &node) { return Trees(context).boolean(is_leaf(context, node)); }

Term is_form1(ProcessContext &context, const Term &node) {
    return Trees(context).boolean(one_of(type_of(context, node), FORM_TYPES));
}

Term subtrees1(ProcessContext &context, const Term &node) { return subtrees(context, node); }

Term data1(ProcessContext &context, const Term &node) { return data(context, node); }

Term is_tree1(ProcessContext &context, const Term &node) { return Trees(context).boolean(is_tree(node)); }

// flatten_form_list_1/2: the forms of nested form lists, in order.
void flatten(ProcessContext &context, const Term &forms, Items &into) {
    for (const auto &form : elements(forms)) {
        if (type_of(context, form) == "form_list") {
            flatten(context, get(context, form, "form_list_elements"), into);
        } else {
            into.push_back(form);
        }
    }
}

Term flatten_form_list(ProcessContext &context, const Term &node) {
    Items forms;
    flatten(context, get(context, node, "form_list_elements"), forms);
    return copy_attrs(context, node, make(context, "form_list", std::array{Trees(context).list(forms)}));
}

constexpr std::array SHAPE_ENTRIES{
    typed_entry<type1>("erl_syntax", "type"),
    typed_entry<is_leaf1>("erl_syntax", "is_leaf"),
    typed_entry<is_form1>("erl_syntax", "is_form"),
    typed_entry<subtrees1>("erl_syntax", "subtrees"),
    typed_entry<data1>("erl_syntax", "data"),
    typed_entry<is_tree1>("erl_syntax", "is_tree"),
    typed_entry<flatten_form_list>("erl_syntax", "flatten_form_list"),
};
} // namespace

bool is_leaf(ProcessContext &context, const Term &node) {
    const auto type = type_of(context, node);
    if (one_of(type, LEAVES)) {
        return true;
    }
    if (type == "map_expr") {
        return map_expr_fields(context, node).is_nil() && is_none(map_expr_argument(context, node));
    }
    if (type == "map_type") {
        return is_atom(map_type_fields(context, node), "any_size");
    }
    if (type == "tuple") {
        return get(context, node, "tuple_elements").is_nil();
    }
    return type == "tuple_type" && is_atom(tuple_type_elements(context, node), "any_size");
}

Term subtrees(ProcessContext &context, const Term &node) {
    if (is_leaf(context, node)) {
        return Trees(context).nil();
    }
    return Trees(context).list(groups_of(context, node, type_of(context, node)));
}
} // namespace clause::runtime::builtins::syntax

namespace clause::runtime {
// Publish erl_syntax's node shape functions.
std::span<const BuiltinEntry> erl_syntax_shape_builtins() noexcept { return builtins::syntax::SHAPE_ENTRIES; }
} // namespace clause::runtime
