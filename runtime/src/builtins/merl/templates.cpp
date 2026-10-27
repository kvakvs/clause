#include "../text.hpp"
#include "merl.hpp"
#include <variant>

// merl's templates as builtins (plan 21 LIB77E5): template/1 finds the metavariables of trees (a leading underscore
// lifts one to its parent node), tree/1 turns templates back into trees, subst/2, tsubst/2 and alpha/2 replace or
// rename metavariables, template_vars/1 lists them and meta_template/1 builds the trees that construct a template.
namespace clause::runtime::builtins::merl {
namespace {
using Name = std::u32string;

// A metavariable name still to be lifted to an enclosing node ("v_..." or "n0...").
struct Pending {
    Name name;
};

// template_1/1's answers: no metavariable below (false), a lift in progress, or a template or metavariable term.
struct Unchanged {};

using Found = std::variant<Unchanged, Pending, Term>;
// template_2/3's answers: also the groups with their templates.
using GroupsFound = std::variant<Unchanged, Pending, Term, std::vector<Items>>;
using GroupFound = std::variant<Unchanged, Pending, Items>;

bool starts(const Name &name, std::u32string_view prefix) { return name.starts_with(prefix); }

// The characters of a list term.
Name codes_of(const Term &chars) {
    Name codes;
    for (const auto &code : syntax::elements(chars)) {
        codes.push_back(char32_t(small(code.word()).value_or(0)));
    }
    return codes;
}

Name spelling_codes(const Term &atom) { return code_points(atom.atom_spelling_unchecked()); }

// The "v" name after `prefix` in a metavariable's text, which needs more than the prefix ("@" or "_@").
std::optional<Name> named_after(const Name &text, std::u32string_view prefix) {
    if (!starts(text, prefix) || text.size() == prefix.size()) {
        return std::nullopt;
    }
    return U"v" + text.substr(prefix.size());
}

// The "n" name of an integer metavariable 909N (at least 9090).
std::optional<Name> integer_metavar(ProcessContext &context, const Term &tree) {
    const auto value = syntax::get(context, tree, "integer_value");
    if (collections::compare(value, Trees(context).integer(9090)) < 0) {
        return std::nullopt;
    }
    const auto digits = code_points(need(value.integer_decimal()));
    return digits.starts_with(U"909") ? std::optional{U"n" + digits.substr(3)} : std::nullopt;
}

// metavar/1: the name ("v..." or "n...") of a metavariable leaf: '@Name', _@Name, 909N or "'@Name".
std::optional<Name> metavar_of(ProcessContext &context, const Term &tree) {
    const auto type = syntax::type_of(context, tree);
    if (type == "atom") {
        return named_after(spelling_codes(syntax::get(context, tree, "atom_value")), U"@");
    }
    if (type == "variable") {
        const auto name = syntax::get(context, tree, "variable_name");
        return named_after(name.is_atom() ? spelling_codes(name) : codes_of(name), U"_@");
    }
    if (type == "string") {
        const auto text = codes_of(syntax::get(context, tree, "string_value"));
        return starts(text, U"'@") ? std::optional{U"v" + text.substr(2)} : std::nullopt;
    }
    return type == "integer" ? integer_metavar(context, tree) : std::nullopt;
}

// The id of a metavariable name: an atom ("v...") or an integer ("n..."); badarg as list_to_integer/1 raises.
Term id_of(ProcessContext &context, std::u32string_view text, bool atom) {
    if (atom) {
        return Trees(context).atom_of(utf8(text));
    }
    auto integer = TermFactory(context).integer_decimal(utf8(text));
    if (!integer) {
        bad_argument();
    }
    return *integer;
}

// The metavariable a complete name stands for: {Id}, {'*', Id} for "v@"/"n9" names, or the lift of "v_"/"n0" ones.
std::variant<Pending, Term> classify(ProcessContext &context, const Name &name) {
    const Trees trees(context);
    const auto atom = name.front() == U'v';
    const auto longer = name.size() > 2;
    if (longer && (starts(name, U"v_") || starts(name, U"n0"))) {
        return Pending{name};
    }
    if (longer && (starts(name, U"v@") || starts(name, U"n9"))) {
        return trees.tuple(std::array{trees.atom("*"_atom), id_of(context, std::u32string_view(name).substr(2), atom)});
    }
    return trees.tuple(std::array{id_of(context, std::u32string_view(name).substr(1), atom)});
}

Found template_node(ProcessContext &context, const Term &tree);

// template_3/3: a group's templates; a lift in a subtree moves up one level ("v_X" becomes "vX").
GroupFound template_group(ProcessContext &context, const Items &group) {
    Items found;
    bool changed = false;
    for (const auto &tree : group) {
        auto answer = template_node(context, tree);
        if (const auto *lift = std::get_if<Pending>(&answer)) {
            return Pending{lift->name.substr(0, 1) + lift->name.substr(2)};
        }
        const auto *term = std::get_if<Term>(&answer);
        changed = changed || term != nullptr;
        found.push_back(term != nullptr ? *term : tree);
    }
    return changed ? GroupFound{std::move(found)} : GroupFound{Unchanged{}};
}

// template_2/3: the groups' templates; a lift that reaches this node makes it the metavariable.
GroupsFound template_groups(ProcessContext &context, const std::vector<Items> &groups) {
    std::vector<Items> found;
    bool changed = false;
    for (const auto &group : groups) {
        auto answer = template_group(context, group);
        if (const auto *lift = std::get_if<Pending>(&answer)) {
            return std::visit([](auto &&made) { return GroupsFound{made}; }, classify(context, lift->name));
        }
        const auto *items = std::get_if<Items>(&answer);
        changed = changed || items != nullptr;
        found.push_back(items != nullptr ? *items : group);
    }
    return changed ? GroupsFound{std::move(found)} : GroupsFound{Unchanged{}};
}

// {template, Type, Attrs, Groups} of a node and its groups' templates.
Term template_term(ProcessContext &context, const Term &tree, const std::vector<Items> &groups) {
    const Trees trees(context);
    Items lists;
    for (const auto &group : groups) {
        lists.push_back(trees.list(group));
    }
    return trees.tuple(std::array{trees.atom("template"_atom), node_type(context, tree),
                                  syntax::get_attrs(context, tree), trees.list(lists)});
}

// template_1/1.
Found template_node(ProcessContext &context, const Term &tree) {
    const auto groups = node_groups(context, tree);
    if (groups.empty()) {
        const auto name = metavar_of(context, tree);
        if (!name) {
            return Unchanged{};
        }
        return std::visit([](auto &&made) { return Found{made}; }, classify(context, *name));
    }
    auto answer = template_groups(context, groups);
    if (auto *found = std::get_if<std::vector<Items>>(&answer)) {
        return template_term(context, tree, *found);
    }
    return std::visit(
        [](auto &&made) -> Found {
            using Made = std::decay_t<decltype(made)>;
            if constexpr (std::is_same_v<Made, std::vector<Items>>) {
                return Unchanged{};
            } else {
                return made;
            }
        },
        std::move(answer));
}

// Apply `one` to a term or to each element of a list (merl's functions over a tree or trees).
template <typename One> Term each(ProcessContext &context, const Term &input, One one) {
    if (!input.is_list()) {
        return one(input);
    }
    Items mapped;
    for (const auto &item : syntax::elements(input)) {
        mapped.push_back(one(item));
    }
    return Trees(context).list(mapped);
}

// The groups of a template, each item mapped by `one` and the results flattened (lists:flatten/1).
template <typename One> std::vector<Items> map_groups(const Term &groups, One one) {
    std::vector<Items> mapped;
    for (const auto &group : syntax::elements(groups)) {
        Items items;
        for (const auto &item : syntax::elements(group)) {
            flatten_into(one(item), items);
        }
        mapped.push_back(std::move(items));
    }
    return mapped;
}

// The tree of a metavariable: the atom '@Name' ('@@Name' for a glob) or the integer 909N (9099N).
Term variable_tree(ProcessContext &context, const Term &id, bool glob) {
    const Trees trees(context);
    if (id.is_atom()) {
        const auto name = std::string(glob ? "@@" : "@") + std::string(id.atom_spelling_unchecked());
        return syntax::atom(context, trees.atom_of(name));
    }
    const auto digits = std::string(glob ? "9099" : "909") + need(id.integer_decimal());
    return syntax::make(context, "integer", std::array{id_of(context, code_points(digits), false)});
}

// tree_1/1.
Term tree_of(ProcessContext &context, const Term &item) {
    if (is_template(item)) {
        const auto groups = map_groups(element(item, 3), [&](const Term &t) { return tree_of(context, t); });
        const auto made = make_node(context, element(item, 1), groups);
        return syntax::set_attrs(context, made, {element(item, 2)});
    }
    const auto variable = is_variable(item) && (element(item, 0).is_atom() || element(item, 0).is_integer());
    const auto glob = is_glob(item) && (element(item, 1).is_atom() || element(item, 1).is_integer());
    if (variable || glob) {
        return variable_tree(context, element(item, glob ? 1 : 0), glob);
    }
    return item;
}

// The roles of the substitution environment (typed.hpp's Named).
struct EnvRole;
using Env = Named<EnvRole>;

// lists:keyfind(Id, 1, Env) matched against {Id, Value}: the value, none when absent, case_clause for another tuple.
std::optional<Term> lookup(const Term &id, const Env &env) {
    for (const auto &entry : syntax::elements(env.term)) {
        if (!entry.is_tuple() || entry.tuple_size_unchecked() == 0 ||
            collections::compare(element(entry, 0), id) != 0) {
            continue;
        }
        if (entry.tuple_size_unchecked() != 2 || !collections::same(element(entry, 0), id)) {
            string::case_clause(entry);
        }
        return element(entry, 1);
    }
    return std::nullopt;
}

// subst_1/2 (RENAME: alpha_1/2): metavariables replaced by their trees, or renamed to their new ids.
template <bool RENAME> Term replace(ProcessContext &context, const Term &item, const Env &env) {
    const Trees trees(context);
    if (is_template(item)) {
        const auto groups =
            map_groups(element(item, 3), [&](const Term &t) { return replace<RENAME>(context, t, env); });
        Items lists;
        for (const auto &group : groups) {
            lists.push_back(trees.list(group));
        }
        return trees.tuple(std::array{element(item, 0), element(item, 1), element(item, 2), trees.list(lists)});
    }
    if (!is_variable(item) && !is_glob(item)) {
        return item;
    }
    const auto glob = is_glob(item);
    const auto found = lookup(element(item, glob ? 1 : 0), env);
    if (!found || !RENAME) {
        return found.value_or(item);
    }
    return glob ? trees.tuple(std::array{element(item, 0), *found}) : trees.tuple(std::array{*found});
}

Term template1(ProcessContext &context, const Term &trees) {
    return each(context, trees, [&](const Term &tree) { return template_of(context, tree); });
}

Term tree1(ProcessContext &context, const Term &templates) {
    return each(context, templates, [&](const Term &item) { return tree_of(context, item); });
}

Term subst2(ProcessContext &context, const Term &trees, const Env &env) {
    return each(context, trees, [&](const Term &tree) {
        return tree_of(context, replace<false>(context, template_of(context, tree), env));
    });
}

Term tsubst2(ProcessContext &context, const Term &trees, const Env &env) {
    return each(context, trees,
                [&](const Term &tree) { return replace<false>(context, template_of(context, tree), env); });
}

Term alpha2(ProcessContext &context, const Term &trees, const Env &env) {
    return each(context, trees,
                [&](const Term &tree) { return replace<true>(context, template_of(context, tree), env); });
}

// ordsets:add_element/2.
void add_id(Items &ids, const Term &id) {
    const auto at = std::ranges::find_if(ids, [&](const Term &known) { return collections::compare(known, id) >= 0; });
    if (at == ids.end() || collections::compare(*at, id) != 0) {
        ids.insert(at, id);
    }
}

// template_vars_1/2.
void collect_ids(const Term &item, Items &ids) {
    if (is_template(item)) {
        for (const auto &group : syntax::elements(element(item, 3))) {
            for (const auto &part : syntax::elements(group)) {
                collect_ids(part, ids);
            }
        }
    } else if (is_variable(item)) {
        add_id(ids, element(item, 0));
    } else if (is_glob(item)) {
        add_id(ids, element(item, 1));
    }
}

Term template_vars1(ProcessContext &context, const Term &templates) {
    Items ids;
    if (templates.is_list()) {
        for (const auto &item : syntax::elements(templates)) {
            collect_ids(item, ids);
        }
    } else {
        collect_ids(templates, ids);
    }
    return Trees(context).list(ids);
}

constexpr std::array TEMPLATES{
    typed_entry<template1>("merl", "template"), typed_entry<tree1>("merl", "tree"),
    typed_entry<subst2>("merl", "subst"),       typed_entry<tsubst2>("merl", "tsubst"),
    typed_entry<alpha2>("merl", "alpha"),       typed_entry<template_vars1>("merl", "template_vars"),
};
} // namespace

Term template_of(ProcessContext &context, const Term &pattern) {
    if (is_template(pattern) || is_glob(pattern) || is_variable(pattern)) {
        return pattern;
    }
    auto found = template_node(context, pattern);
    if (const auto *term = std::get_if<Term>(&found)) {
        return *term;
    }
    if (const auto *lift = std::get_if<Pending>(&found)) {
        const auto name = lift->name.substr(1);
        if (std::ranges::any_of(name, [](char32_t code) { return code > 255; })) {
            bad_argument();
        }
        fail(context, U"bad metavariable: '" + name + U"'");
    }
    return pattern;
}
} // namespace clause::runtime::builtins::merl

namespace clause::runtime {
// Publish merl's template functions.
std::span<const BuiltinEntry> merl_template_builtins() noexcept { return builtins::merl::TEMPLATES; }
} // namespace clause::runtime
