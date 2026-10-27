#include "../io_lib/deep.hpp"
#include "../io_lib/write.hpp"
#include "../text.hpp"
#include "merl.hpp"

// merl's matching and meta templates as builtins (plan 21 LIB77E5): match/2 binds the metavariables of patterns to
// the subtrees of trees (globs to runs of them) by merl's shapes, comparing everything else node by node, and
// meta_template/1 builds the trees that construct a template, with uppercase and Q-variables made Erlang variables.
namespace clause::runtime::builtins::merl {
namespace {
// A pattern that does not match (merl's throw(error)).
struct NoMatch {};

// The bindings of a match: an orddict of {Id, Tree} by id.
using Bindings = std::vector<std::pair<Term, Term>>;

// orddict:store/3.
void store(Bindings &bindings, const Term &id, const Term &value) {
    const auto at = std::ranges::find_if(
        bindings, [&](const auto &binding) { return collections::compare(binding.first, id) >= 0; });
    if (at != bindings.end() && collections::compare(at->first, id) == 0) {
        *at = {id, value};
        return;
    }
    bindings.insert(at, {id, value});
}

// The anonymous ids '_' and 0.
bool is_anonymous(const Term &id) {
    return is_atom(id, "_") || (id.is_integer() && small(id.word()) == std::optional<std::int64_t>{0});
}

bool compare_trees(ProcessContext &context, const Term &left, const Term &right);

// compare_leaves/3: the values of two leaves of a type.
bool compare_leaves(ProcessContext &context, std::string_view type, const Term &left, const Term &right) {
    constexpr std::array<std::pair<std::string_view, syntax::FieldName>, 7> VALUES{{
        {"atom", "atom_value"},
        {"char", "char_value"},
        {"float", "float_value"},
        {"integer", "integer_value"},
        {"string", "string_value"},
        {"variable", "variable_name"},
        {"text", "text_string"},
    }};
    if (type == "operator") {
        return collections::same(syntax::get(context, left, "operator_name"),
                                 syntax::get(context, right, "operator_name"));
    }
    for (const auto &[name, field] : VALUES) {
        if (name == type) {
            return collections::same(syntax::get(context, left, field), syntax::get(context, right, field));
        }
    }
    return true;
}

// compare_trees_1/2 and compare_trees_2/2: groups of the same shape whose trees compare equal, left to right.
bool compare_groups(ProcessContext &context, const std::vector<Items> &left, const std::vector<Items> &right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t at = 0; at < left.size(); ++at) {
        const auto &one = left[at];
        const auto &other = right[at];
        for (std::size_t item = 0; item < std::min(one.size(), other.size()); ++item) {
            if (!compare_trees(context, one[item], other[item])) {
                return false;
            }
        }
        if (one.size() != other.size()) {
            return false;
        }
    }
    return true;
}

bool compare_trees(ProcessContext &context, const Term &left, const Term &right) {
    const auto type = node_type(context, left);
    if (!collections::same(node_type(context, right), type)) {
        return false;
    }
    const auto left_groups = node_groups(context, left);
    const auto right_groups = node_groups(context, right);
    if (left_groups.empty() || right_groups.empty()) {
        return left_groups.empty() && right_groups.empty() &&
               compare_leaves(context, type.atom_spelling_unchecked(), left, right);
    }
    return compare_groups(context, left_groups, right_groups);
}

void match_template(ProcessContext &context, const Term &pattern, const Term &tree, Bindings &bindings);

// The message ~w gives a glob id: an atom as written, an integer in decimal.
std::u32string written_id(const Term &id) {
    if (id.is_atom()) {
        io_lib::Deep deep;
        return deep.flat(io_lib::atom_text(deep, id, io_lib::Encoding::latin1));
    }
    return code_points(need(id.integer_decimal()));
}

// match_glob/4 over reversed groups: the trees after the glob match from the end; the rest is the glob's.
void match_glob(ProcessContext &context, const Items &patterns, const Items &trees, const Term &glob,
                Bindings &bindings) {
    std::size_t at = 0;
    for (; at < patterns.size(); ++at) {
        if (is_glob(patterns[at])) {
            fail(context, U"multiple glob variables in same match group: " + written_id(element(patterns[at], 1)));
        }
        if (at == trees.size()) {
            throw NoMatch{};
        }
        match_template(context, patterns[at], trees[at], bindings);
    }
    const auto id = element(glob, 1);
    if (is_anonymous(id)) {
        return;
    }
    Items rest(trees.begin() + std::ptrdiff_t(at), trees.end());
    std::ranges::reverse(rest);
    store(bindings, id, Trees(context).list(rest));
}

// match_template_2/3: a group of patterns against a group of trees, left to right up to a glob.
void match_group(ProcessContext &context, const Items &patterns, const Items &trees, Bindings &bindings) {
    for (std::size_t at = 0; at < patterns.size(); ++at) {
        const auto &pattern = patterns[at];
        if (is_glob(pattern)) {
            Items rest_patterns(patterns.rbegin(), patterns.rend() - std::ptrdiff_t(at + 1));
            Items rest_trees(trees.rbegin(), trees.rend() - std::ptrdiff_t(std::min(at, trees.size())));
            match_glob(context, rest_patterns, rest_trees, pattern, bindings);
            return;
        }
        if (at == trees.size()) {
            throw NoMatch{};
        }
        if (is_variable(pattern) && !is_anonymous(element(pattern, 0))) {
            store(bindings, element(pattern, 0), trees[at]);
        } else if (!is_variable(pattern)) {
            match_template(context, pattern, trees[at], bindings);
        }
    }
    if (patterns.size() != trees.size()) {
        throw NoMatch{};
    }
}

// A template's node type and groups against a tree's; match_template_1/3 matches the last group first.
void match_node(ProcessContext &context, const Term &pattern, const Term &tree, Bindings &bindings) {
    if (!collections::same(node_type(context, tree), element(pattern, 1))) {
        throw NoMatch{};
    }
    const auto groups = node_groups(context, tree);
    const auto patterns = syntax::elements(element(pattern, 3));
    if (patterns.size() != groups.size()) {
        throw NoMatch{};
    }
    for (auto at = patterns.size(); at-- > 0;) {
        match_group(context, syntax::elements(patterns[at]), groups[at], bindings);
    }
}

// match_template/3.
void match_template(ProcessContext &context, const Term &pattern, const Term &tree, Bindings &bindings) {
    if (is_template(pattern)) {
        match_node(context, pattern, tree, bindings);
    } else if (is_variable(pattern)) {
        if (!is_anonymous(element(pattern, 0))) {
            store(bindings, element(pattern, 0), tree);
        }
    } else if (!compare_trees(context, pattern, tree)) {
        throw NoMatch{};
    }
}

Term bindings_term(ProcessContext &context, const Bindings &bindings) {
    const Trees trees(context);
    Items pairs;
    for (const auto &[id, value] : bindings) {
        pairs.push_back(trees.tuple(std::array{id, value}));
    }
    return trees.tuple(std::array{trees.atom("ok"_atom), trees.list(pairs)});
}

// match/2: {ok, Bindings} or error; patterns and trees in lists of the same length (merl_match_arity otherwise).
Term match2(ProcessContext &context, const Term &patterns, const Term &trees) {
    const auto pattern_list = patterns.is_list() ? syntax::elements(patterns) : Items{patterns};
    const auto tree_list = trees.is_list() ? syntax::elements(trees) : Items{trees};
    const auto lists = patterns.is_list() || trees.is_list();
    Bindings bindings;
    try {
        for (std::size_t at = 0; at < std::min(pattern_list.size(), tree_list.size()); ++at) {
            match_template(context, template_of(context, pattern_list[at]), tree_list[at], bindings);
        }
    } catch (const NoMatch &) {
        return Trees(context).atom("error"_atom);
    }
    if (lists && pattern_list.size() != tree_list.size()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                             .payload = Trees(context).atom("merl_match_arity"_atom).word()};
    }
    return bindings_term(context, bindings);
}

