#include "parser.hpp"
#include <array>

// erl_parse's operands (plan 21 step LIB31): expr_max and pat_expr_max, records, maps, binaries, strings, sigils
// and comprehensions.
namespace clause::runtime::builtins::front_end::parsing {
namespace {
// The elements of a proper list; `++`'s badarg for any other term.
std::vector<Term> appended_elements(const Term &list) {
    std::vector<Term> items;
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        items.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return items;
}

bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}
} // namespace

// The expressions only expressions have: blocks and the control expressions.
struct Parser::Keyword {
    std::string_view category;
    Term (Parser::*parse)();
};

const std::array<Parser::Keyword, 7> Parser::KEYWORDS{{
    {"begin", &Parser::block},
    {"if", &Parser::if_expression},
    {"case", &Parser::case_expression},
    {"receive", &Parser::receive_expression},
    {"fun", &Parser::fun_expression},
    {"try", &Parser::try_expression},
    {"maybe", &Parser::maybe_expression},
}};

Operand Parser::primary(Mode mode) {
    if (const auto leaf = atomic()) {
        return *leaf;
    }
    if (const auto bracketed = this->bracketed(mode)) {
        return *bracketed;
    }
    const auto name = category();
    if (name == "#" || name == "#_") {
        return hash_primary(mode);
    }
    for (const auto &keyword : KEYWORDS) {
        if (mode == Mode::expression && keyword.category == name) {
            return {(this->*keyword.parse)(), Shape::max};
        }
    }
    fail();
}

// Lists, binaries, tuples and parenthesized expressions.
std::optional<Operand> Parser::bracketed(Mode mode) {
    const auto name = category();
    if (name == "[") {
        return Operand{list_expression(mode), Shape::max};
    }
    if (name == "<<") {
        return Operand{binary_expression(mode), Shape::max};
    }
    if (name == "{") {
        return Operand{tuple_expression(), Shape::max};
    }
    if (name == "(") {
        return Operand{paren(mode), Shape::max};
    }
    return std::nullopt;
}

// var and atomic: tokens are their own nodes; strings concatenate.
std::optional<Operand> Parser::atomic() {
    const auto name = category();
    if (name == "var" || name == "char" || name == "integer" || name == "float" || name == "atom") {
        return Operand{token(take()), Shape::max};
    }
    if (name == "string") {
        return Operand{strings(), Shape::max};
    }
    if (name == "sigil_prefix") {
        return Operand{sigil(), Shape::max};
    }
    return std::nullopt;
}

// expr_max: no record or map expression, though a map comprehension is one.
Term Parser::expr_max() {
    if (at("#")) {
        const auto hash = take();
        if (!at("{")) {
            fail();
        }
        return map_creation(hash, Mode::expression, true).term;
    }
    if (at("#_")) {
        fail();
    }
    return primary(Mode::expression).term;
}

// '#' map_tuple, a map comprehension, '#' atom '.' atom, '#' record_name record_tuple, its native forms and '#_'.
Operand Parser::hash_primary(Mode mode) {
    const auto hash = take();
    if (is_category(hash, "#_")) {
        return {tuple({atom("record"_atom), anno(hash), terms_.nil(), record_tuple_fields()}), Shape::record};
    }
    if (at("{")) {
        return map_creation(hash, mode, false);
    }
    if (at("atom") && at(".", 1)) {
        const auto name = take();
        take();
        const auto field = expect("atom");
        return {tuple({atom("record_index"_atom), anno(hash), value(name), token(field)}), Shape::record};
    }
    auto id = Term{};
    if (at("atom") && at(":", 1)) {
        const auto module = take();
        take();
        id = tuple({value(module), record_name_value(record_name())});
    } else {
        id = record_name_value(record_name());
    }
    return {tuple({atom("record"_atom), anno(hash), id, record_tuple_fields()}), Shape::record};
}

// '#' '{' ...: a map, or (in expressions) a comprehension, which is all expr_max allows.
Operand Parser::map_creation(std::size_t hash, Mode mode, bool comprehension_only) {
    take();
    if (!comprehension_only && at("}")) {
        take();
        return {tuple({atom("map"_atom), anno(hash), terms_.nil()}), Shape::map};
    }
    std::vector<Term> fields{map_field()};
    while (at(",")) {
        take();
        fields.push_back(map_field());
    }
    if (mode == Mode::expression && at("||")) {
        take();
        const auto template_term = fields.size() == 1 ? fields.front() : list(fields);
        const auto generators = qualifiers();
        expect("}");
        return {tuple({atom("mc"_atom), anno(hash), template_term, list(generators)}), Shape::max};
    }
    if (comprehension_only) {
        fail();
    }
    expect("}");
    return {tuple({atom("map"_atom), anno(hash), list(fields)}), Shape::map};
}

