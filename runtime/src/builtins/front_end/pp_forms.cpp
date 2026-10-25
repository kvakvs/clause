#include "../text.hpp"
#include "front_end.hpp"
#include "pp.hpp"
#include <array>

// The forms, attributes and clauses of the C++ erl_pp's first pass (plan 21 step LIB34C), in erl_pp.erl's order.
namespace clause::runtime::builtins::front_end::pp {
using forms::element;
using forms::is_atom;
using forms::is_node;

namespace {
bool is_pair(const Term &term) { return term.is_tuple() && term.tuple_size_unchecked() == 2; }

bool is_proper_list(const Term &term) {
    auto rest = term;
    while (rest.is_cons()) {
        rest = rest.tail_unchecked();
    }
    return rest.is_nil();
}

// The arrow of a generator, empty for any other qualifier.
std::string_view arrow(std::string_view generator) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 6> ARROWS{{
        {"generate", " <-"},
        {"generate_strict", " <:-"},
        {"b_generate", " <="},
        {"b_generate_strict", " <:="},
        {"m_generate", " <-"},
        {"m_generate_strict", " <:-"},
    }};
    for (const auto &[name, text] : ARROWS) {
        if (name == generator) {
            return text;
        }
    }
    return {};
}
} // namespace

Index Printer::message(const Term &message, const Opts &opts) {
    const std::array args{message};
    return text(opts.unicode ? "~tp\n" : "~p\n", args);
}

Index Printer::attribute(const Term &attribute, const Opts &opts) {
    const auto kind = element(attribute, 2);
    Index body{};
    if (is_atom(kind, "type") || is_atom(kind, "opaque") || is_atom(kind, "nominal")) {
        body = type_attribute(attribute);
    } else if (is_atom(kind, "spec") || is_atom(kind, "callback")) {
        body = spec_attribute(attribute);
    } else {
        body = attribute_body(attribute, opts);
    }
    return cat({body, leaf(".\n")});
}

// attribute_body/3: the attributes printed their own way, by name, then any other as -Name(Abstract).
Index Printer::attribute_body(const Term &attribute, const Opts &opts) {
    struct Clause {
        std::string_view name;
        AttributeRule rule;
    };

    static constexpr auto CLAUSES = std::to_array<Clause>({
        {"module", &Printer::module_attribute},
        {"export", &Printer::names_attribute},
        {"import", &Printer::import_attribute},
        {"export_type", &Printer::names_attribute},
        {"optional_callbacks", &Printer::optional_callbacks},
        {"file", &Printer::file_attribute},
        {"record", &Printer::record_attribute},
        {"native_record", &Printer::record_attribute},
    });
    const auto name = element(attribute, 2);
    const auto spelling = name.is_atom() ? name.atom_spelling_unchecked() : std::string_view{};
    for (const auto &clause : CLAUSES) {
        if (clause.name != spelling) {
            continue;
        }
        if (const auto printed = (this->*clause.rule)(attribute, opts)) {
            return *printed;
        }
        break;
    }
    const std::array args{abstract(element(attribute, 3), opts)};
    return simple_attribute(name, args);
}

// {var, A, dotted(Name)}: a module name printed as a variable named by its dotted text.
Term Printer::dotted_var(const Term &name) {
    std::vector<Term> codes;
    for (const auto code : dotted(name)) {
        codes.push_back(build_.integer(code));
    }
    return build_.node("var"_atom, build_.integer(0), build_.list(codes));
}

// -module(M) and -module(M, Params).
std::optional<Index> Printer::module_attribute(const Term &attribute, const Opts & /*opts*/) {
    const auto arg = element(attribute, 3);
    const auto anno = build_.integer(0);
    std::vector<Term> args{dotted_var(is_pair(arg) ? element(arg, 0) : arg)};
    if (is_pair(arg)) {
        auto params = build_.node("nil"_atom, anno);
        const auto vars = forms::elements(element(arg, 1));
        for (auto var = vars.rbegin(); var != vars.rend(); ++var) {
            params = build_.node("cons"_atom, anno, build_.node("var"_atom, anno, *var), params);
        }
        args.push_back(params);
    }
    return simple_attribute(element(attribute, 2), args);
}

