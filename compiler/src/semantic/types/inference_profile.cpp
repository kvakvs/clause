#include "inference_profile.hpp"
#include "lattice.hpp"
#include "printing.hpp"
#include <algorithm>
#include <set>
#include <span>

namespace clause::semantic::types {
namespace {
// A summary as numbers that are equal exactly when the summaries are.
using Fingerprint = std::vector<std::size_t>;
using Fingerprints = std::map<const Function *, Fingerprint>;

// Append a list of facts, led by its length so that concatenations stay unambiguous.
void add_ids(Fingerprint &out, const std::vector<Id> &ids) {
    out.push_back(ids.size());
    for (const auto id : ids) {
        out.push_back(id.index);
    }
}

void add_fact(Fingerprint &out, const Fact &fact) {
    out.push_back(fact.type.index);
    out.push_back(fact.argument ? *fact.argument + 1 : 0);
    out.push_back(fact.dependent ? *fact.dependent + 1 : 0);
}

Fingerprint fingerprint(const Summary &summary) {
    Fingerprint out;
    add_ids(out, summary.inputs);
    add_fact(out, summary.result);
    add_ids(out, summary.entry);
    out.push_back(summary.types.size());
    for (const auto &type : summary.types) {
        add_ids(out, type.inputs);
        add_fact(out, type.result);
        out.push_back(type.exact ? 1 : 0);
    }
    return out;
}

// The fingerprint of every summary.
Fingerprints fingerprints(const Inference &inference) {
    Fingerprints result;
    for (const auto &[function, summary] : inference.functions) {
        result.emplace(function, fingerprint(summary));
    }
    return result;
}

// Each component's callees outside it, from the batch's direct calls.
std::vector<std::vector<const Function *>> outside_callees(const CallGraph &calls) {
    std::map<const Function *, std::size_t> component_of;
    for (std::size_t index = 0; index < calls.components.size(); ++index) {
        for (const auto member : calls.components[index].members) {
            component_of.emplace(member.function, index);
        }
    }
    std::vector<std::set<const Function *>> callees(calls.components.size());
    for (const auto &call : calls.calls) {
        const auto caller = component_of.find(call.caller.function);
        const auto callee = component_of.find(call.callee.function);
        if (caller != component_of.end() && (callee == component_of.end() || callee->second != caller->second)) {
            callees[caller->second].insert(call.callee.function);
        }
    }
    std::vector<std::vector<const Function *>> result;
    result.reserve(callees.size());
    for (const auto &set : callees) {
        result.emplace_back(set.begin(), set.end());
    }
    return result;
}

// Whether a function's entry in two maps is the same, a missing entry being equal only to another missing one.
template <typename Map> bool same(const Map &now, const Map &before, const Function *function) {
    const auto current = now.find(function);
    const auto previous = before.find(function);
    if (current == now.end() || previous == before.end()) {
        return (current == now.end()) == (previous == before.end());
    }
    return current->second == previous->second;
}

// Note the graph's growth since the previous note.
void note_graph(const Graph &graph, InferenceProfile::PassMemory &memory, InferenceProfile::Incremental &incremental) {
    incremental.nodes_ = graph.size();
    incremental.created_ = graph.size() - memory.nodes_;
    incremental.widenings_ = graph.widenings() - memory.widenings_;
    memory.nodes_ = graph.size();
    memory.widenings_ = graph.widenings();
}

// Count a clean component whose summaries changed all the same, once per component with the pass it first did.
void note_clean_changed(InferenceProfile &profile, const Component &component) {
    const auto found =
        std::ranges::find(profile.clean_changed_, &component, &InferenceProfile::CleanChanged::component_);
    if (found != profile.clean_changed_.end()) {
        ++found->times_;
        return;
    }
    profile.clean_changed_.push_back({.component_ = &component, .pass_ = profile.passes_.size(), .times_ = 1});
}

// Classify one component of the pass against the previous one: dirty by its members' inputs or its callees'
// summaries, else clean, with the time and work spent on it.
void note_component(InferenceProfile &profile, const Inference &inference, const std::size_t index,
                    const Fingerprints &summaries) {
    const auto &memory = profile.memory_;
    const auto &component = *profile.costs_[index].component_;
    auto &incremental = profile.incremental_.back();
    const auto inputs_differ = std::ranges::any_of(component.members, [&](const FunctionRef member) {
        return !same(inference.inputs, memory.inputs_, member.function);
    });
    const auto callees_differ = std::ranges::any_of(
        memory.callees_[index], [&](const Function *callee) { return !same(summaries, memory.summaries_, callee); });
    incremental.inputs_differ_ += inputs_differ ? 1 : 0;
    incremental.callees_differ_ += callees_differ ? 1 : 0;
    if (inputs_differ || callees_differ) {
        return;
    }
    ++incremental.clean_;
    const auto &cost = profile.costs_[index];
    incremental.clean_elapsed_ += cost.elapsed_ - memory.elapsed_[index];
    incremental.clean_work_ += cost.work_ - memory.work_[index];
    if (std::ranges::any_of(component.members, [&](const FunctionRef member) {
            return !same(summaries, memory.summaries_, member.function);
        })) {
        ++incremental.clean_changed_;
        note_clean_changed(profile, component);
    }
}

// Whether every member of a component has the summary it had at the end of the previous pass.
bool as_before(const Inference &inference, const Component &component, const Fingerprints &previous) {
    return std::ranges::all_of(component.members, [&](const FunctionRef member) {
        const auto found = previous.find(member.function);
        return found != previous.end() && found->second == fingerprint(inference.functions.at(member.function));
    });
}

// Count the recursive components whose solve in this pass ended on the previous pass's summaries. Added for the
// solve repetition measurements of --print-stage-times.
void note_repeated(InferenceProfile &profile, const Inference &inference) {
    profile.repetitions_.resize(profile.costs_.size());
    for (std::size_t index = 0; index < profile.costs_.size(); ++index) {
        const auto &component = *profile.costs_[index].component_;
        if (component.recursive && as_before(inference, component, profile.memory_.summaries_)) {
            ++profile.repetitions_[index].repeated_;
        }
    }
}

// Tree sizes past this count as this. Added for the fact size measurements of --print-stage-times.
constexpr std::size_t SIZE_CAP = 1'000'000;
// Characters of a largest fact printed.
constexpr std::size_t LARGEST_TEXT = 200;

// Whether a node is a container that counts toward the depth budget.
bool container(const Kind kind) {
    return kind == Kind::tuple || kind == Kind::list || kind == Kind::map || kind == Kind::record ||
           kind == Kind::positional;
}

// Whether a node is a tuple or map at the element budget.
bool at_elements(const Node &node, const FactLimits &limits) {
    return (node.kind == Kind::tuple && node.children.size() >= limits.elements) ||
           (node.kind == Kind::map && node.children.size() / 2 >= limits.elements);
}

// Per graph node: its tree size (capped), container depth, widest union, and whether a tuple or map in it reaches
// the element budget. Children are interned before their parents, so one pass in index order fills them. Added for
// the fact size measurements of --print-stage-times.
struct NodeSizes {
    std::vector<std::size_t> size_;
    std::vector<std::size_t> depth_;
    std::vector<std::size_t> union_;
    std::vector<bool> wide_;

