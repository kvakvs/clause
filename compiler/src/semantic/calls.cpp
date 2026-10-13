#include "calls.hpp"
#include "behaviours.hpp"
#include "capabilities.hpp"
#include "erlang_module.hpp"
#include "features.hpp"
#include "funs.hpp"
#include "imports.hpp"
#include "records.hpp"
#include "services.hpp"
#include <algorithm>
#include <clause/compiler/printing.hpp>

namespace clause::semantic {
namespace {
using Modules = std::map<std::u32string, Module *>;

// erlang:get_stacktrace/0 was removed in OTP 23; OTP 29 lint names the replacement and the call fails.
bool removed_call(const ast::Module &syntax, const ast::RemoteExpression &remote, const std::size_t arity) {
    const auto &module = std::get<ast::Atom>(syntax.expression(ungroup(syntax, remote.module)).value).name;
    const auto &function = std::get<ast::Atom>(syntax.expression(ungroup(syntax, remote.function)).value).name;
    return module == U"erlang" && function == U"get_stacktrace" && arity == 0;
}

// Resolve literal remote names without ever falling back to another project target.
Module *call_module(const FunctionRef caller, const ast::CallExpression &call, const ast::RemoteExpression &remote,
                    const Modules &modules, const ast::NodeSource &source, const Reporter &out) {
    const auto &syntax = *caller.module->syntax;
    if (removed_call(syntax, remote, call.arguments.size())) {
        report(*caller.module, &source,
               "erlang:get_stacktrace/0 is removed; use the new try/catch syntax for retrieving the stack backtrace",
               out);
        return nullptr;
    }
    const auto &name = std::get<ast::Atom>(syntax.expression(ungroup(syntax, remote.module)).value).name;
    const auto found = modules.find(name);
    if (found == modules.end()) {
        report(*caller.module, &source, "unknown module " + utf8(name), out);
        return nullptr;
    }
    return found->second;
}

// The function a call names: a remote call reaches any function, a local one only those its source may name.
Function *named_function(Module &owner, const FunctionKey &key, const bool remote) {
    if (!remote) {
        const auto index = source_function(owner, key);
        return index ? &owner.functions.at(*index) : nullptr;
    }
    const auto found = owner.lookup.find(key);
    return found == owner.lookup.end() ? nullptr : &owner.functions.at(found->second);
}

struct CallTarget {
    // The module whose function a call reaches, and whether it is reached as a remote call.
    Module *module_;
    bool remote_;
};

// The module an unqualified call reaches: its own, or the erlang library module for an OTP auto-import it does not
// define (step 41B; without that module in the batch the call stays an undefined local one).
CallTarget local_target(const FunctionRef caller, const FunctionKey &key, const Modules &modules) {
    const auto erlang = modules.find(U"erlang");
    if (erlang != modules.end() && library_auto_import(*caller.module->syntax, key)) {
        return CallTarget{erlang->second, true};
    }
    return CallTarget{caller.module, false};
}

// The module a call reaches: its literal module, the module -import names for a local call, or the caller's own.
std::optional<CallTarget> call_target(const FunctionRef caller, const ast::CallExpression &call,
                                      const ast::RemoteExpression *remote, const FunctionKey &key,
                                      const Modules &modules, const ast::NodeSource &source, const Reporter &out) {
    if (remote) {
        auto *owner = call_module(caller, call, *remote, modules, source, out);
        return owner ? std::optional{CallTarget{owner, true}} : std::nullopt;
    }
    const auto *imported = import_owner(*caller.module, key);
    if (!imported) {
        return local_target(caller, key, modules);
    }
    const auto found = modules.find(*imported);
    if (found == modules.end()) {
        report(*caller.module, &source, "unknown module " + utf8(*imported), out);
        return std::nullopt;
    }
    return CallTarget{found->second, true};
}

// Match name/arity first, then enforce remote visibility even for self-qualified and imported calls.
std::optional<FunctionRef> callee(const FunctionRef caller, const ast::CallExpression &call, const Modules &modules,
                                  const ast::NodeSource &source, const Reporter &out) {
    const auto &syntax = *caller.module->syntax;
    const auto &target = syntax.expression(ungroup(syntax, call.target)).value;
    const auto *remote = std::get_if<ast::RemoteExpression>(&target);
    const auto &name = remote ? std::get<ast::Atom>(syntax.expression(ungroup(syntax, remote->function)).value)
                              : std::get<ast::Atom>(target);
    const FunctionKey key{name.name, call.arguments.size()};
    const auto resolved = call_target(caller, call, remote, key, modules, source, out);
    if (!resolved) {
        return {};
    }
    auto *owner = resolved->module_;
    auto *function = named_function(*owner, key, resolved->remote_);
    if (!function) {
        report(*caller.module, &source,
               "undefined function " + utf8(owner->name) + ":" + utf8(key.name) + "/" + std::to_string(key.arity), out);
        return {};
    }
    if (resolved->remote_ && !function->exported) {
        report(*caller.module, &source, "remote function is not exported: " + utf8(key.name), out);
        return {};
    }
    return FunctionRef{owner, function};
}

// A direct call of a named function: not a service, record_info/2 or the call of a value.
bool direct_call(const FunctionRef caller, const ast::Expression &expression) {
    const auto &syntax = *caller.module->syntax;
    const auto *call = std::get_if<ast::CallExpression>(&expression.value);
    return call && !caller.function->services.contains(&expression) && !record_info_call(syntax, expression.value) &&
           !fun_call(syntax, *call) && !dynamic_call(syntax, *call);
}

// A local fun F/A must name a function of its module or an auto-imported builtin (erl_lint undefined_function), and
// never an imported one (fun_import).
void check_reference(const Module &module, const ast::Expression &expression, const Reporter &out) {
    const auto *reference = std::get_if<ast::LocalFunReference>(&expression.value);
    const auto count = reference ? arity(reference->arity) : std::nullopt;
    if (count && import_owner(module, {reference->name.name, *count})) {
        report(module, &expression.source,
               "creating a fun from imported name " + atom_source(utf8(reference->name.name)) + "/" +
                   std::to_string(*count) + " is not allowed",
               out);
        return;
    }
    if (!reference || fun_target(module, *reference) || module.fun_entries.contains(&expression)) {
        return;
    }
    report(module, &expression.source,
           "function " + utf8(reference->name.name) + "/" + reference->arity.decimal + " undefined", out);
}

// Walk all accepted bodies iteratively; source-order dependencies remain deterministic.
void body(CallGraph &graph, const FunctionRef caller, const Modules &modules, const Reporter &out) {
    const auto &syntax = *caller.module->syntax;
    auto pending = function_roots(std::get<ast::Function>(syntax.form(caller.function->form).value));
    std::ranges::reverse(pending);
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto &expression = syntax.expression(id);
        if (direct_call(caller, expression)) {
            const auto &call = std::get<ast::CallExpression>(expression.value);
            if (const auto resolved = callee(caller, call, modules, expression.source, out)) {
                graph.calls.push_back({id, caller, *resolved});
            }
        }
        check_reference(*caller.module, expression, out);
        const auto children = expression_children(*caller.module, expression);
        pending.insert(pending.end(), children.rbegin(), children.rend());
    }
}

// Index every module before resolving any call, preserving forward references.
Modules module_index(const std::span<const std::unique_ptr<Module>> modules, const Reporter &out) {
    Modules result;
    for (const auto &module : modules) {
        if (!result.emplace(module->name, module.get()).second) {
            const auto *source = module->declaration ? &module->syntax->form(*module->declaration).source : nullptr;
            report(*module, source, "duplicate module in compilation batch: " + utf8(module->name), out);
        }
    }
    return result;
}

// Translate stable declaration pointers to dense indices; each caller lists its callees in source order.
std::vector<std::vector<std::size_t>> adjacency(const CallGraph &graph, const std::vector<FunctionRef> &functions) {
    std::map<Function *, std::size_t> indices;
    for (std::size_t i = 0; i < functions.size(); ++i) {
        indices.emplace(functions[i].function, i);
    }
    std::vector<std::vector<std::size_t>> result(functions.size());
    for (const auto &call : graph.calls) {
        result[indices.at(call.caller.function)].push_back(indices.at(call.callee.function));
    }
    return result;
}

// Tarjan's algorithm with explicit frames emits each component after every component it calls.
class Components {
  public:
    // Borrow the dense call edges; every node starts unvisited.
    explicit Components(const std::vector<std::vector<std::size_t>> &edges)
        : edges_(edges), index_(edges.size(), UNVISITED), low_(edges.size()), stacked_(edges.size()) {}

