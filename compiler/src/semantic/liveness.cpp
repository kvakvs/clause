#include "liveness.hpp"
#include "builtin_continuations.hpp"
#include "capabilities.hpp"
#include "funs.hpp"
#include <algorithm>
#include <array>
#include <clause/abi/builtins.hpp>
#include <clause/compiler/source.hpp>
#include <iterator>

namespace clause::semantic {
namespace {
struct MfaBuiltin {
    // A builtin that calls Module:Function with an argument list (or arity) it takes at `module`, `module + 1` and
    // `module + 2`.
    std::u32string_view name;
    std::size_t arity;
    std::size_t module;
};

// The erlang builtins whose module and function operands name the code they call.
constexpr std::array MFA_BUILTINS{MfaBuiltin{U"apply", 3, 0},         MfaBuiltin{U"spawn", 3, 0},
                                  MfaBuiltin{U"spawn_link", 3, 0},    MfaBuiltin{U"spawn_monitor", 3, 0},
                                  MfaBuiltin{U"spawn_opt", 4, 0},     MfaBuiltin{U"spawn_opt", 5, 1},
                                  MfaBuiltin{U"spawn_request", 3, 0}, MfaBuiltin{U"spawn_request", 5, 1},
                                  MfaBuiltin{U"hibernate", 3, 0},     MfaBuiltin{U"make_fun", 3, 0}};

struct BuiltinName {
    // An erlang builtin's name and arity.
    std::u32string_view name;
    std::size_t arity;
};

// The erlang builtins that can return atoms no literal of the program spells, such as module names.
constexpr std::array ATOM_PRODUCERS{BuiltinName{U"list_to_atom", 1},
                                    BuiltinName{U"list_to_existing_atom", 1},
                                    BuiltinName{U"binary_to_atom", 1},
                                    BuiltinName{U"binary_to_atom", 2},
                                    BuiltinName{U"binary_to_existing_atom", 1},
                                    BuiltinName{U"binary_to_existing_atom", 2},
                                    BuiltinName{U"binary_to_term", 1},
                                    BuiltinName{U"binary_to_term", 2},
                                    BuiltinName{U"fun_info", 1},
                                    BuiltinName{U"fun_info", 2},
                                    BuiltinName{U"fun_info_mfa", 1},
                                    BuiltinName{U"process_info", 1},
                                    BuiltinName{U"process_info", 2},
                                    BuiltinName{U"loaded", 0},
                                    BuiltinName{U"pre_loaded", 0}};

// Whether erlang:Name/Arity is an atom-producing builtin.
bool atom_producer(const FunctionKey &key) {
    return std::ranges::any_of(
        ATOM_PRODUCERS, [&](const BuiltinName &item) { return item.name == key.name && item.arity == key.arity; });
}

// The MFA builtin erlang:Name/Arity is, if any.
const MfaBuiltin *mfa_builtin(const FunctionKey &key) {
    const auto found = std::ranges::find_if(
        MFA_BUILTINS, [&](const MfaBuiltin &item) { return item.name == key.name && item.arity == key.arity; });
    return found == MFA_BUILTINS.end() ? nullptr : &*found;
}

// The atom an expression is, after parentheses.
const ast::Atom *literal_atom(const ast::Module &syntax, const ast::ExprId &id) {
    return std::get_if<ast::Atom>(&syntax.expression(ungroup(syntax, id)).value);
}

// The arity an argument operand gives: the length of a literal proper list, or a literal integer (make_fun/3).
std::optional<std::size_t> applied_arity(const ast::Module &syntax, const ast::ExprId &id) {
    if (const auto *list = std::get_if<ast::List>(&syntax.expression(ungroup(syntax, id)).value)) {
        return list->tail ? std::nullopt : std::optional{list->elements.size()};
    }
    const auto value = integer_literal(syntax, id, 64);
    return value && *value >= 0 ? std::optional{static_cast<std::size_t>(*value)} : std::nullopt;
}

// Name a function in reasons: module:function/arity.
std::string function_text(const FunctionRef function) {
    return utf8(function.module->name) + ":" + utf8(function.function->key.name) + "/" +
           std::to_string(function.function->key.arity);
}

// The reachability walk: a worklist of live functions, the modules whose exports all stay live, and the two facts
// whose combination keeps every export of the program.
class Liveness {
  public:
    // Index modules by name and every function's direct callees.
    Liveness(const std::span<const std::unique_ptr<Module>> modules, const CallGraph &calls, const bool linked_node)
        : modules_(modules), linked_node_(linked_node) {
        for (const auto &module : modules) {
            names_.emplace(module->name, module.get());
            for (auto &function : module->functions) {
                function.live_ = false;
            }
        }
        for (const auto &call : calls.calls) {
            callees_[call.caller.function].push_back(call.callee);
        }
    }

