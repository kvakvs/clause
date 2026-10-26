#include "checker_types.hpp"

// Added for the ASN.1 compiler (plan 21 step LIB84): constraints. Table constraints with component relations give
// open types their object tables (X.682 clause 10); value, size and alphabet constraints give PER its effective
// constraints (X.691 clause 10.3). Other constraints are not seen by encoders.
namespace clause::asn1::detail {
namespace {
using Kind = ConstraintSyntax::Kind;

// The table constraint inside an extensible or exception-specified constraint.
const ConstraintSyntax *table_of(const ConstraintSyntax &constraint) {
    if (constraint.kind_ == Kind::table) {
        return &constraint;
    }
    if ((constraint.kind_ == Kind::exception || constraint.kind_ == Kind::extensible) &&
        !constraint.children_.empty() && constraint.children_.front()) {
        return table_of(*constraint.children_.front());
    }
    return nullptr;
}

// An effective constraint while it is computed: PER-visible bounds and whether it is extensible. An absent value
// or size means "not constrained".
struct Effective {
    std::optional<Bounds> value_;
    std::optional<Bounds> size_;
    bool extensible_ = false;
};

std::optional<Bounds> hull(const std::optional<Bounds> &left, const std::optional<Bounds> &right) {
    if (!left || !right) {
        return std::nullopt;
    }
    Bounds result;
    result.present_ = true;
    if (left->lower_ && right->lower_) {
        result.lower_ = std::min(*left->lower_, *right->lower_);
    }
    if (left->upper_ && right->upper_) {
        result.upper_ = std::max(*left->upper_, *right->upper_);
    }
    return result;
}

std::optional<BigInt> tighter(const std::optional<BigInt> &left, const std::optional<BigInt> &right, bool lower) {
    if (!left || !right) {
        return left ? left : right;
    }
    return lower ? std::max(*left, *right) : std::min(*left, *right);
}

std::optional<Bounds> meet(const std::optional<Bounds> &left, const std::optional<Bounds> &right) {
    if (!left || !right) {
        return left ? left : right;
    }
    Bounds result;
    result.present_ = true;
    result.lower_ = tighter(left->lower_, right->lower_, true);
    result.upper_ = tighter(left->upper_, right->upper_, false);
    return result;
}

// Computes the effective constraint of one constraint notation on a type.
class Evaluator {
  public:
    Evaluator(Checker &checker, const Scope &scope, const Node &node) : checker_(checker), scope_(scope), node_(node) {}

    Effective evaluate(const ConstraintSyntax &constraint);

  private:
    Effective combine(const ConstraintSyntax &constraint, bool all);
    Effective extensible(const ConstraintSyntax &constraint);
    Effective size(const ConstraintSyntax &constraint);
    Effective range(const ConstraintSyntax &constraint);
    Effective single(const ConstraintSyntax &constraint);
    Effective contained(const ConstraintSyntax &constraint);
    std::optional<BigInt> endpoint(TokenRange range, bool open, int step);

