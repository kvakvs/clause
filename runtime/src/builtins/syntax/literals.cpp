#include "../front_end/abstract.hpp"
#include "syntax.hpp"
#include <algorithm>

// erl_syntax's literals (plan 21 LIB77E2): abstract/1, concrete/1, is_literal/1 and meta/1, which builds the
// erl_syntax calls that rebuild a tree.
namespace clause::runtime::builtins::syntax {
namespace {
// A character io_lib's latin1 printable range holds: graphic Latin-1 and the usual control characters.
bool printable_char(std::int64_t c) {
    constexpr std::string_view CONTROLS = "\n\r\t\v\b\f\x1B";
    const auto control = c > 0 && c < 0x20 && CONTROLS.find(char(c)) != std::string_view::npos;
    return (c >= 0x20 && c <= 0x7E) || (c >= 0xA0 && c <= 0xFF) || control;
}

// io_lib:printable_list/1 as Clause's io_lib answers it (the latin1 range): a flat list of printable characters.
bool printable(const Term &list) {
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        if (!printable_char(small(rest.head_unchecked().word()).value_or(-1))) {
            return false;
        }
    }
    return rest.is_nil();
}

Term integer_tree(ProcessContext &context, std::int64_t value) {
    return make(context, "integer", std::array{Trees(context).integer(value)});
}

// abstract_tail/2: cons(abstract(H), ...) from the last element back, so a proper list becomes one list tree.
Term abstract_list(ProcessContext &context, const Term &list) {
    Items heads;
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        heads.push_back(abstract(context, rest.head_unchecked()));
    }
    auto result = abstract(context, rest);
    for (auto head = heads.rbegin(); head != heads.rend(); ++head) {
        result = cons(context, *head, result);
    }
    return result;
}

// binary_field(integer(B)) of each byte, and binary_field(integer(I), integer(Bits), []) of trailing bits.
Term abstract_bits(ProcessContext &context, const Term &bits) {
    const Trees trees(context);
    const auto size = need(bits.bit_size());
    const auto bytes = need(bits.bitstring_bytes());
    Items fields;
    for (std::size_t at = 0; at < size / 8; ++at) {
        const auto byte = std::to_integer<std::int64_t>(bytes[at]);
        fields.push_back(make(context, "binary_field", std::array{integer_tree(context, byte), trees.nil()}));
    }
    if (const auto rest = size % 8; rest != 0) {
        const auto value = std::to_integer<std::int64_t>(bytes[size / 8]) >> (8 - rest);
        const std::array sized{integer_tree(context, value), integer_tree(context, std::int64_t(rest))};
        fields.push_back(
            make(context, "binary_field", std::array{make(context, "size_qualifier", sized), trees.nil()}));
    }
    return make(context, "binary", std::array{trees.list(fields)});
}

// map_expr([map_field_assoc(abstract(K), abstract(V)) || {K, V} <- maps:to_list(Map)]).
Term abstract_map(ProcessContext &context, const Term &map) {
    Items fields;
    for (const auto &[key, value] : need(map.map_entries())) {
        const std::array parts{abstract(context, key), abstract(context, value)};
        fields.push_back(make(context, "map_field_assoc", parts));
    }
    return make(context, "map_expr", std::array{Trees(context).atom("none"_atom), Trees(context).list(fields)});
}

// [concrete(list_head(Node)) | concrete(list_tail(Node))], walking the list skeleton.
Term concrete_list(ProcessContext &context, const Term &node) {
    Items heads;
    auto at = node;
    while (type_of(context, at) == "list") {
        heads.push_back(concrete(context, list_head(context, at)));
        at = list_tail(context, at);
    }
    return Trees(context).list(heads, concrete(context, at));
}

Term concrete_tuple(ProcessContext &context, const Term &node) {
    Items values;
    for (const auto &item : elements(get(context, node, "tuple_elements"))) {
        values.push_back(concrete(context, item));
    }
    return Trees(context).tuple(values);
}

// maps:from_list of the fields' {Name, Value}, merged over the concrete argument when there is one.
Term concrete_map(ProcessContext &context, const Term &node) {
    std::vector<std::pair<Term, Term>> entries;
    const auto argument = map_expr_argument(context, node);
    const auto base = is_none(argument) ? std::nullopt : std::optional{concrete(context, argument)};
    for (const auto &field : elements(map_expr_fields(context, node))) {
        entries.emplace_back(concrete(context, get(context, field, "map_field_assoc_name")),
                             concrete(context, get(context, field, "map_field_assoc_value")));
    }
    if (base && !base->is_map()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmap, .payload = base->word()};
    }
    if (base) {
        auto merged = need(base->map_entries());
        merged.insert(merged.end(), entries.begin(), entries.end());
        entries = std::move(merged);
    }
    return need(TermFactory(context).map(entries));
}

