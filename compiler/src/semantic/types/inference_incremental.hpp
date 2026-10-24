#pragma once
#include "inference.hpp"

// Incremental passes (docs/semantic.md#inference): a component whose members' inputs and every summary and input it
// read are the same as at its last walk would walk the same way again, so a pass keeps its summaries and expression
// facts instead. Added for incremental passes (CONV.2).
namespace clause::semantic::types {
// Whether nothing a component read changed since `last`, its last walk.
bool unchanged(const Inference &inference, const ComponentWalk &last);
// Keep a skipped walk's effects on the pass: its work, its re-analysis work and its fun F/A facts.
void replay(Inference &inference, const ComponentWalk &last, std::size_t &work);

// Tracks one component's walk from its start to `finish`: what it reads, and its members' summaries before it.
class WalkTracker {
  public:
    WalkTracker(Inference &inference, const Component &component, std::size_t work);
    WalkTracker(const WalkTracker &) = delete;
    WalkTracker &operator=(const WalkTracker &) = delete;
    WalkTracker(WalkTracker &&) = delete;
    WalkTracker &operator=(WalkTracker &&) = delete;
    ~WalkTracker();

    // Stop tracking: bump the versions of members whose summaries changed and describe the walk for later passes.
    ComponentWalk finish(std::size_t work);

  private:
    // Bump the versions of the members whose summaries the walk changed.
    void bump_versions();
    // The summaries and inputs the walk read, with the versions it read.
    std::vector<std::pair<const Function *, std::size_t>> summary_reads() const;
    std::vector<std::pair<const Function *, std::size_t>> input_reads() const;

    Inference &inference_;
    const Component &component_;
    // The reads of the walk, and the work, re-analysis work and refusals before it.
    WalkReads reads_;
    std::size_t work_;
    std::size_t reanalysis_work_;
    std::size_t checks_;
    std::size_t refusals_;
    // The members' summaries and versions before the walk, to tell which changed.
    std::vector<std::optional<Summary>> before_;
    std::vector<std::size_t> versions_;
    bool finished_ = false;
};
} // namespace clause::semantic::types