    // Reach everything the entry reaches, and everything a node keeps when the program is linked as one, then count the
    // exports left out.
    LivenessReport run(const FunctionRef entry) {
        entry_ = entry;
        reach(entry);
        if (linked_node_) {
            become_node(entry, "the program is linked as a node");
        }
        reach_on_load(entry);
        while (!pending_.empty()) {
            const auto function = pending_.back();
            pending_.pop_back();
            scan(function);
        }
        for (const auto &module : modules_) {
            report_.dropped_[module->name] = static_cast<std::size_t>(std::ranges::count_if(
                module->functions, [](const Function &function) { return function.exported && !function.live_; }));
            report_.live_functions_ += static_cast<std::size_t>(
                std::ranges::count_if(module->functions, [](const Function &function) { return function.live_; }));
            report_.functions_ += module->functions.size();
        }
        return std::move(report_);
    }

  private:
    using Skipped = std::set<const ast::Expression *>;

    // A node runs the functions other nodes name, with atoms the analysis never sees, so every export stays by the
    // distribution rule; the runtime boots it through clause_dist_start:boot/3 to start distribution from the node
    // start options (plan 30 step DIST4b1b), and node mode through node/1 (DIST10a). Added for export liveness
    // (CLAU12c).
    void become_node(const FunctionRef entry, const std::string &why) {
        if (node_) {
            return;
        }
        node_ = true;
        reach_name(U"clause_dist_start", {U"boot", 3}, entry);
        reach_name(U"clause_dist_start", {U"node", 1}, entry);
        for (const auto &module : modules_) {
            report_.kept_.insert_or_assign(module->name, "the distribution rule: " + why);
            reach_exports(*module);
        }
    }

    // A call of net_kernel:start/1,2 from outside net_kernel starts distribution at run time, so the program is a
    // node; exports kept for other reasons do not count. Added for export liveness (CLAU12c).
    void note_node_start(const FunctionRef site, const std::u32string &module, const FunctionKey &key) {
        if (module == U"net_kernel" && key.name == U"start" && (key.arity == 1 || key.arity == 2) &&
            site.module->name != module) {
            become_node(entry_,
                        "net_kernel:start/" + std::to_string(key.arity) + " is called in " + function_text(site));
        }
    }

    // Every -on_load function, and clause_on_load:boot/3, which the runtime calls before the entry to run them, when
    // there is one (CLAU12c). Added for -on_load (plan 21 step MIM9d).
    void reach_on_load(const FunctionRef entry) {
        bool any = false;
        for (const auto &module : modules_) {
            if (module->on_load_) {
                reach({module.get(), &module->functions.at(*module->on_load_)});
                any = true;
            }
        }
        if (any) {
            reach_name(U"clause_on_load", {U"boot", 3}, entry);
        }
    }

    // The entry call of clause_on_load:boot/3: an apply/3 of the entry, which is a root already, so it is no call
    // with a runtime module. Added for export liveness (CLAU12c).
    static bool boot_entry_call(const FunctionRef site) {
        return site.module->library_ && site.module->name == U"clause_on_load" &&
               site.function->key == FunctionKey{U"boot", 3};
    }

    // Mark one function live and queue its body, with the code its builtin implementation continues in.
    void reach(const FunctionRef function) {
        if (!function.function->live_) {
            function.function->live_ = true;
            pending_.push_back(function);
            continue_builtin(utf8(function.module->name), utf8(function.function->key.name),
                             function.function->key.arity, function);
        }
    }

    // Reach the code the builtin Module:Function/Arity continues in, if it is one that does (step 41U).
    void continue_builtin(const std::string &module, const std::string &name, const std::size_t arity,
                          const FunctionRef site) {
        each_continuation(module, name, arity, [&](const BuiltinContinuation &item) {
            reach_name(std::u32string(item.target_module), {std::u32string(item.target_name), item.target_arity}, site);
        });
    }

    // Reach Module:Function/Arity by name: an exported function of the batch, noting atom-producing builtins.
    void reach_name(const std::u32string &module, const FunctionKey &key, const FunctionRef site) {
        if (module == U"erlang" && atom_producer(key)) {
            produced(site, "erlang:" + utf8(key.name) + "/" + std::to_string(key.arity));
        }
        note_node_start(site, module, key);
        const auto owner = names_.find(module);
        if (owner == names_.end()) {
            return;
        }
        const auto found = owner->second->lookup.find(key);
        if (found != owner->second->lookup.end() && owner->second->functions.at(found->second).exported) {
            reach({owner->second, &owner->second->functions.at(found->second)});
        }
    }

    // Reach every exported arity of Module:Function, for an argument list of unknown length.
    void reach_arities(const std::u32string &module, const ast::Atom &function) {
        const auto owner = names_.find(module);
        if (owner == names_.end()) {
            return;
        }
        for (auto &candidate : owner->second->functions) {
            if (candidate.exported && candidate.key.name == function.name) {
                reach({owner->second, &candidate});
            }
        }
    }