// The binary of a binary tree: each field as revert_binary_field/1 gives it, with its value and size reverted to
// literal forms, built as erl_parse:normalise/1 builds {bin, _, Fields}.
Term concrete_binary(ProcessContext &context, const Term &node) {
    const Trees trees(context);
    Items fields;
    for (const auto &field : elements(get(context, node, "binary_fields"))) {
        const auto body = binary_field_body(context, field);
        const auto sized = type_of(context, body) == "size_qualifier";
        const auto value = sized ? get(context, body, "size_qualifier_body") : body;
        const auto size =
            sized ? revert(context, get(context, body, "size_qualifier_argument")) : trees.atom("default"_atom);
        const auto types = binary_field_types(context, field);
        const auto folded = types.is_nil() ? trees.atom("default"_atom) : fold_binary_field_types(context, types);
        fields.push_back(trees.node("bin_element"_atom, trees.integer(0), revert(context, value), size, folded));
    }
    return front_end::abstract::normalise(context, trees.node("bin"_atom, trees.integer(0), trees.list(fields)));
}

// concrete/1 of arity_qualifier(atom, integer): the pair of the trees themselves.
Term concrete_arity(ProcessContext &context, const Term &node) {
    const auto arity = get(context, node, "arity_qualifier_argument");
    const auto function = get(context, node, "arity_qualifier_body");
    if (type_of(context, arity) != "integer" || type_of(context, function) != "atom") {
        badarg_of(context, node);
    }
    return Trees(context).tuple(std::array{function, arity});
}

bool is_literal(ProcessContext &context, const Term &node);

// is_literal_binary_field/1: no type or utf8 alone, and literal parts.
bool is_literal_binary_field(ProcessContext &context, const Term &field) {
    const auto types = binary_field_types(context, field);
    if (!types.is_nil() && !list_of(types, 1)) {
        case_clause(context, types);
    }
    if (!types.is_nil()) {
        const auto type = types.head_unchecked();
        if (!is_literal(context, type) || !is_atom(concrete(context, type), "utf8")) {
            return false;
        }
    }
    const auto body = binary_field_body(context, field);
    if (type_of(context, body) == "size_qualifier") {
        return is_literal(context, get(context, body, "size_qualifier_body")) &&
               is_literal(context, get(context, body, "size_qualifier_argument"));
    }
    return is_literal(context, body);
}

// is_literal_map_field/1: a literal => field; := fields are never literal.
bool is_literal_map_field(ProcessContext &context, const Term &field) {
    const auto type = type_of(context, field);
    if (type == "map_field_assoc") {
        return is_literal(context, get(context, field, "map_field_assoc_name")) &&
               is_literal(context, get(context, field, "map_field_assoc_value"));
    }
    if (type != "map_field_exact") {
        case_clause(context, Trees(context).atom_of(type));
    }
    return false;
}

bool is_literal_binary(ProcessContext &context, const Term &node) {
    const auto fields = elements(get(context, node, "binary_fields"));
    return std::ranges::all_of(fields, [&](const Term &field) { return is_literal_binary_field(context, field); });
}

bool is_literal_map(ProcessContext &context, const Term &node) {
    const auto argument = map_expr_argument(context, node);
    if (!is_none(argument) && !is_literal(context, argument)) {
        return false;
    }
    const auto fields = elements(map_expr_fields(context, node));
    return std::ranges::all_of(fields, [&](const Term &field) { return is_literal_map_field(context, field); });
}

// The types whose nodes are always literals.
constexpr auto LITERAL_LEAVES = std::to_array<std::string_view>({"atom", "integer", "float", "char", "string", "nil"});

bool is_literal(ProcessContext &context, const Term &node) {
    const auto type = type_of(context, node);
    if (std::ranges::find(LITERAL_LEAVES, type) != LITERAL_LEAVES.end()) {
        return true;
    }
    if (type == "list") {
        return is_literal(context, list_head(context, node)) && is_literal(context, list_tail(context, node));
    }
    if (type == "tuple") {
        const auto items = elements(get(context, node, "tuple_elements"));
        return std::ranges::all_of(items, [&](const Term &item) { return is_literal(context, item); });
    }
    if (type == "map_expr") {
        return is_literal_map(context, node);
    }
    return type == "binary" && is_literal_binary(context, node);
}

