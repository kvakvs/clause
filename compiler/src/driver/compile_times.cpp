// Added for the compile phases of --print-stage-times.
#include "compile_times.hpp"
#include "../semantic/types/inference_inputs.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <clause/compiler/source.hpp>
#include <cstdint>
#include <format>
#include <span>
#include <utility>

namespace clause::cli {
namespace {
using semantic::types::InferenceProfile;

// How many of the costliest components are listed.
constexpr std::size_t SLOWEST = 10;

// A component as module:function/arity, with the count of its other members when it is recursive.
std::string component_name(const semantic::Component &component) {
    const auto &first = component.members.front();
    auto text =
        std::format("{}:{}/{}", utf8(first.module->name), utf8(first.function->key.name), first.function->key.arity);
    if (component.members.size() > 1) {
        text += std::format(" and {} more", component.members.size() - 1);
    }
    return text;
}

// The kinds of input change as printed. Added for the input churn measurements.
constexpr std::array<std::string_view, semantic::types::CHURN_KINDS> CHURN_NAMES{
    "first", "integer bound", "union member", "shape", "deeper", "function types", "term()"};

// How the gathering after a pass changed local inputs, by kind, and the fun F/A reads that were not final. Added
// for the input churn measurements of --print-stage-times.
std::string churn_note(const InferenceProfile::InputChurn &churn) {
    auto text = std::format("; inputs: {} functions, {} slots changed", churn.functions_, churn.slots_);
    std::string kinds;
    for (std::size_t kind = 0; kind < churn.kinds_.size(); ++kind) {
        if (churn.kinds_.at(kind) != 0) {
            kinds += std::format("{}{} {}", kinds.empty() ? "" : ", ", churn.kinds_.at(kind), CHURN_NAMES.at(kind));
        }
    }
    if (!kinds.empty()) {
        text += " (" + kinds + ")";
    }
    return text + std::format(", {} fun reads not final", churn.open_fun_reads_);
}

// The graph's growth during a pass, and the components that could have kept the previous pass's results, with
// what they cost. Added for the incremental-potential measurements of --print-stage-times.
std::string incremental_note(const InferenceProfile::Pass &pass, const InferenceProfile::Incremental &noted) {
    const std::chrono::duration<double, std::milli> clean = noted.clean_elapsed_;
    const std::chrono::duration<double, std::milli> elapsed = pass.elapsed_;
    const auto share = elapsed.count() == 0 ? 0.0 : 100.0 * clean.count() / elapsed.count();
    return std::format("; graph: {} nodes (+{}), {} widenings; clean: {} of {} components "
                       "({} inputs differ, {} callees differ), {} changed, {:.3f} ms ({:.1f}% of pass), {} work steps",
                       noted.nodes_, noted.created_, noted.widenings_, noted.clean_, noted.components_,
                       noted.inputs_differ_, noted.callees_differ_, noted.clean_changed_, clean.count(), share,
                       noted.clean_work_);
}

// One line per inference pass: its time, work steps, how it ended and how it changed local inputs.
void print_passes(const InferenceProfile &profile, const std::string_view subject) {
    for (std::size_t index = 0; index < profile.passes_.size(); ++index) {
        const auto &pass = profile.passes_[index];
        auto note = std::format("{} of {} work steps, {}", pass.work_, profile.budget_, pass.outcome_);
        if (pass.exhausted_at_) {
            note += ", work budget ran out in " + component_name(*pass.exhausted_at_);
        }
        // The forced last pass gathers no inputs.
        if (pass.churn_.gathered_) {
            note += churn_note(pass.churn_);
        }
        if (index < profile.incremental_.size() && profile.incremental_[index].noted_) {
            note += incremental_note(pass, profile.incremental_[index]);
        }
        print_stage_time(std::format("inference pass {}", index + 1), subject, pass.elapsed_, note);
    }
}

// Local input slots by how often they changed over the batch, with the time gathering inputs took, then the most
// changed slots with their last change. Added for the input churn measurements of --print-stage-times.
void print_churn(const Analysis &analysis, const std::string_view subject) {
    const auto &profile = analysis.profile;
    constexpr std::array<std::pair<std::size_t, std::string_view>, 7> BUCKETS{
        {{1, "1"}, {2, "2"}, {3, "3"}, {4, "4"}, {8, "5-8"}, {16, "9-16"}, {SIZE_MAX, ">16"}}};
    std::array<std::size_t, BUCKETS.size()> counts{};
    for (const auto &[slot, churn] : profile.slots_) {
        const auto bucket =
            std::ranges::find_if(BUCKETS, [&](const auto &entry) { return churn.changes_ <= entry.first; });
        ++counts.at(static_cast<std::size_t>(bucket - BUCKETS.begin()));
    }
    std::string note = "slots by changes";
    for (std::size_t index = 0; index < BUCKETS.size(); ++index) {
        note += std::format("{} {}x: {}", index == 0 ? "" : ",", BUCKETS.at(index).second, counts.at(index));
    }
    note += std::format("; {} of {} local input slots never changed", profile.slots_total_ - profile.slots_.size(),
                        profile.slots_total_);
    InferenceProfile::Duration gathering{};
    for (const auto &pass : profile.passes_) {
        gathering += pass.churn_.elapsed_;
    }
    print_stage_time("inference churn", subject, gathering, note);
    for (const auto &slot : analysis.inferred ? semantic::types::churned_slots(*analysis.inferred, analysis.calls)
                                              : std::vector<InferenceProfile::ChurnedSlot>{}) {
        print_stage_time("inference churned", slot.name_, {},
                         std::format("{} changes, last {}: {} -> {}", slot.changes_,
                                     CHURN_NAMES.at(static_cast<std::size_t>(slot.last_)), slot.before_, slot.after_));
    }
}

// Components whose inputs and callee summaries were unchanged but whose summaries changed all the same, with the
// pass that first saw it. Added for the incremental-potential measurements of --print-stage-times.
void print_clean_changed(const InferenceProfile &profile) {
    for (const auto &entry :
         std::span(profile.clean_changed_).first(std::min(SLOWEST, profile.clean_changed_.size()))) {
        print_stage_time(
            "inference clean-changed", component_name(*entry.component_), {},
            std::format("first in pass {}, {} {}", entry.pass_, entry.times_, entry.times_ == 1 ? "pass" : "passes"));
    }
}

// The components that took longest over all passes, costliest first.
void print_slowest(const InferenceProfile &profile) {
    std::vector<const InferenceProfile::Cost *> costs;
    costs.reserve(profile.costs_.size());
    for (const auto &cost : profile.costs_) {
        costs.push_back(&cost);
    }
    const auto shown = std::min(SLOWEST, costs.size());
    const auto slower = [](const auto *left, const auto *right) { return left->elapsed_ > right->elapsed_; };
    std::ranges::partial_sort(costs, costs.begin() + static_cast<std::ptrdiff_t>(shown), slower);
    for (const auto *cost : std::span(costs).first(shown)) {
        const auto budget = cost->passes_ * profile.budget_;
        const auto share = budget == 0 ? 0.0 : 100.0 * static_cast<double>(cost->work_) / static_cast<double>(budget);
        const auto note = std::format("{} work steps, {:.1f}% of {} {} of {} budget{}", cost->work_, share,
                                      cost->passes_, cost->passes_ == 1 ? "pass" : "passes", profile.budget_,
                                      cost->widened_ ? ", widened to term()" : "");
        print_stage_time("inference slowest", component_name(*cost->component_), cost->elapsed_, note);
    }
}

// A round series in at most ten groups of consecutive rounds, each as its mean members changed.
std::string round_series(const std::vector<std::size_t> &changes) {
    const auto groups = std::min<std::size_t>(10, changes.size());
    std::string text;
    for (std::size_t group = 0; group < groups; ++group) {
        const auto begin = group * changes.size() / groups;
        const auto end = (group + 1) * changes.size() / groups;
        std::size_t sum = 0;
        for (auto index = begin; index < end; ++index) {
            sum += changes[index];
        }
        // At most as many groups as rounds, so a group holds at least one round.
        const auto rounds = std::max<std::size_t>(1, end - begin);
        text += std::format("{}{}", group == 0 ? "" : " ", (sum + rounds / 2) / rounds);
    }
    return text;
}

// One recursive component's fixed-point rounds: solves, rounds against the cap, how solves gave up, the share of
// member walks that changed a summary, and the members changed per round of its last solve.
void print_rounds(const InferenceProfile::Cost &cost) {
    const auto &rounds = cost.rounds_;
    const auto useful =
        rounds.walks_ == 0 ? 0.0 : 100.0 * static_cast<double>(rounds.changes_) / static_cast<double>(rounds.walks_);
    const auto note = std::format(
        "{} solves, {} rounds (cap {} per solve), {} capped, {} over work share, {} remembered, {} out "
        "of pass budget; {} member walks ({} skipped), {:.1f}% changed a summary ({} grown, {} replaced, {} "
        "flipped); last solve: {} rounds, members changed per round (in tenths) {}",
        rounds.solves_, rounds.rounds_, rounds.round_cap_, rounds.capped_, rounds.over_share_, rounds.remembered_,
        rounds.exhausted_, rounds.walks_, rounds.skipped_, useful, rounds.grown_, rounds.replaced_, rounds.flipped_,
        rounds.last_changes_.size(), round_series(rounds.last_changes_));
    print_stage_time("inference rounds", component_name(*cost.component_), cost.elapsed_, note);
}

// The recursive components that took longest over all passes, with how their rounds went. Added for the
// recursive-component measurements of --print-stage-times.
void print_recursive(const InferenceProfile &profile) {
    std::vector<const InferenceProfile::Cost *> costs;
    for (const auto &cost : profile.costs_) {
        if (cost.rounds_.solves_ != 0) {
            costs.push_back(&cost);
        }
    }
    const auto shown = std::min(SLOWEST, costs.size());
    const auto slower = [](const auto *left, const auto *right) { return left->elapsed_ > right->elapsed_; };
    std::ranges::partial_sort(costs, costs.begin() + static_cast<std::ptrdiff_t>(shown), slower);
    for (const auto *cost : std::span(costs).first(shown)) {
        print_rounds(*cost);
    }
}
} // namespace

void print_compile_phases(const Analysis &analysis, const std::string_view subject) {
    for (const auto &[phase, elapsed] : analysis.times.phases()) {
        if (phase == "inference-cache") {
            // Added for the inference cache (58W).
            print_stage_time(
                phase, subject, elapsed,
                std::format("{} of {} covered modules reused", analysis.reused_modules_, analysis.covered_modules_));
            continue;
        }
        print_stage_time(phase, subject, elapsed);
        if (phase == "inference") {
            print_passes(analysis.profile, subject);
            print_churn(analysis, subject);
            print_clean_changed(analysis.profile);
            print_slowest(analysis.profile);
            print_recursive(analysis.profile);
        }
    }
}
} // namespace clause::cli
