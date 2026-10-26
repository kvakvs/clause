#include "generate.hpp"
#include <array>
#include <deque>
#include <map>

// Added for the ASN.1 compiler (plan 21 step LIB84): the schema term of a compiled module (docs/asn1.md#schema).
// Nodes reachable from the module's types are renumbered from 0; every node is a tuple {Kind, Tags, ...}.
namespace clause::asn1 {
namespace {
// The flag bits of the schema's Flags field, shared with runtime/src/builtins/asn1/schema.hpp.
constexpr int FLAG_DER = 1;
constexpr int FLAG_MAPS = 2;
constexpr int FLAG_LEGACY_TYPES = 4;
constexpr int FLAG_LEGACY_BIT_STRING = 8;
constexpr int FLAG_COMPACT_BIT_STRING = 16;
constexpr int FLAG_UNDEC_REST = 32;
// Added for PER: uper's no_final_padding.
constexpr int FLAG_NO_FINAL_PADDING = 64;

Term small(const std::uint64_t value) { return Term::integer(BigInt(value)); }

Term boolean(const bool value) { return Term::atom(value ? "true" : "false"); }

// A tag as one integer: the number shifted left by two, or-ed with the class (universal 0 ... private 3).
Term tag(const TagValue &value) { return small((value.number_ << 2U) | static_cast<std::uint64_t>(value.class_)); }

Term bounds(const Bounds &value) {
    if (!value.present_) {
        return Term::atom("none");
    }
    std::vector<Term> items{value.lower_ ? Term::integer(*value.lower_) : Term::atom("MIN"),
                            value.upper_ ? Term::integer(*value.upper_) : Term::atom("MAX"),
                            boolean(value.extensible_)};
    // Added for PER: the root values of an extensible single-value list.
    if (!value.root_values_.empty()) {
        std::vector<Term> roots;
        roots.reserve(value.root_values_.size());
        for (const auto &root : value.root_values_) {
            roots.push_back(Term::integer(root));
        }
        items.push_back(Term::list(std::move(roots)));
    }
    return Term::tuple(std::move(items));
}

// A PER permitted alphabet: none, or the code points in order. Added for PER.
Term alphabet(const std::vector<std::uint32_t> &characters) {
    if (characters.empty()) {
        return Term::atom("none");
    }
    std::vector<Term> items;
    items.reserve(characters.size());
    for (const auto c : characters) {
        items.push_back(small(c));
    }
    return Term::list(std::move(items));
}

Term named(const std::vector<std::pair<std::string, BigInt>> &numbers, const std::size_t begin, const std::size_t end) {
    std::vector<Term> items;
    for (auto i = begin; i < end; ++i) {
        items.push_back(Term::tuple({Term::atom(numbers[i].first), Term::integer(numbers[i].second)}));
    }
    return Term::list(std::move(items));
}

// Builds node tuples, numbering the nodes it reaches.
class SchemaBuilder {
  public:
    explicit SchemaBuilder(const CompiledModule &module) : module_(module) {}

    Term index(NodeId id);
    Term nodes();

  private:
    Term node(const Node &node);
    Term kind_fields(const Node &node);
    Term numbered_fields(const Node &node);
    Term constructed_fields(const Node &node);
    Term other_fields(const Node &node);
    Term components(const Node &node);
    Term alternatives(const Node &node);
    Term table(const Node &node);