// meta_call/2: erl_syntax:F(Arguments...) as an application tree.
Term meta_call(ProcessContext &context, AtomName function, Parts arguments) {
    const Trees trees(context);
    const auto module = atom(context, trees.atom("erl_syntax"_atom));
    const auto name = atom(context, trees.atom(function));
    const auto qualified = make(context, "module_qualifier", std::array{module, name});
    return make(context, "application", std::array{qualified, trees.list(arguments)});
}

Term meta(ProcessContext &context, const Term &node);

// list(meta_list(Nodes)): a list tree of the meta trees of each node.
Term meta_list(ProcessContext &context, const Term &nodes) {
    Items metas;
    for (const auto &node : elements(nodes)) {
        metas.push_back(meta(context, node));
    }
    return list(context, Trees(context).list(metas), Trees(context).atom("none"_atom));
}

// string(Text) of a character list.
Term string_tree(ProcessContext &context, const Term &text) { return make(context, "string", std::array{text}); }

// The meta trees of the leaves rebuilt from themselves or from nothing; none for other types.
std::optional<Term> meta_plain_leaf(ProcessContext &context, const Term &node, std::string_view type) {
    if (type == "atom" || type == "char" || type == "float" || type == "integer" || type == "string") {
        return meta_call(context, type, std::array{node});
    }
    if (type == "eof_marker" || type == "nil" || type == "underscore") {
        return meta_call(context, type, {});
    }
    return std::nullopt;
}

std::optional<Term> meta_named_leaf(ProcessContext &context, const Term &node, std::string_view type);

// The meta trees of leaves, none for the types meta_1/1 rebuilds otherwise.
std::optional<Term> meta_leaf(ProcessContext &context, const Term &node, std::string_view type) {
    const Trees trees(context);
    if (const auto plain = meta_plain_leaf(context, node, type)) {
        return plain;
    }
    if (type == "comment") {
        Items strings;
        for (const auto &text : elements(get(context, node, "comment_text"))) {
            strings.push_back(string_tree(context, text));
        }
        return meta_call(context, "comment"_atom,
                         std::array{list(context, trees.list(strings), trees.atom("none"_atom))});
    }
    if (type == "error_marker") {
        return meta_call(context, type, std::array{abstract(context, get(context, node, "error_marker_info"))});
    }
    if (type == "warning_marker") {
        return meta_call(context, type, std::array{abstract(context, get(context, node, "warning_marker_info"))});
    }
    return meta_named_leaf(context, node, type);
}

// The meta trees of the leaves rebuilt from a name or text; none for other types.
std::optional<Term> meta_named_leaf(ProcessContext &context, const Term &node, std::string_view type) {
    const Trees trees(context);
    if (type == "operator") {
        return meta_call(context, "operator"_atom, std::array{atom(context, get(context, node, "operator_name"))});
    }
    if (type == "text") {
        return meta_call(context, "text"_atom, std::array{string_tree(context, get(context, node, "text_string"))});
    }
    if (type == "variable") {
        const auto name = get(context, node, "variable_name").atom_spelling_unchecked();
        return meta_call(context, "variable"_atom, std::array{string_tree(context, trees.chars(name))});
    }
    return std::nullopt;
}

// meta_1/1: the call that rebuilds a node without comments.
Term meta_1(ProcessContext &context, const Term &node) {
    const auto type = type_of(context, node);
    if (const auto leaf = meta_leaf(context, node, type)) {
        return *leaf;
    }
    const Trees trees(context);
    if (type == "list") {
        const auto prefix = meta_list(context, list_prefix(context, node));
        const auto suffix = list_suffix(context, node);
        return is_none(suffix) ? meta_call(context, "list"_atom, std::array{prefix})
                               : meta_call(context, "list"_atom, std::array{prefix, meta(context, suffix)});
    }
    if (type == "tuple") {
        return meta_call(context, "tuple"_atom, std::array{meta_list(context, get(context, node, "tuple_elements"))});
    }
    Items groups;
    for (const auto &group : elements(subtrees(context, node))) {
        groups.push_back(meta_list(context, group));
    }
    const std::array arguments{abstract(context, trees.atom_of(type)),
                               list(context, trees.list(groups), trees.atom("none"_atom))};
    return meta_call(context, "make_tree"_atom, arguments);
}

