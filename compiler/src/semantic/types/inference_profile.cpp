#include "inference_profile.hpp"
#include <algorithm>
#include <set>

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
    }
    remember(profile, inference, std::move(summaries));
}
} // namespace clause::semantic::types