// -export([Name/Arity, ...]) and -export_type.
std::optional<Index> Printer::names_attribute(const Term &attribute, const Opts & /*opts*/) {
    return listed_attribute(element(attribute, 2), name_arities(element(attribute, 3)));
}

// -import(Module) and -import(Module, [Name/Arity, ...]).
std::optional<Index> Printer::import_attribute(const Term &attribute, const Opts & /*opts*/) {
    const auto name = element(attribute, 2);
    const auto arg = element(attribute, 3);
    if (arg.is_list()) {
        const std::array args{dotted_var(arg)};
        return simple_attribute(name, args);
    }
    if (!is_pair(arg)) {
        return std::nullopt;
    }
    return listed_attribute(name, {items_.leaf(dotted(element(arg, 0))), cat(name_arities(element(arg, 1)))});
}

// -optional_callbacks([Name/Arity, ...]); another argument prints as a term.
std::optional<Index> Printer::optional_callbacks(const Term &attribute, const Opts & /*opts*/) {
    const auto arg = element(attribute, 3);
    if (!is_proper_list(arg)) {
        return std::nullopt;
    }
    return listed_attribute(element(attribute, 2), name_arities(arg));
}

std::optional<Index> Printer::file_attribute(const Term &attribute, const Opts & /*opts*/) {
    const auto arg = element(attribute, 3);
    if (!is_pair(arg)) {
        return std::nullopt;
    }
    const std::array args{build_.node("string"_atom, build_.integer(0), element(arg, 0)),
                          build_.node("integer"_atom, build_.integer(0), element(arg, 1))};
    return simple_attribute(element(attribute, 2), args);
}

// -record(Name, {Fields}) and -record #Name{Fields}.
std::optional<Index> Printer::record_attribute(const Term &attribute, const Opts &opts) {
    const auto arg = element(attribute, 3);
    if (!is_pair(arg)) {
        return std::nullopt;
    }
    const auto name = atom(element(arg, 0));
    const auto fields = record_fields(element(arg, 1), opts);
    if (is_atom(element(attribute, 2), "record")) {
        return cat({items_.first({.before = cat({leaf("-record("), name, ch(',')}), .part = fields}), ch(')')});
    }
    return cat({items_.first({.before = cat({leaf("-record #"), name}), .part = fields})});
}

// -Name(Args).
Index Printer::simple_attribute(const Term &name, std::span<const Term> args) {
    std::vector<Index> parts;
    parts.reserve(args.size());
    for (const auto &arg : args) {
        parts.push_back(item(arg, 0, NO_HOOK));
    }
    return items_.first({.before = cat({ch('-'), atom(name)}),
                         .part = seq({.open = ch('('), .close = ch(')')}, ch(','), std::move(parts))});
}

Index Printer::listed_attribute(const Term &name, std::vector<Index> args) {
    return items_.first({.before = cat({ch('-'), atom(name)}),
                         .part = cat({seq({.open = ch('('), .close = ch(')')}, ch(','), std::move(args))})});
}

// dotted/1: the dotted parts of an old package name, or an atom, each written by io_lib:write/1.
std::u32string Printer::dotted(const Term &name) {
    auto &context = build_.context();
    if (name.is_atom()) {
        return write_text(context, name);
    }
    std::u32string out;
    const auto parts = forms::elements(name);
    for (std::size_t at = 0; at < parts.size(); ++at) {
        if (is_atom(parts[at], "")) {
            out += U'.';
            continue;
        }
        out += write_text(context, parts[at]);
        if (at + 1 < parts.size()) {
            out += U'.';
        }
    }
    if (parts.empty() || is_atom(parts.back(), "")) {
        lists::no_clause();
    }
    return out;
}