// meta_precomment/1 and meta_postcomment/1: the comments set again around the call that rebuilds the node.
Term meta_comments(ProcessContext &context, const Term &node) {
    const auto pre = get_precomments(context, node);
    const auto post = get_postcomments(context, node);
    auto result = meta_1(context, remove_comments(context, node));
    if (!post.is_nil()) {
        result = meta_call(context, "set_postcomments"_atom, std::array{result, meta_list(context, post)});
    }
    if (!pre.is_nil()) {
        result = meta_call(context, "set_precomments"_atom, std::array{result, meta_list(context, pre)});
    }
    return result;
}

Term meta(ProcessContext &context, const Term &node) {
    if (type_of(context, node) == "variable") {
        auto annotations = elements(get_ann(context, node));
        const auto marked = std::ranges::find_if(annotations, [](const Term &a) { return is_atom(a, "meta_var"); });
        if (marked == annotations.end()) {
            return meta_comments(context, node);
        }
        annotations.erase(marked);
        return set_ann(context, node, {Trees(context).list(annotations)});
    }
    return has_comments(node) ? meta_comments(context, node) : meta_1(context, node);
}

Term abstract1(ProcessContext &context, const Term &term) { return abstract(context, term); }

Term concrete1(ProcessContext &context, const Term &node) { return concrete(context, node); }

Term is_literal1(ProcessContext &context, const Term &node) {
    return Trees(context).boolean(is_literal(context, node));
}

Term meta1(ProcessContext &context, const Term &node) { return meta(context, node); }

// The value accessors of the leaf types concrete/1 reads.
constexpr std::array<std::pair<std::string_view, FieldName>, 5> LEAF_VALUES{{
    {"atom", "atom_value"},
    {"integer", "integer_value"},
    {"float", "float_value"},
    {"char", "char_value"},
    {"string", "string_value"},
}};

constexpr std::array LITERALS{
    typed_entry<abstract1>("erl_syntax", "abstract"),
    typed_entry<concrete1>("erl_syntax", "concrete"),
    typed_entry<is_literal1>("erl_syntax", "is_literal"),
    typed_entry<meta1>("erl_syntax", "meta"),
};
} // namespace

namespace {
// abstract/1 of a tuple, map or bitstring; error({badarg, Term}) for any other term.
Term abstract_compound(ProcessContext &context, const Term &term) {
    if (term.is_tuple()) {
        Items items;
        for (const auto &item : need(term.tuple_elements())) {
            items.push_back(abstract(context, item));
        }
        return make(context, "tuple", std::array{Trees(context).list(items)});
    }
    if (term.is_map()) {
        return abstract_map(context, term);
    }
    if (term.is_bitstring()) {
        return abstract_bits(context, term);
    }
    badarg_of(context, term);
}
} // namespace

Term abstract(ProcessContext &context, const Term &term) {
    const Trees trees(context);
    if (term.is_cons()) {
        const auto string = term.head_unchecked().is_integer() && printable(term);
        return string ? make(context, "string", std::array{term}) : abstract_list(context, term);
    }
    if (term.is_atom()) {
        return trees.tree("atom"_atom, term);
    }
    if (term.is_integer() || term.is_float()) {
        return trees.tree(term.is_integer() ? "integer"_atom : "float"_atom, term);
    }
    if (term.is_nil()) {
        return make(context, "nil", {});
    }
    return abstract_compound(context, term);
}

Term concrete(ProcessContext &context, const Term &node) {
    const auto type = type_of(context, node);
    for (const auto &[leaf, value] : LEAF_VALUES) {
        if (leaf == type) {
            return get(context, node, value);
        }
    }
    if (type == "nil") {
        return Trees(context).nil();
    }
    if (type == "list") {
        return concrete_list(context, node);
    }
    if (type == "tuple") {
        return concrete_tuple(context, node);
    }
    if (type == "map_expr") {
        return concrete_map(context, node);
    }
    if (type == "binary") {
        return concrete_binary(context, node);
    }
    if (type == "arity_qualifier") {
        return concrete_arity(context, node);
    }
    badarg_of(context, node);
}
} // namespace clause::runtime::builtins::syntax

namespace clause::runtime {
// Publish erl_syntax's literal functions.
std::span<const BuiltinEntry> erl_syntax_literal_builtins() noexcept { return builtins::syntax::LITERALS; }
} // namespace clause::runtime