    // Keep every export of a batch module whose name the program may use to call it.
    void escape(const std::u32string &module, const std::string &reason) {
        const auto owner = names_.find(module);
        if (owner == names_.end() || !report_.kept_.emplace(module, reason).second) {
            return;
        }
        reach_exports(*owner->second);
    }

    // Reach every exported function of a module.
    void reach_exports(Module &module) {
        for (auto &function : module.functions) {
            if (function.exported) {
                reach({&module, &function});
            }
        }
    }

    // An atom used as a value may name a called module later, so its module keeps every export; a strict module's
    // values name only itself, as in its error tuples (docs/compile.md#strict-liveness). Added for strict liveness.
    void escape_value(const FunctionRef site, const std::u32string &module, const std::string &reason) {
        if (!site.module->strict_liveness_ || module == site.module->name) {
            escape(module, reason);
        }
    }

    // A call whose module is a runtime value; with an atom-producing builtin it may name any module.
    void dynamic(const FunctionRef site) {
        if (dynamic_.empty()) {
            dynamic_ = function_text(site);
            keep_all();
        }
    }

    // An atom-producing builtin; with a dynamic call it may name any module, unless its module's atoms never name
    // a called module (-compile({clause_liveness, strict}), docs/compile.md#strict-liveness).
    void produced(const FunctionRef site, const std::string &builtin) {
        if (site.module->strict_liveness_) {
            return;
        }
        if (producer_.empty()) {
            producer_ = builtin + " in " + function_text(site);
            keep_all();
        }
    }

    // Keep every export of the program once both a dynamic call and an atom-producing builtin are live.
    void keep_all() {
        if (dynamic_.empty() || producer_.empty()) {
            return;
        }
        for (const auto &module : modules_) {
            escape(module->name, "a dynamic call in " + dynamic_ + " and the atom-producing " + producer_);
        }
    }

    // Visit the body of a live function: its direct callees and every expression of its clauses.
    void scan(const FunctionRef function) {
        for (const auto &callee : callees_[function.function]) {
            if (callee.module->name == U"erlang" && atom_producer(callee.function->key)) {
                produced(function, function_text(callee));
            }
            note_node_start(function, callee.module->name, callee.function->key);
            reach(callee);
        }
        if (predefined_form(*function.module, function.function->form)) {
            return; // module_info/0,1 and behaviour_info/1 return literal data, naming no called code (CLAU12c).
        }
        const auto &syntax = *function.module->syntax;
        auto pending = function_roots(std::get<ast::Function>(syntax.form(function.function->form).value));
        Skipped skipped;
        while (!pending.empty()) {
            const auto &expression = syntax.expression(pending.back());
            pending.pop_back();
            visit(function, expression, skipped);
            const auto children = expression_children(*function.module, expression);
            pending.insert(pending.end(), children.begin(), children.end());
        }
    }

    // Reach what one expression names; parents are visited before the operands they skip.
    void visit(const FunctionRef function, const ast::Expression &expression, Skipped &skipped) {
        if (const auto *atom = std::get_if<ast::Atom>(&expression.value)) {
            if (!skipped.contains(&expression)) {
                escape_value(function, atom->name,
                             "'" + utf8(atom->name) + "' is used as a value in " + function_text(function));
            }
        } else if (const auto *call = std::get_if<ast::CallExpression>(&expression.value)) {
            visit_call(function, expression, *call, skipped);
        } else if (const auto *local = std::get_if<ast::LocalFunReference>(&expression.value)) {
            visit_local_fun(function, expression, *local);
        } else if (const auto *remote = std::get_if<ast::RemoteFunReference>(&expression.value)) {
            visit_remote_fun(function, *remote);
        } else if (const auto *record = std::get_if<ast::RecordExpression>(&expression.value)) {
            visit_record(function, *record);
        }
    }

    // A builtin call (apply/spawn targets, atom producers) or a call with a runtime module.
    void visit_call(const FunctionRef function, const ast::Expression &expression, const ast::CallExpression &call,
                    Skipped &skipped) {
        const auto &syntax = *function.module->syntax;
        if (const auto found = function.function->services.find(&expression);
            found != function.function->services.end()) {
            if (const auto index = found->second.builtin) {
                const auto &name = abi::v1::bridge_builtins.at(*index);
                continue_builtin(std::string(name.module), std::string(name.function), name.arity, function);
            }
            visit_builtin(function, found->second.identity, call, skipped);
            return;
        }
        if (dynamic_call(syntax, call)) {
            const auto &remote = std::get<ast::RemoteExpression>(syntax.expression(ungroup(syntax, call.target)).value);
            if (const auto *module = literal_atom(syntax, remote.module)) {
                escape(module->name, "a runtime function of " + utf8(module->name) + " in " + function_text(function));
            } else {
                dynamic(function);
            }
        }
    }

