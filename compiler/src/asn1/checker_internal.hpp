#pragma once
// Added for the ASN.1 compiler (plan 21 step LIB84): the checker's scopes, information object model and entry
// points shared by its translation units.
#include "checker.hpp"
#include "parser.hpp"
#include <map>
#include <set>

namespace clause::asn1::detail {
struct ModuleScope {
    std::shared_ptr<ModuleSyntax> syntax_;
    std::map<std::string, const AssignmentSyntax *, std::less<>> assignments_;
    // Imported symbol -> the module it comes from.
    std::map<std::string, std::string, std::less<>> imports_;
};

struct Binding;
using Bindings = std::map<std::string, Binding, std::less<>>;

// Where names resolve: a module, and the actual parameters of the parameterized definition being instantiated.
struct Scope {
    ModuleScope *module_ = nullptr;
    std::shared_ptr<const Bindings> parameters_;
};

struct ClassDef;
struct ObjectDef;
struct ObjectSetDef;
using ClassPtr = std::shared_ptr<const ClassDef>;
using ObjectPtr = std::shared_ptr<const ObjectDef>;
using ObjectSetPtr = std::shared_ptr<const ObjectSetDef>;

// A type as written somewhere; resolved where it is used, since its record names depend on that place.
struct TypeRef {
    std::shared_ptr<TypeSyntax> syntax_;
    Scope scope_;
};

enum class FieldKind : std::uint8_t {
    type,
    fixed_value,
    variable_value,
    fixed_value_set,
    variable_value_set,
    object,
    object_set,
};

struct FieldSpec {
    std::string name_;
    FieldKind kind_ = FieldKind::type;
    // The governor of a fixed-type value or value set field.
    TypeRef type_;
    // The class of an object or object set field.
    ClassPtr class_;
    // The type field naming a variable-type field's type.
    std::string type_field_;
    bool unique_ = false;
    Presence presence_ = Presence::required;
    TokenRange default_;
};

struct ClassDef {
    std::string name_;
    std::vector<FieldSpec> fields_;
    bool with_syntax_ = false;
    std::vector<SyntaxItem> syntax_;
    // Where the class was defined: its fields' governors and defaults resolve there.
    Scope scope_;

    const FieldSpec *field(std::string_view name) const;
};

// The setting of one object field.
struct Setting {
    FieldKind kind_ = FieldKind::type;
    TypeRef type_;
    Term value_;
    std::vector<Term> values_;
    ObjectPtr object_;
    ObjectSetPtr set_;
};

struct ObjectDef {
    ClassPtr class_;
    // The object's name (types defined inline in it are named after it); empty for an inline object.
    std::string name_;
    std::map<std::string, Setting, std::less<>> settings_;
};

struct ObjectSetDef {
    ClassPtr class_;
    std::vector<ObjectPtr> objects_;
    bool extensible_ = false;
};

// An actual parameter: kept as written and resolved on use, in the scope that wrote it.
struct Binding {
    enum class Kind : std::uint8_t { type, value, value_set, class_, object, object_set };
    Kind kind_ = Kind::type;
    // A type parameter's actual type; a value or value set parameter's governor.
    TypeRef type_;
    TokenRange range_;
    Scope scope_;
    ClassPtr class_;
};

// One element of asn1ct's type name path: a component, `SEQOF`/`SETOF`, or a parameterized type's name.
struct PathPart {
    std::string name_;
    bool ptype_ = false;
};

// A SEQUENCE or SET body enclosing the type being checked, for component relation constraints.
struct Frame {
    const TypeSyntax *syntax_ = nullptr;
    Scope scope_;
};

// Where a type is checked: its name path and the enclosing SEQUENCE/SET bodies of its type assignment.
struct Context {
    std::vector<PathPart> path_;
    std::vector<Frame> frames_;

    Context with(PathPart part) const;
};

// One tag a notation applies; `implicit_` when IMPLICIT (written or by default), which an untagged CHOICE or open
// type turns into EXPLICIT.
struct TagOperation {
    TagValue value_;
    bool implicit_ = false;
};

// asn1ct's runtime record name of a path (list2rname).
std::string record_name(const std::vector<PathPart> &path);

using NamedNode = std::pair<std::string, NodeId>;

class Checker {
  public:
    Checker(const CompileOptions &options, const ModuleLoader &loader);
    CompiledModule run(const std::vector<std::shared_ptr<ModuleSyntax>> &modules, const std::string &name);

    // Modules and names (checker.cpp).
    ModuleScope &module(const std::string &name, const Scope *from, std::uint32_t token);
    ModuleScope &add_module(std::shared_ptr<ModuleSyntax> syntax);

    struct Found {
        // The assignment's module (no parameters), the assignment, or a parameter binding.
        Scope scope_;
        const AssignmentSyntax *assignment_ = nullptr;
        const Binding *binding_ = nullptr;
    };

    Found find(const Reference &reference, const Scope &scope);
    std::optional<Found> try_find(const Reference &reference, const Scope &scope);
    // The definition an unqualified name has in the module it is imported from.
    std::optional<Found> imported(const Reference &reference, const Scope &scope);
    [[noreturn]] void fail(const Scope &scope, std::uint32_t token, const std::string &message) const;
    Parser parser(const Scope &scope, TokenRange range) const;
    Scope instantiate(const Found &found, const std::vector<TokenRange> &arguments, const Scope &caller,
                      std::uint32_t token);

