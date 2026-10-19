#include "../lists/list_walk.hpp"
#include "abstract.hpp"
#include "parser.hpp"
#include <array>

// erl_parse's attributes (plan 21 step LIB31): -Name Value, typed definitions, records and specs, with
// build_attribute/2, build_typed_attribute/2, build_record/2 and build_type_spec/2 and their errors.
namespace clause::runtime::builtins::front_end::parsing {
namespace {
bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

// A tuple of `size` elements tagged `tag`.
bool is_node(const Term &term, std::string_view tag, std::size_t size) {
    return term.is_tuple() && term.tuple_size_unchecked() == size && is_atom(term.tuple_element_unchecked(0), tag);
}

Term element(const Term &tuple, std::size_t index) { return tuple.tuple_element_unchecked(index); }

std::vector<Term> proper_elements(const Term &list) {
    std::vector<Term> items;
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        items.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        lists::no_clause();
    }
    return items;
}

// {op, _, '/', Name, Arity}: the fun reference of an attribute value.
bool is_farity(const Term &node) { return is_node(node, "op", 5) && is_atom(element(node, 2), "/"); }
} // namespace

Term Parser::attribute() {
    take();
    if (at("spec") || at("callback")) {
        const auto kind = take();
        const auto parenthesized = at("(");
        if (parenthesized) {
            take();
        }
        return spec_attribute(kind, parenthesized);
    }
    if (at("record")) {
        return record_attribute();
    }
    return plain_attribute(expect("atom"));
}

// '-' atom attr_val, '-' atom typed_attr_val or '-' atom '(' typed_attr_val ')'. A parenthesis closed after one
// expression was that expression's own: the value goes on as an expression.
Term Parser::plain_attribute(std::size_t name) {
    if (!at("(")) {
        return attribute_values(name, expression(), false);
    }
    take();
    const auto first = expression();
    if (!at(")")) {
        return attribute_values(name, first, true);
    }
    take();
    return attribute_values(name, continue_expression(postfix({first, Shape::max}), Mode::expression, 0), false);
}

Term Parser::attribute_values(std::size_t name, const Term &first, bool parenthesized) {
    if (at("::")) {
        take();
        const auto type = top_type();
        close_attribute(parenthesized);
        return build_typed_attribute(name, tuple({atom("type_def"), first, type}));
    }
    if (at(",")) {
        take();
        auto values = values_after_comma();
        close_attribute(parenthesized);
        if (values.typed) {
            return build_typed_attribute(name, tuple({atom("typed_record"), first, *values.typed}));
        }
        values.exprs.insert(values.exprs.begin(), first);
        return build_attribute(name, values.exprs);
    }
    if (parenthesized) {
        fail();
    }
    return build_attribute(name, {first});
}

// exprs, or typed_record_fields when a '{' ... '}' holds a typed field.
Parser::Values Parser::values_after_comma() {
    Values values;
    if (!at("{")) {
        values.exprs = exprs();
        return values;
    }
    std::vector<Term> untyped;
    if (auto typed = typed_tuple(untyped)) {
        values.typed = typed;
        return values;
    }
    values.exprs.push_back(continue_expression(postfix({untyped.front(), Shape::max}), Mode::expression, 0));
    while (at(",")) {
        take();
        values.exprs.push_back(expression());
    }
    return values;
}

// typed_record_fields -> '{' typed_exprs '}': a typed tuple, or none with the tuple expression in `untyped`.
std::optional<Term> Parser::typed_tuple(std::vector<Term> &untyped) {
    const auto open = take();
    std::vector<Term> elements;
    auto typed = false;
    if (!at("}")) {
        elements.push_back(typed_element(typed));
        while (at(",")) {
            take();
            elements.push_back(typed_element(typed));
        }
    }
    expect("}");
    const auto result = tuple({atom("tuple"), anno(open), list(elements)});
    if (typed) {
        return result;
    }
    untyped.push_back(result);
    return std::nullopt;
}

// typed_expr -> expr '::' top_type: {typed, Expr, Type}; `typed` records that one was.
Term Parser::typed_element(bool &typed) {
    auto element = expression();
    if (!at("::")) {
        return element;
    }
    take();
    typed = true;
    return tuple({atom("typed"), element, top_type()});
}

