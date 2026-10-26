#include "records.hpp"
#include <algorithm>

// Added for the ASN.1 compiler (plan 21 step LIB84): .hrl records. Parameterized types contribute records from
// their unchecked definitions, other types from their checked nodes, as asn1ct_gen:gen_record does.
namespace clause::asn1::detail {
namespace {
std::string joined(const std::vector<std::string> &path) {
    std::string result;
    for (const auto &part : path) {
        result += (result.empty() ? "" : "_") + part;
    }
    return result;
}

std::vector<std::string> extended(std::vector<std::string> path, std::string part) {
    path.push_back(std::move(part));
    return path;
}

// A component as the record lists it: name, place in the extension structure and initial value.
struct Field {
    std::string name_;
    // 0: before the first ellipsis, 1: an extension addition, 2: after the second ellipsis.
    int section_ = 0;
    std::string initial_;
};

// asn1ct's do_gen_record_0: comments mark the extension structure.
RecordDefinition record(const std::string &name, const std::vector<Field> &fields, const int ellipses) {
    RecordDefinition result;
    result.name_ = name;
    const auto add = [&](const int section) {
        for (const auto &field : fields) {
            if (field.section_ == section) {
                result.fields_.push_back({field.name_, field.initial_, ""});
            }
        }
    };
    const auto comment = [&](const char *text) { result.fields_.push_back({"", "", text}); };
    add(0);
    if (ellipses == 0) {
        return result;
    }
    const bool additions = std::ranges::any_of(fields, [](const Field &field) { return field.section_ == 1; });
    if (ellipses == 1 && !additions) {
        comment("with extension mark");
        return result;
    }
    comment("with extensions");
    add(1);
    if (ellipses == 2) {
        comment("end of extensions");
        add(2);
    }
    return result;
}

std::string initial(const Presence presence) {
    if (presence == Presence::optional) {
        return "asn1_NOVALUE";
    }
    return presence == Presence::default_ ? "asn1_DEFAULT" : "";
}

// Records of checked types: inline SEQUENCEs and SETs, looking through CHOICEs and collections, never into types
// other assignments name.
class CheckedRecords {
  public:
    CheckedRecords(Checker &checker, CompiledModule &result) : checker_(checker), result_(result) {}

    void walk(NodeId id, const std::vector<std::string> &path, bool root);

  private:
    static bool walked(const Node &node, bool root);
    void constructed(const Node &node, const std::vector<std::string> &path);

    Checker &checker_;
    CompiledModule &result_;
};

void CheckedRecords::walk(const NodeId id, const std::vector<std::string> &path, const bool root) {
    const auto node = checker_.node(id);
    if (!walked(node, root)) {
        return;
    }
    switch (node.kind_) {
    case NodeKind::sequence:
    case NodeKind::set:
        constructed(node, path);
        break;
    case NodeKind::choice:
        for (const auto &alternative : node.components_) {
            walk(alternative.type_, extended(path, alternative.name_), false);
        }
        break;
    case NodeKind::sequence_of:
        walk(node.element_, extended(path, "SEQOF"), false);
        break;
    case NodeKind::set_of:
        walk(node.element_, extended(path, "SETOF"), false);
        break;
    default:
        break;
    }
}

// References, aliases and inner named types have records of their own elsewhere; asn1ct generates no record for a
// SET instance of a parameterized type inside another type.
bool CheckedRecords::walked(const Node &node, const bool root) {
    if (node.kind_ == NodeKind::reference || (root ? node.alias_ : !node.named_.empty())) {
        return false;
    }
    return node.kind_ != NodeKind::set || node.pname_.empty();
}

void CheckedRecords::constructed(const Node &node, const std::vector<std::string> &path) {
    std::vector<Field> fields;
    for (const auto &component : node.components_) {
        const int section = component.root2_ ? 2 : (component.addition_ != 0 ? 1 : 0);
        fields.push_back({component.name_, section, initial(component.presence_)});
    }
    result_.records_.push_back(record(joined(path), fields, node.ellipses_));
    for (const auto &component : node.components_) {
        walk(component.type_, extended(path, component.name_), false);
    }
}

// Records of a parameterized type's definition, read from its notation (its parameters are not bound).
class DefinitionRecords {
  public:
    explicit DefinitionRecords(CompiledModule &result) : result_(result) {}

    void walk(const TypeSyntax &syntax, const std::vector<std::string> &path);

  private:
    void constructed(const TypeSyntax &syntax, const std::vector<std::string> &path);

    CompiledModule &result_;
};

void DefinitionRecords::walk(const TypeSyntax &syntax, const std::vector<std::string> &path) {
    switch (syntax.kind_) {
    case TypeKind::sequence:
    case TypeKind::set:
        constructed(syntax, path);
        break;
    case TypeKind::choice:
        for (const auto &alternative : syntax.components_) {
            if (alternative.kind_ == ComponentSyntax::Kind::named) {
                walk(*alternative.type_, extended(path, alternative.name_));
            }
        }
        break;
    case TypeKind::sequence_of:
        walk(*syntax.element_, extended(path, "SEQOF"));
        break;
    case TypeKind::set_of:
        walk(*syntax.element_, extended(path, "SETOF"));
        break;
    default:
        break;
    }
}

void DefinitionRecords::constructed(const TypeSyntax &syntax, const std::vector<std::string> &path) {
    std::vector<Field> fields;
    int ellipses = 0;
    for (const auto &component : syntax.components_) {
        if (component.kind_ == ComponentSyntax::Kind::ellipsis) {
            ++ellipses;
        } else if (component.kind_ == ComponentSyntax::Kind::named) {
            fields.push_back({component.name_, ellipses, initial(component.presence_)});
        }
    }
    result_.records_.push_back(record(joined(path), fields, ellipses));
    for (const auto &component : syntax.components_) {
        if (component.kind_ == ComponentSyntax::Kind::named) {
            walk(*component.type_, extended(path, component.name_));
        }
    }
}
} // namespace

void collect_records(Checker &checker, const std::vector<const AssignmentSyntax *> &definitions,
                     const std::vector<NamedNode> &types, CompiledModule &result) {
    DefinitionRecords parameterized(result);
    for (const auto *assignment : definitions) {
        parameterized.walk(*assignment->type_, {assignment->name_});
    }
    CheckedRecords checked(checker, result);
    for (const auto &[name, id] : types) {
        checked.walk(id, {name}, true);
    }
}
} // namespace clause::asn1::detail