    NodeSizes(const Graph &graph, const FactLimits &limits) {
        const auto owner = graph.top().owner;
        for (std::size_t index = 0; index < graph.size(); ++index) {
            add(graph.get({owner, index}), limits);
        }
    }

  private:
    void add(const Node &node, const FactLimits &limits) {
        std::size_t size = 1;
        std::size_t depth = 0;
        std::size_t widest = node.kind == Kind::union_type ? node.children.size() : 0;
        bool wide = at_elements(node, limits);
        for (const auto child : node.children) {
            size = std::min(SIZE_CAP, size + size_[child.index]);
            depth = std::max(depth, depth_[child.index]);
            widest = std::max(widest, union_[child.index]);
            wide = wide || wide_[child.index];
        }
        size_.push_back(size);
        depth_.push_back(depth + (container(node.kind) ? 1 : 0));
        union_.push_back(widest);
        wide_.push_back(wide);
    }
};

// Count one fact into the pass's sizes.
void add_size(InferenceProfile::FactSizes &sizes, const NodeSizes &nodes, const FactLimits &limits, const Id fact) {
    ++sizes.facts_;
    sizes.size_max_ = std::max(sizes.size_max_, nodes.size_[fact.index]);
    sizes.size_sum_ += nodes.size_[fact.index];
    sizes.union_max_ = std::max(sizes.union_max_, nodes.union_[fact.index]);
    sizes.at_depth_ += nodes.depth_[fact.index] >= limits.depth ? 1 : 0;
    sizes.at_members_ += nodes.union_[fact.index] >= limits.members ? 1 : 0;
    sizes.at_elements_ += nodes.wide_[fact.index] ? 1 : 0;
}

// Every fact of a summary: its domain, result, entry domain and function types.
std::vector<Id> summary_facts(const Summary &summary) {
    std::vector<Id> facts = summary.inputs;
    facts.push_back(summary.result.type);
    facts.insert(facts.end(), summary.entry.begin(), summary.entry.end());
    for (const auto &type : summary.types) {
        facts.insert(facts.end(), type.inputs.begin(), type.inputs.end());
        facts.push_back(type.result.type);
    }
    return facts;
}

// Note the sizes of every summary's facts and every local input after the pass.
void note_sizes(InferenceProfile &profile, const Inference &inference) {
    const FactLimits limits;
    const NodeSizes nodes(inference.graph, limits);
    profile.fact_sizes_.resize(profile.passes_.size());
    auto &sizes = profile.fact_sizes_.back();
    for (const auto &[function, summary] : inference.functions) {
        for (const auto fact : summary_facts(summary)) {
            add_size(sizes, nodes, limits, fact);
        }
        sizes.full_types_ += summary.types.size() >= FUNCTION_TYPES ? 1 : 0;
    }
    for (const auto &[function, inputs] : inference.inputs) {
        for (const auto fact : inputs) {
            add_size(sizes, nodes, limits, fact);
        }
    }
    sizes.dependents_ = inference.dependents.size();
}

// Keep what the next note compares against.
void remember(InferenceProfile &profile, const Inference &inference, Fingerprints summaries) {
    auto &memory = profile.memory_;
    memory.summaries_ = std::move(summaries);
    memory.inputs_ = inference.inputs;
    memory.elapsed_.clear();
    memory.work_.clear();
    for (const auto &cost : profile.costs_) {
        memory.elapsed_.push_back(cost.elapsed_);
        memory.work_.push_back(cost.work_);
    }
}

// A component's fact with the largest tree size.
Id largest_fact(const Inference &inference, const NodeSizes &nodes, const Component &component) {
    Id largest = inference.graph.bottom();
    for (const auto member : component.members) {
        for (const auto fact : summary_facts(inference.functions.at(member.function))) {
            largest = nodes.size_[fact.index] > nodes.size_[largest.index] ? fact : largest;
        }
    }
    return largest;
}

// A fact as text, cut to LARGEST_TEXT characters.
std::string largest_text(const Graph &graph, const Id fact) {
    auto text = type_source(graph, fact);
    if (text.size() > LARGEST_TEXT) {
        text.resize(LARGEST_TEXT);
        text += "...";
    }
    return text;
}
} // namespace

void note_pass(Inference &inference, const CallGraph &calls) {
    auto &profile = *inference.profile;
    profile.incremental_.resize(profile.passes_.size());
    auto &incremental = profile.incremental_.back();
    incremental.noted_ = true;
    note_graph(inference.graph, profile.memory_, incremental);
    incremental.components_ = calls.components.size();
    auto summaries = fingerprints(inference);
    // The first pass has nothing to compare with.
    if (profile.memory_.callees_.empty()) {
        profile.memory_.callees_ = outside_callees(calls);
    } else {
        for (std::size_t index = 0; index < profile.costs_.size(); ++index) {
            note_component(profile, inference, index, summaries);
        }
        note_repeated(profile, inference);
    }
    note_sizes(profile, inference);
    remember(profile, inference, std::move(summaries));
}

void note_round(Inference &inference, const Component &component, const std::size_t round) {
    auto &profile = *inference.profile;
    // The first pass has nothing to compare with.
    if (profile.memory_.summaries_.empty()) {
        return;
    }
    profile.repetitions_.resize(profile.costs_.size());
    auto &repetition = profile.repetitions_[static_cast<std::size_t>(&component - profile.costs_.front().component_)];
    if (round == 0) {
        repetition.reaching_ = false;
    }
    if (repetition.reaching_ || !as_before(inference, component, profile.memory_.summaries_)) {
        return;
    }
    repetition.reaching_ = true;
    ++repetition.reached_;
    repetition.skippable_ += round + 1;
}

std::vector<LargestFact> largest_facts(const Inference &inference, const std::size_t count) {
    const auto &profile = *inference.profile;
    std::vector<const InferenceProfile::Cost *> costs;
    costs.reserve(profile.costs_.size());
    for (const auto &cost : profile.costs_) {
        costs.push_back(&cost);
    }
    const auto shown = std::min(count, costs.size());
    const auto slower = [](const auto *left, const auto *right) { return left->elapsed_ > right->elapsed_; };
    std::ranges::partial_sort(costs, costs.begin() + static_cast<std::ptrdiff_t>(shown), slower);
    const NodeSizes nodes(inference.graph, FactLimits{});
    std::vector<LargestFact> result;
    result.reserve(shown);
    for (const auto *cost : std::span(costs).first(shown)) {
        const auto largest = largest_fact(inference, nodes, *cost->component_);
        result.push_back({.component_ = cost->component_,
                          .size_ = nodes.size_[largest.index],
                          .text_ = largest_text(inference.graph, largest)});
    }
    return result;
}
} // namespace clause::semantic::types
