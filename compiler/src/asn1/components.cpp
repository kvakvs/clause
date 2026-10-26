#include "components.hpp"
#include "checker_types.hpp"
#include <algorithm>

// Added for the ASN.1 compiler (plan 21 step LIB84): component lists of X.680 clauses 25-29 and automatic tagging
// as asn1ct numbers it (root components first, then extension additions).
namespace clause::asn1::detail {
namespace {
// A component in written order with where its notation resolves.
struct Item {
    const ComponentSyntax *syntax_ = nullptr;
    Scope scope_;
    // Where the item sits: root (0), extension addition (its index) or the root after the second ellipsis.
    std::uint32_t addition_ = 0;
    bool root2_ = false;
    // Inside an extension addition group. Added for PER.
    bool group_ = false;
    // Items brought in by COMPONENTS OF do not decide whether automatic tagging applies.
    bool included_ = false;
};

// Walks a component list, tracking ellipses and addition groups.
class Layout {
  public:
    Layout(Checker &checker, const Scope &scope) : checker_(checker), scope_(scope) {}

    void walk(const TypeSyntax &syntax);
    std::vector<Item> items_;
    bool extensible_ = false;
    std::uint8_t ellipses_ = 0;

  private:
    void named(const ComponentSyntax &entry);
    void ellipsis();
    void include(const ComponentSyntax &entry);
    const TypeSyntax &included_body(const TypeSyntax &type, Scope &scope);

    Checker &checker_;
    const Scope &scope_;
    bool extension_ = false;
    bool root2_ = false;
    bool group_ = false;
    std::uint32_t addition_ = 0;
};

void Layout::walk(const TypeSyntax &syntax) {
    for (const auto &entry : syntax.components_) {
        switch (entry.kind_) {
        case ComponentSyntax::Kind::named:
            named(entry);
            break;
        case ComponentSyntax::Kind::components_of:
            include(entry);
            break;
        case ComponentSyntax::Kind::ellipsis:
            ellipsis();
            break;
        case ComponentSyntax::Kind::group_begin:
            group_ = true;
            ++addition_;
            break;
        case ComponentSyntax::Kind::group_end:
            group_ = false;
            break;
        }
    }
}

void Layout::ellipsis() {
    extensible_ = true;
    ellipses_ = static_cast<std::uint8_t>(std::min(ellipses_ + 1, 2));
    if (extension_) {
        root2_ = true;
    }
    extension_ = true;
}

void Layout::named(const ComponentSyntax &entry) {
    Item item{&entry, scope_};
    if (root2_) {
        item.root2_ = true;
    } else if (extension_) {
        item.addition_ = group_ ? addition_ : ++addition_;
        item.group_ = group_;
    }
    items_.push_back(item);
}

// COMPONENTS OF T: the root components of SEQUENCE (or SET) T, checked again here as asn1ct does.
void Layout::include(const ComponentSyntax &entry) {
    Scope scope = scope_;
    const auto &body = included_body(*entry.type_, scope);
    bool extension = false;
    std::uint32_t ellipses = 0;
    for (const auto &component : body.components_) {
        if (component.kind_ == ComponentSyntax::Kind::ellipsis) {
            extension = ++ellipses == 1;
        } else if (component.kind_ == ComponentSyntax::Kind::named && !extension) {
            Item item{&component, scope};
            item.root2_ = root2_;
            item.included_ = true;
            items_.push_back(item);
        }
    }
}

// The SEQUENCE or SET body a COMPONENTS OF names, following type references.
const TypeSyntax &Layout::included_body(const TypeSyntax &type, Scope &scope) {
    const auto *current = &type;
    while (current->kind_ == TypeKind::reference) {
        const auto found = checker_.find(current->reference_, scope);
        if (found.assignment_ == nullptr || found.assignment_->kind_ != AssignmentSyntax::Kind::type ||
            found.assignment_->parameterized_) {
            checker_.fail(scope, current->token_, "COMPONENTS OF needs a SEQUENCE or SET type");
        }
        scope = found.scope_;
        current = found.assignment_->type_.get();
    }
    if (current->kind_ != TypeKind::sequence && current->kind_ != TypeKind::set) {
        checker_.fail(scope, type.token_, "COMPONENTS OF needs a SEQUENCE or SET type");
    }
    return *current;
}

// asn1ct's tag_nums: root components (before and after the extensions) count from 0, additions continue after them.
std::vector<std::uint64_t> automatic_numbers(const std::vector<Item> &items) {
    std::uint64_t root = 0;
    for (const auto &item : items) {
        root += item.addition_ == 0 ? 1 : 0;
    }
    std::vector<std::uint64_t> numbers;
    numbers.reserve(items.size());
    std::uint64_t next_root = 0;
    std::uint64_t next_addition = root;
    for (const auto &item : items) {
        numbers.push_back(item.addition_ == 0 ? next_root++ : next_addition++);
    }
    return numbers;
}

bool automatic(const ModuleSyntax &module, const std::vector<Item> &items) {
    if (!module.automatic_tags_) {
        return false;
    }
    for (const auto &item : items) {
        if (!item.included_ && !item.syntax_->type_->tags_.empty()) {
            return false;
        }
    }
    return true;
}

Component check_item(Checker &checker, const Item &item, const Context &context) {
    const auto &syntax = *item.syntax_;
    Component component;
    component.name_ = syntax.name_;
    component.presence_ = syntax.presence_;
    component.addition_ = item.addition_;
    component.root2_ = item.root2_;
    component.group_ = item.group_;
    component.type_ = checker.type(*syntax.type_, item.scope_, context.with({syntax.name_, false}));
    return component;
}
} // namespace

void check_components(Checker &checker, const NodeId id, const TypeSyntax &syntax, const Scope &scope,
                      const Context &context) {
    Layout layout(checker, scope);
    layout.walk(syntax);
    const bool tags = automatic(*scope.module_->syntax_, layout.items_);
    const auto numbers = automatic_numbers(layout.items_);
    std::vector<Component> components;
    for (std::size_t i = 0; i < layout.items_.size(); ++i) {
        const auto &item = layout.items_[i];
        auto component = check_item(checker, item, context);
        if (tags) {
            component.type_ = tag_component(checker, component.type_, {TagClass::context, numbers[i]});
        }
        if (item.syntax_->presence_ == Presence::default_) {
            component.default_ = checker.value(item.syntax_->default_, component.type_, item.scope_);
        }
        component.textual_ = static_cast<std::uint32_t>(i);
        components.push_back(std::move(component));
    }
    auto &node = checker.node(id);
    node.components_ = std::move(components);
    node.extensible_ = layout.extensible_ || scope.module_->syntax_->extensibility_implied_;
    node.ellipses_ = layout.ellipses_;
}
} // namespace clause::asn1::detail
