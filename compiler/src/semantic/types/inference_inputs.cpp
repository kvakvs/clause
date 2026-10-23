#include "inference_inputs.hpp"
#include "inference_funs.hpp"
#include "inference_profile.hpp"
#include "lattice.hpp"
#include "printing.hpp"
#include <algorithm>
#include <chrono>
#include <clause/compiler/source.hpp>
#include <format>
#include <set>
#include <span>
#include <string_view>
#include <utility>

namespace clause::semantic::types {
namespace {
// Add the functions of `module` that its fun F/A values name.
void add_fun_named(const Module &module, std::set<const Function *> &named) {
    for (const auto &fun : module.funs) {
        const auto found =
            fun.external || fun.expression ? module.lookup.end() : module.lookup.find({fun.function, fun.arity});
        if (found != module.lookup.end()) {
            named.insert(&module.functions.at(found->second));
        }
    }
}

// The functions of the batch that fun F/A names, whose callers are unknown.
std::set<const Function *> fun_named(const CallGraph &calls) {
    std::set<const Module *> modules;
    for (const auto function : calls.order) {
        modules.insert(function.module);
    }
    std::set<const Function *> result;
    for (const auto *module : modules) {
        add_fun_named(*module, result);
    }
    return result;
}

// The argument facts of each call of a function taking its callers' inputs, by function and argument.
std::map<const Function *, std::vector<std::vector<Id>>> call_arguments(const Inference &inference,
                                                                        const CallGraph &calls) {
    std::map<const Function *, std::vector<std::vector<Id>>> result;
    for (const auto &call : calls.calls) {
        if (!inference.inputs.contains(call.callee.function)) {
            continue;
        }
        const auto &syntax = *call.caller.module->syntax;
        const auto &arguments = std::get<ast::CallExpression>(syntax.expression(call.expression).value).arguments;
        auto &facts = result[call.callee.function];
        facts.resize(arguments.size());
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const auto found = inference.expressions.find(&syntax.expression(arguments[index]));
            facts[index].push_back(found == inference.expressions.end() ? inference.graph.top() : found->second.type);
        }
    }
    return result;
}

// Nodes one shape walk reads; past it, two facts count as differing in shape. Added for the input churn measurements
// of --print-stage-times.
constexpr std::size_t SHAPE_BUDGET = 4096;
// Characters of a fact the most changed input slots print. Added for the input churn measurements.
constexpr std::size_t CHURN_FACT_TEXT = 200;
// The most changed input slots printed. Added for the input churn measurements.
constexpr std::size_t CHURNED_SLOTS = 10;

// Whether a node holds integers only: a singleton, a range or an integer category.
bool integer_node(const Node &node) {
    static const std::set<std::string_view> CATEGORIES{"integer", "non_neg_integer", "pos_integer", "neg_integer"};
    return node.kind == Kind::integer || node.kind == Kind::range ||
           (node.kind == Kind::application && CATEGORIES.contains(node.name));
}

// Whether a node is a container that counts toward the depth budget.
bool container(const Kind kind) {
    return kind == Kind::tuple || kind == Kind::list || kind == Kind::map || kind == Kind::record ||
           kind == Kind::positional;
}

// A fact's structure as text, with every integer fact (and, when `functions_`, every fun) as one letter, and its
// container depth: two facts with equal text differ only in those. Added for the input churn measurements.
struct ShapeWalk {
    const Graph &graph_;
    bool functions_ = false;
    std::size_t budget_ = SHAPE_BUDGET;
    std::size_t depth_ = 0;
    std::string text_;

