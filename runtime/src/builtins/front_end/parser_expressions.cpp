#include "parser.hpp"
#include <algorithm>
#include <array>

// erl_parse's operator grammar (plan 21 step LIB31): OTP's precedences as precedence climbing, with patterns
// (pat_expr) lacking catch, sends, short circuits, calls, remotes and '#' after an operand.
namespace clause::runtime::builtins::front_end::parsing {
namespace {
using Build = Operator::Build;
using Associativity = Operator::Associativity;

constexpr std::array<Operator, 30> OPERATORS{{
    {"=", 100, Associativity::right, Build::match, true},    {"!", 100, Associativity::right, Build::op, false},
    {"orelse", 150, Associativity::right, Build::op, false}, {"andalso", 160, Associativity::right, Build::op, false},
    {"==", 200, Associativity::none, Build::op, true},       {"/=", 200, Associativity::none, Build::op, true},
    {"=<", 200, Associativity::none, Build::op, true},       {"<", 200, Associativity::none, Build::op, true},
    {">=", 200, Associativity::none, Build::op, true},       {">", 200, Associativity::none, Build::op, true},
    {"=:=", 200, Associativity::none, Build::op, true},      {"=/=", 200, Associativity::none, Build::op, true},
    {"++", 300, Associativity::right, Build::op, true},      {"--", 300, Associativity::right, Build::op, true},
    {"+", 400, Associativity::left, Build::op, true},        {"-", 400, Associativity::left, Build::op, true},
    {"bor", 400, Associativity::left, Build::op, true},      {"bxor", 400, Associativity::left, Build::op, true},
    {"bsl", 400, Associativity::left, Build::op, true},      {"bsr", 400, Associativity::left, Build::op, true},
    {"or", 400, Associativity::left, Build::op, true},       {"xor", 400, Associativity::left, Build::op, true},
    {"/", 500, Associativity::left, Build::op, true},        {"*", 500, Associativity::left, Build::op, true},
    {"div", 500, Associativity::left, Build::op, true},      {"rem", 500, Associativity::left, Build::op, true},
    {"band", 500, Associativity::left, Build::op, true},     {"and", 500, Associativity::left, Build::op, true},
    {"(", 750, Associativity::left, Build::call, false},     {":", 800, Associativity::none, Build::remote, false},
}};

// prefix_op: Unary 600, so an operand holds only the operators above it.
constexpr int PREFIX_OPERAND = 601;

// reserved_word, the words a record name may be.
constexpr std::array<std::string_view, 29> RESERVED_WORDS{
    "after", "and",   "andalso", "band",   "begin",   "bnot", "bor", "bsl",  "bsr", "bxor",
    "case",  "catch", "cond",    "div",    "else",    "end",  "fun", "if",   "let", "maybe",
    "not",   "of",    "or",      "orelse", "receive", "rem",  "try", "when", "xor"};
} // namespace

bool is_prefix_operator(std::string_view category) {
    return category == "+" || category == "-" || category == "bnot" || category == "not";
}

// The tokens an expression may start with (FIRST(expr)).
constexpr std::array<std::string_view, 25> EXPRESSION_STARTS{
    "catch", "+", "-", "bnot", "not", "var",   "char", "integer", "float",   "atom", "string", "sigil_prefix", "[",
    "<<",    "{", "(", "#",    "#_",  "begin", "if",   "case",    "receive", "fun",  "try",    "maybe"};

bool Parser::at_expression_start() const {
    const auto name = category();
    return std::find(EXPRESSION_STARTS.begin(), EXPRESSION_STARTS.end(), name) != EXPRESSION_STARTS.end();
}

Term Parser::expression(Mode mode, int minimum) { return continue_expression(unary(mode), mode, minimum); }

// The operators after an operand that bind at least `minimum`; nonassociative ones may not follow each other.
Term Parser::continue_expression(Operand left, Mode mode, int minimum) {
    auto term = left.term;
    for (const auto *operation = infix(mode); operation && operation->precedence >= minimum; operation = infix(mode)) {
        term = binary(term, *operation, mode);
        const auto *next = infix(mode);
        if (operation->associativity == Associativity::none && next && next->precedence == operation->precedence) {
            fail();
        }
    }
    return term;
}

// 'catch' expr, prefix_op expr, or an operand with its '#' suffixes.
Operand Parser::unary(Mode mode) {
    if (mode == Mode::expression && at("catch")) {
        const auto keyword = take();
        return {tuple({atom("catch"_atom), anno(keyword), expression()}), Shape::other};
    }
    if (is_prefix_operator(category())) {
        const auto operation = take();
        const auto operand = expression(mode, PREFIX_OPERAND);
        return {tuple({atom("op"_atom), anno(operation), category_atom(operation), operand}), Shape::other};
    }
    return mode == Mode::pattern ? primary(mode) : postfix(primary(mode));
}

const Operator *Parser::infix(Mode mode) const {
    const auto name = category();
    for (const auto &operation : OPERATORS) {
        if (operation.name == name) {
            return mode == Mode::pattern && !operation.in_patterns ? nullptr : &operation;
        }
    }
    return nullptr;
}

Term Parser::binary(const Term &left, const Operator &operation, Mode mode) {
    if (operation.build == Build::call) {
        const auto items = arguments();
        return tuple({atom("call"_atom), first_anno(left), left, list(items)});
    }
    const auto symbol = take();
    const auto right_minimum = operation.precedence + (operation.associativity == Associativity::right ? 0 : 1);
    const auto right = expression(mode, right_minimum);
    switch (operation.build) {
    case Build::match:
        return tuple({atom("match"_atom), first_anno(left), left, right});
    case Build::remote:
        return tuple({atom("remote"_atom), anno(symbol), left, right});
    default:
        return tuple({atom("op"_atom), anno(symbol), category_atom(symbol), left, right});
    }
}

// expr_max '#' ..., map_expr '#' map_tuple and record_expr '#' record_name ...: what may follow which.
Operand Parser::postfix(Operand left) {
    while (left.shape != Shape::other && (at("#") || at("#_"))) {
        if (!at("#_")) {
            take();
            left = hash_suffix(left.term, left.shape);
            continue;
        }
        if (left.shape != Shape::max) {
            fail();
        }
        const auto hash = take();
        left = field_or_update(left.term, hash, terms_.nil());
    }
    return left;
}

// After '#': a map update, a native record of a module, or a record.
Operand Parser::hash_suffix(const Term &left, Shape shape) {
    const auto hash = pos_ - 1;
    if (at("{")) {
        if (shape == Shape::record) {
            fail();
        }
        return {tuple({atom("map"_atom), anno(hash), left, map_tuple()}), Shape::map};
    }
    if (shape == Shape::map) {
        fail();
    }
    if (shape == Shape::max && at("atom") && at(":", 1)) {
        const auto module = take();
        take();
        const auto name = record_name();
        return field_or_update(left, hash, tuple({value(module), record_name_value(name)}));
    }
    return field_or_update(left, hash, record_name_value(record_name()));
}

// '.' atom or a record tuple after a record's name: its {record_field, ...} or {record, ...} node.
Operand Parser::field_or_update(const Term &left, std::size_t hash, const Term &id) {
    if (at(".")) {
        take();
        const auto field = expect("atom");
        return {tuple({atom("record_field"_atom), anno(hash), left, id, token(field)}), Shape::record};
    }
    if (!at("{")) {
        fail();
    }
    return {tuple({atom("record"_atom), anno(hash), left, id, record_tuple_fields()}), Shape::record};
}

bool Parser::at_record_name() const {
    const auto name = category();
    return name == "atom" || name == "var" ||
           std::find(RESERVED_WORDS.begin(), RESERVED_WORDS.end(), name) != RESERVED_WORDS.end();
}

// record_name -> atom | var | reserved_word: {atom, Anno, Name}.
Term Parser::record_name() {
    if (!at_record_name()) {
        fail();
    }
    const auto name = take();
    if (is_category(name, "atom")) {
        return token(name);
    }
    const auto spelling = is_category(name, "var") ? value(name) : category_atom(name);
    return tuple({atom("atom"_atom), anno(name), spelling});
}

Term Parser::record_name_value(const Term &name) const { return name.tuple_element_unchecked(2); }
} // namespace clause::runtime::builtins::front_end::parsing