    // Types (checker_types.cpp, checker_components.cpp, checker_tags.cpp).
    NodeId type(const TypeSyntax &syntax, const Scope &scope, const Context &context);
    NodeId named_type(const Found &found);
    // A named type made for a type notation that is not a type assignment (asn1ct names types objects
    // instantiate `Object_PType`).
    NodeId declare(const std::string &name, const TypeSyntax &syntax, const Scope &scope);

    // While run() checks a module's definitions: the one being checked, and the types objects instantiate.
    const std::string &current_definition() const { return current_definition_; }

    void object_type(const TypeSyntax &syntax, const Scope &scope);
    NodeId declare_into(const std::pair<std::string, std::string> &key, const std::string &name,
                        const TypeSyntax &syntax, const Scope &scope);
    NodeId add(Node node);

    const Node &node(NodeId id) const { return nodes_[id]; }

    Node &node(NodeId id) { return nodes_[id]; }

    // The node a reference stands for, following references still being checked.
    const Node &content(NodeId id) const;

    // INSTANCE OF without a constraint: asn1ct's 'INSTANCE OF' type, and the modules whose notation uses it.
    NodeId instance_of_type(const Scope &scope);

    const std::set<std::string> &instance_of_modules() const { return instance_of_modules_; }

    // Values (checker_values.cpp).
    Term value(TokenRange range, NodeId governor, const Scope &scope);
    BigInt integer(TokenRange range, const Scope &scope, const std::vector<std::pair<std::string, BigInt>> &named = {});
    Term named_value(const Found &found);

    // Information objects (checker_objects.cpp).
    ClassPtr class_of(const TypeSyntax &syntax, const Scope &scope);
    ClassPtr class_definition(const Found &found);
    ClassPtr found_class(const Found &found);
    ClassPtr builtin_class(const std::string &name);
    ObjectPtr object(TokenRange range, const ClassPtr &definition, const Scope &scope);
    ObjectSetPtr object_set(TokenRange range, const ClassPtr &definition, const Scope &scope);
    ObjectSetPtr object_set(const ConstraintSyntax &spec, const ClassPtr &definition, const Scope &scope);
    ObjectPtr named_object(const TypeSyntax &reference, const ClassPtr &definition, const Scope &scope);
    ObjectPtr object_from_object(const TypeSyntax &reference, const Scope &scope);
    ObjectSetPtr named_object_set(const TypeSyntax &reference, const ClassPtr &definition, const Scope &scope);
    ObjectSetPtr object_set_element(TokenRange range, const ClassPtr &definition, const Scope &scope);
    ObjectSetPtr combined_set(const ConstraintSyntax &spec, const ClassPtr &definition, const Scope &scope);

    // Tags of references to types still being checked, applied once their targets are complete.
    void defer_tags(NodeId id, std::vector<TagOperation> operations);
    void finish_references();

    // The class and field a `CLASS.&field` type names (checker_fields.cpp).
    struct FieldOf {
        ClassPtr class_;
        const FieldSpec *spec_ = nullptr;
    };

    FieldOf class_field_spec(const TypeSyntax &syntax, const Scope &scope);

    // Constraints (checker_constraints.cpp, checker_fields.cpp).
    void constrain(NodeId id, const TypeSyntax &syntax, const Scope &scope, const Context &context);
    void table_constraint(NodeId id, const TypeSyntax &syntax, const ConstraintSyntax &table, const Scope &scope,
                          const Context &context);

    const CompileOptions &options() const { return options_; }

  private:
    void check_assignments(ModuleScope &root, CompiledModule &result, std::vector<NamedNode> &types);
    void check_module(ModuleScope &root, CompiledModule &result, std::vector<NamedNode> &types);
    void check_objects(ModuleScope &root, AssignmentSyntax::Kind kind);

    friend class TypeChecker;
    friend class ValueChecker;
    friend class ObjectChecker;

    const CompileOptions &options_;
    const ModuleLoader &loader_;
    std::map<std::string, std::unique_ptr<ModuleScope>, std::less<>> modules_;
    std::vector<Node> nodes_;
    // Checked type and value assignments and classes, by module and name.
    std::map<std::pair<std::string, std::string>, NodeId> named_types_;
    std::map<std::pair<std::string, std::string>, Term> named_values_;
    std::set<std::pair<std::string, std::string>> values_in_progress_;
    std::vector<std::pair<NodeId, std::vector<TagOperation>>> deferred_tags_;
    NodeId instance_of_ = NO_NODE;
    std::string current_definition_;
    std::vector<NamedNode> object_types_;
    std::set<std::string> instance_of_modules_;
    std::map<std::pair<std::string, std::string>, ClassPtr> classes_;
    std::map<std::pair<std::string, std::string>, ObjectPtr> objects_;
    std::map<std::pair<std::string, std::string>, ObjectSetPtr> object_sets_;
};
} // namespace clause::asn1::detail
