#include "inference_bindings.hpp"
#include "../capabilities.hpp"
#include "../records.hpp"
#include "dependent.hpp"
#include "inference_containers.hpp"
#include <algorithm>

namespace clause::semantic::types {
namespace {
// Narrowing a dependent variable narrows its parameters, theirs in turn, at most this many levels deep.
constexpr std::size_t IMPLIED_DEPTH = 4;

// A name and the names bound to the same value through `aliases`, in the order they are found.
std::vector<BindingId> bound_together(const std::map<BindingId, std::set<BindingId>> &aliases,
                                      const BindingId identity) {
    std::vector<BindingId> result{identity};
    for (std::size_t index = 0; index < result.size(); ++index) {
        const auto linked = aliases.find(result[index]);
        for (const auto other : linked == aliases.end() ? std::set<BindingId>{} : linked->second) {
            if (!std::ranges::contains(result, other)) {
                result.push_back(other);
            }
        }
    }
    return result;
}

// Every event or alias step consumes the shared inference budget before adding a fact.
bool spend(const Inference &inference, std::size_t &work) {
    if (work >= inference.graph.limits().syntax_work) {
        return false;
    }
    ++work;
    return true;
}

// Index a function's bindings: events by expression (the first one of each), its value slots, which start as
// term() with their argument relations, and the names defined again. Added for walk containers (OPTC.D).
BindingIndex index_bindings(const Inference &inference, const FunctionRef function) {
    BindingIndex result;
    result.offsets.push_back(0);
    for (const auto &clause : function.function->clause_bindings) {
        result.offsets.push_back(result.offsets.back() + clause.definitions.size());
        for (const auto &definition : clause.definitions) {
            result.initial.push_back({inference.graph.top(), definition.argument});
        }
    }
    std::vector<bool> seen(result.initial.size(), false);
    for (const auto &binding : function.function->bindings) {
        result.events.emplace(&function.module->syntax->expression(binding.expression), &binding);
        const auto slot = result.offsets.at(binding.identity.clause) + binding.identity.local;
        if (seen.at(slot) && binding.use == BindingUse::definition) {
            result.shared.insert(binding.identity);
        }
        seen.at(slot) = true;
    }
    return result;
}

// The function's binding index, built at its first walk. Added for walk containers (OPTC.D).
const BindingIndex &binding_index(Inference &inference, const FunctionRef function) {
    const auto found = inference.binding_indices.find(function.function);
    if (found != inference.binding_indices.end()) {
        return found->second;
    }
    return inference.binding_indices.emplace(function.function, index_bindings(inference, function)).first->second;
}

// A pattern and the fact of the value it matches.
using Matched = std::pair<ast::ExprId, Fact>;

// The fact of a map pattern's key: a literal atom or integer, or a bound variable's fact; term() otherwise.
Id key_fact(const BindingFacts &bindings, Lattice &lattice, const ast::ExprId &key) {
    const auto &value = bindings.function.module->syntax->expression(key).value;
    if (const auto *atom = std::get_if<ast::Atom>(&value)) {
        return lattice.atom(utf8(atom->name));
    }
    if (const auto *integer = std::get_if<ast::IntegerLiteral>(&value)) {
        return lattice.integer(integer->value.decimal);
    }
    return std::holds_alternative<ast::Variable>(value) ? bindings.read(key).type : lattice.graph().top();
}

// Adds the parts of a pattern with the facts of the values they match: aliases and groups match the whole value,
// tuple, list, map and tuple-record patterns their elements (docs/semantic.md#inference).
struct PatternParts {
    const BindingFacts &bindings;
    Lattice &lattice;
    // The value the pattern matches and the parts found.
    Fact value;
    std::vector<Matched> &pending;

    template <typename T> void operator()(const T &) const {}

    void operator()(const ast::Group &pattern) const { pending.emplace_back(pattern.expression, value); }

    void operator()(const ast::MatchExpression &pattern) const {
        pending.emplace_back(pattern.left, value);
        pending.emplace_back(pattern.right, value);
    }

