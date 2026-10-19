#include "parser.hpp"
#include "../lists/list_walk.hpp"
#include "abstract.hpp"
#include "front_end.hpp"

// erl_parse's parser (plan 21 step LIB31): the token cursor, the terms it builds, forms, functions and clauses.
namespace clause::runtime::builtins::front_end::parsing {
namespace {
// The category of an element that is no tuple: reading it is element/2's badarg.
constexpr std::string_view NOT_A_TOKEN = "\x01";
// The category past the last token.
constexpr std::string_view END = "$end";

std::string_view category_of(const Term &token) {
    if (!token.is_tuple()) {
        return NOT_A_TOKEN;
    }
    if (token.tuple_size_unchecked() == 0) {
        return NOT_A_TOKEN;
    }
    const auto first = token.tuple_element_unchecked(0);
    return first.is_atom() ? first.atom_spelling_unchecked() : std::string_view{};
}

// A tuple's element; element/2's badarg when it has none at `index`.
Term element_of(const Term &tuple, std::size_t index) {
    if (!tuple.is_tuple() || tuple.tuple_size_unchecked() <= index) {
        bad_argument();
    }
    return tuple.tuple_element_unchecked(index);
}

std::size_t length_of(const Term &list) {
    std::size_t length = 0;
    for (auto rest = list; rest.is_cons(); rest = rest.tail_unchecked()) {
        ++length;
    }
    return length;
}
} // namespace

Parser::Parser(ProcessContext &context, const Term &tokens) : context_(context), terms_(context) {
    auto rest = tokens;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        tokens_.push_back(rest.head_unchecked());
        categories_.push_back(category_of(rest.head_unchecked()));
    }
    improper_ = !rest.is_nil();
}

std::string_view Parser::category(std::size_t ahead) const {
    const auto index = pos_ + ahead;
    if (index >= tokens_.size()) {
        if (improper_) {
            lists::no_clause();
        }
        return END;
    }
    if (categories_[index] == NOT_A_TOKEN) {
        bad_argument();
    }
    return categories_[index];
}

bool Parser::at(std::string_view category, std::size_t ahead) const { return this->category(ahead) == category; }

bool Parser::is_category(std::size_t index, std::string_view category) const { return categories_[index] == category; }

std::size_t Parser::take() {
    if (category() == END) {
        fail();
    }
    return pos_++;
}

std::size_t Parser::expect(std::string_view category) {
    if (!at(category)) {
        fail();
    }
    return pos_++;
}

void Parser::fail() const { throw SyntaxError{pos_}; }

Term Parser::token(std::size_t index) const { return tokens_[index]; }

Term Parser::anno(std::size_t index) const { return element_of(tokens_[index], 1); }

Term Parser::value(std::size_t index) const { return element_of(tokens_[index], 2); }

Term Parser::atom(const AtomName name) const { return terms_.atom(name); }

Term Parser::tuple(std::initializer_list<Term> items) const {
    return terms_.tuple(std::span<const Term>(items.begin(), items.size()));
}

Term Parser::list(const std::vector<Term> &items) const { return terms_.list(items); }

Term Parser::first_anno(const Term &node) const { return abstract::first_anno(context_, node); }

Term Parser::last_anno(const Term &node) const { return abstract::last_anno(context_, node); }

Term Parser::location(const Term &anno) const { return anno_location(context_, anno); }

Term Parser::category_atom(std::size_t index) const { return element_of(tokens_[index], 0); }

void Parser::error_at(const Term &location, std::string_view message) const {
    std::vector<Term> chars;
    for (const auto code : message) {
        chars.push_back(terms_.integer(static_cast<unsigned char>(code)));
    }
    throw ActionError{location, terms_.list(chars)};
}

void Parser::error_at_node(const Term &node, std::string_view message) const {
    error_at(abstract::first_location(context_, node), message);
}

void Parser::bad_declaration(const Term &node, AtomName what) const {
    throw FormatError{abstract::first_location(context_, node), "bad ~tw declaration", {atom(what)}};
}