// A Latin-1 or Unicode uppercase letter that starts an Erlang variable (A-Z, À-Þ but ×).
bool is_upper(char32_t code) {
    return (code >= U'A' && code <= U'Z') || (code >= 0xC0 && code <= 0xDE && code != 0xD7);
}

// merl:term(Var): the application merl:term(Var) of a variable.
Term term_call(ProcessContext &context, std::string_view variable) {
    const Trees trees(context);
    const auto module = syntax::atom(context, trees.atom("merl"_atom));
    const auto function = syntax::atom(context, trees.atom("term"_atom));
    const auto name = syntax::make(context, "module_qualifier", std::array{module, function});
    const auto argument = syntax::variable(context, trees.atom_of(variable));
    return syntax::make(context, "application", std::array{name, trees.list(std::array{argument})});
}

// meta_template_2/2 of an atom id: an uppercase one as its Erlang variable (Name@ wrapped in merl:term/1), any
// other as the abstract metavariable.
Term meta_atom(ProcessContext &context, const Term &variable) {
    const auto id = element(variable, is_glob(variable) ? 1 : 0);
    const auto name = id.atom_spelling_unchecked();
    const auto codes = code_points(name);
    if (codes.empty() || !is_upper(codes.front())) {
        return syntax::abstract(context, variable);
    }
    if (name.size() > 1 && name.ends_with('@')) {
        return term_call(context, name.substr(0, name.size() - 1));
    }
    return syntax::variable(context, id);
}

