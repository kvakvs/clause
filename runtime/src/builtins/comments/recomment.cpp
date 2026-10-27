#include "../collections/collections.hpp"
#include "../front_end/front_end.hpp"
#include "../syntax/syntax.hpp"

// erl_recomment as builtins (plan 21 LIB77E4b, docs/builtins.md#comments): comments of erl_comment_scan attached to
// the nodes of a syntax tree by line ranges, as OTP's extended tree of leaf, tree and list nodes decides; every
// rebuilt node becomes an erl_syntax tree with its original attributes.
namespace clause::runtime::builtins::comments {
namespace {
using syntax::Items;
using syntax::Trees;

// A comment {Line, Column, Indentation, Text}.
struct Comment {
    Term term;
    std::int64_t line = 0;
    std::int64_t column = 0;
    Term indent;
    Term text;
};

// The integer of a comment field; badarg for anything else.
std::int64_t field_value(const Term &field) {
    const auto value = small(field.word());
    if (!value) {
        bad_argument();
    }
    return *value;
}

// `{L, Col, Ind, Text} = C`: badmatch for anything but a four-tuple.
Comment comment_of(const Term &term) {
    if (!term.is_tuple() || term.tuple_size_unchecked() != 4) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch, .payload = term.word()};
    }
    return {term, field_value(syntax::element(term, 0)), field_value(syntax::element(term, 1)),
            syntax::element(term, 2), syntax::element(term, 3)};
}

// comment_delta/1: the lines a comment spans after its first.
std::int64_t comment_delta(const Comment &comment) {
    const auto lines = std::int64_t(syntax::length(comment.text));
    return lines > 0 ? lines - 1 : 0;
}

// exit(Reason).
[[noreturn]] void exit_with(const Term &reason) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_exit, .payload = reason.word()};
}

// minpos/2: the smaller of two lines that is positive, or 0.
std::int64_t minpos(std::int64_t left, std::int64_t right) {
    const auto low = std::min(left, right);
    const auto high = std::max(left, right);
    if (low >= 1) {
        return low;
    }
    return high >= 1 ? high : 0;
}

enum class Kind : std::uint8_t { leaf, tree, list };

// OTP's extended tree: a leaf with its syntax tree, a tree with its type and attributes, or a list of nodes; each
// with the line range it covers and the comments put before and after it, oldest first.
struct Node {
    Kind kind = Kind::list;
    std::int64_t min = 0;
    std::int64_t max = 0;
    // A leaf's syntax tree.
    Term value;
    // A tree node's type and attributes.
    Term type;
    Term attrs;
    std::vector<Comment> precomments;
    std::vector<Comment> postcomments;
    std::vector<Node> subtrees;
};

// The lines a node covers.
struct Range {
    std::int64_t min = 0;
    std::int64_t max = 0;
};

// leaf_node/3.
Node leaf(Range range, const Term &value) {
    Node node;
    node.kind = Kind::leaf;
    node.min = range.min;
    node.max = range.max;
    node.value = value;
    return node;
}

// next_min_in_list/1: the first line of the first leaf or tree node in a list, looking into list nodes; none if none.
std::optional<std::int64_t> next_min(std::span<const Node> nodes) {
    for (const auto &node : nodes) {
        if (node.kind != Kind::list) {
            return node.min;
        }
        if (const auto found = next_min(node.subtrees)) {
            return found;
        }
    }
    return std::nullopt;
}

// The recommenting of one process's trees.
class Recommenter final {
  public:
    explicit Recommenter(ProcessContext &context) : context_(context), trees_(context) {}

    // build_tree/1 and revert_tree/1.
    Node build(const Term &tree) const;
    Term revert(const Node &node) const;
    // insert/5 and insert_comments/2 (the comments below the node are returned, in order).
    void insert(Node &node, const Comment &comment) const;
    std::vector<Comment> insert_comments(Node &node, const std::vector<Comment> &comments) const;
    // recomment_forms_1/3 over the top-level nodes, and filter_forms/1.
    void recomment_forms(std::vector<Node> &forms, const Comment &comment, bool inserting) const;
    void place(std::vector<Node> &forms, std::size_t at, const Comment &comment, bool inserting) const;
    void filter_forms(std::vector<Node> &forms) const;
    // standalone_comment/1: a comment form of its own.
    Node standalone(const Comment &comment) const;