void Parser::close_attribute(bool parenthesized) {
    if (parenthesized) {
        expect(")");
    }
}

// '-' 'record': record_spec, typed_record_spec, either parenthesized, or a type_spec (which OTP's action rejects).
Term Parser::record_attribute() {
    const auto record = take();
    const auto parenthesized = at("(");
    if (parenthesized) {
        take();
    }
    if (at("#")) {
        return native_record_attribute(record, parenthesized);
    }
    if (!at("atom")) {
        fail();
    }
    if (at("(", 1) || at(":", 1)) {
        return spec_attribute(record, parenthesized);
    }
    return record_after_name(record, parenthesized);
}

// Without parentheses, OTP reduces record_spec -> atom before anything but a comma: a bad declaration.
Term Parser::record_after_name(std::size_t record, bool parenthesized) {
    const auto name = take();
    if (!parenthesized && !at(",")) {
        bad_declaration(token(name), "record");
    }
    expect(",");
    auto values = values_after_comma();
    close_attribute(parenthesized);
    if (values.typed) {
        return build_record(record, "record", token(name), *values.typed);
    }
    if (values.exprs.size() != 1) {
        lists::no_clause();
    }
    return build_record(record, "record", token(name), values.exprs.front());
}

// '#' record_name exprs, or '#' record_name typed_record_fields.
Term Parser::native_record_attribute(std::size_t record, bool parenthesized) {
    const auto hash = take();
    const auto name = record_name();
    // Without parentheses, OTP reduces record_spec -> '#' record_name before what no expression starts with.
    if (!parenthesized && !at_expression_start()) {
        bad_declaration(token(hash), "record");
    }
    auto values = values_after_comma();
    close_attribute(parenthesized);
    if (values.typed) {
        return build_record(record, "native_record", name, *values.typed);
    }
    if (values.exprs.size() != 1) {
        lists::no_clause();
    }
    return build_record(record, "native_record", name, values.exprs.front());
}

// type_spec -> spec_fun type_sigs | '(' spec_fun type_sigs ')'.
Term Parser::spec_attribute(std::size_t kind, bool parenthesized) {
    const auto name = expect("atom");
    auto function = token(name);
    if (at(":")) {
        take();
        function = tuple({function, token(expect("atom"))});
    }
    const auto specs = type_sigs();
    close_attribute(parenthesized);
    return build_type_spec(kind, function, specs);
}

Term Parser::build_type_spec(std::size_t kind, const Term &function, const std::vector<Term> &specs) const {
    const auto name = category_atom(kind);
    if (!is_atom(name, "spec") && !is_atom(name, "callback")) {
        lists::no_clause();
    }
    const auto arity = spec_arity(specs.front());
    const auto identity = is_node(function, "atom", 3)
                              ? tuple({element(function, 2), arity})
                              : tuple({element(element(function, 0), 2), element(element(function, 1), 2), arity});
    return tuple({atom("attribute"), anno(kind), name, tuple({identity, list(specs)})});
}

// find_arity_from_specs/1: the arguments of the first spec's fun type; a case_clause or badmatch for another type.
Term Parser::spec_arity(const Term &spec) const {
    auto fun = spec;
    if (is_node(fun, "type", 4) && is_atom(element(fun, 2), "bounded_fun")) {
        fun = proper_elements(element(fun, 3)).front();
    } else if (!is_node(fun, "type", 4) || !is_atom(element(fun, 2), "fun")) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::case_clause, .payload = fun.word()};
    }
    const auto parts = proper_elements(element(fun, 3));
    if (parts.size() != 2 || !is_node(parts.front(), "type", 4) || !is_atom(element(parts.front(), 2), "product")) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch, .payload = fun.word()};
    }
    return terms_.integer(static_cast<std::int64_t>(proper_elements(element(parts.front(), 3)).size()));
}

Term Parser::build_record(std::size_t record, std::string_view tag, const Term &name, const Term &fields) const {
    return tuple({atom("attribute"), anno(record), atom(tag), tuple({element(name, 2), record_tuple(fields)})});
}