    // Visit every unvisited root in declaration order so the result stays deterministic.
    std::vector<std::vector<std::size_t>> run() {
        for (std::size_t root = 0; root < edges_.size(); ++root) {
            if (index_[root] == UNVISITED) {
                visit(root);
            }
        }
        return std::move(components_);
    }

  private:
    static constexpr std::size_t UNVISITED = static_cast<std::size_t>(-1);

    struct Frame {
        // The node being explored and the next outgoing edge to inspect.
        std::size_t node;
        std::size_t edge = 0;
    };

    // Number a node and place it on both the component stack and the explicit DFS stack.
    void enter(const std::size_t node, std::vector<Frame> &frames) {
        index_[node] = low_[node] = next_++;
        stack_.push_back(node);
        stacked_[node] = true;
        frames.push_back({node});
    }

    // Pop a finished root's component; members leave the stack in reverse discovery order.
    void close(const std::size_t node) {
        std::vector<std::size_t> component;
        std::size_t member = 0;
        do {
            member = stack_.back();
            stack_.pop_back();
            stacked_[member] = false;
            component.push_back(member);
        } while (member != node);
        std::ranges::sort(component);
        components_.push_back(std::move(component));
    }

    // Advance one frame: descend into an unvisited callee or fold a finished child's low link.
    void step(std::vector<Frame> &frames) {
        auto &frame = frames.back();
        const auto node = frame.node;
        if (frame.edge < edges_[node].size()) {
            const auto callee = edges_[node][frame.edge++];
            if (index_[callee] == UNVISITED) {
                enter(callee, frames);
            } else if (stacked_[callee]) {
                low_[node] = std::min(low_[node], index_[callee]);
            }
            return;
        }
        frames.pop_back();
        if (!frames.empty()) {
            low_[frames.back().node] = std::min(low_[frames.back().node], low_[node]);
        }
        if (low_[node] == index_[node]) {
            close(node);
        }
    }