  private:
    std::int64_t line_of(const Term &tree) const;
    Node build_list(const Term &group) const;
    void insert_in_list(std::vector<Node> &nodes, const Comment &comment) const;
    Term with_comments(const Node &node, const Term &tree) const;
    Term expanded(const std::vector<Comment> &comments) const;
    Term located(const Term &tree, std::int64_t line) const;
    std::optional<std::pair<Term, Term>> file_attribute(const Node &node) const;

    // Builds the rebuilt nodes and the comments.
    ProcessContext &context_;
    // Builds tuples, lists and integers.
    Trees trees_;
};

// get_line/1: the line of a node's position; exit({bad_position, Pos}) when it has none.
std::int64_t Recommenter::line_of(const Term &tree) const {
    const auto pos = syntax::get_pos(context_, tree);
    const auto pair = collections::pair_of(pos);
    const auto direct = pair ? pair->first : pos;
    if (const auto line = small(direct.word()); line && direct.is_integer()) {
        return *line;
    }
    try {
        const auto location = front_end::anno_location(context_, pos);
        const auto located = collections::pair_of(location);
        if (const auto line = small((located ? located->first : location).word())) {
            return *line;
        }
    } catch (const BuiltinFailure &failure) {
        if (failure.term) {
            throw;
        }
    }
    exit_with(trees_.tuple(std::array{trees_.atom("bad_position"_atom), pos}));
}

Node Recommenter::build_list(const Term &group) const {
    Node list;
    for (const auto &item : syntax::elements(group)) {
        auto node = build(item);
        list.min = minpos(node.min, list.min);
        list.max = std::max(node.max, list.max);
        list.subtrees.push_back(std::move(node));
    }
    return list;
}

Node Recommenter::build(const Term &tree) const {
    const auto line = line_of(tree);
    const auto groups = syntax::subtrees(context_, tree);
    if (groups.is_nil()) {
        return leaf({line, line}, tree);
    }
    Node node;
    node.kind = Kind::tree;
    for (const auto &group : syntax::elements(groups)) {
        auto list = build_list(group);
        node.min = minpos(list.min, node.min);
        node.max = std::max(list.max, node.max);
        node.subtrees.push_back(std::move(list));
    }
    node.min = minpos(line, node.min);
    node.max = std::max(line, node.max);
    node.type = syntax::node_type(context_, tree);
    node.attrs = syntax::get_attrs(context_, tree);
    return node;
}

// erl_anno:new(Line) as the position of a comment: badarg for a negative line.
Term Recommenter::located(const Term &tree, std::int64_t line) const {
    if (line < 0) {
        bad_argument();
    }
    return syntax::set_pos(context_, tree, {trees_.integer(line)});
}

// expand_comments/1: comment trees with each comment's indentation as padding, oldest first.
Term Recommenter::expanded(const std::vector<Comment> &comments) const {
    Items items;
    for (const auto &comment : comments) {
        const auto made = syntax::make(context_, "comment", std::array{comment.indent, comment.text});
        items.push_back(located(made, comment.line));
    }
    return trees_.list(items);
}

Term Recommenter::with_comments(const Node &node, const Term &tree) const {
    auto result = tree;
    if (!node.precomments.empty()) {
        result = syntax::add_comments(context_, result, 0, expanded(node.precomments));
    }
    if (!node.postcomments.empty()) {
        result = syntax::add_comments(context_, result, 1, expanded(node.postcomments));
    }
    return result;
}

Term Recommenter::revert(const Node &node) const {
    if (node.kind == Kind::leaf) {
        return with_comments(node, node.value);
    }
    Items groups;
    for (const auto &list : node.subtrees) {
        Items items;
        for (const auto &item : list.subtrees) {
            items.push_back(revert(item));
        }
        groups.push_back(trees_.list(items));
    }
    const auto made = syntax::make_tree(context_, node.type, {trees_.list(groups)});
    return with_comments(node, syntax::set_attrs(context_, made, {node.attrs}));
}

