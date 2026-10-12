#include "map_ownership.hpp"
#include <map>
#include <set>
#include <variant>

namespace clause::semantic {
namespace {
// The expression an id names, through parentheses.
const ast::Expression &unwrapped(const ast::Module &syntax, const ast::ExprId &id) {
    const auto *expression = &syntax.expression(id);
    while (const auto *group = std::get_if<ast::Group>(&expression->value)) {
        expression = &syntax.expression(group->expression);
    }
    return *expression;
}

struct Reads {
    // Reads and exact checks per definition, the expression of its last read, and the definitions funs capture.
    std::map<BindingId, std::size_t> count;
    std::map<BindingId, const ast::Expression *> read;
    std::set<BindingId> captured;
};

// Count every use of each definition of the function except the definitions themselves.
Reads reads_of(const ast::Module &syntax, const Function &function) {
    Reads reads;
    for (const auto &binding : function.bindings) {
        if (binding.use != BindingUse::definition) {
            ++reads.count[binding.identity];
            reads.read[binding.identity] = &syntax.expression(binding.expression);
        }
    }
    for (const auto &entry : function.captures) {
        reads.captured.insert(entry.second.begin(), entry.second.end());
    }
    return reads;
}

// The definition the variable expression of a match makes, when the function records one there.
std::optional<BindingId> defined(const ast::Module &syntax, const Function &function, const ast::Expression &variable) {
    for (const auto &binding : function.bindings) {
        if (binding.use == BindingUse::definition && &syntax.expression(binding.expression) == &variable) {
            return binding.identity;
        }
    }
    return std::nullopt;
}

// The variable of a body item `V = <map expression or map comprehension>`, which builds a map no one else holds.
// This holds while map expressions build their maps at run time; a literal pool (OPT0.4) must exclude constant maps.
const ast::Expression *fresh_map(const ast::Module &syntax, const ast::ExprId &item) {
    const auto *match = std::get_if<ast::MatchExpression>(&unwrapped(syntax, item).value);
    if (!match) {
        return nullptr;
    }
    const auto &variable = unwrapped(syntax, match->left);
    const auto &value = unwrapped(syntax, match->right).value;
    const bool built =
        std::holds_alternative<ast::MapExpression>(value) || std::holds_alternative<ast::MapComprehension>(value);
    return built && std::holds_alternative<ast::Variable>(variable.value) ? &variable : nullptr;
}

// The places of a body item evaluated exactly once with it: the item, a match's right side, a call's arguments.
std::vector<const ast::Expression *> evaluated_once(const ast::Module &syntax, const ast::ExprId &item) {
    const auto &expression = unwrapped(syntax, item);
    std::vector<const ast::Expression *> places{&expression};
    if (const auto *match = std::get_if<ast::MatchExpression>(&expression.value)) {
        places.push_back(&unwrapped(syntax, match->right));
    } else if (const auto *call = std::get_if<ast::CallExpression>(&expression.value)) {
        for (const auto &argument : call->arguments) {
            places.push_back(&unwrapped(syntax, argument));
        }
    }
    return places;
}

// The map update among a body item's once-evaluated places whose base is `read`.
const ast::Expression *update_of(const ast::Module &syntax, const ast::ExprId &item, const ast::Expression *read) {
    for (const auto *place : evaluated_once(syntax, item)) {
        const auto *update = std::get_if<ast::MapExpression>(&place->value);
        if (update && update->base && &unwrapped(syntax, *update->base) == read) {
            return place;
        }
    }
    return nullptr;
}

// The single read of the fresh map a body item defines, when nothing else reads it and no fun captures it.
const ast::Expression *only_read(const ast::Module &syntax, const Function &function, const Reads &reads,
                                 const ast::ExprId &item) {
    const auto *variable = fresh_map(syntax, item);
    const auto identity = variable ? defined(syntax, function, *variable) : std::nullopt;
    if (!identity || reads.captured.contains(*identity)) {
        return nullptr;
    }
    const auto count = reads.count.find(*identity);
    return count != reads.count.end() && count->second == 1 ? reads.read.at(*identity) : nullptr;
}

// Mark the owned updates of one clause body.
void mark_body(const ast::Module &syntax, Function &function, const Reads &reads, std::span<const ast::ExprId> body) {
    for (std::size_t i = 0; i < body.size(); ++i) {
        const auto *read = only_read(syntax, function, reads, body[i]);
        for (const auto &item : read ? body.subspan(i + 1) : std::span<const ast::ExprId>{}) {
            if (const auto *update = update_of(syntax, item, read)) {
                function.owned_map_updates.insert(update);
                break;
            }
        }
    }
}
} // namespace

void mark_owned_map_updates(Module &module) {
    const auto &syntax = *module.syntax;
    for (auto &function : module.functions) {
        function.owned_map_updates.clear();
        const auto reads = reads_of(syntax, function);
        for (const auto &clause : std::get<ast::Function>(syntax.form(function.form).value).clauses) {
            mark_body(syntax, function, reads, clause.body);
        }
    }
}
} // namespace clause::semantic
