#pragma once
#include "../front_end/forms.hpp"
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

// erl_syntax in C++ (plan 21 LIB77E2, docs/builtins.md#erl-syntax). Nodes keep OTP's representation: erl_parse forms,
// #wrapper{type, attr, tree} around a form that carries attributes, and #tree{type, attr, data} built by the
// constructors, whose data are OTP's records (#case_expr{argument, clauses}, ...); attributes are
// #attr{pos, ann, com} with com = none | #com{pre, post}. Every function raises OTP's errors for malformed nodes.
namespace clause::runtime::builtins::syntax {
using front_end::forms::element;
using front_end::forms::is_atom;
using front_end::forms::is_node;
using Items = std::vector<Term>;
using Parts = std::span<const Term>;

// The roles of terms that go with a node and would be easily swapped with it (typed.hpp's Named): a position, a
// value to store in its attributes, the node receiving another's, the subtree groups of make_tree/2 and a list tail.
struct PosRole;
struct ValueRole;
struct TargetRole;
struct GroupsRole;
struct TailRole;
using Pos = Named<PosRole>;
using Value = Named<ValueRole>;
using Target = Named<TargetRole>;
using Groups = Named<GroupsRole>;
using Tail = Named<TailRole>;

// The shape of a data record: its tag and tuple size (field count + 1); size 0 stands for the data itself.
struct Record {
    std::string_view tag;
    std::size_t size = 0;
};

// Term building for erl_syntax nodes in one process.
class Trees final {
  public:
    explicit Trees(ProcessContext &context) : build_(context) {}

    ProcessContext &context() const { return build_.context(); }

    Term atom(AtomName name) const { return build_.atom(name); }

    Term integer(std::int64_t value) const { return build_.integer(value); }

    Term nil() const { return build_.nil(); }

    Term boolean(bool value) const { return build_.terms().boolean(value); }

    Term list(Parts items) const { return build_.list(items); }

    Term list(Parts items, const Term &tail) const { return build_.terms().list(items, tail); }

    Term tuple(Parts items) const { return build_.tuple(items); }

    // The atom of a spelling read from another term.
    Term atom_of(std::string_view spelling) const;
    // A character list of a UTF-8 spelling (atom_to_list/1).
    Term chars(std::string_view spelling) const;

    // #attr{pos = Pos, ann = Ann, com = Com}; the default one is {attr, 0, [], none}.
    Term attr() const;
    Term attr(const Term &pos, const Term &ann, const Term &com) const;
    // #tree{type = Type, attr = #attr{}, data = Data}.
    Term tree(const Term &type, const Term &data) const;

    Term tree(AtomName type, const Term &data) const { return tree(atom(type), data); }

    // A data record {Tag, Fields...}.
    Term record(AtomName tag, Parts fields) const;
    // set_pos(Constructor(Value), Pos) for a leaf constructor (atom, integer, variable, string, operator).
    Term leaf_at(AtomName type, const Term &value, const Term &pos) const;

    // {Tag, Pos, Parts...}, an erl_parse node.
    template <typename... Rest> Term node(AtomName tag, const Term &pos, const Rest &...rest) const {
        const std::array<Term, sizeof...(Rest) + 2> items{atom(tag), pos, rest...};
        return tuple(items);
    }