    // apply(M, F, Args) and the spawn family reach a literal M:F; a runtime M is a dynamic call.
    void visit_builtin(const FunctionRef function, const FunctionKey &key, const ast::CallExpression &call,
                       Skipped &skipped) {
        if (atom_producer(key)) {
            produced(function, "erlang:" + utf8(key.name) + "/" + std::to_string(key.arity));
        }
        if (key.name == U"binary_to_term" && !function.module->strict_liveness_) {
            dynamic(function); // Decoded external funs are called like dynamic calls.
        }
        const auto *mfa = mfa_builtin(key);
        if (!mfa) {
            return;
        }
        const auto &syntax = *function.module->syntax;
        const auto *module = literal_atom(syntax, call.arguments[mfa->module]);
        const auto *name = literal_atom(syntax, call.arguments[mfa->module + 1]);
        if (!module) {
            if (!boot_entry_call(function)) {
                dynamic(function);
            }
            return;
        }
        if (!name) {
            escape(module->name, "a runtime function of " + utf8(module->name) + " in " + function_text(function));
            return;
        }
        skipped.insert(&syntax.expression(ungroup(syntax, call.arguments[mfa->module])));
        skipped.insert(&syntax.expression(ungroup(syntax, call.arguments[mfa->module + 1])));
        if (const auto count = applied_arity(syntax, call.arguments[mfa->module + 2])) {
            reach_name(module->name, {name->name, *count}, function);
        } else {
            reach_arities(module->name, *name);
        }
    }

    // fun F/A reaches the local function, or the erlang builtin an auto-imported name stands for.
    void visit_local_fun(const FunctionRef function, const ast::Expression &expression,
                         const ast::LocalFunReference &reference) {
        const auto count = arity(reference.arity);
        const auto index = count ? source_function(*function.module, {reference.name.name, *count}) : std::nullopt;
        if (index) {
            reach({function.module, &function.module->functions.at(*index)});
        } else if (const auto found = function.function->builtin_funs.find(&expression);
                   found != function.function->builtin_funs.end()) {
            reach_name(U"erlang", found->second, function);
        }
    }

    // A literal fun M:F/A reaches M:F/A; a runtime function or arity keeps M's exports, a runtime M is dynamic.
    void visit_remote_fun(const FunctionRef function, const ast::RemoteFunReference &reference) {
        if (const auto target = external_fun(reference)) {
            reach_name(std::get<0>(*target), {std::get<1>(*target), std::get<2>(*target)}, function);
        } else if (const auto *module = std::get_if<ast::Atom>(&reference.module)) {
            escape(module->name,
                   "fun " + utf8(module->name) + ":F/A with a runtime part in " + function_text(function));
        } else {
            dynamic(function);
        }
    }

    // A tuple record's construction uses its name as the tag value.
    void visit_record(const FunctionRef function, const ast::RecordExpression &record) {
        const auto *name = std::get_if<ast::UnresolvedRecordName>(&record.identity.value);
        if (name && !record.base && !function.module->strict_liveness_) {
            escape(name->name.name, "record tag '" + utf8(name->name.name) + "' in " + function_text(function));
        }
    }

    std::span<const std::unique_ptr<Module>> modules_;
    std::map<std::u32string, Module *> names_;
    std::map<const Function *, std::vector<FunctionRef>> callees_;
    std::vector<FunctionRef> pending_;
    LivenessReport report_;
    // Whether the program is linked as a node, the entry, and whether it became a node: every export stays by the
    // distribution rule. Added for export liveness (CLAU12c).
    bool linked_node_ = false;
    FunctionRef entry_{};
    bool node_ = false;
    // Where the first dynamic call and atom-producing builtin were found; empty until then.
    std::string dynamic_;
    std::string producer_;
};
} // namespace

LivenessReport mark_live(const std::span<const std::unique_ptr<Module>> modules, const CallGraph &calls,
                         const FunctionRef entry, const bool linked_node) {
    return Liveness(modules, calls, linked_node).run(entry);
}

CallGraph live_calls(const CallGraph &calls) {
    const auto live = [](const FunctionRef function) { return function.function->live_; };
    CallGraph result;
    std::ranges::copy_if(calls.calls, std::back_inserter(result.calls),
                         [&](const Call &call) { return live(call.caller); });
    std::ranges::copy_if(calls.order, std::back_inserter(result.order), live);
    for (const auto &component : calls.components) {
        Component kept{.members = {}, .recursive = component.recursive};
        std::ranges::copy_if(component.members, std::back_inserter(kept.members), live);
        if (!kept.members.empty()) {
            result.components.push_back(std::move(kept));
        }
    }
    return result;
}
} // namespace clause::semantic
