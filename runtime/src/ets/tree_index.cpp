#include "index.hpp"
#include <iterator>
#include <set>
#include <utility>

// The tree of ordered_set tables (plan 21 step LIB10A, docs/runtime.md#ets-tables): one node per key in term order,
// where keys that compare equal (1 and 1.0) are one key. A walk steps from any key, held or not, to the nearest
// one, so fixation changes nothing here.
namespace clause::runtime::detail::ets {
namespace {
class TreeIndex final : public Index {
  public:
    explicit TreeIndex(std::size_t keypos) : nodes_(Order{keypos}) {}

    const Objects *find(const Key &key) const override {
        const auto found = nodes_.find(key);
        return found == nodes_.end() ? nullptr : &found->objects;
    }

    void insert(const Key &key, DetachedTerm object) override {
        const auto at = nodes_.lower_bound(key);
        if (at != nodes_.end() && !nodes_.key_comp()(key, *at)) {
            // An equal key: the new object replaces the old one, its key too, in the same place of the order.
            replace_front(*at, std::move(object));
            return;
        }
        Node node;
        node.objects.push_back(std::move(object));
        added(node.objects.front());
        nodes_.insert(at, std::move(node));
    }

    void replace(const Key &key, DetachedTerm object) override { replace_front(*nodes_.find(key), std::move(object)); }

    bool erase(const Key &key) override {
        const auto found = nodes_.find(key);
        if (found == nodes_.end()) {
            return false;
        }
        removed(found->objects.front());
        nodes_.erase(found);
        return true;
    }

    void erase_object(const Key &key, Word object) override {
        const auto found = nodes_.find(key);
        if (found != nodes_.end() && same_object(found->objects.front().word(), object)) {
            removed(found->objects.front());
            nodes_.erase(found);
        }
    }

    void clear() override {
        for (const auto &node : nodes_) {
            removed(node.objects.front());
        }
        nodes_.clear();
    }

    const Objects *first() const override { return nodes_.empty() ? nullptr : &nodes_.begin()->objects; }

    const Objects *last() const override { return nodes_.empty() ? nullptr : &nodes_.rbegin()->objects; }

    Step next(const Key &key) const override {
        const auto after = nodes_.upper_bound(key);
        return {.objects = after == nodes_.end() ? nullptr : &after->objects};
    }

    Step prev(const Key &key) const override {
        const auto at = nodes_.lower_bound(key);
        return {.objects = at == nodes_.begin() ? nullptr : &std::prev(at)->objects};
    }

    void each(const std::function<void(const Objects &)> &visit) const override {
        for (const auto &node : nodes_) {
            visit(node.objects);
        }
    }

    std::size_t slots() const override { return nodes_.size(); }

    void slot(std::size_t index, std::vector<Word> &objects) const override {
        objects.push_back(std::next(nodes_.begin(), static_cast<std::ptrdiff_t>(index))->objects.front().word());
    }

    void fix() override {}

    void unfix() override {}

  private:
    // One key's object. Replacing it with an object of an equal key keeps the node's place in the order.
    struct Node {
        mutable Objects objects;
    };

    // Term order of node keys, looked up by Key too.
    struct Order {
        using is_transparent = void;
        std::size_t keypos;

        Word key(const Node &node) const noexcept { return key_of(node.objects.front().word(), keypos); }

        bool operator()(const Node &left, const Node &right) const noexcept {
            return key_order(key(left), key(right)) < 0;
        }

        bool operator()(const Node &left, const Key &right) const noexcept {
            return key_order(key(left), right.word) < 0;
        }

        bool operator()(const Key &left, const Node &right) const noexcept {
            return key_order(left.word, key(right)) < 0;
        }
    };

    void replace_front(const Node &node, DetachedTerm object) {
        auto &front = node.objects.front();
        removed(front);
        front = std::move(object);
        added(front);
    }

    std::set<Node, Order> nodes_;
};
} // namespace

std::unique_ptr<Index> tree_index(std::size_t keypos) { return std::make_unique<TreeIndex>(keypos); }
} // namespace clause::runtime::detail::ets