    // Explore one root iteratively, avoiding host recursion for long call chains.
    void visit(const std::size_t root) {
        std::vector<Frame> frames;
        enter(root, frames);
        while (!frames.empty()) {
            step(frames);
        }
    }

    const std::vector<std::vector<std::size_t>> &edges_;
    // Discovery numbers, lowest reachable numbers and component stack membership per node.
    std::vector<std::size_t> index_;
    std::vector<std::size_t> low_;
    std::vector<bool> stacked_;
    std::vector<std::size_t> stack_;
    std::vector<std::vector<std::size_t>> components_;
    std::size_t next_ = 0;
};

// A component is recursive when it has several members or its single member calls itself.
bool recursive(const std::vector<std::size_t> &members, const std::vector<std::vector<std::size_t>> &edges) {
    return members.size() > 1 ||
           std::ranges::find(edges[members.front()], members.front()) != edges[members.front()].end();
}

// Record components with callees first; the flattened order keeps that dependency direction.
void order(CallGraph &graph, const std::vector<FunctionRef> &functions) {
    const auto edges = adjacency(graph, functions);
    for (const auto &members : Components(edges).run()) {
        Component component{{}, recursive(members, edges)};
        component.members.reserve(members.size());
        for (const auto member : members) {
            component.members.push_back(functions[member]);
            graph.order.push_back(functions[member]);
        }
        graph.components.push_back(std::move(component));
    }
}

} // namespace

