#include "../collections/collections.hpp"
#include "../text.hpp"
#include "syntax.hpp"

// erl_syntax's node representation: trees, wrappers and erl_parse forms, their types, positions, attributes and
// comments (plan 21 LIB77E2).
namespace clause::runtime::builtins::syntax {
namespace {
// The erl_parse forms type/1 tells apart by tag and size alone.
struct Form {
    std::string_view tag;
    std::size_t size;
    std::string_view type;
};

constexpr auto FORMS = std::to_array<Form>({
    {"atom", 3, "atom"},
    {"char", 3, "char"},
    {"float", 3, "float"},
    {"integer", 3, "integer"},
    {"nil", 2, "nil"},
    {"string", 3, "string"},
    {"error", 2, "error_marker"},
    {"warning", 2, "warning_marker"},
    {"eof", 2, "eof_marker"},
    {"case", 4, "case_expr"},
    {"catch", 3, "catch_expr"},
    {"named_fun", 4, "named_fun_expr"},
    {"if", 3, "if_expr"},
    {"maybe", 3, "maybe_expr"},
    {"maybe", 4, "maybe_expr"},
    {"else", 3, "else_expr"},
    {"receive", 5, "receive_expr"},
    {"receive", 3, "receive_expr"},
    {"attribute", 4, "attribute"},
    {"bin", 3, "binary"},
    {"bin_element", 5, "binary_field"},
    {"block", 3, "block_expr"},
    {"call", 4, "application"},
    {"clause", 5, "clause"},
    {"cons", 4, "list"},
    {"function", 5, "function"},
    {"b_generate", 4, "binary_generator"},
    {"b_generate_strict", 4, "strict_binary_generator"},
    {"generate", 4, "generator"},
    {"generate_strict", 4, "strict_generator"},
    {"m_generate", 4, "map_generator"},
    {"m_generate_strict", 4, "strict_map_generator"},
    {"zip", 3, "zip_generator"},
    {"lc", 4, "list_comp"},
    {"bc", 4, "binary_comp"},
    {"mc", 4, "map_comp"},
    {"match", 4, "match_expr"},
    {"map", 4, "map_expr"},
    {"map", 3, "map_expr"},
    {"map_field_assoc", 4, "map_field_assoc"},
    {"map_field_exact", 4, "map_field_exact"},
    {"maybe_match", 4, "maybe_match_expr"},
    {"op", 5, "infix_expr"},
    {"op", 4, "prefix_expr"},
    {"record", 5, "record_expr"},
    {"record", 4, "record_expr"},
    {"record_field", 5, "record_access"},
    {"record_index", 4, "record_index_expr"},
    {"remote", 4, "module_qualifier"},
    {"try", 6, "try_expr"},
    {"tuple", 3, "tuple"},
    {"ann_type", 3, "annotated_type"},
    {"remote_type", 3, "type_application"},
    {"user_type", 4, "user_type_application"},
});

// {type, _, Name, Args} forms by type name; the others are type applications.
struct TypeForm {
    std::string_view name;
    std::string_view type;
    // The argument count the form needs, or -1 for any arguments.
    int arguments;
};

constexpr auto TYPE_FORMS = std::to_array<TypeForm>({
    {"binary", "bitstring_type", 2},
    {"bounded_fun", "constrained_function_type", 2},
    {"constraint", "constraint", 2},
    {"fun", "fun_type", 0},
    {"fun", "function_type", 2},
    {"map", "map_type", -1},
    {"map_field_assoc", "map_type_assoc", -1},
    {"map_field_exact", "map_type_exact", -1},
    {"record", "record_type", -1},
    {"field_type", "record_type_field", -1},
    {"range", "integer_range_type", -1},
    {"tuple", "tuple_type", -1},
    {"union", "type_union", -1},
});

// The type of a {type, _, Name, Args} form.
std::string_view type_form(const Term &node) {
    const auto name = element(node, 2);
    const auto arguments = element(node, 3);
    for (const auto &form : TYPE_FORMS) {
        if (is_atom(name, form.name) && (form.arguments < 0 || list_of(arguments, std::size_t(form.arguments)))) {
            return form.type;
        }
    }
    return "type_application";
}

// The type of a {'fun', _, What} form: clauses or a function name; none for another shape.
std::string_view fun_form(const Term &what) {
    if (is_node(what, "clauses", 2)) {
        return "fun_expr";
    }
    return is_node(what, "function", 3) || is_node(what, "function", 4) ? "implicit_fun" : std::string_view{};
}

// The type of a var, fun or type form, which their contents decide; none for other tags and sizes.
std::optional<std::string_view> decided_form(const Term &node, std::string_view tag, std::size_t size) {
    if (tag == "var" && size == 3) {
        return is_atom(element(node, 2), "_") ? "underscore" : "variable";
    }
    if (tag == "fun" && size == 3) {
        return fun_form(element(node, 2));
    }
    if (tag == "type" && size == 4) {
        return type_form(node);
    }
    return std::nullopt;
}

// The type of an erl_parse form, empty for a term that is none.
std::string_view form_type(const Term &node) {
    const auto tag = front_end::forms::tag_of(node);
    if (tag.empty()) {
        return {};
    }
    const auto size = node.tuple_size_unchecked();
    if (const auto decided = decided_form(node, tag, size)) {
        return *decided;
    }
    for (const auto &form : FORMS) {
        if (form.tag == tag && form.size == size) {
            return form.type;
        }
    }
    return {};
}

// The attributes of a tree or wrapper (element 2), none for an erl_parse form.
std::optional<Term> attrs_of(const Term &node) {
    return is_tree(node) || is_wrapper(node) ? std::optional{element(node, 2)} : std::nullopt;
}

// Field `field` of an #attr{} or #com{} record; error({badrecord, Term}) for another term.
Term attr_field(const Term &term, const Record &record, std::size_t field) {
    if (!is_node(term, record.tag, record.size)) {
        badrecord(term);
    }
    return element(term, field + 1);
}

constexpr Record ATTR{"attr", 4};
constexpr Record COM{"com", 3};

// A copy of a record with field `field` replaced; error({badrecord, Term}) when the term is not that record.
Term with_field(const Trees &trees, const Term &term, const Record &record, std::size_t field, const Term &value) {
    static_cast<void>(attr_field(term, record, field));
    return front_end::forms::with_element(front_end::forms::Builder(trees.context()), term, field + 1, value);
}

// The node with its attributes replaced by `edit` of them; an erl_parse node is wrapped first.
template <typename Edit> Term edit_attrs(ProcessContext &context, const Term &node, Edit &&edit) {
    const Trees trees(context);
    const auto target = attrs_of(node) ? node : wrap(context, node);
    return front_end::forms::with_element(front_end::forms::Builder(context), target, 2,
                                          edit(trees, element(target, 2)));
}

// The comments of `attr` with one list replaced: field 0 pre, 1 post.
Term with_comments(const Trees &trees, const Term &attr, std::size_t field, const Term &comments) {
    const auto com = attr_field(attr, ATTR, 2);
    if (is_none(com)) {
        const std::array fresh{field == 0 ? comments : trees.nil(), field == 0 ? trees.nil() : comments};
        return with_field(trees, attr, ATTR, 2, trees.record("com"_atom, fresh));
    }
    return with_field(trees, attr, ATTR, 2, with_field(trees, com, COM, field, comments));
}

// One comment list (field 0 pre, 1 post) of a node's attributes; [] without comments.
Term comments_of(ProcessContext &context, const Term &node, std::size_t field) {
    const auto attr = attrs_of(node);
    if (!attr) {
        return Trees(context).nil();
    }
    const auto com = attr_field(*attr, ATTR, 2);
    return is_none(com) ? Trees(context).nil() : attr_field(com, COM, field);
}
} // namespace

Term Trees::atom_of(std::string_view spelling) const { return need(context().atom_storage().intern(spelling)); }

Term Trees::chars(std::string_view spelling) const {
    Items codes;
    for (const auto code : code_points(spelling)) {
        codes.push_back(integer(code));
    }
    return list(codes);
}

Term Trees::attr() const { return attr(integer(0), nil(), atom("none"_atom)); }

Term Trees::attr(const Term &pos, const Term &ann, const Term &com) const {
    const std::array items{atom("attr"_atom), pos, ann, com};
    return tuple(items);
}

Term Trees::tree(const Term &type, const Term &data) const {
    const std::array items{atom("tree"_atom), type, attr(), data};
    return tuple(items);
}

Term Trees::record(AtomName tag, Parts fields) const {
    Items items{atom(tag)};
    items.insert(items.end(), fields.begin(), fields.end());
    return tuple(items);
}

Term Trees::leaf_at(AtomName type, const Term &value, const Term &pos) const {
    const std::array items{atom("tree"_atom), atom(type), attr(pos, nil(), atom("none"_atom)), value};
    return tuple(items);
}

bool is_tree(const Term &node) { return is_node(node, "tree", 4); }

bool is_wrapper(const Term &node) { return is_node(node, "wrapper", 4); }

Term unwrap(const Term &node) { return is_wrapper(node) ? element(node, 3) : node; }

bool is_none(const Term &term) { return is_atom(term, "none"); }

Term data(ProcessContext &context, const Term &node) {
    if (!is_tree(node)) {
        badarg_of(context, node);
    }
    return element(node, 3);
}

Term record_field(ProcessContext &context, const Term &node, const Record &record, std::size_t field) {
    const auto value = data(context, node);
    if (record.size == 0) {
        return value;
    }
    if (!is_node(value, record.tag, record.size)) {
        badrecord(value);
    }
    return element(value, field + 1);
}

std::string_view type_of(ProcessContext &context, const Term &node) {
    if (is_tree(node) || is_wrapper(node)) {
        const auto type = element(node, 1);
        // A tree type need not be an atom (tree/2 takes any term); callers compare spellings only.
        return type.is_atom() ? type.atom_spelling_unchecked() : std::string_view{};
    }
    const auto type = form_type(node);
    if (type.empty()) {
        badarg_of(context, node);
    }
    return type;
}

Term get_pos(ProcessContext &context, const Term &node) {
    if (const auto attr = attrs_of(node)) {
        return attr_field(*attr, ATTR, 0);
    }
    if ((is_node(node, "error", 2) || is_node(node, "warning", 2)) && element(node, 1).is_tuple() &&
        element(node, 1).tuple_size_unchecked() == 3) {
        return element(element(node, 1), 0);
    }
    if (!node.is_tuple() || node.tuple_size_unchecked() < 2) {
        static_cast<void>(context);
        bad_argument();
    }
    return element(node, 1);
}

Term set_pos(ProcessContext &context, const Term &node, const Pos &pos) {
    return edit_attrs(context, node,
                      [&](const Trees &trees, const Term &attr) { return with_field(trees, attr, ATTR, 0, pos.term); });
}

Term wrap(ProcessContext &context, const Term &node) {
    const Trees trees(context);
    const auto type = trees.atom_of(type_of(context, node));
    const std::array items{trees.atom("wrapper"_atom), type,
                           trees.attr(get_pos(context, node), trees.nil(), trees.atom("none"_atom)), node};
    return trees.tuple(items);
}

Term get_attrs(ProcessContext &context, const Term &node) {
    if (const auto attr = attrs_of(node)) {
        return *attr;
    }
    const Trees trees(context);
    return trees.attr(get_pos(context, node), trees.nil(), trees.atom("none"_atom));
}

Term set_attrs(ProcessContext &context, const Term &node, const Value &attr) {
    return edit_attrs(context, node, [&](const Trees &, const Term &) { return attr.term; });
}

Term copy_attrs(ProcessContext &context, const Term &source, const Term &target) {
    return set_attrs(context, target, {get_attrs(context, source)});
}

Term copy_comments(ProcessContext &context, const Term &source, const Target &target) {
    const auto attr = attrs_of(source);
    const auto com = attr ? attr_field(*attr, ATTR, 2) : Trees(context).atom("none"_atom);
    return edit_attrs(context, target.term,
                      [&](const Trees &trees, const Term &old) { return with_field(trees, old, ATTR, 2, com); });
}

Term join_comments(ProcessContext &context, const Term &source, const Term &target) {
    const auto with_pre = add_comments(context, target, 0, get_precomments(context, source));
    return add_comments(context, with_pre, 1, get_postcomments(context, source));
}

Term add_comments(ProcessContext &context, const Term &node, std::size_t field, const Term &comments) {
    return edit_attrs(context, node, [&](const Trees &trees, const Term &attr) {
        const auto com = attr_field(attr, ATTR, 2);
        if (is_none(com)) {
            return with_comments(trees, attr, field, comments);
        }
        const auto old = attr_field(com, COM, field);
        return with_comments(trees, attr, field, collections::append(context, {old}, comments));
    });
}

Term get_ann(ProcessContext &context, const Term &node) {
    const auto attr = attrs_of(node);
    return attr ? attr_field(*attr, ATTR, 1) : Trees(context).nil();
}

Term set_ann(ProcessContext &context, const Term &node, const Value &ann) {
    return edit_attrs(context, node,
                      [&](const Trees &trees, const Term &attr) { return with_field(trees, attr, ATTR, 1, ann.term); });
}

bool has_comments(const Term &node) {
    const auto attr = attrs_of(node);
    if (!attr) {
        return false;
    }
    const auto com = attr_field(*attr, ATTR, 2);
    if (is_none(com)) {
        return false;
    }
    return !(is_node(com, "com", 3) && element(com, 1).is_nil() && element(com, 2).is_nil());
}

Term get_precomments(ProcessContext &context, const Term &node) { return comments_of(context, node, 0); }

Term get_postcomments(ProcessContext &context, const Term &node) { return comments_of(context, node, 1); }

Term remove_comments(ProcessContext &context, const Term &node) {
    if (!attrs_of(node)) {
        return node;
    }
    return edit_attrs(context, node, [&](const Trees &trees, const Term &attr) {
        return with_field(trees, attr, ATTR, 2, trees.atom("none"_atom));
    });
}

Term set_precomments(ProcessContext &context, const Term &node, const Value &comments) {
    return edit_attrs(context, node, [&](const Trees &trees, const Term &attr) {
        return with_comments(trees, attr, 0, comments.term);
    });
}

Term set_postcomments(ProcessContext &context, const Term &node, const Value &comments) {
    return edit_attrs(context, node, [&](const Trees &trees, const Term &attr) {
        return with_comments(trees, attr, 1, comments.term);
    });
}

void badarg_of(ProcessContext &context, const Term &term) {
    const Trees trees(context);
    const std::array reason{trees.atom("badarg"_atom), term};
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = trees.tuple(reason).word()};
}

void badrecord(const Term &term) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::badrecord, .payload = term.word()};
}

void case_clause(ProcessContext &context, const Term &term) {
    static_cast<void>(context);
    string::case_clause(term);
}

Items elements(const Term &list) {
    Items items;
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        items.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return items;
}

Term first(const Term &list) {
    if (!list.is_cons()) {
        bad_argument();
    }
    return list.head_unchecked();
}

std::size_t length(const Term &list) { return elements(list).size(); }

bool list_of(const Term &term, std::size_t count) {
    auto rest = term;
    for (std::size_t at = 0; at < count; ++at) {
        if (!rest.is_cons()) {
            return false;
        }
        rest = rest.tail_unchecked();
    }
    return rest.is_nil();
}
} // namespace clause::runtime::builtins::syntax
