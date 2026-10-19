#include "parser.hpp"

// erl_parse's control expressions (plan 21 step LIB31): if, case, receive, try, maybe and fun, with build_try/4,
// build_fun/2 and the try clauses' {Class, Pattern, Stacktrace} tuples.
namespace clause::runtime::builtins::front_end::parsing {
namespace {
// hd(Term) of a list term.
Term head_of(const Term &list) { return list.head_unchecked(); }
} // namespace

// if_clause -> guard clause_body: {clause, first_anno(hd(hd(Guard))), [], Guard, Body}.
Term Parser::if_expression() {
    const auto keyword = take();
    std::vector<Term> clauses;
    for (;;) {
        const auto guard = this->guard();
        const auto body = clause_body();
        clauses.push_back(
            tuple({atom("clause"), first_anno(head_of(head_of(guard))), terms_.nil(), guard, list(body)}));
        if (!at(";")) {
            break;
        }
        take();
    }
    expect("end");
    return tuple({atom("if"), anno(keyword), list(clauses)});
}

Term Parser::case_expression() {
    const auto keyword = take();
    const auto subject = expression();
    expect("of");
    const auto clauses = cr_clauses();
    expect("end");
    return tuple({atom("case"), anno(keyword), subject, list(clauses)});
}

std::vector<Term> Parser::cr_clauses() {
    std::vector<Term> clauses{cr_clause()};
    while (at(";")) {
        take();
        clauses.push_back(cr_clause());
    }
    return clauses;
}

// cr_clause -> expr clause_guard clause_body: case patterns are expressions.
Term Parser::cr_clause() {
    const auto pattern = expression();
    const auto guard = clause_guard();
    const auto body = clause_body();
    return tuple({atom("clause"), first_anno(pattern), list({pattern}), guard, list(body)});
}

// receive with clauses, an after, or both.
Term Parser::receive_expression() {
    const auto keyword = take();
    std::vector<Term> clauses;
    if (!at("after")) {
        clauses = cr_clauses();
        if (!at("after")) {
            expect("end");
            return tuple({atom("receive"), anno(keyword), list(clauses)});
        }
    }
    take();
    const auto timeout = expression();
    const auto body = clause_body();
    expect("end");
    return tuple({atom("receive"), anno(keyword), list(clauses), timeout, list(body)});
}

// try_expr: a body, of clauses, then catch clauses, an after, or both.
Term Parser::try_expression() {
    const auto keyword = take();
    const auto body = exprs();
    auto of_clauses = terms_.nil();
    if (at("of")) {
        take();
        of_clauses = list(cr_clauses());
    }
    auto catch_clauses = terms_.nil();
    auto after = terms_.nil();
    if (at("catch")) {
        take();
        catch_clauses = list(try_clauses());
    } else if (!at("after")) {
        fail();
    }
    if (at("after")) {
        take();
        after = list(exprs());
    }
    expect("end");
    return tuple({atom("try"), anno(keyword), list(body), of_clauses, catch_clauses, after});
}

std::vector<Term> Parser::try_clauses() {
    std::vector<Term> clauses{try_clause()};
    while (at(";")) {
        take();
        clauses.push_back(try_clause());
    }
    return clauses;
}

// A pattern alone is a throw: {clause, A, [{tuple, A, [{atom, A, throw}, Pattern, {var, Az, '_'}]}], ...}.
Term Parser::try_clause() {
    if ((at("atom") || at("var")) && at(":", 1)) {
        return class_clause();
    }
    const auto pattern = expression(Mode::pattern);
    const auto guard = clause_guard();
    const auto body = clause_body();
    const auto first = first_anno(pattern);
    const auto stacktrace = tuple({atom("var"), last_anno(pattern), atom("_")});
    const auto exception =
        tuple({atom("tuple"), first, list({tuple({atom("atom"), first, atom("throw")}), pattern, stacktrace})});
    return tuple({atom("clause"), first, list({exception}), guard, list(body)});
}

// Class ':' pattern [':' Stacktrace]: a missing stacktrace is {var, last_anno(Pattern), '_'}.
Term Parser::class_clause() {
    const auto exception_class = take();
    take();
    const auto pattern = expression(Mode::pattern);
    std::optional<Term> stacktrace;
    if (at(":")) {
        take();
        stacktrace = token(expect("var"));
    }
    const auto guard = clause_guard();
    const auto body = clause_body();
    if (!stacktrace) {
        stacktrace = tuple({atom("var"), last_anno(pattern), atom("_")});
    }
    const auto class_anno = anno(exception_class);
    const auto exception = tuple({atom("tuple"), class_anno, list({token(exception_class), pattern, *stacktrace})});
    return tuple({atom("clause"), class_anno, list({exception}), guard, list(body)});
}

// maybe ... end, or maybe ... else cr_clauses end.
Term Parser::maybe_expression() {
    const auto keyword = take();
    const auto body = maybe_body();
    if (!at("else")) {
        expect("end");
        return tuple({atom("maybe"), anno(keyword), list(body)});
    }
    const auto otherwise = take();
    const auto clauses = cr_clauses();
    expect("end");
    return tuple({atom("maybe"), anno(keyword), list(body), tuple({atom("else"), anno(otherwise), list(clauses)})});
}

// maybe_match_exprs: expressions and Pattern ?= Expression matches.
std::vector<Term> Parser::maybe_body() {
    std::vector<Term> body;
    for (;;) {
        const auto left = expression();
        if (at("?=")) {
            const auto match = take();
            body.push_back(tuple({atom("maybe_match"), anno(match), left, expression()}));
        } else {
            body.push_back(left);
        }
        if (!at(",")) {
            return body;
        }
        take();
    }
}

// fun Name/Arity, fun Module:Name/Arity, or fun clauses end.
Term Parser::fun_expression() {
    const auto keyword = take();
    if (at("(") || (at("var") && at("(", 1))) {
        return fun_clauses(keyword);
    }
    if (!at("atom") && !at("var")) {
        fail();
    }
    if (at(":", 1)) {
        return fun_reference(keyword);
    }
    const auto name = take();
    if (!is_category(name, "atom") || !at("/")) {
        fail();
    }
    take();
    const auto arity = expect("integer");
    return tuple({atom("fun"), anno(keyword), tuple({atom("function"), value(name), value(arity)})});
}

// 'fun' atom_or_var ':' atom_or_var '/' integer_or_var: the tokens themselves.
Term Parser::fun_reference(std::size_t fun) {
    const auto module = take();
    take();
    if (!at("atom") && !at("var")) {
        fail();
    }
    const auto name = take();
    expect("/");
    if (!at("integer") && !at("var")) {
        fail();
    }
    const auto arity = take();
    return tuple({atom("fun"), anno(fun), tuple({atom("function"), token(module), token(name), token(arity)})});
}

// build_fun/2 after 'end': {'fun', A, {clauses, Cs}}, or {named_fun, A, Name, Cs}.
Term Parser::fun_clauses(std::size_t fun) {
    std::vector<Term> clauses{fun_clause()};
    while (at(";")) {
        take();
        clauses.push_back(fun_clause());
    }
    expect("end");
    const auto name = clauses.front().tuple_element_unchecked(2);
    const auto checked = list(checked_clauses(clauses));
    if (name.is_atom() && name.atom_spelling_unchecked() == "fun") {
        return tuple({atom("fun"), anno(fun), tuple({atom("clauses"), checked})});
    }
    return tuple({atom("named_fun"), anno(fun), name, checked});
}

// fun_clause -> pat_argument_list ... ('fun' and the anno of '('), or var pat_argument_list ... (the var).
Term Parser::fun_clause() {
    std::optional<std::size_t> name;
    if (at("var")) {
        name = take();
    }
    const auto [arguments, open] = pattern_arguments();
    const auto guard = clause_guard();
    const auto body = clause_body();
    const auto clause_anno = name ? anno(*name) : open;
    const auto clause_name = name ? value(*name) : atom("fun");
    return tuple({atom("clause"), clause_anno, clause_name, list(arguments), guard, list(body)});
}
} // namespace clause::runtime::builtins::front_end::parsing
