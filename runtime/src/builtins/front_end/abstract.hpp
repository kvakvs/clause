#pragma once
#include "../support.hpp"
#include <vector>

// Abstract-format helpers of erl_parse (plan 21 step LIB31): the annotation walk of map_anno/2 and fold_anno/3,
// first_anno/1 and last_anno/1 as the grammar's actions use them, and normalise/1.
namespace clause::runtime::builtins::front_end::abstract {
// Why a walk stopped or went on.
enum class Visit : std::uint8_t { next, stop };

// The annotations of `root` in OTP's modify_anno1/3 order (depth first, left to right); a term it has no clause for
// raises function_clause. `visit` answers whether to go on.
template <typename Visitor> void walk_annos(const Term &root, Visitor &&visit);

// The parts of one node: its annotation (when it has one) and its children in visiting order.
struct Parts {
    std::optional<Term> anno;
    std::vector<Term> children;
};

// modify_anno1/3's clause for `node`.
Parts parts_of(const Term &node);

// loc_lte/2: Location1 =< Location2, a bare line standing for {Line, 1}.
bool location_lte(ProcessContext &context, const Term &left, const Term &right);
// first_anno/1: the annotation with the smallest location, the walk stopping at the first larger one.
Term first_anno(ProcessContext &context, const Term &node);
// erl_anno:location/1 of first_anno/1.
Term first_location(ProcessContext &context, const Term &node);
// last_anno/1: the annotation with the largest location, set to its end location when it has one.
Term last_anno(ProcessContext &context, const Term &node);
// map_anno(fun(_) -> Anno end, Node): every annotation of `node` replaced by `anno`, in modify_anno1/3's clauses.
// Added for the C++ erl_expand_records.
Term replace_annos(ProcessContext &context, const Term &node, const Term &anno);
// normalise/1: the term an abstract literal stands for; error({badarg, Node}) for anything else.
Term normalise(ProcessContext &context, const Term &node);

template <typename Visitor> void walk_annos(const Term &root, Visitor &&visit) {
    std::vector<Term> pending{root};
    while (!pending.empty()) {
        const auto node = pending.back();
        pending.pop_back();
        auto parts = parts_of(node);
        if (parts.anno && visit(*parts.anno) == Visit::stop) {
            return;
        }
        pending.insert(pending.end(), parts.children.rbegin(), parts.children.rend());
    }
}
} // namespace clause::runtime::builtins::front_end::abstract