    void operator()(const ast::Tuple &pattern) const {
        for (std::size_t index = 0; index < pattern.elements.size(); ++index) {
            const auto element = tuple_element(lattice, value.type, index + 1, pattern.elements.size());
            pending.emplace_back(pattern.elements[index], Fact{element});
        }
    }

    void operator()(const ast::List &pattern) const {
        auto rest = value.type;
        for (const auto &element : pattern.elements) {
            pending.emplace_back(element, Fact{head(lattice, rest)});
            rest = tail(lattice, rest);
        }
        if (pattern.tail) {
            pending.emplace_back(*pattern.tail, Fact{rest});
        }
    }

    void operator()(const ast::MapExpression &pattern) const {
        for (const auto &field : pattern.fields) {
            pending.emplace_back(field.value,
                                 Fact{map_value(lattice, {value.type, key_fact(bindings, lattice, field.key)})});
        }
    }

    void operator()(const ast::RecordExpression &pattern) const {
        const auto *layout = record_layout(*bindings.function.module, pattern.identity);
        if (!layout || layout->native) {
            return;
        }
        const auto fields = record_values(*bindings.function.module, pattern, true);
        for (std::size_t position = 0; position < fields.size(); ++position) {
            if (const auto &field = fields[position]) {
                const auto element = tuple_element(lattice, value.type, position + 2, fields.size() + 1);
                pending.emplace_back(*field, Fact{element});
            }
        }
    }
};
} // namespace

BindingFacts::BindingFacts(const FunctionRef owner, Inference &facts, std::size_t &work)
    : BindingFacts(owner, facts, binding_index(facts, owner), work) {}

BindingFacts::BindingFacts(const FunctionRef owner, Inference &facts, const BindingIndex &index, std::size_t &work)
    : function(owner), inference(facts), events(index.events), values(index.offsets, index.initial),
      shared(index.shared) {
    // Each binding costs one work step, as indexing it did before the index was kept; a walk that ran out of work
    // never reads it.
    const auto limit = inference.graph.limits().syntax_work;
    if (work < limit) {
        work = std::min(work + function.function->bindings.size(), limit);
    }
}

Fact BindingFacts::read(const ast::ExprId &id) const {
    const auto event = events.find(&function.module->syntax->expression(id));
    if (event != events.end() && event->second->use == BindingUse::read) {
        if (const auto *fact = values.find(event->second->identity)) {
            return resolved(inference, *fact, values);
        }
    }
    return {inference.graph.top()};
}

void BindingFacts::bind_lambda(const ast::MatchExpression &match) {
    const auto &syntax = *function.module->syntax;
    const auto &fun = syntax.expression(ungroup(syntax, match.right));
    const auto event = events.find(&syntax.expression(ungroup(syntax, match.left)));
    const auto source = events.find(&fun);
    // Y = X binds Y to X's value: narrowing either narrows both.
    if (event != events.end() && source != events.end() && event->second->use == BindingUse::definition &&
        source->second->use == BindingUse::read) {
        link(event->second->identity, source->second->identity);
    }
    if (fun_clauses(fun.value) && event != events.end() && event->second->use == BindingUse::definition &&
        !shared.contains(event->second->identity)) {
        lambdas.insert_or_assign(event->second->identity, ungroup(syntax, match.right));
    }
}

std::optional<ast::ExprId> BindingFacts::lambda(const ast::ExprId &read) const {
    const auto event = events.find(&function.module->syntax->expression(read));
    if (event == events.end() || event->second->use != BindingUse::read) {
        return std::nullopt;
    }
    const auto found = lambdas.find(event->second->identity);
    return found == lambdas.end() ? std::nullopt : std::optional{found->second};
}

void BindingFacts::narrow(const ast::ExprId &read, const Id fact) {
    const auto &syntax = *function.module->syntax;
    const auto event = events.find(&syntax.expression(ungroup(syntax, read)));
    if (event != events.end() && event->second->use == BindingUse::read) {
        (void)narrow_identity(values, event->second->identity, fact);
    }
}

Id BindingFacts::narrow_identity(BindingValues &facts, const BindingId identity, const Id fact,
                                 const std::size_t depth) const {
    Lattice lattice(inference.graph);
    std::vector<std::pair<BindingId, Id>> proven;
    for (const auto name : bound_together(aliases, identity)) {
        auto &value = facts.at(name);
        value.type = lattice.meet(value.type, fact);
        if (depth < IMPLIED_DEPTH) {
            const auto implies = implied(inference, value, facts);
            proven.insert(proven.end(), implies.begin(), implies.end());
        }
    }
    for (const auto &[parameter, input] : proven) {
        (void)narrow_identity(facts, parameter, input, depth + 1);
    }
    return facts.at(identity).type;
}

void BindingFacts::link(const BindingId first, const BindingId second) {
    if (first != second) {
        aliases[first].insert(second);
        aliases[second].insert(first);
    }
}

bool BindingFacts::stopped(const ast::Expression &maybe) const {
    const auto &body = std::get<ast::MaybeExpression>(maybe.value).body;
    for (std::size_t position = 0; position < body.size(); ++position) {
        const auto found = conditionals.find({&maybe, position});
        if (found != conditionals.end() && found->second.matched == inference.graph.bottom()) {
            return true;
        }
    }
    return false;
}

Id BindingFacts::failures(const ast::Expression &maybe) const {
    Lattice lattice(inference.graph);
    const auto &body = std::get<ast::MaybeExpression>(maybe.value).body;
    auto result = inference.graph.bottom();
    for (std::size_t position = 0; position < body.size(); ++position) {
        const auto found = conditionals.find({&maybe, position});
        if (found == conditionals.end()) {
            continue;
        }
        result = lattice.join(result, found->second.failed);
        if (found->second.matched == inference.graph.bottom()) {
            break;
        }
    }
    return result;
}

void BindingFacts::expect(const ast::ExprValue &value) {
    if (const auto *qualifiers = comprehension_qualifiers(value)) {
        for (const auto &qualifier : *qualifiers) {
            for (const auto &simple : zipped(qualifier)) {
                expect_generator(simple);
            }
        }
    }
}

void BindingFacts::expect_generator(const ast::Qualifier &qualifier) {
    const auto &syntax = *function.module->syntax;
    if (const auto *list = std::get_if<ast::ListGenerator>(&qualifier.value)) {
        waiting[&syntax.expression(list->input)].push_back({pattern_root(syntax, list->pattern), Part::elements});
    } else if (const auto *map = std::get_if<ast::MapGenerator>(&qualifier.value)) {
        auto &patterns = waiting[&syntax.expression(map->input)];
        patterns.push_back({pattern_root(syntax, map->key), Part::keys});
        patterns.push_back({pattern_root(syntax, map->value), Part::values});
    }
}

void BindingFacts::matched(const ast::Expression &expression, const Fact fact, std::size_t &work) {
    const auto found = waiting.find(&expression);
    if (found == waiting.end()) {
        return;
    }
    Lattice lattice(inference.graph);
    for (const auto &[pattern, part] : found->second) {
        if (part == Part::whole) {
            publish(pattern, fact, work);
        } else {
            const auto parts = part == Part::elements ? elements(lattice, fact.type)
                                                      : map_entries(lattice, fact.type, part == Part::keys);
            publish(pattern, Fact{parts}, work);
        }
    }
}

void BindingFacts::publish(const ast::ExprId &pattern, Fact fact, std::size_t &work) {
    Lattice lattice(inference.graph);
    std::vector<Matched> pending{{pattern, fact}};
    while (!pending.empty()) {
        if (!spend(inference, work)) {
            return;
        }
        const auto [id, value] = pending.back();
        pending.pop_back();
        const auto &expression = function.module->syntax->expression(id);
        const auto event = events.find(&expression);
        if (event != events.end() && event->second->use == BindingUse::definition) {
            values.at(event->second->identity) = value;
        }
        std::visit(PatternParts{*this, lattice, value, pending}, expression.value);
    }
}
} // namespace clause::semantic::types
