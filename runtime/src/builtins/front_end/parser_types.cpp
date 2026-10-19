#include "front_end.hpp"
#include "parser.hpp"
#include <array>

// erl_parse's types and specs (plan 21 step LIB31): top_type, type with its operators, fun types, constraints,
// and the actions build_type/2, build_gen_type/1, lift_unions/2, build_bin_type/2 and the constraint builders.
namespace clause::runtime::builtins::front_end::parsing {
namespace {
bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

bool is_node(const Term &term, std::string_view tag, std::size_t size) {
    return term.is_tuple() && term.tuple_size_unchecked() == size && is_atom(term.tuple_element_unchecked(0), tag);
}

// The binary operators of types: '..' (Nonassoc 200), add_op (Left 400) and mult_op (Left 500).
struct TypeOperator {
    std::string_view name;
    int precedence;
};

constexpr std::array<TypeOperator, 15> TYPE_OPERATORS{{
    {"..", 200},
    {"+", 400},
    {"-", 400},
    {"bor", 400},
    {"bxor", 400},
    {"bsl", 400},
    {"bsr", 400},
    {"or", 400},
    {"xor", 400},
    {"/", 500},
    {"*", 500},
    {"div", 500},
    {"rem", 500},
    {"band", 500},
    {"and", 500},
}};

const TypeOperator *type_operator(std::string_view name) {
    for (const auto &operation : TYPE_OPERATORS) {
        if (operation.name == name) {
            return &operation;
        }
    }
    return nullptr;
}
} // namespace

// top_type -> var '::' top_type | type '|' top_type | type.
Term Parser::top_type() {
    if (at("var") && at("::", 1)) {
        const auto variable = take();
        take();
        const auto type = top_type();
        return tuple({atom("ann_type"_atom), anno(variable), list({token(variable), type})});
    }
    const auto left = type();
    if (!at("|")) {
        return left;
    }
    take();
    return lift_unions(left, top_type());
}

std::vector<Term> Parser::top_types() {
    std::vector<Term> types{top_type()};
    while (at(",")) {
        take();
        types.push_back(top_type());
    }
    return types;
}

// lift_unions/2: a union on the right takes the left type in front.
Term Parser::lift_unions(const Term &left, const Term &right) const {
    const auto items = is_node(right, "type", 4) && is_atom(right.tuple_element_unchecked(2), "union")
                           ? terms_.cons(left, right.tuple_element_unchecked(3))
                           : list({left, right});
    return tuple({atom("type"_atom), first_anno(left), atom("union"_atom), items});
}

// type -> type '..' type | type add_op type | type mult_op type | prefix_op type | ...
Term Parser::type(int minimum) {
    auto left = type_operand();
    for (const auto *operation = type_operator(category()); operation && operation->precedence >= minimum;
         operation = type_operator(category())) {
        const auto symbol = take();
        const auto right = type(operation->precedence + 1);
        if (operation->name == "..") {
            left = tuple({atom("type"_atom), left.tuple_element_unchecked(1), atom("range"_atom), list({left, right})});
            if (at("..")) {
                fail();
            }
        } else {
            left = tuple({atom("op"_atom), anno(symbol), category_atom(symbol), left, right});
        }
    }
    return left;
}

// prefix_op type binds tighter than every binary type operator.
Term Parser::type_operand() {
    if (!is_prefix_operator(category())) {
        return type_primary();
    }
    const auto operation = take();
    const auto operand = type_operand();
    return tuple({atom("op"_atom), anno(operation), category_atom(operation), operand});
}

Term Parser::type_primary() {
    const auto name = category();
    if (name == "var" || name == "integer" || name == "char") {
        return token(take());
    }
    if (name == "(") {
        take();
        const auto inner = top_type();
        expect(")");
        return inner;
    }
    if (name == "atom") {
        return atom_type();
    }
    if (name == "fun") {
        return fun_type_after_fun();
    }
    return bracketed_type();
}

// Lists, maps, records, tuples and binaries.
Term Parser::bracketed_type() {
    const auto name = category();
    if (name == "[") {
        return list_type();
    }
    if (name == "#") {
        return at("{", 1) ? map_type() : record_type(take());
    }
    if (name == "{") {
        return tuple_type();
    }
    if (name == "<<") {
        return binary_type();
    }
    fail();
}

// atom, atom '(' [top_types] ')' (build_gen_type/1, build_type/2) or atom ':' atom '(' [top_types] ')'.
Term Parser::atom_type() {
    const auto name = take();
    if (at(":")) {
        take();
        const auto remote = expect("atom");
        expect("(");
        const auto arguments = at(")") ? std::vector<Term>{} : top_types();
        expect(")");
        return tuple({atom("remote_type"_atom), anno(name), list({token(name), token(remote), list(arguments)})});
    }
    if (!at("(")) {
        return token(name);
    }
    take();
    const auto arguments = at(")") ? std::vector<Term>{} : top_types();
    expect(")");
    const auto type_name = value(name);
    const auto spelling = type_name.atom_spelling_unchecked();
    if (arguments.empty() && (spelling == "tuple" || spelling == "map")) {
        return tuple({atom("type"_atom), anno(name), type_name, atom("any"_atom)});
    }
    const auto predefined = is_predefined_type(spelling, static_cast<std::int64_t>(arguments.size()));
    return tuple({atom(predefined ? "type"_atom : "user_type"_atom), anno(name), type_name, list(arguments)});
}

// '[' ']', '[' top_type ']' and '[' top_type ',' '...' ']'.
Term Parser::list_type() {
    const auto open = take();
    if (at("]")) {
        take();
        return tuple({atom("type"_atom), anno(open), atom("nil"_atom), terms_.nil()});
    }
    const auto element = top_type();
    auto kind = "list";
    if (at(",")) {
        take();
        expect("...");
        kind = "nonempty_list";
    }
    expect("]");
    return tuple({atom("type"_atom), anno(open), atom(kind), list({element})});
}

// '#' '{' [map_pair_types] '}'.
Term Parser::map_type() {
    const auto hash = take();
    take();
    std::vector<Term> pairs;
    if (!at("}")) {
        pairs.push_back(map_pair_type());
        while (at(",")) {
            take();
            pairs.push_back(map_pair_type());
        }
    }
    expect("}");
    return tuple({atom("type"_atom), anno(hash), atom("map"_atom), list(pairs)});
}

// map_pair_type -> top_type '=>' top_type | top_type ':=' top_type.
Term Parser::map_pair_type() {
    const auto key = top_type();
    if (!at("=>") && !at(":=")) {
        fail();
    }
    const auto arrow = take();
    const auto kind = is_category(arrow, "=>") ? "map_field_assoc" : "map_field_exact";
    return tuple({atom("type"_atom), anno(arrow), atom(kind), list({key, top_type()})});
}

// '#' atom '{' [field_types] '}', or '#' atom ':' record_name ... with {tuple, A, [Module, Name]} as the record.
Term Parser::record_type(std::size_t hash) {
    const auto name = expect("atom");
    auto id = token(name);
    if (at(":")) {
        take();
        id = tuple({atom("tuple"_atom), anno(hash), list({token(name), record_name()})});
    }
    expect("{");
    auto fields = at("}") ? std::vector<Term>{} : field_types();
    expect("}");
    fields.insert(fields.begin(), id);
    return tuple({atom("type"_atom), anno(hash), atom("record"_atom), list(fields)});
}

// field_type -> atom '::' top_type.
std::vector<Term> Parser::field_types() {
    std::vector<Term> fields;
    for (;;) {
        const auto name = expect("atom");
        expect("::");
        fields.push_back(
            tuple({atom("type"_atom), anno(name), atom("field_type"_atom), list({token(name), top_type()})}));
        if (!at(",")) {
            return fields;
        }
        take();
    }
}

Term Parser::tuple_type() {
    const auto open = take();
    const auto elements = at("}") ? std::vector<Term>{} : top_types();
    expect("}");
    return tuple({atom("type"_atom), anno(open), atom("tuple"_atom), list(elements)});
}

// binary_type: '<<' '>>', '<<' Var ':' type '>>', '<<' Var ':' Var '*' type '>>' or both, base first.
Term Parser::binary_type() {
    const auto open = take();
    if (at(">>")) {
        take();
        return tuple(
            {atom("type"_atom), anno(open), atom("binary"_atom), list({zero_at(anno(open)), zero_at(anno(open))})});
    }
    const auto variable = expect("var");
    expect(":");
    if (at("var") && at("*", 1)) {
        return unit_type(open, std::nullopt, variable);
    }
    const auto base = bin_type({variable}, type());
    if (!at(",")) {
        expect(">>");
        return tuple({atom("type"_atom), anno(open), atom("binary"_atom), list({base, zero_at(anno(open))})});
    }
    take();
    const auto unit_variable = expect("var");
    expect(":");
    return unit_type(open, base, unit_variable);
}

// bin_unit_type -> var ':' var '*' type, read from the second variable on.
Term Parser::unit_type(std::size_t open, std::optional<Term> base, std::size_t variable) {
    const auto unit_variable = expect("var");
    expect("*");
    const auto unit = bin_type({variable, unit_variable}, type());
    expect(">>");
    const auto size = base.value_or(zero_at(anno(open)));
    return tuple({atom("type"_atom), anno(open), atom("binary"_atom), list({size, unit})});
}

// build_bin_type/2: each variable must be '_'.
Term Parser::bin_type(std::initializer_list<std::size_t> variables, const Term &type) const {
    for (const auto variable : variables) {
        if (!is_atom(value(variable), "_")) {
            error_at(location(anno(variable)), "Bad binary type");
        }
    }
    return type;
}

// abstract2(0, Anno): the integer 0 annotated with the annotation's line.
Term Parser::zero_at(const Term &anno) const {
    const auto where = location(anno);
    const auto line = where.is_tuple() ? where.tuple_element_unchecked(0) : where;
    return tuple({atom("integer"_atom), line, terms_.integer(0)});
}

// 'fun' '(' ')' or 'fun' '(' fun_type ')'.
Term Parser::fun_type_after_fun() {
    const auto keyword = take();
    expect("(");
    if (at(")")) {
        take();
        return tuple({atom("type"_atom), anno(keyword), atom("fun"_atom), terms_.nil()});
    }
    const auto fun = fun_type();
    expect(")");
    return fun;
}

// fun_type -> '(' '...' ')' '->' top_type | '(' [top_types] ')' '->' top_type.
Term Parser::fun_type() {
    const auto open = expect("(");
    Term arguments;
    if (at("...")) {
        take();
        arguments = tuple({atom("type"_atom), anno(open), atom("any"_atom)});
    } else {
        const auto types = at(")") ? std::vector<Term>{} : top_types();
        arguments = tuple({atom("type"_atom), anno(open), atom("product"_atom), list(types)});
    }
    expect(")");
    expect("->");
    const auto result = top_type();
    return tuple({atom("type"_atom), anno(open), atom("fun"_atom), list({arguments, result})});
}

std::vector<Term> Parser::type_sigs() {
    std::vector<Term> signatures{type_sig()};
    while (at(";")) {
        take();
        signatures.push_back(type_sig());
    }
    return signatures;
}

// type_sig -> fun_type | fun_type 'when' type_guards.
Term Parser::type_sig() {
    const auto fun = fun_type();
    if (!at("when")) {
        return fun;
    }
    take();
    std::vector<Term> guards{type_guard()};
    while (at(",")) {
        take();
        guards.push_back(type_guard());
    }
    return tuple(
        {atom("type"_atom), fun.tuple_element_unchecked(1), atom("bounded_fun"_atom), list({fun, list(guards)})});
}

// type_guard -> atom '(' top_types ')' (build_compat_constraint/2) | var '::' top_type (build_constraint/2).
Term Parser::type_guard() {
    if (at("var")) {
        const auto variable = token(take());
        expect("::");
        return constraint(variable, top_type());
    }
    const auto name = expect("atom");
    expect("(");
    const auto types = top_types();
    expect(")");
    if (!is_atom(value(name), "is_subtype") || types.size() != 2) {
        throw FormatError{location(anno(name)), "unsupported constraint ~tw", {value(name)}};
    }
    if (!is_node(types.front(), "var", 3)) {
        error_at_node(types.front(), "bad type variable");
    }
    return constraint(types.front(), types.back());
}

// {type, A, constraint, [{atom, A, is_subtype}, [Var, Type]]}; the variable may not be '_'.
Term Parser::constraint(const Term &variable, const Term &type) const {
    if (is_atom(variable.tuple_element_unchecked(2), "_")) {
        error_at(location(variable.tuple_element_unchecked(1)), "bad type variable");
    }
    const auto first = first_anno(variable);
    return tuple({atom("type"_atom), first, atom("constraint"_atom),
                  list({tuple({atom("atom"_atom), first, atom("is_subtype"_atom)}), list({variable, type})})});
}
} // namespace clause::runtime::builtins::front_end::parsing