    Checker &checker_;
    const Scope &scope_;
    const Node &node_;
};

Effective Evaluator::evaluate(const ConstraintSyntax &constraint) {
    switch (constraint.kind_) {
    case Kind::union_:
        return combine(constraint, false);
    case Kind::intersection:
        return combine(constraint, true);
    case Kind::extensible:
        return extensible(constraint);
    case Kind::exception:
        return evaluate(*constraint.children_.front());
    case Kind::size:
        return size(constraint);
    case Kind::value_range:
        return range(constraint);
    case Kind::single_value:
        return single(constraint);
    case Kind::contained_subtype:
        return contained(constraint);
    default:
        return {};
    }
}

// A union is the hull of its operands, an intersection their overlap.
Effective Evaluator::combine(const ConstraintSyntax &constraint, const bool all) {
    auto result = evaluate(*constraint.children_.front());
    for (std::size_t i = 1; i < constraint.children_.size(); ++i) {
        const auto next = evaluate(*constraint.children_[i]);
        result.value_ = all ? meet(result.value_, next.value_) : hull(result.value_, next.value_);
        result.size_ = all ? meet(result.size_, next.size_) : hull(result.size_, next.size_);
        result.extensible_ = result.extensible_ || next.extensible_;
    }
    return result;
}

// `root, ...` and `root, ..., additions`: the root's bounds, marked extensible.
Effective Evaluator::extensible(const ConstraintSyntax &constraint) {
    Effective result;
    if (constraint.children_.front()) {
        result = evaluate(*constraint.children_.front());
    }
    result.extensible_ = true;
    return result;
}

Effective Evaluator::size(const ConstraintSyntax &constraint) {
    Node integer;
    integer.kind_ = NodeKind::integer;
    const auto inner = Evaluator(checker_, scope_, integer).evaluate(*constraint.children_.front());
    Effective result;
    result.size_ = inner.value_;
    if (result.size_) {
        result.size_->extensible_ = inner.extensible_;
    }
    return result;
}

std::optional<BigInt> Evaluator::endpoint(const TokenRange range, const bool open, const int step) {
    auto parser = checker_.parser(scope_, range);
    if (parser.is("MIN") || parser.is("MAX")) {
        return std::nullopt;
    }
    const auto value = checker_.integer(range, scope_, node_.named_numbers_);
    return open ? BigInt(value + step) : value;
}

Effective Evaluator::range(const ConstraintSyntax &constraint) {
    if (node_.kind_ != NodeKind::integer) {
        return {};
    }
    Bounds bounds;
    bounds.present_ = true;
    bounds.lower_ = endpoint(constraint.value_, constraint.lower_open_, 1);
    bounds.upper_ = endpoint(constraint.upper_, constraint.upper_open_, -1);
    return {bounds, std::nullopt, false};
}

Effective Evaluator::single(const ConstraintSyntax &constraint) {
    if (node_.kind_ != NodeKind::integer) {
        return {};
    }
    const auto value = checker_.integer(constraint.value_, scope_, node_.named_numbers_);
    Bounds bounds;
    bounds.present_ = true;
    bounds.lower_ = value;
    bounds.upper_ = value;
    return {bounds, std::nullopt, false};
}

// A contained subtype contributes the constraints of the type it names.
Effective Evaluator::contained(const ConstraintSyntax &constraint) {
    const auto id = checker_.type(*constraint.type_, scope_, {});
    const auto &node = checker_.content(id);
    Effective result;
    if (node.value_.present_) {
        result.value_ = node.value_;
    }
    if (node.size_.present_) {
        result.size_ = node.size_;
    }
    return result;
}

// Apply serial constraints `T (C1) (C2)`: bounds intersect, and the last one decides extensibility.
void apply_effective(Node &node, const Effective &effective) {
    if (effective.value_) {
        auto value = meet(node.value_.present_ ? std::optional<Bounds>(node.value_) : std::nullopt, effective.value_);
        node.value_ = *value;
        node.value_.extensible_ = effective.extensible_;
    }
    if (effective.size_) {
        auto size = meet(node.size_.present_ ? std::optional<Bounds>(node.size_) : std::nullopt, effective.size_);
        node.size_ = *size;
        node.size_.extensible_ = effective.size_->extensible_ || effective.extensible_;
    }
}
} // namespace

void Checker::constrain(const NodeId id, const TypeSyntax &syntax, const Scope &scope, const Context &context) {
    for (const auto &constraint : syntax.constraints_) {
        if (const auto *table = table_of(*constraint)) {
            table_constraint(id, syntax, *table, scope, context);
            continue;
        }
        const auto node = content(id);
        const auto effective = Evaluator(*this, scope, node).evaluate(*constraint);
        apply_effective(nodes_[id], effective);
    }
}
} // namespace clause::asn1::detail