Term Parser::record_tuple(const Term &tuple) const {
    if (!is_node(tuple, "tuple", 3)) {
        error_at_node(tuple, "bad record declaration");
    }
    return list(record_fields(proper_elements(element(tuple, 2))));
}

std::vector<Term> Parser::record_fields(const std::vector<Term> &fields) const {
    std::vector<Term> built;
    for (const auto &field : fields) {
        if (is_node(field, "typed", 3)) {
            built.push_back(tuple({atom("typed_record_field"), record_field(element(field, 1)), element(field, 2)}));
        } else {
            built.push_back(record_field(field));
        }
    }
    return built;
}

// record_fields/1 of one field: an atom, or an atom = default.
Term Parser::record_field(const Term &field) const {
    if (is_node(field, "atom", 3)) {
        const auto field_anno = element(field, 1);
        return tuple({atom("record_field"), field_anno, tuple({atom("atom"), field_anno, element(field, 2)})});
    }
    if (is_node(field, "match", 4) && is_node(element(field, 2), "atom", 3)) {
        const auto name = element(field, 2);
        const auto field_anno = element(name, 1);
        return tuple(
            {atom("record_field"), field_anno, tuple({atom("atom"), field_anno, element(name, 2)}), element(field, 3)});
    }
    error_at_node(field, "bad record field");
}

Term Parser::build_typed_attribute(std::size_t name, const Term &definition) const {
    const auto attribute = value(name);
    const auto definable = is_atom(attribute, "type") || is_atom(attribute, "opaque") || is_atom(attribute, "nominal");
    const auto head = element(definition, 1);
    if (definable && is_atom(element(definition, 0), "type_def") && is_node(head, "call", 4) &&
        is_node(element(head, 2), "atom", 3)) {
        return build_type_definition(name, head, element(definition, 2));
    }
    for (const auto declared : {"record", "type", "nominal", "native_record", "opaque"}) {
        if (is_atom(attribute, declared)) {
            bad_declaration(token(name), declared);
        }
    }
    error_at(location(anno(name)), "bad attribute");
}

// {attribute, A, type, {Name, Type, Variables}}: the arguments must be variables other than '_'.
Term Parser::build_type_definition(std::size_t name, const Term &call, const Term &type) const {
    const auto variables = proper_elements(element(call, 3));
    for (const auto &variable : variables) {
        if (is_node(variable, "var", 3) && is_atom(element(variable, 2), "_")) {
            error_at(location(element(variable, 1)), "bad type variable");
        }
    }
    for (const auto &variable : variables) {
        if (!is_node(variable, "var", 3)) {
            error_at_node(variable, "bad type variable");
        }
    }
    return tuple(
        {atom("attribute"), anno(name), value(name), tuple({element(element(call, 2), 2), type, element(call, 3)})});
}

// The attributes build_attribute/2 has clauses for; any other takes one literal value.
struct Parser::AttributeBuilder {
    std::string_view name;
    Term (Parser::*build)(std::size_t, const std::vector<Term> &) const;
};

const std::array<Parser::AttributeBuilder, 8> Parser::ATTRIBUTE_BUILDERS{{
    {"module", &Parser::module_attribute},
    {"export", &Parser::export_attribute},
    {"import", &Parser::import_attribute},
    {"import_record", &Parser::import_attribute},
    {"native_record", &Parser::native_record_definition},
    {"file", &Parser::file_attribute},
    {"doc", &Parser::build_doc},
    {"moduledoc", &Parser::build_doc},
}};

Term Parser::build_attribute(std::size_t name, const std::vector<Term> &values) const {
    const auto attribute = value(name);
    for (const auto &builder : ATTRIBUTE_BUILDERS) {
        if (is_atom(attribute, builder.name)) {
            return (this->*builder.build)(name, values);
        }
    }
    if (values.size() != 1) {
        error_at_node(values[1], "bad attribute");
    }
    return made_attribute(name, attribute_term(attribute_farity(values.front())));
}

Term Parser::made_attribute(std::size_t name, const Term &content) const {
    return tuple({atom("attribute"), anno(name), value(name), content});
}