namespace {
// The atom an expression is, after parentheses.
const ast::Atom *literal_atom(const ast::Module &syntax, const ast::ExprId &id) {
    return std::get_if<ast::Atom>(&syntax.expression(ungroup(syntax, id)).value);
}

// The module apply(M, F, Args) or erlang:apply(M, F, Args) names with a literal atom.
const ast::Atom *applied_module(const ast::Module &syntax, const ast::CallExpression &call) {
    const auto &target = syntax.expression(ungroup(syntax, call.target)).value;
    const auto *name = std::get_if<ast::Atom>(&target);
    const auto *remote = std::get_if<ast::RemoteExpression>(&target);
    if (remote) {
        const auto *owner = literal_atom(syntax, remote->module);
        name = owner && owner->name == U"erlang" ? literal_atom(syntax, remote->function) : nullptr;
    }
    const bool apply = name && name->name == U"apply" && call.arguments.size() == 3;
    return apply ? literal_atom(syntax, call.arguments.front()) : nullptr;
}

// Whether a literal M:F(...) call is a builtin of the runtime's catalog (io:format/2, os:type/0) or an erlang
// function the compiler handles, which needs no library module.
bool builtin_call(const ast::Module &syntax, const ast::RemoteExpression &remote, std::size_t arity) {
    const auto *module = literal_atom(syntax, remote.module);
    const auto *function = literal_atom(syntax, remote.function);
    if (!module || !function) {
        return false;
    }
    const FunctionKey key{function->name, arity};
    // erlang functions the compiler handles itself (is_record/3, apply/3) need no library module either (step 41B).
    return bridge_builtin(module->name, key) || (module->name == U"erlang" && compiler_handled(key));
}

// The module one expression names with a literal atom, if any; calls of catalog builtins name none.
// The erlang library module, named by an unqualified call or fun F/A of an OTP auto-import the module does not
// define (step 41B).
const ast::Atom *auto_imported_module(const ast::Module &syntax, const ast::Expression &expression) {
    static const ast::Atom ERLANG{U"erlang"};
    std::optional<FunctionKey> key;
    if (const auto *reference = std::get_if<ast::LocalFunReference>(&expression.value)) {
        const auto count = arity(reference->arity);
        key = count ? std::optional{FunctionKey{reference->name.name, *count}} : std::nullopt;
    } else if (const auto *call = std::get_if<ast::CallExpression>(&expression.value)) {
        const auto *name = std::get_if<ast::Atom>(&syntax.expression(ungroup(syntax, call->target)).value);
        key = name ? std::optional{FunctionKey{name->name, call->arguments.size()}} : std::nullopt;
    }
    return key && library_auto_import(syntax, *key) ? &ERLANG : nullptr;
}

const ast::Atom *named_module(const ast::Module &syntax, const ast::Expression &expression) {
    if (const auto *module = auto_imported_module(syntax, expression)) {
        return module;
    }
    if (const auto *reference = std::get_if<ast::RemoteFunReference>(&expression.value)) {
        return std::get_if<ast::Atom>(&reference->module);
    }
    const auto *call = std::get_if<ast::CallExpression>(&expression.value);
    if (!call) {
        return nullptr;
    }
    if (const auto *remote =
            std::get_if<ast::RemoteExpression>(&syntax.expression(ungroup(syntax, call->target)).value);
        remote && !applied_module(syntax, *call)) {
        return builtin_call(syntax, *remote, call->arguments.size()) ? nullptr : literal_atom(syntax, remote->module);
    }
    return applied_module(syntax, *call);
}

// The module an attribute form names: -behaviour(M) or -import(M, [...]). Added for --print-inputs.
const ast::Atom *attribute_module(const ast::Module &syntax, const ast::Form &form) {
    if (const auto *import = std::get_if<ast::ImportAttribute>(&form.value)) {
        return &import->module;
    }
    return behaviour_name(syntax, form);
}

// Referenced module names with their earliest naming site. Added for --print-inputs.
using References = std::map<std::u32string, ast::NodeSource>;

// Keep the earliest naming site: forms are visited in order, tokens within one form compare by position.
// Added for --print-inputs.
void note(References &result, const std::u32string &name, const ast::NodeSource &source) {
    const auto [found, inserted] = result.try_emplace(name, source);
    if (!inserted && found->second.form == source.form && source.begin < found->second.begin) {
        found->second = source;
    }
}

// Note the modules one function's expressions name. Added for --print-inputs.
void note_function(const ast::Module &syntax, const ast::Function &function, References &result) {
    auto pending = function_roots(function);
    while (!pending.empty()) {
        const auto &expression = syntax.expression(pending.back());
        pending.pop_back();
        if (const auto *module = named_module(syntax, expression)) {
            note(result, module->name, expression.source);
        }
        const auto children = expression_children(expression);
        pending.insert(pending.end(), children.begin(), children.end());
    }
}

// Function keys by the literal module that names them. Added for ignored modules.
using FunctionReferences = std::map<std::u32string, std::set<FunctionKey>>;

// Note fun M:F/A when module, name and arity are all literal. Added for ignored modules.
void note_fun_reference(const ast::RemoteFunReference &reference, FunctionReferences &result) {
    const auto *module = std::get_if<ast::Atom>(&reference.module);
    const auto *name = std::get_if<ast::Atom>(&reference.name);
    const auto *count = std::get_if<Integer>(&reference.arity);
    const auto value = count ? arity(*count) : std::nullopt;
    if (module && name && value) {
        result[module->name].insert({name->name, *value});
    }
}

// Note apply(M, F, [A1, ..., An]) when M and F are atoms and the arguments a literal proper list. Added for ignored
// modules.
void note_apply(const ast::Module &syntax, const ast::Atom &module, const ast::CallExpression &call,
                FunctionReferences &result) {
    const auto *name = literal_atom(syntax, call.arguments[1]);
    const auto *list = std::get_if<ast::List>(&syntax.expression(ungroup(syntax, call.arguments[2])).value);
    if (name && list && !list->tail) {
        result[module.name].insert({name->name, list->elements.size()});
    }
}

// Note the remote function one call or fun expression names with literal atoms. Added for ignored modules.
void note_remote(const ast::Module &syntax, const ast::Expression &expression, FunctionReferences &result) {
    if (const auto *reference = std::get_if<ast::RemoteFunReference>(&expression.value)) {
        note_fun_reference(*reference, result);
        return;
    }
    const auto *call = std::get_if<ast::CallExpression>(&expression.value);
    if (!call) {
        return;
    }
    if (const auto *module = applied_module(syntax, *call)) {
        note_apply(syntax, *module, *call, result);
        return;
    }
    const auto *remote = std::get_if<ast::RemoteExpression>(&syntax.expression(ungroup(syntax, call->target)).value);
    const auto *module = remote ? literal_atom(syntax, remote->module) : nullptr;
    const auto *name = remote ? literal_atom(syntax, remote->function) : nullptr;
    if (module && name && !builtin_call(syntax, *remote, call->arguments.size())) {
        result[module->name].insert({name->name, call->arguments.size()});
    }
}

// Note the functions an -import attribute names. Added for ignored modules.
void note_imports(const ast::ImportAttribute &import, FunctionReferences &result) {
    auto &keys = result[import.module.name];
    for (const auto &function : import.functions) {
        if (const auto value = arity(function.arity)) {
            keys.insert({function.name.name, *value});
        }
    }
}
} // namespace

