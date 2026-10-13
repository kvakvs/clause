// Added for the compile phases of --print-stage-times.
#include "compile_times.hpp"
#include <algorithm>
#include <clause/compiler/source.hpp>
#include <format>
#include <span>

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

// One line per inference pass: its time, work steps and how it ended.
void print_passes(const InferenceProfile &profile, const std::string_view subject) {
    for (std::size_t index = 0; index < profile.passes_.size(); ++index) {
        const auto &pass = profile.passes_[index];
        auto note = std::format("{} of {} work steps, {}", pass.work_, profile.budget_, pass.outcome_);
        if (pass.exhausted_at_) {
            note += ", work budget ran out in " + component_name(*pass.exhausted_at_);
        }
        print_stage_time(std::format("inference pass {}", index + 1), subject, pass.elapsed_, note);
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
        "of pass budget; {} member walks, {:.1f}% changed a summary ({} grown, {} replaced, {} flipped); last "
        "solve: {} rounds, members changed per round (in tenths) {}",
        rounds.solves_, rounds.rounds_, rounds.round_cap_, rounds.capped_, rounds.over_share_, rounds.remembered_,
        rounds.exhausted_, rounds.walks_, useful, rounds.grown_, rounds.replaced_, rounds.flipped_,
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
        print_stage_time(phase, subject, elapsed);
        if (phase == "inference") {
            print_passes(analysis.profile, subject);
            print_slowest(analysis.profile);
            print_recursive(analysis.profile);
        }
    }
}
} // namespace clause::cli