Term Parser::map_tuple() {
    expect("{");
    std::vector<Term> fields;
    if (!at("}")) {
        fields.push_back(map_field());
        while (at(",")) {
            take();
            fields.push_back(map_field());
        }
    }
    expect("}");
    return list(fields);
}

// map_key '=>' expr | map_key ':=' expr.
Term Parser::map_field() {
    const auto key = expression();
    if (!at("=>") && !at(":=")) {
        fail();
    }
    const auto arrow = take();
    const auto tag = is_category(arrow, "=>") ? "map_field_assoc" : "map_field_exact";
    return tuple({atom(tag), anno(arrow), key, expression()});
}

Term Parser::record_tuple_fields() {
    expect("{");
    std::vector<Term> fields;
    if (!at("}")) {
        fields.push_back(record_field_expression());
        while (at(",")) {
            take();
            fields.push_back(record_field_expression());
        }
    }
    expect("}");
    return list(fields);
}

// record_field -> var '=' expr | atom '=' expr.
Term Parser::record_field_expression() {
    if (!at("var") && !at("atom")) {
        fail();
    }
    const auto name = take();
    expect("=");
    return tuple({atom("record_field"_atom), anno(name), token(name), expression()});
}

// '[' ']', '[' expr tail, or a list comprehension of one template or several.
Term Parser::list_expression(Mode mode) {
    const auto open = take();
    if (at("]")) {
        take();
        return tuple({atom("nil"_atom), anno(open)});
    }
    std::vector<Term> elements{expression()};
    while (at(",")) {
        take();
        elements.push_back(expression());
    }
    if (mode == Mode::expression && at("||")) {
        take();
        const auto template_term = elements.size() == 1 ? elements.front() : list(elements);
        const auto generators = qualifiers();
        expect("]");
        return tuple({atom("lc"_atom), anno(open), template_term, list(generators)});
    }
    return list_tail(elements, open);
}

// tail -> ']' | '|' expr ']' | ',' expr tail: the conses from the last element back.
Term Parser::list_tail(std::vector<Term> &elements, std::size_t open) {
    Term tail;
    if (at("|")) {
        take();
        tail = expression();
        expect("]");
    } else {
        tail = tuple({atom("nil"_atom), anno(expect("]"))});
    }
    for (auto index = elements.size() - 1; index > 0; --index) {
        tail = tuple({atom("cons"_atom), first_anno(elements[index]), elements[index], tail});
    }
    return tuple({atom("cons"_atom), anno(open), elements.front(), tail});
}

Term Parser::tuple_expression() {
    const auto open = take();
    std::vector<Term> items;
    if (!at("}")) {
        items = exprs();
    }
    expect("}");
    return tuple({atom("tuple"_atom), anno(open), list(items)});
}

// '<<' '>>', '<<' bin_elements '>>' or (in expressions) '<<' expr_max '||' lc_exprs '>>'.
Term Parser::binary_expression(Mode mode) {
    const auto open = take();
    std::vector<Term> elements;
    if (!at(">>")) {
        std::optional<Term> first;
        if (!is_prefix_operator(category())) {
            first = expr_max();
            if (mode == Mode::expression && at("||")) {
                take();
                const auto generators = qualifiers();
                expect(">>");
                return tuple({atom("bc"_atom), anno(open), *first, list(generators)});
            }
        }
        elements.push_back(bin_element(first));
        while (at(",")) {
            take();
            elements.push_back(bin_element(std::nullopt));
        }
    }
    expect(">>");
    binary_span_ = {open, pos_};
    return tuple({atom("bin"_atom), anno(open), list(elements)});
}

// bit_expr opt_bit_size_expr opt_bit_type_list; the value may be read already.
Term Parser::bin_element(std::optional<Term> value) {
    if (!value && is_prefix_operator(category())) {
        const auto operation = take();
        const auto operand = expr_max();
        value = tuple({atom("op"_atom), anno(operation), category_atom(operation), operand});
    } else if (!value) {
        value = expr_max();
    }
    auto size = atom("default"_atom);
    if (at(":")) {
        take();
        size = expr_max();
    }
    auto types = atom("default"_atom);
    if (at("/")) {
        take();
        types = bit_types();
    }
    return tuple({atom("bin_element"_atom), first_anno(*value), *value, size, types});
}