std::optional<std::u32string> declared_module(const ast::Module &syntax) {
    for (const auto &id : syntax.forms()) {
        if (const auto *attribute = std::get_if<ast::ModuleAttribute>(&syntax.form(id).value)) {
            return attribute->name.name;
        }
    }
    return std::nullopt;
}

std::map<std::u32string, ast::NodeSource> referenced_modules(const ast::Module &syntax) {
    References result;
    for (const auto &id : syntax.forms()) {
        const auto &form = syntax.form(id);
        if (const auto *module = attribute_module(syntax, form)) {
            note(result, module->name, form.source);
        }
        if (const auto *function = std::get_if<ast::Function>(&form.value)) {
            note_function(syntax, *function, result);
        }
    }
    return result;
}

std::map<std::u32string, std::set<FunctionKey>> referenced_functions(const ast::Module &syntax) {
    FunctionReferences result;
    for (const auto &id : syntax.forms()) {
        const auto &form = syntax.form(id);
        if (const auto *import = std::get_if<ast::ImportAttribute>(&form.value)) {
            note_imports(*import, result);
        }
        const auto *function = std::get_if<ast::Function>(&form.value);
        auto pending = function ? function_roots(*function) : std::vector<ast::ExprId>{};
        while (!pending.empty()) {
            const auto &expression = syntax.expression(pending.back());
            pending.pop_back();
            note_remote(syntax, expression, result);
            const auto children = expression_children(expression);
            pending.insert(pending.end(), children.begin(), children.end());
        }
    }
    return result;
}