    const CompiledModule &module_;
    std::map<NodeId, std::uint64_t> numbers_;
    std::deque<NodeId> pending_;
};

Term SchemaBuilder::index(const NodeId id) {
    const auto [found, added] = numbers_.emplace(id, numbers_.size());
    if (added) {
        pending_.push_back(id);
    }
    return small(found->second);
}

Term SchemaBuilder::nodes() {
    std::vector<Term> result;
    while (!pending_.empty()) {
        const auto id = pending_.front();
        pending_.pop_front();
        result.push_back(node(module_.nodes_[id]));
    }
    return Term::tuple(std::move(result));
}

Term SchemaBuilder::node(const Node &node) {
    std::vector<Term> tags;
    tags.reserve(node.tags_.size());
    for (const auto &value : node.tags_) {
        tags.push_back(tag(value));
    }
    auto fields = kind_fields(node);
    fields.items_.insert(fields.items_.begin(), {small(static_cast<std::uint64_t>(node.kind_)), Term::list(tags)});
    return fields;
}

// The fields of the kinds with named numbers.
Term SchemaBuilder::numbered_fields(const Node &node) {
    switch (node.kind_) {
    case NodeKind::integer:
        return Term::tuple({named(node.named_numbers_, 0, node.named_numbers_.size()), bounds(node.value_)});
    case NodeKind::enumerated:
        return Term::tuple({named(node.named_numbers_, 0, node.enumeration_root_),
                            named(node.named_numbers_, node.enumeration_root_, node.named_numbers_.size()),
                            boolean(node.extensible_)});
    default:
        return Term::tuple({named(node.named_numbers_, 0, node.named_numbers_.size()), bounds(node.size_)});
    }
}

Term SchemaBuilder::kind_fields(const Node &node) {
    switch (node.kind_) {
    case NodeKind::integer:
    case NodeKind::enumerated:
    case NodeKind::bit_string:
        return numbered_fields(node);
    case NodeKind::sequence:
    case NodeKind::set:
    case NodeKind::sequence_of:
    case NodeKind::set_of:
    case NodeKind::choice:
        return constructed_fields(node);
    default:
        return other_fields(node);
    }
}

// The fields of the kinds with components or elements.
Term SchemaBuilder::constructed_fields(const Node &node) {
    switch (node.kind_) {
    case NodeKind::sequence:
    case NodeKind::set:
        return Term::tuple({Term::atom(node.record_), components(node), boolean(node.extensible_)});
    case NodeKind::choice:
        return Term::tuple({alternatives(node), boolean(node.extensible_)});
    default:
        return Term::tuple({index(node.element_), bounds(node.size_)});
    }
}

// The fields of open types, references, restricted strings and the remaining kinds, which only have a size.
Term SchemaBuilder::other_fields(const Node &node) {
    switch (node.kind_) {
    case NodeKind::open_type:
        return Term::tuple({table(node)});
    case NodeKind::reference:
        return Term::tuple({index(node.target_)});
    case NodeKind::restricted_string:
        return Term::tuple({small(string_universal_tag(node.string_)), bounds(node.size_),
                            alphabet(node.alphabet_)});
    default:
        return Term::tuple({bounds(node.size_)});
    }
}

// {Name, Node, Presence (0 required, 1 optional, 2 default), Default, Addition, Root2, Group}.
Term SchemaBuilder::components(const Node &node) {
    std::vector<Term> items;
    items.reserve(node.components_.size());
    for (const auto &component : node.components_) {
        items.push_back(Term::tuple({Term::atom(component.name_), index(component.type_),
                                     small(static_cast<std::uint64_t>(component.presence_)),
                                     component.default_ ? *component.default_ : Term::atom("undefined"),
                                     small(component.addition_), boolean(component.root2_),
                                     boolean(component.group_)}));
    }
    return Term::tuple(std::move(items));
}

// {Name, Node, Addition}.
Term SchemaBuilder::alternatives(const Node &node) {
    std::vector<Term> items;
    items.reserve(node.components_.size());
    for (const auto &alternative : node.components_) {
        items.push_back(
            Term::tuple({Term::atom(alternative.name_), index(alternative.type_), small(alternative.addition_)}));
    }
    return Term::tuple(std::move(items));
}

// none, or {LevelsUp, Path, #{Key => Node | none}, Extensible, Field, KeyField}.
Term SchemaBuilder::table(const Node &node) {
    if (!node.table_) {
        return Term::atom("none");
    }
    const auto &table = *node.table_;
    std::vector<Term> path;
    path.reserve(table.path_.size());
    for (const auto &name : table.path_) {
        path.push_back(Term::atom(name));
    }
    std::vector<Term> entries;
    entries.reserve(table.entries_.size() * 2);
    for (const auto &[key, type] : table.entries_) {
        entries.push_back(key);
        entries.push_back(type == NO_NODE ? Term::atom("none") : index(type));
    }
    return Term::tuple({small(table.levels_up_), Term::list(std::move(path)), Term::map(std::move(entries)),
                        boolean(table.extensible_), Term::atom(table.field_), Term::atom(table.key_)});
}

int flags(const CompileOptions &options) {
    int result = options.der_ ? FLAG_DER : 0;
    result |= options.maps_ ? FLAG_MAPS : 0;
    result |= options.legacy_erlang_types_ ? FLAG_LEGACY_TYPES : 0;
    result |= options.bit_string_format_ == "legacy" ? FLAG_LEGACY_BIT_STRING : 0;
    result |= options.bit_string_format_ == "compact" ? FLAG_COMPACT_BIT_STRING : 0;
    result |= options.undec_rest_ ? FLAG_UNDEC_REST : 0;
    result |= options.no_final_padding_ ? FLAG_NO_FINAL_PADDING : 0;
    return result;
}

const char *rule_name(const EncodingRule rule) {
    static constexpr std::array NAMES = {"ber", "per", "uper"};
    return NAMES[static_cast<std::size_t>(rule)];
}
} // namespace

// The universal tag of each restricted character string type (X.680 clause 41).
std::uint64_t string_universal_tag(const std::string_view name) {
    struct Entry {
        std::string_view name_;
        std::uint64_t tag_;
    };

    static constexpr std::array TAGS = {
        Entry{"UTF8String", 12},      Entry{"NumericString", 18},  Entry{"PrintableString", 19},
        Entry{"TeletexString", 20},   Entry{"VideotexString", 21}, Entry{"IA5String", 22},
        Entry{"GraphicString", 25},   Entry{"VisibleString", 26},  Entry{"GeneralString", 27},
        Entry{"UniversalString", 28}, Entry{"BMPString", 30}};
    for (const auto &entry : TAGS) {
        if (entry.name_ == name) {
            return entry.tag_;
        }
    }
    return 0;
}

// {asn1_schema, 1, Rule, Flags, #{TypeName => Node}, {Node0, Node1, ...}}.
Term schema(const CompiledModule &module, const CompileOptions &options) {
    SchemaBuilder builder(module);
    std::vector<Term> types;
    for (const auto &[name, id] : module.types_) {
        types.push_back(Term::atom(name));
        types.push_back(builder.index(id));
    }
    auto nodes = builder.nodes();
    return Term::tuple({Term::atom("asn1_schema"), small(1), Term::atom(rule_name(options.rule_)),
                        small(static_cast<std::uint64_t>(flags(options))), Term::map(std::move(types)),
                        std::move(nodes)});
}
} // namespace clause::asn1