Term Parser::form() {
    Term result;
    if (at("-")) {
        result = attribute();
    } else if (at("atom")) {
        result = function();
    } else {
        fail();
    }
    expect("dot");
    if (!at(END)) {
        fail();
    }
    return result;
}

// build_function/1: OTP reduces the clauses before it reads the dot, so a head mismatch wins over what follows.
Term Parser::function() {
    std::vector<Term> clauses{function_clause()};
    while (at(";")) {
        take();
        clauses.push_back(function_clause());
    }
    const auto &first = clauses.front();
    const auto arity = length_of(first.tuple_element_unchecked(3));
    return tuple({atom("function"_atom), first.tuple_element_unchecked(1), first.tuple_element_unchecked(2),
                  terms_.integer(static_cast<std::int64_t>(arity)), list(checked_clauses(clauses))});
}

// {clause, Anno, Name, Args, Guard, Body}: the name stays until check_clauses/3 drops it.
Term Parser::function_clause() {
    const auto name = expect("atom");
    const auto arguments = pattern_arguments().first;
    const auto guard = clause_guard();
    const auto body = clause_body();
    return tuple({atom("clause"_atom), anno(name), value(name), list(arguments), guard, list(body)});
}

std::vector<Term> Parser::checked_clauses(const std::vector<Term> &clauses) const {
    const auto name = clauses.front().tuple_element_unchecked(2);
    const auto arity = length_of(clauses.front().tuple_element_unchecked(3));
    std::vector<Term> checked;
    for (const auto &clause : clauses) {
        const auto same_name = need(clause.tuple_element_unchecked(2).exactly_equal(name));
        if (!same_name || length_of(clause.tuple_element_unchecked(3)) != arity) {
            head_mismatch(clause, arity, name);
        }
        checked.push_back(
            tuple({atom("clause"_atom), clause.tuple_element_unchecked(1), clause.tuple_element_unchecked(3),
                   clause.tuple_element_unchecked(4), clause.tuple_element_unchecked(5)}));
    }
    return checked;
}

void Parser::head_mismatch(const Term &clause, std::size_t arity, const Term &name) const {
    const auto other = clause.tuple_element_unchecked(2);
    const auto where = location(clause.tuple_element_unchecked(1));
    const auto expected = terms_.integer(static_cast<std::int64_t>(arity));
    const auto found = terms_.integer(static_cast<std::int64_t>(length_of(clause.tuple_element_unchecked(3))));
    if (need(other.exactly_equal(name))) {
        throw FormatError{where,
                          "head mismatch: function ~s with arities ~w and ~w is regarded as two distinct functions. "
                          "Is the number of arguments incorrect or is the semicolon in ~s/~w unwanted?",
                          {name, expected, found, name, expected}};
    }
    throw FormatError{where,
                      "head mismatch: previous function ~s/~w is distinct from ~s/~w. Is the semicolon in ~s/~w "
                      "unwanted?",
                      {name, expected, other, found, name, expected}};
}

std::pair<std::vector<Term>, Term> Parser::pattern_arguments() {
    const auto open = expect("(");
    std::vector<Term> patterns;
    if (!at(")")) {
        patterns.push_back(expression(Mode::pattern));
        while (at(",")) {
            take();
            patterns.push_back(expression(Mode::pattern));
        }
    }
    expect(")");
    return {std::move(patterns), anno(open)};
}

std::vector<Term> Parser::arguments() {
    expect("(");
    std::vector<Term> items;
    if (!at(")")) {
        items = exprs();
    }
    expect(")");
    return items;
}

Term Parser::clause_guard() {
    if (!at("when")) {
        return terms_.nil();
    }
    take();
    return guard();
}

// guard -> exprs | exprs ';' guard.
Term Parser::guard() {
    std::vector<Term> sequences{list(exprs())};
    while (at(";")) {
        take();
        sequences.push_back(list(exprs()));
    }
    return list(sequences);
}

std::vector<Term> Parser::clause_body() {
    expect("->");
    return exprs();
}

std::vector<Term> Parser::exprs() {
    std::vector<Term> items{expression()};
    while (at(",")) {
        take();
        items.push_back(expression());
    }
    return items;
}
} // namespace clause::runtime::builtins::front_end::parsing
