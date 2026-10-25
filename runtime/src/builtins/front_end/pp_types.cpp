#include "front_end.hpp"
#include "pp.hpp"
#include <array>

// The types and specifications of the C++ erl_pp's first pass (plan 21 step LIB34C): type_item/2 and its helpers,
// in erl_pp.erl's order.
namespace clause::runtime::builtins::front_end::pp {
using forms::element;
using forms::is_atom;
using forms::is_node;

namespace {
bool is_zero(const Term &node) { return is_node(node, "integer", 3) && string::small_of(element(node, 2)) == 0; }

// The arguments of a {type, _, Name, Args} node when it has exactly `count`.
std::optional<std::vector<Term>> arguments_of(const Term &node, std::size_t count) {
    const auto args = element(node, 3);
    if (!args.is_list()) {
        return std::nullopt;
    }
    auto parts = forms::elements(args);
    return parts.size() == count ? std::optional{std::move(parts)} : std::nullopt;
}
} // namespace

// -type Name(Vars) :: Type.
Index Printer::type_attribute(const Term &attribute) {
    const auto kind = element(attribute, 2);
    const auto type = element(attribute, 3);
    if (!type.is_tuple() || type.tuple_size_unchecked() != 3) {
        lists::no_clause();
    }
    const auto keyword = "-" + std::string(kind.atom_spelling_unchecked()) + " ";
    const auto head = call(atom(element(type, 0)), element(type, 2), 0, NO_HOOK);
    return items_.first({.before = leaf(keyword), .part = typed(head, element(type, 1))});
}

// -spec Name Signatures; and -callback.
Index Printer::spec_attribute(const Term &attribute) {
    const auto kind = element(attribute, 2);
    const auto spec = element(attribute, 3);
    if (!spec.is_tuple() || spec.tuple_size_unchecked() != 2 || !element(spec, 0).is_tuple()) {
        lists::no_clause();
    }
    const auto function = element(spec, 0);
    Index name{};
    if (function.tuple_size_unchecked() == 2) {
        name = atom(element(function, 0));
    } else if (function.tuple_size_unchecked() == 3) {
        name = cat({atom(element(function, 0)), ch(':'), atom(element(function, 1))});
    } else {
        lists::no_clause();
    }
    std::vector<Index> signatures;
    for (const auto &type : forms::elements(element(spec, 1))) {
        signatures.push_back(signature(type));
    }
    const auto keyword = "-" + std::string(kind.atom_spelling_unchecked()) + " ";
    return items_.first(
        {.before = leaf(keyword),
         .part = items_.list({items_.first(
             {.before = name, .part = items_.prefer_nl(listed(ch(';')), true, std::move(signatures))})})});
}

Index Printer::type_item(const Term &type, std::int64_t precedence) {
    if (is_node(type, "ann_type", 3)) {
        return ann_type(type, precedence);
    }
    if (is_node(type, "paren_type", 3)) {
        const auto inner = forms::elements(element(type, 2));
        if (inner.size() == 1) {
            return type_item(inner[0], precedence);
        }
    }
    if (is_node(type, "type", 4)) {
        return builtin_type(type, precedence);
    }
    if (is_node(type, "user_type", 4)) {
        return named_type(item(build_.node("atom"_atom, element(type, 1), element(type, 2)), 0, NO_HOOK),
                          element(type, 3));
    }
    if (is_node(type, "remote_type", 3)) {
        const auto parts = forms::elements(element(type, 2));
        if (parts.size() == 3) {
            return named_type(remote_items(parts[0], parts[1], 0, NO_HOOK), parts[2]);
        }
    }
    if (is_node(type, "atom", 3)) {
        return items_.of_term(Kind::singleton, element(type, 2));
    }
    return item(type, precedence, NO_HOOK);
}

Index Printer::ann_type(const Term &node, std::int64_t precedence) {
    const auto parts = forms::elements(element(node, 2));
    if (parts.size() != 2) {
        return item(node, precedence, NO_HOOK);
    }
    const auto prec = type_inop_precedence("::");
    return paren(prec.precedence, precedence,
                 items_.list({items_.step(Kind::cstep, cat({type_item(parts[0], prec.left), word(" ::")}),
                                          type_item(parts[1], prec.right))}));
}

std::optional<Index> Printer::union_type(const Term &node, std::int64_t precedence) {
    const auto prec = type_inop_precedence("|");
    std::vector<Index> parts;
    for (const auto &type : forms::elements(element(node, 3))) {
        parts.push_back(type_item(type, prec.right));
    }
    return paren(prec.precedence, precedence, separated(listed(word(" |")), true, std::move(parts)));
}

// The {type, _, Name, Args} clauses of type_item/2, by name; a clause whose arguments do not match leaves the type
// to the last one, a named type.
Index Printer::builtin_type(const Term &node, std::int64_t precedence) {
    struct Clause {
        std::string_view name;
        TypeRule rule;
    };

    static constexpr auto CLAUSES = std::to_array<Clause>({
        {"union", &Printer::union_type},
        {"list", &Printer::list_type},
        {"nonempty_list", &Printer::nonempty_list_type},
        {"nil", &Printer::nil_type},
        {"map", &Printer::map_type},
        {"tuple", &Printer::tuple_type},
        {"record", &Printer::record_type},
        {"range", &Printer::range_type},
        {"binary", &Printer::binary_type_item},
        {"fun", &Printer::fun_type_item},
    });
    const auto name = element(node, 2);
    const auto spelling = name.is_atom() ? name.atom_spelling_unchecked() : std::string_view{};
    for (const auto &clause : CLAUSES) {
        if (clause.name != spelling) {
            continue;
        }
        if (const auto printed = (this->*clause.rule)(node, precedence)) {
            return *printed;
        }
        break;
    }
    return named_type(atom(name), element(node, 3));
}

std::optional<Index> Printer::list_type(const Term &node, std::int64_t /*precedence*/) {
    const auto one = arguments_of(node, 1);
    if (!one) {
        return std::nullopt;
    }
    const auto comma = ch(',');
    return items_.seq(
        {.before = ch('['), .after = ch(']'), .separator = comma, .last_separator = comma, .spaced = false},
        {type_item((*one)[0], 0)});
}

std::optional<Index> Printer::nonempty_list_type(const Term &node, std::int64_t /*precedence*/) {
    const auto one = arguments_of(node, 1);
    if (!one) {
        return std::nullopt;
    }
    return seq({.open = ch('['), .close = ch(']')}, ch(','), {type_item((*one)[0], 0), leaf("...")});
}

std::optional<Index> Printer::nil_type(const Term &node, std::int64_t /*precedence*/) {
    return element(node, 3).is_nil() ? std::optional{word("[]")} : std::nullopt;
}

std::optional<Index> Printer::tuple_type(const Term &node, std::int64_t /*precedence*/) {
    const auto types = element(node, 3);
    if (is_atom(types, "any")) {
        return named_type(atom(element(node, 2)), build_.nil());
    }
    std::vector<Index> parts;
    for (const auto &type : forms::elements(types)) {
        parts.push_back(type_item(type, 0));
    }
    return parts.empty() ? leaf("{}") : seq({.open = ch('{'), .close = ch('}')}, ch(','), std::move(parts));
}

std::optional<Index> Printer::map_type(const Term &node, std::int64_t precedence) {
    const auto pairs_term = element(node, 3);
    if (is_atom(pairs_term, "any")) {
        return named_type(atom(element(node, 2)), build_.nil());
    }
    const auto prec = type_preop_precedence("#");
    std::vector<Index> pairs;
    for (const auto &pair : forms::elements(pairs_term)) {
        pairs.push_back(map_pair_type(pair, 0));
    }
    const auto fields =
        pairs.empty() ? leaf("{}") : seq({.open = ch('{'), .close = ch('}')}, ch(','), std::move(pairs));
    return paren(prec.precedence, precedence, items_.first({.before = cat({ch('#')}), .part = fields}));
}

std::optional<Index> Printer::binary_type_item(const Term &node, std::int64_t /*precedence*/) {
    const auto bounds = arguments_of(node, 2);
    if (!bounds) {
        return std::nullopt;
    }
    return binary_type((*bounds)[0], (*bounds)[1]);
}

// #name{Fields} as a type; none when the arguments do not start with the record's name.
std::optional<Index> Printer::record_type(const Term &node, std::int64_t precedence) {
    const auto args = element(node, 3);
    if (!args.is_cons() || !is_node(args.head_unchecked(), "atom", 3)) {
        return std::nullopt;
    }
    const auto prec = type_preop_precedence("#");
    std::vector<Index> fields;
    for (const auto &field : forms::elements(args.tail_unchecked())) {
        fields.push_back(field_type(field));
    }
    const auto tuple =
        fields.empty() ? leaf("{}") : seq({.open = ch('{'), .close = ch('}')}, ch(','), std::move(fields));
    return paren(prec.precedence, precedence,
                 items_.first({.before = cat({cat(record_name(element(args.head_unchecked(), 2)))}), .part = tuple}));
}

std::optional<Index> Printer::range_type(const Term &node, std::int64_t precedence) {
    if (!arguments_of(node, 2)) {
        return std::nullopt;
    }
    const auto prec = type_inop_precedence("..");
    std::vector<Index> bounds;
    for (const auto &bound : forms::elements(element(node, 3))) {
        bounds.push_back(item(bound, prec.right, NO_HOOK));
    }
    return paren(prec.precedence, precedence, separated(word(".."), false, std::move(bounds)));
}

// fun() and fun((Args) -> Result); none for any other fun type.
std::optional<Index> Printer::fun_type_item(const Term &node, std::int64_t /*precedence*/) {
    const auto args = element(node, 3);
    if (args.is_nil()) {
        return leaf("fun()");
    }
    const auto parts = arguments_of(node, 2);
    if (!parts) {
        return std::nullopt;
    }
    const auto &domain = (*parts)[0];
    const auto any_or_product =
        is_node(domain, "type", 3) && (is_atom(element(domain, 2), "any") || is_atom(element(domain, 2), "product"));
    if (!any_or_product && !(is_node(domain, "type", 4) && is_atom(element(domain, 2), "product"))) {
        return std::nullopt;
    }
    return cat({fun_type(cat({word("fun"), ch('(')}), node), ch(')')});
}

// <<_:Base, _:_*Unit>>, each part left out when 0.
Index Printer::binary_type(const Term &base, const Term &unit) {
    std::vector<Index> parts;
    if (!is_zero(base)) {
        parts.push_back(cat({leaf("_:"), item(base, MAX_PRECEDENCE, NO_HOOK)}));
    }
    if (!is_zero(unit)) {
        parts.push_back(cat({leaf("_:_*"), item(unit, MAX_PRECEDENCE, NO_HOOK)}));
    }
    if (parts.empty()) {
        return leaf("<<>>");
    }
    return seq({.open = word("<<"), .close = word(">>")}, ch(','), std::move(parts));
}

Index Printer::map_pair_type(const Term &pair, std::int64_t precedence) {
    const auto assoc = is_node(pair, "type", 4) && is_atom(element(pair, 2), "map_field_assoc");
    const auto exact = is_node(pair, "type", 4) && is_atom(element(pair, 2), "map_field_exact");
    const auto parts = assoc || exact ? forms::elements(element(pair, 3)) : std::vector<Term>{};
    if (parts.size() != 2) {
        lists::no_clause();
    }
    return items_.list({items_.step(Kind::cstep, cat({type_item(parts[0], precedence), leaf(assoc ? " =>" : " :=")}),
                                    type_item(parts[1], precedence))});
}

Index Printer::field_type(const Term &field) {
    const auto parts = is_node(field, "type", 4) && is_atom(element(field, 2), "field_type")
                           ? forms::elements(element(field, 3))
                           : std::vector<Term>{};
    if (parts.size() != 2) {
        lists::no_clause();
    }
    return typed(item(parts[0], 0, NO_HOOK), parts[1]);
}

Index Printer::typed(Index before, const Term &type) {
    return items_.list({items_.step(Kind::cstep, cat({before, word(" ::")}), type_item(type, 0))});
}

Index Printer::signature(const Term &type) {
    if (is_node(type, "type", 4) && is_atom(element(type, 2), "bounded_fun")) {
        const auto parts = forms::elements(element(type, 3));
        if (parts.size() != 2) {
            lists::no_clause();
        }
        std::vector<Index> constraints;
        for (const auto &bound : forms::elements(parts[1])) {
            constraints.push_back(constraint(bound));
        }
        const auto guard = items_.list(
            {items_.step(Kind::step, word("when"), separated(listed(ch(',')), true, std::move(constraints)))});
        return items_.list({items_.step(Kind::step, fun_type(std::nullopt, parts[0]), guard)});
    }
    return fun_type(std::nullopt, type);
}

Index Printer::constraint(const Term &bound) {
    const auto parts = is_node(bound, "type", 4) && is_atom(element(bound, 2), "constraint")
                           ? forms::elements(element(bound, 3))
                           : std::vector<Term>{};
    if (parts.size() != 2) {
        lists::no_clause();
    }
    const auto &tag = parts[0];
    if (is_node(tag, "atom", 3) && is_atom(element(tag, 2), "is_subtype") && parts[1].is_list()) {
        const auto subtype = forms::elements(parts[1]);
        if (subtype.size() == 2 && is_node(subtype[0], "var", 3)) {
            return typed(item(subtype[0], 0, NO_HOOK), subtype[1]);
        }
    }
    return named_type(item(tag, 0, NO_HOOK), parts[1]);
}

// fun_type/2: (Args) -> Result after `before`.
Index Printer::fun_type(std::optional<Index> before, const Term &fun) {
    const auto parts = is_node(fun, "type", 4) && is_atom(element(fun, 2), "fun") ? forms::elements(element(fun, 3))
                                                                                  : std::vector<Term>{};
    if (parts.size() != 2) {
        lists::no_clause();
    }
    const auto &args = parts[0];
    Index arguments{};
    if (is_node(args, "type", 3) && is_atom(element(args, 2), "any")) {
        arguments = leaf("(...)");
    } else if (is_node(args, "type", 4) && is_atom(element(args, 2), "product")) {
        arguments = type_list(element(args, 3));
    } else {
        lists::no_clause();
    }
    return items_.first({.before = before.value_or(items_.empty()),
                         .part = items_.step(Kind::step, cat({arguments, word(" ->")}), type_item(parts[1], 0))});
}

Index Printer::named_type(Index name, const Term &types) {
    return items_.first({.before = name, .part = type_list(types)});
}

Index Printer::type_list(const Term &types) {
    std::vector<Index> parts;
    for (const auto &type : forms::elements(types)) {
        parts.push_back(type_item(type, 0));
    }
    return seq({.open = ch('('), .close = ch(')')}, ch(','), std::move(parts));
}
} // namespace clause::runtime::builtins::front_end::pp
