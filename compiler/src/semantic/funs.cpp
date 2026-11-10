#include "funs.hpp"
#include "capabilities.hpp"
#include "symbols.hpp"
#include <algorithm>

namespace clause::semantic {
bool fun_value(const ast::ExprValue &value) {
    return std::holds_alternative<ast::LocalFunReference>(value) ||
           std::holds_alternative<ast::RemoteFunReference>(value) || std::holds_alternative<ast::FunExpression>(value);
}

bool fun_call(const ast::Module &syntax, const ast::CallExpression &call) {
    const auto &target = syntax.expression(ungroup(syntax, call.target)).value;
    return !std::holds_alternative<ast::Atom>(target) && !std::holds_alternative<ast::RemoteExpression>(target);
}

bool dynamic_call(const ast::Module &syntax, const ast::CallExpression &call) {
    const auto *remote = std::get_if<ast::RemoteExpression>(&syntax.expression(ungroup(syntax, call.target)).value);
    const auto literal = [&](const ast::ExprId &id) {
        return std::holds_alternative<ast::Atom>(syntax.expression(ungroup(syntax, id)).value);
    };
    return remote && (!literal(remote->module) || !literal(remote->function));
}

bool dynamic_fun(const ast::RemoteFunReference &reference) {
    return std::holds_alternative<ast::Variable>(reference.module) ||
           std::holds_alternative<ast::Variable>(reference.name) ||
           std::holds_alternative<ast::Variable>(reference.arity);
}

const Function *fun_target(const Module &module, const ast::LocalFunReference &reference) {
    const auto count = arity(reference.arity);
    if (!count) {
        return nullptr;
    }
    const auto found = source_function(module, {reference.name.name, *count});
    return found ? &module.functions.at(*found) : nullptr;
}

std::optional<std::tuple<std::u32string, std::u32string, std::size_t>>
external_fun(const ast::RemoteFunReference &reference) {
    const auto *module = std::get_if<ast::Atom>(&reference.module);
    const auto *name = std::get_if<ast::Atom>(&reference.name);
    const auto *number = std::get_if<Integer>(&reference.arity);
    const auto count = number ? arity(*number) : std::nullopt;
    if (!module || !name || !count || *count > 255) {
        return {};
    }
    return std::tuple{module->name, name->name, *count};
}

namespace {
// Give each distinct fun value one entry; a local fun F/A named twice is one value, as in OTP.
class FunIndexer {
  public:
    // Fill the tables of `module`, which start empty.
    explicit FunIndexer(Module &module) : module_(module) {}

    // Start numbering the anonymous funs of `function`.
    void begin(const Function &function) {
        owner_ = &function;
        lambdas_ = 0;
    }

    // Record the entry of one fun expression; other expressions and unresolved funs are left alone.
    void visit(const ast::Expression &expression) {
        if (const auto *clauses = fun_clauses(expression.value)) {
            lambda(expression, clauses->front().arguments.size());
        } else if (const auto *local = std::get_if<ast::LocalFunReference>(&expression.value)) {
            if (const auto *target = fun_target(module_, *local)) {
                add(expression, {false, module_.name, target->key.name, target->key.arity, 0, target->symbol});
            }
        } else if (const auto *remote = std::get_if<ast::RemoteFunReference>(&expression.value)) {
            if (const auto names = external_fun(*remote)) {
                const auto &[owner, name, count] = *names;
                add(expression, {true, owner, name, count, 0, {}});
            }
        }
    }

    // The local fun the runtime calls the -on_load function through: the module's own fun F/0 when it makes one.
    // Added for -on_load (plan 21 step MIM9d).
    std::size_t on_load(const Function &function) {
        const auto key = std::tuple{false, module_.name, function.key.name, function.key.arity};
        const auto [found, fresh] = known_.try_emplace(key, module_.funs.size());
        if (fresh) {
            module_.funs.push_back(
                {false, module_.name, function.key.name, function.key.arity, locals_++, function.symbol});
        }
        return found->second;
    }

  private:
    // An anonymous fun is a value of its own, named -Function/Arity-fun-N- after its function, like OTP's.
    void lambda(const ast::Expression &expression, std::size_t arity) {
        // A record default is one expression, expanded at every construction: it is one fun.
        if (module_.fun_entries.contains(&expression)) {
            return;
        }
        const auto &owner = *owner_;
        const auto number = [](std::size_t value) {
            const auto text = std::to_string(value);
            return std::u32string(text.begin(), text.end());
        };
        FunEntry entry{false,
                       module_.name,
                       U"-" + owner.key.name + U"/" + number(owner.key.arity) + U"-fun-" + number(lambdas_++) + U"-",
                       arity,
                       locals_++,
                       {}};
        if (const auto found = owner.captures.find(&expression); found != owner.captures.end()) {
            entry.captures = found->second;
        }
        if (const auto found = owner.fun_names.find(&expression); found != owner.fun_names.end()) {
            entry.self = found->second;
        }
        if (arity + entry.captures.size() <= 255) {
            entry.symbol = encode_symbol({utf8(module_.name), utf8(entry.function), arity + entry.captures.size()});
        }
        entry.expression = &expression;
        entry.owner = &owner;
        module_.fun_entries.emplace(&expression, module_.funs.size());
        module_.funs.push_back(std::move(entry));
    }

    // Reuse the entry of an equal value or append a new one; local funs take the next index.
    void add(const ast::Expression &expression, FunEntry entry) {
        const auto key = std::tuple{entry.external, entry.module, entry.function, entry.arity};
        const auto [found, fresh] = known_.try_emplace(key, module_.funs.size());
        if (fresh) {
            entry.index = entry.external ? 0 : locals_++;
            module_.funs.push_back(std::move(entry));
        }
        module_.fun_entries.emplace(&expression, found->second);
    }

    Module &module_;
    // Entries of the values seen so far, and the number of local funs among them.
    std::map<std::tuple<bool, std::u32string, std::u32string, std::size_t>, std::size_t> known_;
    std::size_t locals_ = 0;
    // The function being walked and the number of its anonymous funs so far.
    const Function *owner_ = nullptr;
    std::size_t lambdas_ = 0;
};
} // namespace

void index_funs(Module &module) {
    FunIndexer indexer(module);
    for (const auto &function : module.functions) {
        indexer.begin(function);
        auto pending = function_roots(std::get<ast::Function>(module.syntax->form(function.form).value));
        std::ranges::reverse(pending);
        while (!pending.empty()) {
            const auto &expression = module.syntax->expression(pending.back());
            pending.pop_back();
            indexer.visit(expression);
            const auto children = expression_children(module, expression);
            pending.insert(pending.end(), children.rbegin(), children.rend());
        }
    }
    if (module.on_load_) {
        module.on_load_fun_ = indexer.on_load(module.functions.at(*module.on_load_));
    }
}

void add_builtin_fun(Module &module, const ast::Expression &expression, const FunctionKey &key) {
    const auto equal = [&](const FunEntry &entry) {
        return entry.external && entry.module == U"erlang" && entry.function == key.name && entry.arity == key.arity;
    };
    const auto found = std::ranges::find_if(module.funs, equal);
    const auto index = static_cast<std::size_t>(found - module.funs.begin());
    if (found == module.funs.end()) {
        module.funs.push_back({true, U"erlang", key.name, key.arity, 0, {}});
    }
    module.fun_entries.emplace(&expression, index);
}

const FunEntry &fun_entry(const Module &module, const ast::Expression &expression) {
    return module.funs.at(module.fun_entries.at(&expression));
}
} // namespace clause::semantic