  private:
    front_end::forms::Builder build_;
};

// #tree{} and #wrapper{}: 4-tuples tagged tree and wrapper; anything else is taken for an erl_parse form.
bool is_tree(const Term &node);
bool is_wrapper(const Term &node);
// unwrap/1: the form inside a wrapper, any other node as it is.
Term unwrap(const Term &node);
// Whether a term is the atom none (an absent optional part).
bool is_none(const Term &term);
// data/1: a tree's data; error({badarg, Node}) for anything else.
Term data(ProcessContext &context, const Term &node);
// Field `field` (zero-based) of a tree's data record; error({badrecord, Data}) when the data is not that record.
Term record_field(ProcessContext &context, const Term &node, const Record &record, std::size_t field);
// type/1's spelling; error({badarg, Node}) for a term that is no node.
std::string_view type_of(ProcessContext &context, const Term &node);
// get_pos/1, set_pos/2 and wrap/1 (a wrapper carrying the position of an erl_parse node).
Term get_pos(ProcessContext &context, const Term &node);
Term set_pos(ProcessContext &context, const Term &node, const Pos &pos);
Term wrap(ProcessContext &context, const Term &node);
// get_attrs/1 and set_attrs/2 (a wrapper first for an erl_parse node), and copy_attrs/2.
Term get_attrs(ProcessContext &context, const Term &node);
Term set_attrs(ProcessContext &context, const Term &node, const Value &attr);
Term copy_attrs(ProcessContext &context, const Term &source, const Term &target);
// copy_comments/2 and join_comments/2.
Term copy_comments(ProcessContext &context, const Term &source, const Target &target);
Term join_comments(ProcessContext &context, const Term &source, const Term &target);
// get_ann/1 and set_ann/2, has_comments/1, get_precomments/1, get_postcomments/1, remove_comments/1.
Term get_ann(ProcessContext &context, const Term &node);
Term set_ann(ProcessContext &context, const Term &node, const Value &ann);
bool has_comments(const Term &node);
Term get_precomments(ProcessContext &context, const Term &node);
Term get_postcomments(ProcessContext &context, const Term &node);
Term remove_comments(ProcessContext &context, const Term &node);
Term set_precomments(ProcessContext &context, const Term &node, const Value &comments);
Term set_postcomments(ProcessContext &context, const Term &node, const Value &comments);
// add_precomments/2 (field 0) and add_postcomments/2 (field 1): the node's comments followed by `comments`.
Term add_comments(ProcessContext &context, const Term &node, std::size_t field, const Term &comments);

// error({badarg, Term}), error({badrecord, Term}) and error({case_clause, Term}).
[[noreturn]] void badarg_of(ProcessContext &context, const Term &term);
[[noreturn]] void badrecord(const Term &term);
[[noreturn]] void case_clause(ProcessContext &context, const Term &term);
// The elements of a proper list; error({badarg, ...}) as length/1 and list comprehensions raise badarg otherwise.
Items elements(const Term &list);
// Whether a term is a proper list of exactly `count` elements.
bool list_of(const Term &term, std::size_t count);
// hd/1 and length/1 of a list; badarg for anything else.
Term first(const Term &list);
std::size_t length(const Term &list);

// Where an erl_parse form holds a field: element `element` of a tuple of `size` elements tagged `tag` (size 0: no form
// holds it). A type form also needs element 2 to be the atom `name`; with `items` the element is a list of exactly
// that many and the field is its item `item`.
struct Plain {
    std::string_view tag;
    std::size_t size = 0;
    std::size_t element = 0;
    std::string_view name = {};
    std::size_t items = 0;
    std::size_t item = 0;
};

// A field accessor with one erl_parse form: case unwrap(Node) of Form -> Field; Node1 -> (data(Node1))#record.field.
struct Field {
    std::string_view function;
    Plain plain;
    Record record;
    std::size_t field = 0;
};

// The field accessors of that shape, registered from this table (fields.cpp); C++ reads them by name with get/3.
inline constexpr auto FIELDS = std::to_array<Field>({
    {"variable_name", {"var", 3, 2}, {}},
    {"integer_value", {"integer", 3, 2}, {}},
    {"float_value", {"float", 3, 2}, {}},
    {"char_value", {"char", 3, 2}, {}},
    {"string_value", {"string", 3, 2}, {}},
    {"atom_value", {"atom", 3, 2}, {}},
    {"map_field_assoc_name", {"map_field_assoc", 4, 2}, {"map_field_assoc", 3}, 0},
    {"map_field_assoc_value", {"map_field_assoc", 4, 3}, {"map_field_assoc", 3}, 1},
    {"map_field_exact_name", {"map_field_exact", 4, 2}, {"map_field_exact", 3}, 0},
    {"map_field_exact_value", {"map_field_exact", 4, 3}, {"map_field_exact", 3}, 1},
    {"tuple_elements", {"tuple", 3, 2}, {}},
    {"binary_fields", {"bin", 3, 2}, {}},
    {"size_qualifier_body", {}, {"size_qualifier", 3}, 0},
    {"size_qualifier_argument", {}, {"size_qualifier", 3}, 1},
    {"error_marker_info", {"error", 2, 1}, {}},
    {"warning_marker_info", {"warning", 2, 1}, {}},
    {"arity_qualifier_body", {}, {"arity_qualifier", 3}, 0},
    {"arity_qualifier_argument", {}, {"arity_qualifier", 3}, 1},
    {"module_qualifier_argument", {"remote", 4, 2}, {"module_qualifier", 3}, 0},
    {"module_qualifier_body", {"remote", 4, 3}, {"module_qualifier", 3}, 1},
    {"function_clauses", {"function", 5, 4}, {"func", 3}, 1},
    {"clause_patterns", {"clause", 5, 2}, {"clause", 4}, 0},
    {"clause_body", {"clause", 5, 4}, {"clause", 4}, 2},
    {"disjunction_body", {}, {}},
    {"conjunction_body", {}, {}},
    {"catch_expr_body", {"catch", 3, 2}, {}},
    {"match_expr_pattern", {"match", 4, 2}, {"match_expr", 3}, 0},
    {"match_expr_body", {"match", 4, 3}, {"match_expr", 3}, 1},
    {"maybe_match_expr_pattern", {"maybe_match", 4, 2}, {"maybe_match_expr", 3}, 0},
    {"maybe_match_expr_body", {"maybe_match", 4, 3}, {"maybe_match_expr", 3}, 1},
    {"operator_name", {}, {}},
    {"infix_expr_left", {"op", 5, 3}, {"infix_expr", 4}, 1},
    {"infix_expr_right", {"op", 5, 4}, {"infix_expr", 4}, 2},
    {"prefix_expr_argument", {"op", 4, 3}, {"prefix_expr", 3}, 1},
    {"record_field_name", {}, {"record_field", 3}, 0},
    {"record_field_value", {}, {"record_field", 3}, 1},
    {"record_index_expr_field", {"record_index", 4, 3}, {"record_index_expr", 3}, 1},
    {"record_access_argument", {"record_field", 5, 2}, {"record_access", 4}, 0},
    {"record_access_field", {"record_field", 5, 4}, {"record_access", 4}, 2},
    {"application_operator", {"call", 4, 2}, {"application", 3}, 0},
    {"application_arguments", {"call", 4, 3}, {"application", 3}, 1},
    {"annotated_type_name", {"ann_type", 3, 2, {}, 2, 0}, {"annotated_type", 3}, 0},
    {"annotated_type_body", {"ann_type", 3, 2, {}, 2, 1}, {"annotated_type", 3}, 1},
    {"bitstring_type_m", {"type", 4, 3, "binary", 2, 0}, {"bitstring_type", 3}, 0},
    {"bitstring_type_n", {"type", 4, 3, "binary", 2, 1}, {"bitstring_type", 3}, 1},
    {"constrained_function_type_body", {"type", 4, 3, "bounded_fun", 2, 0}, {"constrained_function_type", 3}, 0},
    {"function_type_return", {"type", 4, 3, "fun", 2, 1}, {"function_type", 3}, 1},
    {"constraint_argument", {"type", 4, 3, "constraint", 2, 0}, {"constraint", 3}, 0},
    {"constraint_body", {"type", 4, 3, "constraint", 2, 1}, {"constraint", 3}, 1},
    {"map_type_assoc_name", {"type", 4, 3, "map_field_assoc", 2, 0}, {"map_type_assoc", 3}, 0},
    {"map_type_assoc_value", {"type", 4, 3, "map_field_assoc", 2, 1}, {"map_type_assoc", 3}, 1},
    {"map_type_exact_name", {"type", 4, 3, "map_field_exact", 2, 0}, {"map_type_exact", 3}, 0},
    {"map_type_exact_value", {"type", 4, 3, "map_field_exact", 2, 1}, {"map_type_exact", 3}, 1},
    {"integer_range_type_low", {"type", 4, 3, "range", 2, 0}, {"integer_range_type", 3}, 0},
    {"integer_range_type_high", {"type", 4, 3, "range", 2, 1}, {"integer_range_type", 3}, 1},
    {"record_type_field_name", {"type", 4, 3, "field_type", 2, 0}, {"record_type_field", 3}, 0},
    {"record_type_field_type", {"type", 4, 3, "field_type", 2, 1}, {"record_type_field", 3}, 1},
    {"user_type_application_arguments", {"user_type", 4, 3}, {"user_type_application", 3}, 1},
    {"typed_record_field_body", {}, {"typed_record_field", 3}, 0},
    {"typed_record_field_type", {}, {"typed_record_field", 3}, 1},
    {"list_comp_template", {"lc", 4, 2}, {"list_comp", 3}, 0},
    {"list_comp_body", {"lc", 4, 3}, {"list_comp", 3}, 1},
    {"binary_comp_template", {"bc", 4, 2}, {"binary_comp", 3}, 0},
    {"binary_comp_body", {"bc", 4, 3}, {"binary_comp", 3}, 1},
    {"map_comp_template", {"mc", 4, 2}, {"map_comp", 3}, 0},
    {"map_comp_body", {"mc", 4, 3}, {"map_comp", 3}, 1},
    {"generator_pattern", {"generate", 4, 2}, {"generator", 3}, 0},
    {"generator_body", {"generate", 4, 3}, {"generator", 3}, 1},
    {"strict_generator_pattern", {"generate_strict", 4, 2}, {"strict_generator", 3}, 0},
    {"strict_generator_body", {"generate_strict", 4, 3}, {"strict_generator", 3}, 1},
    {"binary_generator_pattern", {"b_generate", 4, 2}, {"binary_generator", 3}, 0},
    {"binary_generator_body", {"b_generate", 4, 3}, {"binary_generator", 3}, 1},
    {"strict_binary_generator_pattern", {"b_generate_strict", 4, 2}, {"strict_binary_generator", 3}, 0},
    {"strict_binary_generator_body", {"b_generate_strict", 4, 3}, {"strict_binary_generator", 3}, 1},
    {"map_generator_pattern", {"m_generate", 4, 2}, {"map_generator", 3}, 0},
    {"map_generator_body", {"m_generate", 4, 3}, {"map_generator", 3}, 1},
    {"strict_map_generator_pattern", {"m_generate_strict", 4, 2}, {"strict_map_generator", 3}, 0},
    {"strict_map_generator_body", {"m_generate_strict", 4, 3}, {"strict_map_generator", 3}, 1},
    {"zip_generator_body", {"zip", 3, 2}, {"zip_generator", 2}, 0},
    {"block_expr_body", {"block", 3, 2}, {}},
    {"if_expr_clauses", {"if", 3, 2}, {}},
    {"case_expr_argument", {"case", 4, 2}, {"case_expr", 3}, 0},
    {"case_expr_clauses", {"case", 4, 3}, {"case_expr", 3}, 1},
    {"else_expr_clauses", {"else", 3, 2}, {}},
    {"try_expr_body", {"try", 6, 2}, {"try_expr", 5}, 0},
    {"try_expr_clauses", {"try", 6, 3}, {"try_expr", 5}, 1},
    {"try_expr_after", {"try", 6, 5}, {"try_expr", 5}, 3},
    {"class_qualifier_argument", {}, {"class_qualifier", 4}, 0},
    {"class_qualifier_body", {}, {"class_qualifier", 4}, 1},
    {"class_qualifier_stacktrace", {}, {"class_qualifier", 4}, 2},
    {"named_fun_expr_clauses", {"named_fun", 4, 3}, {"named_fun_expr", 3}, 1},
    {"parentheses_body", {}, {}},
    {"macro_name", {}, {"macro", 3}, 0},
    {"macro_arguments", {}, {"macro", 3}, 1},
    {"comment_padding", {}, {"comment", 3}, 0},
    {"comment_text", {}, {"comment", 3}, 1},
    {"form_list_elements", {}, {}},
    {"text_string", {}, {}},
});

// The FIELDS row of an accessor, found at compile time so a misspelled name does not build.
struct FieldName {
    template <std::size_t SIZE>
    consteval FieldName(const char (&function)[SIZE]) : index(find(std::string_view(function, SIZE - 1))) {}

