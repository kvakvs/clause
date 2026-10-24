#include "inference_incremental.hpp"
#include <algorithm>
#include <numeric>

namespace clause::semantic::types {
namespace {
// How often a function's summary changed so far.
std::size_t summary_version(const Inference &inference, const Function *function) {
    const auto found = inference.summary_versions.find(function);
    return found == inference.summary_versions.end() ? 0 : found->second;
}

// How often a function's local inputs changed so far; 0 for a function without them.
std::size_t input_version(const Inference &inference, const Function *function) {
    const auto found = inference.input_changes.find(function);
    return found == inference.input_changes.end()
               ? 0
               : std::accumulate(found->second.begin(), found->second.end(), std::size_t{0});
}

// The index of `function` among the component's members, if it is one.
std::optional<std::size_t> member_index(const Component &component, const Function *function) {
    const auto found = std::ranges::find(component.members, function, &FunctionRef::function);
    return found == component.members.end()
               ? std::nullopt
               : std::optional{static_cast<std::size_t>(found - component.members.begin())};
}

// Add a member walk's reads to its component walk's. Added for member-granular warm solves (CONV.8).
void merge(WalkReads &into, const WalkReads &reads) {
    into.summaries.insert(reads.summaries.begin(), reads.summaries.end());
    into.inputs.insert(reads.inputs.begin(), reads.inputs.end());
    for (const auto &[function, fact] : reads.funs) {
        into.funs.insert_or_assign(function, fact);
    }
}
} // namespace

bool unchanged(const Inference &inference, const ComponentWalk &last) {
    return std::ranges::all_of(
               last.summaries,
               [&](const auto &read) { return summary_version(inference, read.first) == read.second; }) &&
           std::ranges::all_of(last.inputs,
                               [&](const auto &read) { return input_version(inference, read.first) == read.second; });
}

bool inputs_changed(const Inference &inference, const Component &component, const ComponentWalk &last) {
    return std::ranges::any_of(last.inputs, [&](const auto &read) {
        return member_index(component, read.first) && input_version(inference, read.first) != read.second;
    });
}

void replay(Inference &inference, const ComponentWalk &last, std::size_t &work) {
    work += last.work;
    inference.reanalysis_work += last.reanalysis_work;
    // A component's walk counts the checks of the member walks its solve took again (CONV.8).
    inference.reanalysis_checks += last.reanalysis_checks;
    inference.reanalysis_refusals += last.reanalysis_refusals;
    for (const auto &[function, fact] : last.funs) {
        inference.fun_reads.insert_or_assign(function, fact);
    }
}

bool members_unchanged(const Inference &inference, const MemberWalk &last) {
    return std::ranges::all_of(last.members, [&](const auto &read) {
        const auto &[function, result, types] = read;
        const auto &summary = inference.functions.at(function);
        return summary.result == result && summary.types == types;
    });
}

void replay_member(Inference &inference, const MemberWalk &last, std::size_t &work) {
    replay(inference, last.outside, work);
    inference.replayed_joins += last.joins;
    if (!inference.walk_reads) {
        return;
    }
    WalkReads reads{.summaries = {}, .inputs = {}, .funs = last.outside.funs};
    for (const auto &read : last.outside.summaries) {
        reads.summaries.insert(read.first);
    }
    for (const auto &read : last.members) {
        reads.summaries.insert(std::get<0>(read));
    }
    for (const auto &read : last.outside.inputs) {
        reads.inputs.insert(read.first);
    }
    merge(*inference.walk_reads, reads);
}

WalkTracker::WalkTracker(Inference &inference, const Component &component, const std::size_t work)
    : inference_(inference), component_(component), work_(work), reanalysis_work_(inference.reanalysis_work),
      checks_(inference.reanalysis_checks), refusals_(inference.reanalysis_refusals) {
    for (const auto member : component.members) {
        const auto found = inference.functions.find(member.function);
        before_.push_back(found == inference.functions.end() ? std::nullopt : std::optional{found->second});
        versions_.push_back(summary_version(inference, member.function));
        // Their calls' argument facts may change. Added for input gathering (OPTC.F).
        inference.walked.insert(member.function);
    }
    inference.walk_reads = &reads_;
}

WalkTracker::~WalkTracker() {
    if (!finished_) {
        inference_.walk_reads = nullptr;
    }
}

void WalkTracker::bump_versions() {
    for (std::size_t index = 0; index < component_.members.size(); ++index) {
        const auto *function = component_.members[index].function;
        if (!before_[index] || *before_[index] != inference_.functions.at(function)) {
            ++inference_.summary_versions[function];
        }
    }
}

std::vector<std::pair<const Function *, std::size_t>> WalkTracker::summary_reads() const {
    std::vector<std::pair<const Function *, std::size_t>> result;
    for (const auto *function : reads_.summaries) {
        const auto member = member_index(component_, function);
        // A recursive component's solve starts its members from none(): only a plain function reads itself.
        if (!member) {
            result.emplace_back(function, summary_version(inference_, function));
        } else if (!component_.recursive) {
            result.emplace_back(function, versions_[*member]);
        }
    }
    return result;
}

std::vector<std::pair<const Function *, std::size_t>> WalkTracker::input_reads() const {
    auto functions = reads_.inputs;
    for (const auto member : component_.members) {
        functions.insert(member.function);
    }
    std::vector<std::pair<const Function *, std::size_t>> result;
    result.reserve(functions.size());
    for (const auto *function : functions) {
        result.emplace_back(function, input_version(inference_, function));
    }
    return result;
}

ComponentWalk WalkTracker::finish(const std::size_t work) {
    finished_ = true;
    inference_.walk_reads = nullptr;
    bump_versions();
    return {.walked = true,
            .summaries = summary_reads(),
            .inputs = input_reads(),
            .funs = std::move(reads_.funs),
            .work = work - work_,
            .exhausted = work >= inference_.graph.limits().syntax_work,
            .reanalysis_start = reanalysis_work_,
            .reanalysis_work = inference_.reanalysis_work - reanalysis_work_,
            .reanalysis_checks = inference_.reanalysis_checks - checks_,
            .reanalysis_refusals = inference_.reanalysis_refusals - refusals_};
}

MemberTracker::MemberTracker(Inference &inference, const FunctionRef member, const std::size_t work)
    : inference_(inference), member_(member.function), outer_(inference.walk_reads), work_(work),
      reanalysis_work_(inference.reanalysis_work), checks_(inference.reanalysis_checks),
      refusals_(inference.reanalysis_refusals), joins_(inference.graph.joins()) {
    inference.walk_reads = &reads_;
}

MemberTracker::~MemberTracker() {
    if (!finished_) {
        inference_.walk_reads = outer_;
    }
}

void MemberTracker::finish(const std::size_t work, MemberWalk &walk) {
    finished_ = true;
    inference_.walk_reads = outer_;
    if (outer_) {
        merge(*outer_, reads_);
    }
    reads_.inputs.insert(member_);
    walk.outside = {.walked = true,
                    .summaries = {},
                    .inputs = {},
                    .funs = std::move(reads_.funs),
                    .work = work - work_,
                    .exhausted = work >= inference_.graph.limits().syntax_work,
                    .reanalysis_start = reanalysis_work_,
                    .reanalysis_work = inference_.reanalysis_work - reanalysis_work_,
                    .reanalysis_checks = inference_.reanalysis_checks - checks_,
                    .reanalysis_refusals = inference_.reanalysis_refusals - refusals_};
    for (const auto *function : reads_.inputs) {
        walk.outside.inputs.emplace_back(function, input_version(inference_, function));
    }
    // The solve tracks the members (the functions being solved) by their summaries, the others by versions.
    walk.members.clear();
    for (const auto *function : reads_.summaries) {
        if (inference_.solving.contains(function)) {
            const auto &summary = inference_.functions.at(function);
            walk.members.emplace_back(function, summary.result, summary.types);
        } else {
            walk.outside.summaries.emplace_back(function, summary_version(inference_, function));
        }
    }
    walk.joins = inference_.graph.joins() - joins_;
}
} // namespace clause::semantic::types