// name_arities/1: [Name/Arity, ...], or '[]'.
std::vector<Index> Printer::name_arities(const Term &pairs) {
    const auto all = forms::elements(pairs);
    if (all.empty()) {
        return {word("[]")};
    }
    std::vector<Index> parts;
    for (const auto &pair : all) {
        if (is_pair(pair)) {
            const std::array arity{element(pair, 1)};
            parts.push_back(cat({atom(element(pair, 0)), text("/~w", arity)}));
        }
    }
    const auto comma = ch(',');
    return {
        items_.seq({.before = ch('['), .after = ch(']'), .separator = comma, .last_separator = comma, .spaced = false},
                   std::move(parts))};
}

Index Printer::function(const Term &function, const Opts &opts) {
    const auto name = element(function, 2);
    std::vector<Index> parts;
    for (const auto &clause : forms::elements(element(function, 4))) {
        parts.push_back(function_clause(name, clause, opts));
    }
    return cat({items_.prefer_nl(ch(';'), false, std::move(parts)), leaf(".\n")});
}

Index Printer::function_clause(const Term &name, const Term &clause, const Opts &opts) {
    if (!is_node(clause, "clause", 5)) {
        lists::no_clause();
    }
    const auto head = call(atom(name), element(clause, 2), 0, opts);
    return items_.step(Kind::step, guarded(head, element(clause, 3), opts), body(element(clause, 4), opts));
}

Index Printer::guarded(Index before, const Term &guard, const Opts &opts) {
    return cat({items_.list({items_.step(Kind::step, before, guard_items(guard, opts))}), word(" ->")});
}

Index Printer::guard(const Term &guard, const Opts &opts) { return guard_items(guard, opts); }

// guard_items/2: when G1; G2, a list of tests standing for one conjunction.
Index Printer::guard_items(const Term &guard, const Opts &opts) {
    if (guard.is_nil()) {
        return items_.empty();
    }
    if (!guard.is_cons()) {
        lists::no_clause();
    }
    std::vector<Index> parts;
    if (guard.head_unchecked().is_list()) {
        for (const auto &tests : forms::elements(guard)) {
            parts.push_back(conjunction(tests, opts));
        }
    } else {
        parts.push_back(conjunction(guard, opts));
    }
    return items_.list({items_.step(Kind::step, word("when"), separated(listed(ch(';')), true, std::move(parts)))});
}

Index Printer::conjunction(const Term &tests, const Opts &opts) {
    return separated(listed(ch(',')), true, items(tests, opts));
}

Index Printer::body(const Term &exprs, const Opts &opts) {
    if (exprs.is_cons() && exprs.tail_unchecked().is_nil()) {
        return item(exprs.head_unchecked(), 0, opts);
    }
    return items_.prefer_nl(listed(ch(',')), true, items(exprs, opts));
}

Index Printer::clauses(ClauseRule rule, const Term &clauses, const Opts &opts) {
    std::vector<Index> parts;
    for (const auto &clause : forms::elements(clauses)) {
        parts.push_back((this->*rule)(clause, opts));
    }
    return items_.prefer_nl(listed(ch(';')), true, std::move(parts));
}

Index Printer::if_clause(const Term &clause, const Opts &opts) {
    if (!is_node(clause, "clause", 5) || !element(clause, 2).is_nil()) {
        lists::no_clause();
    }
    return items_.step(Kind::step, cat({if_guard(element(clause, 3), opts), word(" ->")}),
                       body(element(clause, 4), opts));
}

Index Printer::if_guard(const Term &guard, const Opts &opts) {
    if (guard.is_nil()) {
        return leaf("true");
    }
    if (!guard.is_cons()) {
        lists::no_clause();
    }
    std::vector<Index> parts;
    if (guard.head_unchecked().is_list()) {
        for (const auto &tests : forms::elements(guard)) {
            parts.push_back(conjunction(tests, opts));
        }
    } else {
        parts.push_back(conjunction(guard, opts));
    }
    return separated(ch(';'), false, std::move(parts));
}

Index Printer::case_clause(const Term &clause, const Opts &opts) {
    const auto patterns = is_node(clause, "clause", 5) ? forms::elements(element(clause, 2)) : std::vector<Term>{};
    if (patterns.size() != 1) {
        lists::no_clause();
    }
    return items_.step(Kind::step, guarded(item(patterns[0], 0, opts), element(clause, 3), opts),
                       body(element(clause, 4), opts));
}