// bit_type_list -> bit_type '-' bit_type_list | bit_type; bit_type -> atom | atom ':' integer.
Term Parser::bit_types() {
    std::vector<Term> types;
    for (;;) {
        const auto name = expect("atom");
        if (at(":")) {
            take();
            types.push_back(tuple({value(name), value(expect("integer"))}));
        } else {
            types.push_back(value(name));
        }
        if (!at("-")) {
            return list(types);
        }
        take();
    }
}

// strings -> string | string strings: {string, Anno, S1 ++ S2 ++ ...}.
Term Parser::strings() {
    const auto first = take();
    std::vector<std::size_t> parts{first};
    while (at("string")) {
        parts.push_back(take());
    }
    if (parts.size() == 1) {
        return token(first);
    }
    auto joined = value(parts.back());
    for (auto part = parts.rbegin() + 1; part != parts.rend(); ++part) {
        joined = terms_.list(appended_elements(value(*part)), joined);
    }
    return tuple({atom("string"_atom), anno(first), joined});
}

// build_sigil/3: ~s and ~S keep the string, ~, ~b and ~B make a UTF-8 binary; no suffix is allowed.
Term Parser::sigil() {
    const auto prefix = take();
    const auto string = expect("string");
    const auto suffix = expect("sigil_suffix");
    const auto type = value(prefix);
    const auto text = is_atom(type, "s") || is_atom(type, "S");
    const auto binary = is_atom(type, "") || is_atom(type, "b") || is_atom(type, "B");
    if (!text && !binary) {
        error_at(location(anno(prefix)), "illegal sigil prefix");
    }
    if (!value(suffix).is_nil()) {
        error_at(location(anno(suffix)), "illegal sigil suffix");
    }
    if (text) {
        return token(string);
    }
    const auto element =
        tuple({atom("bin_element"_atom), anno(string), token(string), atom("default"_atom), list({atom("utf8"_atom)})});
    return tuple({atom("bin"_atom), anno(prefix), list({element})});
}

Term Parser::paren(Mode mode) {
    take();
    const auto inner = expression(mode);
    expect(")");
    return inner;
}

Term Parser::block() {
    const auto keyword = take();
    const auto body = exprs();
    expect("end");
    return tuple({atom("block"_atom), anno(keyword), list(body)});
}

// lc_exprs: qualifiers separated by ',', those joined by '&&' zipped.
std::vector<Term> Parser::qualifiers() {
    std::vector<Term> items;
    for (;;) {
        const auto item = qualifier();
        items.push_back(at("&&") ? zip(item) : item);
        if (!at(",")) {
            return items;
        }
        take();
    }
}

// zc_exprs: qualifiers joined by '&&'. A zip before ',' takes the comma's anno, a last one its first generator's.
Term Parser::zip(const Term &first) {
    std::vector<Term> zipped{first};
    while (at("&&")) {
        take();
        zipped.push_back(qualifier());
    }
    const auto zip_anno = at(",") ? anno(pos_) : zipped.front().tuple_element_unchecked(1);
    return tuple({atom("zip"_atom), zip_anno, list(zipped)});
}

// lc_expr: a filter, or a generator of a pattern, a map field or a binary.
Term Parser::qualifier() {
    const auto start = pos_;
    const auto left = expression();
    if (at(":=")) {
        const auto arrow = take();
        const auto field = tuple({atom("map_field_exact"_atom), anno(arrow), left, expression()});
        if (!at("<-") && !at("<:-")) {
            fail();
        }
        return generator(field, true);
    }
    if (at("<-") || at("<:-")) {
        return generator(left, false);
    }
    if (at("<=") || at("<:=")) {
        // binary '<=' expr: only a binary as it was written, no operator or parenthesis around it.
        if (binary_span_ != std::pair{start, pos_}) {
            fail();
        }
        return generator(left, false);
    }
    return left;
}

// generate, generate_strict, b_generate, b_generate_strict, m_generate and m_generate_strict by arrow.
Term Parser::generator(const Term &left, bool map_field) {
    struct Generator {
        std::string_view arrow;
        std::string_view tag;
        std::string_view map_tag;
    };

    static constexpr std::array<Generator, 4> GENERATORS{{
        {"<-", "generate", "m_generate"},
        {"<:-", "generate_strict", "m_generate_strict"},
        {"<=", "b_generate", "b_generate"},
        {"<:=", "b_generate_strict", "b_generate_strict"},
    }};
    const auto arrow = take();
    auto tag = std::string_view{};
    for (const auto &row : GENERATORS) {
        if (is_category(arrow, row.arrow)) {
            tag = map_field ? row.map_tag : row.tag;
        }
    }
    const auto right = expression();
    return tuple({atom(tag), anno(arrow), left, right});
}
} // namespace clause::runtime::builtins::front_end::parsing