// meta_template_2/2 of an integer id: one ending in 9 (at least two digits) as a Q-variable, in 99 (at least three)
// wrapped in merl:term/1, any other as the abstract metavariable.
Term meta_integer(ProcessContext &context, const Term &variable, std::optional<std::int64_t> id) {
    if (id && *id > 99 && *id % 100 == 99) {
        return term_call(context, "Q" + std::to_string(*id / 100));
    }
    if (id && *id > 9 && *id % 10 == 9) {
        return syntax::variable(context, Trees(context).atom_of("Q" + std::to_string(*id / 10)));
    }
    return syntax::abstract(context, variable);
}

// meta_template_2/2 of a metavariable {Id} or {'*', Id}; function_clause for an id neither atom nor integer.
Term meta_variable(ProcessContext &context, const Term &variable) {
    const auto id = element(variable, is_glob(variable) ? 1 : 0);
    if (id.is_atom()) {
        return meta_atom(context, variable);
    }
    if (!id.is_integer()) {
        lists::no_clause();
    }
    return meta_integer(context, variable, small(id.word()));
}

// meta_template_1/1.
Term meta_of(ProcessContext &context, const Term &item) {
    const Trees trees(context);
    if (is_template(item)) {
        Items groups;
        for (const auto &group : syntax::elements(element(item, 3))) {
            Items metas;
            for (const auto &part : syntax::elements(group)) {
                metas.push_back(meta_of(context, part));
            }
            groups.push_back(syntax::list(context, trees.list(metas), trees.atom("none"_atom)));
        }
        const std::array parts{syntax::atom(context, trees.atom("template"_atom)),
                               syntax::atom(context, element(item, 1)), syntax::abstract(context, element(item, 2)),
                               syntax::list(context, trees.list(groups), trees.atom("none"_atom))};
        return syntax::make(context, "tuple", std::array{trees.list(parts)});
    }
    if (is_variable(item) || is_glob(item)) {
        return meta_variable(context, item);
    }
    return syntax::abstract(context, item);
}

Term meta_template1(ProcessContext &context, const Term &templates) {
    if (!templates.is_list()) {
        return meta_of(context, templates);
    }
    Items metas;
    for (const auto &item : syntax::elements(templates)) {
        metas.push_back(meta_of(context, item));
    }
    return Trees(context).list(metas);
}

constexpr std::array MATCH{
    typed_entry<match2>("merl", "match"),
    typed_entry<meta_template1>("merl", "meta_template"),
};
} // namespace
} // namespace clause::runtime::builtins::merl

namespace clause::runtime {
// Publish merl's matching and meta templates.
std::span<const BuiltinEntry> merl_match_builtins() noexcept { return builtins::merl::MATCH; }
} // namespace clause::runtime
