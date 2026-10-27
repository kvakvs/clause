#include "syntax.hpp"

// erl_syntax's positions, comments, annotations and attributes as builtins (plan 21 LIB77E2); nodes.cpp implements
// them over the attr and com records.
namespace clause::runtime::builtins::syntax {
namespace {
Term get_pos1(ProcessContext &context, const Term &node) { return get_pos(context, node); }

Term set_pos2(ProcessContext &context, Parts a) { return set_pos(context, a[0], {a[1]}); }

Term copy_pos2(ProcessContext &context, Parts a) { return set_pos(context, a[1], {get_pos(context, a[0])}); }

Term get_precomments1(ProcessContext &context, const Term &node) { return get_precomments(context, node); }

Term set_precomments2(ProcessContext &context, Parts a) { return set_precomments(context, a[0], {a[1]}); }

Term add_precomments2(ProcessContext &context, Parts a) { return add_comments(context, a[1], 0, a[0]); }

Term get_postcomments1(ProcessContext &context, const Term &node) { return get_postcomments(context, node); }

Term set_postcomments2(ProcessContext &context, Parts a) { return set_postcomments(context, a[0], {a[1]}); }

Term add_postcomments2(ProcessContext &context, Parts a) { return add_comments(context, a[1], 1, a[0]); }

Term has_comments1(ProcessContext &context, const Term &node) { return Trees(context).boolean(has_comments(node)); }

Term remove_comments1(ProcessContext &context, const Term &node) { return remove_comments(context, node); }

Term copy_comments2(ProcessContext &context, Parts a) { return copy_comments(context, a[0], {a[1]}); }

Term join_comments2(ProcessContext &context, Parts a) { return join_comments(context, a[0], a[1]); }

Term get_ann1(ProcessContext &context, const Term &node) { return get_ann(context, node); }

Term set_ann2(ProcessContext &context, Parts a) { return set_ann(context, a[0], {a[1]}); }

Term add_ann2(ProcessContext &context, Parts a) {
    return set_ann(context, a[1], {Trees(context).list(std::array{a[0]}, get_ann(context, a[1]))});
}

Term copy_ann2(ProcessContext &context, Parts a) { return set_ann(context, a[1], {get_ann(context, a[0])}); }

Term get_attrs1(ProcessContext &context, const Term &node) { return get_attrs(context, node); }

Term set_attrs2(ProcessContext &context, Parts a) { return set_attrs(context, a[0], {a[1]}); }

Term copy_attrs2(ProcessContext &context, Parts a) { return copy_attrs(context, a[0], a[1]); }

constexpr std::array ATTRS{
    typed_entry<get_pos1>("erl_syntax", "get_pos"),
    made_entry<set_pos2>("set_pos", 2),
    made_entry<copy_pos2>("copy_pos", 2),
    typed_entry<get_precomments1>("erl_syntax", "get_precomments"),
    made_entry<set_precomments2>("set_precomments", 2),
    made_entry<add_precomments2>("add_precomments", 2),
    typed_entry<get_postcomments1>("erl_syntax", "get_postcomments"),
    made_entry<set_postcomments2>("set_postcomments", 2),
    made_entry<add_postcomments2>("add_postcomments", 2),
    typed_entry<has_comments1>("erl_syntax", "has_comments"),
    typed_entry<remove_comments1>("erl_syntax", "remove_comments"),
    made_entry<copy_comments2>("copy_comments", 2),
    made_entry<join_comments2>("join_comments", 2),
    typed_entry<get_ann1>("erl_syntax", "get_ann"),
    made_entry<set_ann2>("set_ann", 2),
    made_entry<add_ann2>("add_ann", 2),
    made_entry<copy_ann2>("copy_ann", 2),
    typed_entry<get_attrs1>("erl_syntax", "get_attrs"),
    made_entry<set_attrs2>("set_attrs", 2),
    made_entry<copy_attrs2>("copy_attrs", 2),
};
} // namespace
} // namespace clause::runtime::builtins::syntax

namespace clause::runtime {
// Publish erl_syntax's attribute functions.
std::span<const BuiltinEntry> erl_syntax_attr_builtins() noexcept { return builtins::syntax::ATTRS; }
} // namespace clause::runtime