    std::size_t index;

  private:
    static consteval std::size_t find(std::string_view function) {
        for (std::size_t at = 0; at < FIELDS.size(); ++at) {
            if (FIELDS[at].function == function) {
                return at;
            }
        }
        throw "not a field accessor";
    }
};

// A field accessor of FIELDS, applied to a node.
Term field(ProcessContext &context, const Term &node, const Field &field);

inline Term get(ProcessContext &context, const Term &node, FieldName name) {
    return field(context, node, FIELDS[name.index]);
}

// A constructor that builds tree(Type, #Record{Arguments...}) (record empty: tree(Type, Argument), or
// tree(Type, []) without arguments); registered from this table (build.cpp), C++ builds them by name with make/3.
struct Simple {
    std::string_view function;
    std::size_t arity;
    std::string_view type;
    std::string_view record = {};
};

inline constexpr auto SIMPLE = std::to_array<Simple>({
    {"nil", 0, "nil"},
    {"eof_marker", 0, "eof_marker"},
    {"fun_type", 0, "fun_type"},
    {"underscore", 0, "underscore"},
    {"integer", 1, "integer"},
    {"float", 1, "float"},
    {"char", 1, "char"},
    {"string", 1, "string"},
    {"text", 1, "text"},
    {"form_list", 1, "form_list"},
    {"tuple", 1, "tuple"},
    {"binary", 1, "binary"},
    {"error_marker", 1, "error_marker"},
    {"warning_marker", 1, "warning_marker"},
    {"disjunction", 1, "disjunction"},
    {"conjunction", 1, "conjunction"},
    {"catch_expr", 1, "catch_expr"},
    {"block_expr", 1, "block_expr"},
    {"if_expr", 1, "if_expr"},
    {"else_expr", 1, "else_expr"},
    {"fun_expr", 1, "fun_expr"},
    {"parentheses", 1, "parentheses"},
    {"map_type", 1, "map_type"},
    {"tuple_type", 1, "tuple_type"},
    {"type_union", 1, "type_union"},
    {"implicit_fun", 1, "implicit_fun"},
    {"zip_generator", 1, "zip_generator", "zip_generator"},
    {"comment", 2, "comment", "comment"},
    {"map_expr", 2, "map_expr", "map_expr"},
    {"map_field_assoc", 2, "map_field_assoc", "map_field_assoc"},
    {"map_field_exact", 2, "map_field_exact", "map_field_exact"},
    {"binary_field", 2, "binary_field", "binary_field"},
    {"size_qualifier", 2, "size_qualifier", "size_qualifier"},
    {"attribute", 2, "attribute", "attribute"},
    {"arity_qualifier", 2, "arity_qualifier", "arity_qualifier"},
    {"module_qualifier", 2, "module_qualifier", "module_qualifier"},
    {"function", 2, "function", "func"},
    {"match_expr", 2, "match_expr", "match_expr"},
    {"maybe_match_expr", 2, "maybe_match_expr", "maybe_match_expr"},
    {"prefix_expr", 2, "prefix_expr", "prefix_expr"},
    {"record_field", 2, "record_field", "record_field"},
    {"record_index_expr", 2, "record_index_expr", "record_index_expr"},
    {"application", 2, "application", "application"},
    {"annotated_type", 2, "annotated_type", "annotated_type"},
    {"type_application", 2, "type_application", "type_application"},
    {"bitstring_type", 2, "bitstring_type", "bitstring_type"},
    {"function_type", 2, "function_type", "function_type"},
    {"constraint", 2, "constraint", "constraint"},
    {"map_type_assoc", 2, "map_type_assoc", "map_type_assoc"},
    {"map_type_exact", 2, "map_type_exact", "map_type_exact"},
    {"integer_range_type", 2, "integer_range_type", "integer_range_type"},
    {"record_type", 2, "record_type", "record_type"},
    {"record_type_field", 2, "record_type_field", "record_type_field"},
    {"user_type_application", 2, "user_type_application", "user_type_application"},
    {"typed_record_field", 2, "typed_record_field", "typed_record_field"},
    {"list_comp", 2, "list_comp", "list_comp"},
    {"binary_comp", 2, "binary_comp", "binary_comp"},
    {"map_comp", 2, "map_comp", "map_comp"},
    {"generator", 2, "generator", "generator"},
    {"strict_generator", 2, "strict_generator", "strict_generator"},
    {"binary_generator", 2, "binary_generator", "binary_generator"},
    {"strict_binary_generator", 2, "strict_binary_generator", "strict_binary_generator"},
    {"map_generator", 2, "map_generator", "map_generator"},
    {"strict_map_generator", 2, "strict_map_generator", "strict_map_generator"},
    {"case_expr", 2, "case_expr", "case_expr"},
    {"maybe_expr", 2, "maybe_expr", "maybe_expr"},
    {"named_fun_expr", 2, "named_fun_expr", "named_fun_expr"},
    {"macro", 2, "macro", "macro"},
    {"record_access", 3, "record_access", "record_access"},
    {"class_qualifier", 3, "class_qualifier", "class_qualifier"},
    {"try_expr", 4, "try_expr", "try_expr"},
});

// The SIMPLE row of a constructor and arity, found at compile time.
struct SimpleName {
    template <std::size_t SIZE>
    consteval SimpleName(const char (&function)[SIZE]) : index(find(std::string_view(function, SIZE - 1))) {}