namespace {
// The atom an expression spells: a plain atom, or the module of a native record it qualifies (`#m:r{}`), whose
// layout inference reads. Added for the inference cache (58W).
const std::u32string *spelled_atom(const ast::Expression &expression) {
    if (const auto *atom = std::get_if<ast::Atom>(&expression.value)) {
        return &atom->name;
    }
    const ast::RecordIdentity *identity = nullptr;
    if (const auto *record = std::get_if<ast::RecordExpression>(&expression.value)) {
        identity = &record->identity;
    } else if (const auto *access = std::get_if<ast::RecordAccess>(&expression.value)) {
        identity = &access->identity;
    }
    const auto *qualified = identity ? std::get_if<ast::QualifiedRecordName>(&identity->value) : nullptr;
    return qualified ? &qualified->module.name : nullptr;
}
} // namespace

std::set<std::u32string> named_atoms(const ast::Module &syntax) {
    std::set<std::u32string> result;
    for (std::size_t index = 0; index < syntax.expression_count(); ++index) {
        if (const auto *atom = spelled_atom(syntax.expression_at(index))) {
            result.insert(*atom);
        }
    }
    for (const auto &[module, site] : referenced_modules(syntax)) {
        result.insert(module);
    }
    // -import_record(M, [...]) makes M's native records usable unqualified.
    for (const auto &id : syntax.forms()) {
        if (const auto *imported = std::get_if<ast::ImportRecordAttribute>(&syntax.form(id).value)) {
            result.insert(imported->module.name);
        }
    }
    return result;
}

CallGraph resolve_calls(const std::span<const std::unique_ptr<Module>> modules, const Reporter &out) {
    const auto names = module_index(modules, out);
    std::size_t total = 0;
    for (const auto &module : modules) {
        module->peers.insert(names.begin(), names.end());
        total += module->functions.size();
    }
    CallGraph graph;
    std::vector<FunctionRef> functions;
    functions.reserve(total);
    for (const auto &module : modules) {
        for (auto &function : module->functions) {
            functions.push_back({module.get(), &function});
            body(graph, functions.back(), names, out);
        }
    }
    order(graph, functions);
    return graph;
}
} // namespace clause::semantic