    void walk(const Id id, const std::size_t level) {
        if (budget_ == 0) {
            return;
        }
        --budget_;
        const auto &node = graph_.get(id);
        if (integer_node(node) || (functions_ && node.kind == Kind::function)) {
            text_ += integer_node(node) ? "I" : "F";
            return;
        }
        const auto inner = level + (container(node.kind) ? 1 : 0);
        depth_ = std::max(depth_, inner);
        text_ += std::format("{}:{}:{}[", static_cast<int>(node.kind), node.module, node.name);
        for (const auto &label : node.labels) {
            text_ += label + ",";
        }
        for (const auto child : node.children) {
            walk(child, inner);
        }
        text_ += ']';
    }
};

// The shape walk of a fact.
ShapeWalk shape(const Graph &graph, const Id fact, const bool functions) {
    ShapeWalk walk{.graph_ = graph, .functions_ = functions, .budget_ = SHAPE_BUDGET, .depth_ = 0, .text_ = {}};
    walk.walk(fact, 0);
    return walk;
}

// The members of a fact's top union, or 1.
std::size_t member_count(const Graph &graph, const Id fact) {
    const auto &node = graph.get(fact);
    return node.kind == Kind::union_type ? node.children.size() : 1;
}

// How an input slot's fact changed from `before` to `after`, the most telling kind first. Added for the input churn
// measurements of --print-stage-times.
Churn churn_kind(const Graph &graph, const Id before, const Id after) {
    if (after == graph.top()) {
        return Churn::term;
    }
    if (before == graph.bottom()) {
        return Churn::first;
    }
    const auto old_shape = shape(graph, before, false);
    const auto new_shape = shape(graph, after, false);
    if (new_shape.depth_ > old_shape.depth_) {
        return Churn::deeper;
    }
    if (member_count(graph, after) > member_count(graph, before)) {
        return Churn::member;
    }
    if (old_shape.budget_ == 0 || new_shape.budget_ == 0) {
        return Churn::shape;
    }
    if (old_shape.text_ == new_shape.text_) {
        return Churn::integer;
    }
    return shape(graph, before, true).text_ == shape(graph, after, true).text_ ? Churn::function : Churn::shape;
}

// Count one changed input slot in the current pass and the batch. Added for the input churn measurements.
void note_churn(Inference &inference, const Function *function, const std::size_t index, const Id before,
                const Id after) {
    auto &profile = *inference.profile;
    const auto kind = churn_kind(inference.graph, before, after);
    auto &churn = profile.passes_.back().churn_;
    ++churn.slots_;
    ++churn.kinds_.at(static_cast<std::size_t>(kind));
    auto &slot = profile.slots_[{function, index}];
    ++slot.changes_;
    slot.last_ = kind;
    slot.before_ = before;
    slot.after_ = after;
}

// Join the argument facts of one function's calls into its inputs; true when one changed.
bool gather_function(Inference &inference, Lattice &lattice, const Function *function, std::vector<Id> &inputs,
                     const std::vector<std::vector<Id>> &arguments, const bool widening) {
    bool changed = false;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const auto seen = lattice.join(arguments.at(index), 0);
        const auto next = widening ? lattice.widen(inputs[index], lattice.join(inputs[index], seen))
                                   : lattice.join(inputs[index], seen);
        if (next != inputs[index]) {
            changed = true;
            if (inference.profile) {
                note_churn(inference, function, index, inputs[index], next);
            }
        }
        inputs[index] = next;
    }
    return changed;
}

// Join the argument facts of every call of a local function into its inputs; true when an input changed.
bool gather_all(Inference &inference, const CallGraph &calls, const bool widening) {
    const auto arguments = call_arguments(inference, calls);
    Lattice lattice(inference.graph);
    bool changed = false;
    for (auto &[function, inputs] : inference.inputs) {
        // Reused inputs stay as kept (58W).
        const auto found = inference.reused.contains(function) ? arguments.end() : arguments.find(function);
        if (found == arguments.end() ||
            !gather_function(inference, lattice, function, inputs, found->second, widening)) {
            continue;
        }
        changed = true;
        if (inference.profile) {
            // Added for the input churn measurements of --print-stage-times.
            ++inference.profile->passes_.back().churn_.functions_;
        }
    }
    return changed;
}