    std::size_t index;

  private:
    static consteval std::size_t find(std::string_view function) {
        for (std::size_t at = 0; at < SIMPLE.size(); ++at) {
            if (SIMPLE[at].function == function) {
                return at;
            }
        }
        throw "not a simple constructor";
    }
};

// A constructor of SIMPLE applied to its arguments.
Term build(ProcessContext &context, const Simple &simple, Parts arguments);

inline Term make(ProcessContext &context, SimpleName name, Parts arguments) {
    return build(context, SIMPLE[name.index], arguments);
}

// A function over C++ terms, registered with a raw body that admits the arguments (made/1 below).
using Make = Term (*)(ProcessContext &context, Parts arguments);

// The raw builtin body of a Make: admit the argument words, call it, publish the result or record its failure.
template <Make Function> Word made(ProcessContext &context, std::span<const Word> words) {
    try {
        Items arguments;
        arguments.reserve(words.size());
        for (const auto word : words) {
            arguments.push_back(need(Term::from_word(word, context)));
        }
        return Function(context, arguments).word();
    } catch (const BuiltinFailure &failure) {
        return fail(context, failure);
    }
}

// The registry entry of a Make under erl_syntax:Function/Arity.
template <Make Function> constexpr BuiltinEntry made_entry(std::string_view function, std::size_t arity) {
    return BuiltinEntry{"erl_syntax", function, arity, &made<Function>};
}

// Constructors with their own rules (build.cpp).
Term atom(ProcessContext &context, const Term &name);
Term variable(ProcessContext &context, const Term &name);
Term operator_node(ProcessContext &context, const Term &name);
Term list(ProcessContext &context, const Term &elements, const Term &tail);
Term clause(ProcessContext &context, Parts patterns_guard_body);
Term conjunction(ProcessContext &context, const Term &tests);
Term record_expr(ProcessContext &context, Parts argument_type_fields);
Term receive_expr(ProcessContext &context, Parts clauses_timeout_action);
Term cons(ProcessContext &context, const Term &head, const Term &tail);
// Accessors with more than one erl_parse form or computed results (accessors.cpp).
Term function_name(ProcessContext &context, const Term &node);
Term function_arity(ProcessContext &context, const Term &node);
Term clause_guard(ProcessContext &context, const Term &node);
Term infix_expr_operator(ProcessContext &context, const Term &node);
Term prefix_expr_operator(ProcessContext &context, const Term &node);
Term record_index_expr_type(ProcessContext &context, const Term &node);
Term record_access_type(ProcessContext &context, const Term &node);
Term record_expr_argument(ProcessContext &context, const Term &node);
Term record_expr_type(ProcessContext &context, const Term &node);
Term record_expr_fields(ProcessContext &context, const Term &node);
Term constrained_function_type_argument(ProcessContext &context, const Term &node);
Term function_type_arguments(ProcessContext &context, const Term &node);
Term type_application_name(ProcessContext &context, const Term &node);
Term type_application_arguments(ProcessContext &context, const Term &node);
Term record_type_name(ProcessContext &context, const Term &node);
Term record_type_fields(ProcessContext &context, const Term &node);
Term user_type_application_name(ProcessContext &context, const Term &node);
Term map_type_fields(ProcessContext &context, const Term &node);
Term tuple_type_elements(ProcessContext &context, const Term &node);
Term type_union_types(ProcessContext &context, const Term &node);
Term map_expr_argument(ProcessContext &context, const Term &node);
Term map_expr_fields(ProcessContext &context, const Term &node);
Term binary_field_body(ProcessContext &context, const Term &node);
Term binary_field_types(ProcessContext &context, const Term &node);
Term attribute_name(ProcessContext &context, const Term &node);
Term attribute_arguments(ProcessContext &context, const Term &node);
Term maybe_expr_body(ProcessContext &context, const Term &node);
Term maybe_expr_else(ProcessContext &context, const Term &node);
Term receive_expr_clauses(ProcessContext &context, const Term &node);
Term receive_expr_timeout(ProcessContext &context, const Term &node);
Term receive_expr_action(ProcessContext &context, const Term &node);
Term try_expr_handlers(ProcessContext &context, const Term &node);
Term implicit_fun_name(ProcessContext &context, const Term &node);
Term fun_expr_clauses(ProcessContext &context, const Term &node);
Term named_fun_expr_name(ProcessContext &context, const Term &node);
// Unfolding erl_parse parts into trees, as the accessors of attributes and records do (accessors.cpp).
Term unfold_record_fields(ProcessContext &context, const Term &fields);
// List skeletons (lists.cpp).
Term list_prefix(ProcessContext &context, const Term &node);
Term list_suffix(ProcessContext &context, const Term &node);
Term list_head(ProcessContext &context, const Term &node);
Term list_tail(ProcessContext &context, const Term &node);
bool is_list_skeleton(ProcessContext &context, const Term &node);
bool is_proper_list(ProcessContext &context, const Term &node);
Term list_elements(ProcessContext &context, const Term &node);
// Node shapes (shapes.cpp): is_leaf/1, subtrees/1, make_tree/2 and update_tree/2.
bool is_leaf(ProcessContext &context, const Term &node);
Term subtrees(ProcessContext &context, const Term &node);
Term make_tree(ProcessContext &context, const Term &type, const Groups &groups);
Term update_tree(ProcessContext &context, const Term &node, const Term &groups);
// revert/1 and fold_binary_field_types/1 (revert.cpp), abstract/1 and concrete/1 (literals.cpp).
Term revert(ProcessContext &context, const Term &node);
Term fold_binary_field_types(ProcessContext &context, const Term &types);
Term abstract(ProcessContext &context, const Term &term);
Term concrete(ProcessContext &context, const Term &node);
} // namespace clause::runtime::builtins::syntax