Term Parser::module_attribute(std::size_t name, const std::vector<Term> &values) const {
    const auto &first = values.front();
    if (values.size() > 2 || !is_node(first, "atom", 3)) {
        bad_declaration(first, "module");
    }
    const auto module = element(first, 2);
    return made_attribute(name, values.size() == 1 ? module : tuple({module, list(var_list(values[1]))}));
}

Term Parser::export_attribute(std::size_t name, const std::vector<Term> &values) const {
    if (values.size() != 1) {
        bad_declaration(values[1], "export");
    }
    return made_attribute(name, list(farity_list(values.front())));
}

// import and import_record: a module and a list; one value is OTP's case_clause.
Term Parser::import_attribute(std::size_t name, const std::vector<Term> &values) const {
    const auto imports = is_atom(value(name), "import");
    if (values.size() == 1) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::case_clause, .payload = list(values).word()};
    }
    if (values.size() != 2 || !is_node(values.front(), "atom", 3)) {
        bad_declaration(values[1], imports ? "import" : "import_record");
    }
    const auto names = imports ? farity_list(values[1]) : native_record_names(values[1]);
    return made_attribute(name, tuple({element(values.front(), 2), list(names)}));
}

Term Parser::native_record_definition(std::size_t name, const std::vector<Term> & /*values*/) const {
    error_at(location(anno(name)), "bad native record definition");
}

Term Parser::file_attribute(std::size_t name, const std::vector<Term> &values) const {
    const auto &first = values.front();
    if (values.size() != 2 || !is_node(first, "string", 3) || !is_node(values[1], "integer", 3)) {
        bad_declaration(first, "file");
    }
    return made_attribute(name, tuple({element(first, 2), element(values[1], 2)}));
}

Term Parser::build_doc(std::size_t name, const std::vector<Term> &values) const {
    const auto &first = values.front();
    if (values.size() != 1) {
        bad_declaration(first, "doc");
    }
    if (const auto plain = plain_doc(first)) {
        return made_attribute(name, *plain);
    }
    if (is_node(first, "bin", 3)) {
        const auto binary = attribute_term(first);
        if (!binary.is_binary()) {
            bad_declaration(first, "doc");
        }
        return made_attribute(name, binary);
    }
    if (is_node(first, "map", 3)) {
        return made_attribute(name, doc_map(first, is_atom(value(name), "doc")));
    }
    bad_declaration(first, "doc");
}

// true, false, hidden, a string or {file, String}.
std::optional<Term> Parser::plain_doc(const Term &node) const {
    const auto content = node.is_tuple() && node.tuple_size_unchecked() == 3 ? element(node, 2) : Term{};
    const auto flag = is_atom(content, "true") || is_atom(content, "false") || is_atom(content, "hidden");
    if ((is_node(node, "atom", 3) && flag) || is_node(node, "string", 3)) {
        return content;
    }
    if (const auto file = doc_file(node)) {
        return tuple({atom("file"), *file});
    }
    return std::nullopt;
}

// {tuple, _, [{atom, _, file}, {string, _, Name}]}: the name.
std::optional<Term> Parser::doc_file(const Term &node) const {
    if (!is_node(node, "tuple", 3)) {
        return std::nullopt;
    }
    const auto items = proper_elements(element(node, 2));
    const auto file = items.size() == 2 && is_node(items[0], "atom", 3) && is_atom(element(items[0], 2), "file");
    if (!file || !is_node(items[1], "string", 3)) {
        return std::nullopt;
    }
    return element(items[1], 2);
}

// A doc map: => fields of literals; under -doc, equiv => Call keeps the call.
Term Parser::doc_map(const Term &map, bool doc) const {
    std::vector<std::pair<Term, Term>> entries;
    for (const auto &field : proper_elements(element(map, 2))) {
        if (!is_node(field, "map_field_assoc", 4)) {
            error_at_node(field, "bad attribute");
        }
        const auto field_value = element(field, 3);
        try {
            const auto key = abstract::normalise(context_, element(field, 2));
            const auto call = field_value.is_tuple() && field_value.tuple_size_unchecked() > 0 &&
                              is_atom(element(field_value, 0), "call");
            if (doc && is_atom(key, "equiv") && call) {
                entries.emplace_back(key, field_value);
            } else {
                entries.emplace_back(key, abstract::normalise(context_, attribute_farity(field_value)));
            }
        } catch (const BuiltinFailure &) {
            error_at_node(map, "bad attribute");
        }
    }
    return need(TermFactory(context_).map(entries));
}