// Class:Reason:Stack ->, the stack left out when it is _.
Index Printer::catch_clause(const Term &clause, const Opts &opts) {
    const auto patterns = is_node(clause, "clause", 5) ? forms::elements(element(clause, 2)) : std::vector<Term>{};
    if (patterns.size() != 1 || !is_node(patterns[0], "tuple", 3)) {
        lists::no_clause();
    }
    const auto parts = forms::elements(element(patterns[0], 2));
    if (parts.size() != 3) {
        lists::no_clause();
    }
    std::vector<Index> pattern{item(parts[0], 0, opts), ch(':'), item(parts[1], 0, opts)};
    if (!is_node(parts[2], "var", 3) || !is_atom(element(parts[2], 2), "_")) {
        pattern.push_back(ch(':'));
        pattern.push_back(item(parts[2], 0, opts));
    }
    return items_.step(Kind::step, guarded(cat(std::move(pattern)), element(clause, 3), opts),
                       body(element(clause, 4), opts));
}

Index Printer::cond_clause(const Term &clause, const Opts &opts) {
    const auto guard = is_node(clause, "clause", 5) ? forms::elements(element(clause, 3)) : std::vector<Term>{};
    const auto tests = guard.size() == 1 ? forms::elements(guard[0]) : std::vector<Term>{};
    if (tests.size() != 1 || !element(clause, 2).is_nil()) {
        lists::no_clause();
    }
    return items_.step(Kind::step, cat({item(tests[0], 0, opts), word(" ->")}), body(element(clause, 4), opts));
}

// fun_clauses/3: a named fun's clauses start with its name; a fun named none prints as an unnamed one.
Index Printer::fun_clauses(const FunClauses &fun, const Opts &opts) {
    const auto &name = fun.name;
    const auto named = name.is_atom() && !is_atom(name, "none");
    std::vector<Index> parts;
    for (const auto &clause : forms::elements(fun.clauses)) {
        if (!is_node(clause, "clause", 5)) {
            lists::no_clause();
        }
        auto head = guarded(arguments(element(clause, 2), opts), element(clause, 3), opts);
        if (named) {
            head = cat({items_.codes(code_points(name.atom_spelling_unchecked())), head});
        }
        parts.push_back(items_.step(Kind::step, head, body(element(clause, 4), opts)));
    }
    return items_.prefer_nl(listed(ch(';')), true, std::move(parts));
}

Index Printer::qualifiers(const Term &qualifiers, const Opts &opts) {
    std::vector<Index> parts;
    for (const auto &qualifier_node : forms::elements(qualifiers)) {
        parts.push_back(qualifier(qualifier_node, opts));
    }
    return items_.prefer_nl(listed(ch(',')), true, std::move(parts));
}

// qualifier/2: zipped generators, generators and filters.
Index Printer::qualifier(const Term &qualifier_node, const Opts &opts) {
    if (is_node(qualifier_node, "zip", 3)) {
        std::vector<Index> parts;
        for (const auto &inner : forms::elements(element(qualifier_node, 2))) {
            parts.push_back(qualifier(inner, opts));
        }
        return items_.prefer_nl(listed(items_.codes(U"&&")), true, std::move(parts));
    }
    const auto tag = forms::tag_of(qualifier_node);
    const auto generator_arrow =
        qualifier_node.is_tuple() && qualifier_node.tuple_size_unchecked() == 4 ? arrow(tag) : std::string_view{};
    if (generator_arrow.empty()) {
        return item(qualifier_node, 0, opts);
    }
    const auto pattern = element(qualifier_node, 2);
    const auto head = tag.starts_with("m_") ? map_field(pattern, opts) : item(pattern, 0, opts);
    return items_.list(
        {items_.step(Kind::step, cat({head, leaf(generator_arrow)}), item(element(qualifier_node, 3), 0, opts))});
}
} // namespace clause::runtime::builtins::front_end::pp