// insert/5: into a list's nodes, before a node it precedes, after a node on one line (or a leaf), else into the
// subtrees whose range holds it.
void Recommenter::insert(Node &node, const Comment &comment) const {
    const auto inside =
        node.kind == Kind::list || (comment.line >= node.min && node.min != node.max && node.kind == Kind::tree);
    if (inside) {
        insert_in_list(node.subtrees, comment);
    } else if (comment.line < node.min) {
        node.precomments.push_back(comment);
    } else {
        node.postcomments.push_back(comment);
    }
}

// insert_in_list/5: the node whose range holds the line, preferring a later one that starts at or after it.
void Recommenter::insert_in_list(std::vector<Node> &nodes, const Comment &comment) const {
    for (std::size_t at = 0; at < nodes.size(); ++at) {
        const auto max = nodes[at].max;
        // -1 when no node follows (a node without a line range counts as none).
        const auto following = next_min(std::span(nodes).subspan(at + 1)).value_or(-1);
        const auto later = comment.line >= following && following >= max;
        if (following < 0 || (!later && comment.line <= max)) {
            insert(nodes[at], comment);
            return;
        }
    }
    const std::array reason{trees_.atom("bad_tree"_atom), syntax::element(comment.term, 0),
                            syntax::element(comment.term, 1)};
    exit_with(trees_.tuple(reason));
}

std::vector<Comment> Recommenter::insert_comments(Node &node, const std::vector<Comment> &comments) const {
    std::vector<Comment> below;
    for (const auto &comment : comments) {
        if (comment.line <= node.max) {
            insert(node, comment);
        } else {
            below.push_back(comment);
        }
    }
    return below;
}

Node Recommenter::standalone(const Comment &comment) const {
    const auto made = syntax::make(context_, "comment", std::array{trees_.integer(comment.column - 1), comment.text});
    const auto line = comment.line;
    return leaf({line, line + comment_delta(comment)}, located(made, line));
}

// recomment_forms_2/3: a comment goes before the first form it does not follow, standalone when a blank line
// separates them or it starts in column 1 over the form's first line, else into the form (Insert) or nowhere.
void Recommenter::recomment_forms(std::vector<Node> &forms, const Comment &comment, bool inserting) const {
    const auto end = comment.line + comment_delta(comment);
    for (std::size_t at = 0; at < forms.size(); ++at) {
        const auto trailing = at + 1 == forms.size() || end < forms[at + 1].min - 2;
        const auto after = comment.line > forms[at].max + 1 || (comment.line == forms[at].max + 1 && !trailing);
        if (!after) {
            place(forms, at, comment, inserting);
            return;
        }
    }
    forms.push_back(standalone(comment));
}

// The second half of recomment_forms_2/3: the comment at the form it does not follow.
void Recommenter::place(std::vector<Node> &forms, std::size_t at, const Comment &comment, bool inserting) const {
    const auto line = comment.line;
    const auto end = line + comment_delta(comment);
    const auto min = forms[at].min;
    if (end < min - 1) {
        forms.insert(forms.begin() + std::ptrdiff_t(at), standalone(comment));
    } else if (line < min) {
        forms[at].precomments.push_back(comment);
    } else if (comment.column <= 1 && line <= min && end >= min) {
        forms.insert(forms.begin() + std::ptrdiff_t(at + 1), standalone(comment));
    } else if (inserting) {
        insert(forms[at], comment);
    }
}

// check_file_attr/1: the file and line of a -file attribute node.
std::optional<std::pair<Term, Term>> Recommenter::file_attribute(const Node &node) const {
    if (node.kind != Kind::tree || !syntax::is_atom(node.type, "attribute") || node.subtrees.size() < 2) {
        return std::nullopt;
    }
    const auto &names = node.subtrees[0].subtrees;
    const auto &arguments = node.subtrees[1].subtrees;
    if (names.empty()) {
        return std::nullopt;
    }
    if (names[0].kind != Kind::leaf) {
        lists::no_clause();
    }
    const auto name = names[0].value;
    if (syntax::type_of(context_, name) != "atom" ||
        !syntax::is_atom(syntax::get(context_, name, "atom_value"), "file") || arguments.size() < 2) {
        return std::nullopt;
    }
    return std::pair{syntax::concrete(context_, revert(arguments[0])),
                     syntax::concrete(context_, revert(arguments[1]))};
}