std::vector<Term> Parser::farity_list(const Term &list) const {
    std::vector<Term> pairs;
    auto rest = list;
    for (; is_node(rest, "cons", 4); rest = element(rest, 3)) {
        const auto head = element(rest, 2);
        if (!is_farity(head)) {
            error_at_node(rest, "bad Name/Arity");
        }
        if (!is_node(element(head, 3), "atom", 3)) {
            error_at_node(element(head, 3), "bad function name");
        }
        if (!is_node(element(head, 4), "integer", 3)) {
            error_at_node(element(head, 4), "bad function arity");
        }
        pairs.push_back(tuple({element(element(head, 3), 2), element(element(head, 4), 2)}));
    }
    if (!is_node(rest, "nil", 2)) {
        error_at_node(rest, "bad Name/Arity");
    }
    return pairs;
}

std::vector<Term> Parser::var_list(const Term &list) const {
    std::vector<Term> names;
    auto rest = list;
    for (; is_node(rest, "cons", 4) && is_node(element(rest, 2), "var", 3); rest = element(rest, 3)) {
        names.push_back(element(element(rest, 2), 2));
    }
    if (!is_node(rest, "nil", 2)) {
        error_at_node(rest, "bad variable list");
    }
    return names;
}

std::vector<Term> Parser::native_record_names(const Term &list) const {
    std::vector<Term> names;
    auto rest = list;
    for (; is_node(rest, "cons", 4) && is_node(element(rest, 2), "atom", 3); rest = element(rest, 3)) {
        names.push_back(element(element(rest, 2), 2));
    }
    if (!is_node(rest, "nil", 2)) {
        error_at_node(rest, "bad native record name");
    }
    return names;
}

// attribute_farity/1: Name/Arity becomes {Name, Arity} inside lists, tuples and map values.
Term Parser::attribute_farity(const Term &node) const {
    if (is_node(node, "cons", 4)) {
        return farity_cons(node);
    }
    if (is_node(node, "tuple", 3)) {
        std::vector<Term> items;
        for (const auto &item : proper_elements(element(node, 2))) {
            items.push_back(attribute_farity(item));
        }
        return tuple({atom("tuple"), element(node, 1), list(items)});
    }
    if (is_node(node, "map", 3)) {
        return farity_map(node);
    }
    if (is_farity(node) && is_node(element(node, 3), "atom", 3) && is_node(element(node, 4), "integer", 3)) {
        return tuple({atom("tuple"), element(node, 1), list({element(node, 3), element(node, 4)})});
    }
    return node;
}

// The conses of a list, walked without recursion along the spine.
Term Parser::farity_cons(const Term &node) const {
    std::vector<std::pair<Term, Term>> cells;
    auto rest = node;
    for (; is_node(rest, "cons", 4); rest = element(rest, 3)) {
        cells.emplace_back(element(rest, 1), attribute_farity(element(rest, 2)));
    }
    auto built = attribute_farity(rest);
    for (auto cell = cells.rbegin(); cell != cells.rend(); ++cell) {
        built = tuple({atom("cons"), cell->first, cell->second, built});
    }
    return built;
}

// Map values only: [{Op, A, K, V'} || {Op, A, K, V} <- Fields], skipping fields of other sizes.
Term Parser::farity_map(const Term &node) const {
    std::vector<Term> fields;
    for (const auto &field : proper_elements(element(node, 2))) {
        if (field.is_tuple() && field.tuple_size_unchecked() == 4) {
            fields.push_back(
                tuple({element(field, 0), element(field, 1), element(field, 2), attribute_farity(element(field, 3))}));
        }
    }
    return tuple({atom("map"), element(node, 1), list(fields)});
}

Term Parser::attribute_term(const Term &node) const {
    try {
        return abstract::normalise(context_, node);
    } catch (const BuiltinFailure &) {
        error_at_node(node, "bad attribute");
    }
}
} // namespace clause::runtime::builtins::front_end::parsing