// The fun F/A reads of the pass that did not see their function's final facts. Added for the input churn
// measurements of --print-stage-times.
std::size_t open_fun_reads(Inference &inference) {
    Lattice lattice(inference.graph);
    return static_cast<std::size_t>(std::ranges::count_if(inference.fun_reads, [&](const auto &read) {
        return summary_fun(lattice, inference.functions.at(read.first), read.first->key.arity) != read.second;
    }));
}

// A fact as text for the most changed slots, cut to CHURN_FACT_TEXT characters.
std::string churn_fact_text(const Graph &graph, const Id fact) {
    auto text = type_source(graph, fact);
    if (text.size() > CHURN_FACT_TEXT) {
        text.resize(CHURN_FACT_TEXT);
        text += "...";
    }
    return text;
}
} // namespace

void prepare_inputs(Inference &inference, const CallGraph &calls) {
    const auto named = fun_named(calls);
    for (const auto function : calls.order) {
        // A reused function keeps the inputs a build cache kept, or none (term()). Added for the inference cache.
        if (!function.function->exported && !named.contains(function.function) &&
            !inference.reused.contains(function.function)) {
            inference.inputs.emplace(function.function,
                                     std::vector<Id>(function.function->key.arity, inference.graph.bottom()));
            if (inference.profile) {
                // Added for the input churn measurements of --print-stage-times.
                inference.profile->slots_total_ += function.function->key.arity;
            }
        }
    }
}

bool gather_inputs(Inference &inference, const CallGraph &calls, const bool widening) {
    if (!inference.profile) {
        return gather_all(inference, calls, widening);
    }
    // Added for the incremental-potential measurements of --print-stage-times: before the inputs change.
    note_pass(inference, calls);
    // Added for the input churn measurements of --print-stage-times.
    const auto started = std::chrono::steady_clock::now();
    const bool changed = gather_all(inference, calls, widening);
    auto &churn = inference.profile->passes_.back().churn_;
    churn.gathered_ = true;
    churn.elapsed_ = std::chrono::steady_clock::now() - started;
    churn.open_fun_reads_ = open_fun_reads(inference);
    return changed;
}

std::vector<Id> inputs_of(const Inference &inference, const Function &function) {
    const auto found = inference.inputs.find(&function);
    return found == inference.inputs.end() ? std::vector<Id>(function.key.arity, inference.graph.top()) : found->second;
}

std::vector<InferenceProfile::ChurnedSlot> churned_slots(const Inference &inference, const CallGraph &calls) {
    const auto &profile = *inference.profile;
    std::map<const Function *, const Module *> modules;
    for (const auto function : calls.order) {
        modules.emplace(function.function, function.module);
    }
    std::vector<std::pair<std::string, const InferenceProfile::Slot *>> slots;
    for (const auto &[key, slot] : profile.slots_) {
        const auto &[function, index] = key;
        slots.emplace_back(std::format("{}:{}/{} #{}", utf8(modules.at(function)->name), utf8(function->key.name),
                                       function->key.arity, index + 1),
                           &slot);
    }
    const auto more = [](const auto &left, const auto &right) {
        return left.second->changes_ != right.second->changes_ ? left.second->changes_ > right.second->changes_
                                                               : left.first < right.first;
    };
    std::ranges::sort(slots, more);
    // The facts of the shown slots only: printing is far slower than counting.
    std::vector<InferenceProfile::ChurnedSlot> result;
    for (const auto &[name, slot] : std::span(slots).first(std::min(CHURNED_SLOTS, slots.size()))) {
        result.push_back({.name_ = name,
                          .changes_ = slot->changes_,
                          .last_ = slot->last_,
                          .before_ = churn_fact_text(inference.graph, slot->before_),
                          .after_ = churn_fact_text(inference.graph, slot->after_)});
    }
    return result;
}
} // namespace clause::semantic::types
