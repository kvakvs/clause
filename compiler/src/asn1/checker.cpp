#include "checker_internal.hpp"
#include "records.hpp"
#include <algorithm>

// Added for the ASN.1 compiler (plan 21 step LIB84): modules, name lookup, parameterization (X.683) and the checked
// module's contents.
namespace clause::asn1 {
namespace detail {
Context Context::with(PathPart part) const {
    Context result = *this;
    result.path_.push_back(std::move(part));
    return result;
}

// list2rname of asn1ct_gen: a parameterized type's name when it is innermost, else every part joined by `_`.
std::string record_name(const std::vector<PathPart> &path) {
    if (path.empty()) {
        return {};
    }
    if (path.back().ptype_) {
        return path.back().name_;
    }
    std::string result;
    for (const auto &part : path) {
        result += (result.empty() ? "" : "_") + part.name_;
    }
    return result;
}

const FieldSpec *ClassDef::field(const std::string_view name) const {
    for (const auto &spec : fields_) {
        if (spec.name_ == name) {
            return &spec;
        }
    }
    return nullptr;
}

Checker::Checker(const CompileOptions &options, const ModuleLoader &loader) : options_(options), loader_(loader) {}

ModuleScope &Checker::add_module(std::shared_ptr<ModuleSyntax> syntax) {
    auto scope = std::make_unique<ModuleScope>();
    for (const auto &assignment : syntax->assignments_) {
        scope->assignments_.emplace(assignment.name_, &assignment);
    }
    for (const auto &import : syntax->imports_) {
        for (const auto &symbol : import.symbols_) {
            scope->imports_.emplace(symbol, import.module_);
        }
    }
    scope->syntax_ = std::move(syntax);
    auto &result = *scope;
    modules_.insert_or_assign(result.syntax_->name_, std::move(scope));
    return result;
}

ModuleScope &Checker::module(const std::string &name, const Scope *from, const std::uint32_t token) {
    if (const auto found = modules_.find(name); found != modules_.end()) {
        return *found->second;
    }
    auto syntax = loader_(name);
    if (!syntax) {
        if (from != nullptr) {
            fail(*from, token, "cannot find ASN.1 module " + name);
        }
        throw Error({name, 0, 0}, "cannot find ASN.1 module " + name);
    }
    return add_module(std::move(syntax));
}

void Checker::fail(const Scope &scope, const std::uint32_t token, const std::string &message) const {
    throw Error(scope.module_->syntax_->location(token), message);
}

Parser Checker::parser(const Scope &scope, const TokenRange range) const { return {*scope.module_->syntax_, range}; }

std::optional<Checker::Found> Checker::try_find(const Reference &reference, const Scope &scope) {
    if (reference.module_.empty() && scope.parameters_) {
        if (const auto binding = scope.parameters_->find(reference.name_); binding != scope.parameters_->end()) {
            return Found{scope, nullptr, &binding->second};
        }
    }
    auto *module = scope.module_;
    if (!reference.module_.empty()) {
        module = &this->module(reference.module_, &scope, reference.token_);
    }
    if (const auto local = module->assignments_.find(reference.name_); local != module->assignments_.end()) {
        return Found{Scope{module, nullptr}, local->second, nullptr};
    }
    if (const auto import = module->imports_.find(reference.name_); import != module->imports_.end()) {
        Scope from{module, nullptr};
        auto &source = this->module(import->second, &from, reference.token_);
        return try_find({"", reference.name_, reference.token_}, Scope{&source, nullptr});
    }
    return std::nullopt;
}

std::optional<Checker::Found> Checker::imported(const Reference &reference, const Scope &scope) {
    const auto import = scope.module_->imports_.find(reference.name_);
    if (!reference.module_.empty() || import == scope.module_->imports_.end()) {
        return std::nullopt;
    }
    auto &source = module(import->second, &scope, reference.token_);
    return try_find({"", reference.name_, reference.token_}, Scope{&source, nullptr});
}

Checker::Found Checker::find(const Reference &reference, const Scope &scope) {
    if (auto found = try_find(reference, scope)) {
        return *found;
    }
    fail(scope, reference.token_, "undefined reference " + reference.name_);
}

namespace {
// A parameter with a governor: an object (set) of a class governor, else a value (set) of the governor type.
void governed(Checker &checker, const ParameterSyntax &formal, const Scope &definition, Binding &binding) {
    const bool upper = !formal.name_.empty() && formal.name_.front() >= 'A' && formal.name_.front() <= 'Z';
    if (auto governor = checker.class_of(*formal.governor_, definition)) {
        binding.kind_ = upper ? Binding::Kind::object_set : Binding::Kind::object;
        binding.class_ = std::move(governor);
    } else {
        binding.kind_ = upper ? Binding::Kind::value_set : Binding::Kind::value;
        binding.type_ = {formal.governor_, definition};
    }
}

// The governor of a formal parameter decides what its actual parameter (the binding's range in the caller's scope)
// is (X.683 8.3).
void bind(Checker &checker, const ParameterSyntax &formal, const Scope &definition, Binding &binding) {
    if (formal.governor_) {
        governed(checker, formal, definition, binding);
        return;
    }
    const auto &caller = binding.scope_;
    auto parser = checker.parser(caller, binding.range_);
    auto type = parser.type();
    if (!parser.at_end()) {
        parser.fail("unexpected tokens in an actual parameter");
    }
    if (auto definition_class = checker.class_of(*type, caller)) {
        binding.kind_ = Binding::Kind::class_;
        binding.class_ = std::move(definition_class);
    } else {
        binding.type_ = {type, caller};
    }
}
} // namespace

Scope Checker::instantiate(const Found &found, const std::vector<TokenRange> &arguments, const Scope &caller,
                           const std::uint32_t token) {
    const auto &formals = found.assignment_->parameters_;
    if (formals.size() != arguments.size()) {
        fail(caller, token, "wrong number of actual parameters for " + found.assignment_->name_);
    }
    auto bindings = std::make_shared<Bindings>();
    Scope definition{found.scope_.module_, bindings};
    for (std::size_t i = 0; i < formals.size(); ++i) {
        // Earlier parameters may govern later ones (`{ALGORITHM-TYPE, ALGORITHM-TYPE:AlgorithmSet}`).
        Binding binding;
        binding.range_ = arguments[i];
        binding.scope_ = caller;
        bind(*this, formals[i], definition, binding);
        bindings->insert_or_assign(formals[i].name_, std::move(binding));
    }
    return definition;
}

NodeId Checker::add(Node node) {
    nodes_.push_back(std::move(node));
    return static_cast<NodeId>(nodes_.size() - 1);
}

const Node &Checker::content(NodeId id) const {
    while (nodes_[id].kind_ == NodeKind::reference) {
        id = nodes_[id].target_;
    }
    return nodes_[id];
}

namespace {
bool is_type_assignment(Checker &checker, const AssignmentSyntax &assignment, const Scope &scope) {
    if (assignment.parameterized_) {
        return false;
    }
    if (assignment.kind_ == AssignmentSyntax::Kind::type) {
        return !checker.class_of(*assignment.type_, scope);
    }
    // A value set assignment `T INTEGER ::= {1 | 2}` defines a type.
    return assignment.kind_ == AssignmentSyntax::Kind::set && !checker.class_of(*assignment.governor_, scope);
}

bool is_value_assignment(Checker &checker, const AssignmentSyntax &assignment, const Scope &scope) {
    return !assignment.parameterized_ && assignment.kind_ == AssignmentSyntax::Kind::value &&
           !checker.class_of(*assignment.governor_, scope);
}
} // namespace

// Types and values of one compiled module, in definition order.
void Checker::check_assignments(ModuleScope &root, CompiledModule &result, std::vector<NamedNode> &types) {
    const Scope scope{&root, nullptr};
    for (const auto &assignment : root.syntax_->assignments_) {
        current_definition_ = assignment.name_;
        if (is_type_assignment(*this, assignment, scope)) {
            types.emplace_back(assignment.name_, named_type({scope, &assignment, nullptr}));
        } else if (is_value_assignment(*this, assignment, scope)) {
            result.values_.push_back({assignment.name_, named_value({scope, &assignment, nullptr})});
        }
    }
    current_definition_.clear();
}

// asn1ct checks every object, then every object set, of the module; types its objects instantiate become named
// types (named after the definition being checked when an object is first needed).
void Checker::check_objects(ModuleScope &root, const AssignmentSyntax::Kind kind) {
    const Scope scope{&root, nullptr};
    for (const auto &assignment : root.syntax_->assignments_) {
        if (assignment.parameterized_ || assignment.kind_ != kind || !class_of(*assignment.governor_, scope)) {
            continue;
        }
        TypeSyntax reference;
        reference.kind_ = TypeKind::reference;
        reference.reference_ = {"", assignment.name_, assignment.token_};
        reference.token_ = assignment.token_;
        current_definition_ = assignment.name_;
        if (kind == AssignmentSyntax::Kind::value) {
            named_object(reference, nullptr, scope);
        } else {
            named_object_set(reference, nullptr, scope);
        }
    }
    current_definition_.clear();
}

// One module's assignments; its types join `types` in asn1ct's order: the module's types, INSTANCE OF, then the
// types its objects instantiate.
void Checker::check_module(ModuleScope &root, CompiledModule &result, std::vector<NamedNode> &types) {
    std::vector<NamedNode> own;
    check_assignments(root, result, own);
    check_objects(root, AssignmentSyntax::Kind::value);
    check_objects(root, AssignmentSyntax::Kind::set);
    if (instance_of_modules_.contains(root.syntax_->name_)) {
        own.emplace_back("INSTANCE OF", instance_of_);
    }
    own.insert(own.end(), object_types_.begin(), object_types_.end());
    object_types_.clear();
    types.insert(types.end(), own.begin(), own.end());
}

CompiledModule Checker::run(const std::vector<std::shared_ptr<ModuleSyntax>> &modules, const std::string &name) {
    std::vector<ModuleScope *> roots;
    roots.reserve(modules.size());
    for (const auto &syntax : modules) {
        roots.push_back(&add_module(syntax));
    }
    CompiledModule result;
    result.name_ = name;
    std::vector<NamedNode> types;
    std::vector<const AssignmentSyntax *> definitions;
    for (auto *root : roots) {
        check_module(*root, result, types);
        for (const auto &assignment : root->syntax_->assignments_) {
            if (assignment.parameterized_ && assignment.kind_ == AssignmentSyntax::Kind::type) {
                definitions.push_back(&assignment);
            }
        }
    }
    if (roots.size() > 1) {
        // asn1ct lists the definitions of the modules a .set.asn merges in reverse.
        std::ranges::reverse(types);
        std::ranges::reverse(definitions);
        std::ranges::reverse(result.values_);
    }
    result.types_ = types;
    finish_references();
    collect_records(*this, definitions, types, result);
    result.nodes_ = nodes_;
    return result;
}
} // namespace detail

CompiledModule check(const std::vector<std::shared_ptr<ModuleSyntax>> &modules, const std::string &name,
                     const CompileOptions &options, const ModuleLoader &loader) {
    return detail::Checker(options, loader).run(modules, name);
}
} // namespace clause::asn1