// filter_forms/1's state: the file of the first -file attribute and the last line seen in it.
struct FileFilter {
    std::optional<Term> file;
    Term line;
    bool kill = false;
};

// Whether a -file attribute keeps its form: it continues the first file at a later line.
bool continues(FileFilter &filter, const std::pair<Term, Term> &attribute) {
    if (!filter.file) {
        filter.file = attribute.first;
        filter.line = attribute.second;
    }
    if (collections::same(*filter.file, attribute.first) && collections::compare(filter.line, attribute.second) <= 0) {
        filter.line = attribute.second;
        filter.kill = false;
        return true;
    }
    return false;
}

// filter_forms/1: the forms after a -file attribute that leaves the first file or goes back in it lose their line
// range (the attribute too, when the forms before it already lost theirs), so no comment lands in them.
void Recommenter::filter_forms(std::vector<Node> &forms) const {
    FileFilter filter;
    filter.line = trees_.integer(0);
    for (auto &form : forms) {
        const auto attribute = file_attribute(form);
        if (attribute && continues(filter, *attribute)) {
            continue;
        }
        if (filter.kill) {
            form.min = -1;
            form.max = -1;
        }
        filter.kill = filter.kill || attribute.has_value();
    }
}

// The roles of the comment list argument (typed.hpp's Named).
struct CommentsRole;
using Comments = Named<CommentsRole>;

std::vector<Comment> comments_of(const Comments &comments) {
    std::vector<Comment> parsed;
    for (const auto &comment : syntax::elements(comments.term)) {
        parsed.push_back(comment_of(comment));
    }
    return parsed;
}

// recomment_forms/3: the comments put among the forms of a form list, or into any other tree.
Term recomment(ProcessContext &context, const Term &tree, const Comments &comments, bool inserting) {
    const Recommenter recommenter(context);
    const auto forms = tree.is_list() ? syntax::make(context, "form_list", std::array{tree}) : tree;
    const auto parsed = comments_of(comments);
    if (syntax::type_of(context, forms) != "form_list") {
        auto node = recommenter.build(forms);
        for (const auto &comment : recommenter.insert_comments(node, parsed)) {
            node.postcomments.push_back(comment);
        }
        return recommenter.revert(node);
    }
    auto node = recommenter.build(syntax::flatten_form_list(context, forms));
    auto &list = node.subtrees.at(0).subtrees;
    recommenter.filter_forms(list);
    for (const auto &comment : parsed) {
        recommenter.recomment_forms(list, comment, inserting);
    }
    return recommenter.revert(node);
}

Term recomment_forms(ProcessContext &context, const Term &tree, const Comments &comments) {
    return recomment(context, tree, comments, true);
}

Term quick_recomment_forms(ProcessContext &context, const Term &tree, const Comments &comments) {
    return recomment(context, tree, comments, false);
}

// recomment_tree/2: {Tree, Comments} with the comments below the tree's lines left over.
Term recomment_tree(ProcessContext &context, const Term &tree, const Comments &comments) {
    const Recommenter recommenter(context);
    auto node = recommenter.build(tree);
    Items rest;
    for (const auto &comment : recommenter.insert_comments(node, comments_of(comments))) {
        rest.push_back(comment.term);
    }
    const Trees trees(context);
    return trees.tuple(std::array{recommenter.revert(node), trees.list(rest)});
}

constexpr std::array RECOMMENT{
    typed_entry<recomment_forms>("erl_recomment", "recomment_forms"),
    typed_entry<quick_recomment_forms>("erl_recomment", "quick_recomment_forms"),
    typed_entry<recomment_tree>("erl_recomment", "recomment_tree"),
};
} // namespace
} // namespace clause::runtime::builtins::comments

namespace clause::runtime {
// Publish erl_recomment's functions.
std::span<const BuiltinEntry> erl_recomment_builtins() noexcept { return builtins::comments::RECOMMENT; }
} // namespace clause::runtime
