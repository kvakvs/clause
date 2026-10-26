#include "checker_types.hpp"

// Added for the ASN.1 compiler (plan 21 step LIB84): tagging (X.680 clause 31) as asn1ct's merge_tags does it.
namespace clause::asn1::detail {
namespace {
// Apply operations to a tag list, innermost (last written) first. An empty list is an untagged CHOICE or open type,
// whose IMPLICIT tags become EXPLICIT.
void apply_operations(std::vector<TagValue> &tags, const std::vector<TagOperation> &operations) {
    for (auto operation = operations.rbegin(); operation != operations.rend(); ++operation) {
        if (operation->implicit_ && !tags.empty()) {
            tags.front() = operation->value_;
        } else {
            tags.insert(tags.begin(), operation->value_);
        }
    }
}

std::vector<TagOperation> operations(Checker &checker, const std::vector<TagSyntax> &tags, const Scope &scope) {
    const bool implicit_default = scope.module_->syntax_->tag_default_ == TagMode::implicit;
    std::vector<TagOperation> result;
    for (const auto &tag : tags) {
        const auto number = checker.integer(tag.number_, scope);
        const bool implicit =
            tag.mode_ == TagMode::implicit || (tag.mode_ == TagMode::module_default && implicit_default);
        result.push_back({{tag.class_, static_cast<std::uint64_t>(number)}, implicit});
    }
    return result;
}

void apply(Checker &checker, const NodeId id, std::vector<TagOperation> tags) {
    auto &node = checker.node(id);
    if (node.kind_ == NodeKind::reference) {
        checker.defer_tags(id, std::move(tags));
        return;
    }
    apply_operations(node.tags_, tags);
}
} // namespace

void apply_tags(Checker &checker, const NodeId id, const std::vector<TagSyntax> &tags, const Scope &scope) {
    if (!tags.empty()) {
        apply(checker, id, operations(checker, tags, scope));
    }
}

NodeId tag_component(Checker &checker, const NodeId id, const TagValue &tag) {
    Node copy;
    if (checker.node(id).kind_ == NodeKind::reference) {
        copy.kind_ = NodeKind::reference;
        copy.target_ = id;
        copy.named_ = checker.node(id).named_;
    } else {
        copy = checker.node(id);
    }
    const auto result = checker.add(std::move(copy));
    apply(checker, result, {{tag, true}});
    return result;
}

void Checker::defer_tags(const NodeId id, std::vector<TagOperation> operations) {
    deferred_tags_.emplace_back(id, std::move(operations));
}

// Give every reference its target's tags with its own applied, targets first.
void Checker::finish_references() {
    std::map<NodeId, std::vector<TagOperation>> pending;
    for (auto &[id, operations] : deferred_tags_) {
        auto &list = pending[id];
        list.insert(list.end(), operations.begin(), operations.end());
    }
    std::set<NodeId> done;
    std::function<void(NodeId)> finish = [&](const NodeId id) {
        if (nodes_[id].kind_ != NodeKind::reference || done.contains(id)) {
            return;
        }
        done.insert(id);
        finish(nodes_[id].target_);
        auto tags = nodes_[nodes_[id].target_].tags_;
        if (const auto own = pending.find(id); own != pending.end()) {
            apply_operations(tags, own->second);
        }
        nodes_[id].tags_ = std::move(tags);
    };
    for (NodeId id = 0; id < nodes_.size(); ++id) {
        finish(id);
    }
}
} // namespace clause::asn1::detail
