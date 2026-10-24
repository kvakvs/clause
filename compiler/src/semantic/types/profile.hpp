#pragma once
// Added for --print-stage-times: where whole-program inference spends its time, recorded only when requested.
#include "../calls.hpp"
#include "domain.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clause::semantic::types {
// How a local input slot changed between passes, from the first fact it got to term(). Added for the input churn
// measurements of --print-stage-times.
enum class Churn : std::uint8_t { first, integer, member, shape, deeper, function, term };
inline constexpr std::size_t CHURN_KINDS = 7;

struct InferenceProfile {
    using Duration = std::chrono::steady_clock::duration;

    // Whether inputs were gathered after the pass, the local inputs that changed, the fun F/A reads of the pass that
    // did not see their function's final facts, and the gathering's time. Added for the input churn measurements of
    // --print-stage-times.
    struct InputChurn {
        bool gathered_ = false;
        std::size_t functions_ = 0;
        std::size_t slots_ = 0;
        std::array<std::size_t, CHURN_KINDS> kinds_{};
        std::size_t open_fun_reads_ = 0;
        Duration elapsed_{};
    };

    // The graph after a pass and what could have kept the previous pass's results, noted before the pass's inputs
    // are gathered. Added for the incremental-potential measurements of --print-stage-times.
    struct Incremental {
        bool noted_ = false;
        // Graph nodes after the pass, and nodes created and widenings to term() since the previous note.
        std::size_t nodes_ = 0;
        std::size_t created_ = 0;
        std::size_t widenings_ = 0;
        // Components, those whose member inputs or read callee summaries differ from the previous pass's (some
        // differ in both), the clean rest, the clean ones whose summaries still changed, and the time and work spent
        // on the clean ones.
        std::size_t components_ = 0;
        std::size_t inputs_differ_ = 0;
        std::size_t callees_differ_ = 0;
        std::size_t clean_ = 0;
        std::size_t clean_changed_ = 0;
        Duration clean_elapsed_{};
        std::size_t clean_work_ = 0;
    };

    struct Pass {
        // The pass's time and work steps, how it ended, and the component running when the work budget ran out.
        Duration elapsed_{};
        std::size_t work_ = 0;
        std::string_view outcome_;
        const Component *exhausted_at_ = nullptr;
        // Added for the input churn measurements of --print-stage-times.
        InputChurn churn_;
        // The components the pass kept from their last walk, and the work steps those walks had cost. Added for
        // incremental passes (CONV.2).
        std::size_t skipped_ = 0;
        std::size_t skipped_work_ = 0;
        // The lattice joins the pass spent, and the costly components it gave term() local inputs. Added for
        // cost-aware give-up (CONV.4).
        std::size_t joins_ = 0;
        std::size_t costly_ = 0;
    };

    // What the previous note saw, to compare the next pass against: graph counters, every summary as an exact
    // fingerprint, the local inputs, each component's time and work so far, and each component's callees outside it
    // (built at the first note). Added for the incremental-potential measurements of --print-stage-times.
    struct PassMemory {
        std::size_t nodes_ = 0;
        std::size_t widenings_ = 0;
        std::map<const Function *, std::vector<std::size_t>> summaries_;
        std::map<const Function *, std::vector<Id>> inputs_;
        std::vector<Duration> elapsed_;
        std::vector<std::size_t> work_;
        std::vector<std::vector<const Function *>> callees_;
    };

    // One recursive component's solves against the previous pass's fixed point: solves that ended on it, solves
    // that reached it on the way and the rounds they ran until then (what a warm start would skip), and whether the
    // current solve reached it. Added for the solve repetition measurements of --print-stage-times.
    struct Repetition {
        std::size_t repeated_ = 0;
        std::size_t reached_ = 0;
        std::size_t skippable_ = 0;
        bool reaching_ = false;
    };

    // The facts of summaries and local inputs after a pass: how many, their largest and summed tree sizes (nodes
    // counted once per path, capped), the widest union, how many reach the depth, union-member and element budgets,
    // the summaries with the most function types, and the dependent facts. Added for the fact size measurements of
    // --print-stage-times.
    struct FactSizes {
        std::size_t facts_ = 0;
        std::size_t size_max_ = 0;
        std::size_t size_sum_ = 0;
        std::size_t union_max_ = 0;
        std::size_t at_depth_ = 0;
        std::size_t at_members_ = 0;
        std::size_t at_elements_ = 0;
        std::size_t full_types_ = 0;
        std::size_t dependents_ = 0;
    };

    // A component that was clean but whose summaries changed, and the pass it first happened in. Added for the
    // incremental-potential measurements of --print-stage-times.
    struct CleanChanged {
        const Component *component_ = nullptr;
        std::size_t pass_ = 0;
        std::size_t times_ = 0;
    };

    // One local input slot over the batch: its changes, the kind of the last one and its facts around it, printed
    // once inference ends. Added for the input churn measurements of --print-stage-times.
    struct Slot {
        std::size_t changes_ = 0;
        Churn last_ = Churn::first;
        Id before_{};
        Id after_{};
    };

    // A slot among the most changed, ready to print: `module:function/arity #argument` and its last change.
    struct ChurnedSlot {
        std::string name_;
        std::size_t changes_ = 0;
        Churn last_ = Churn::first;
        std::string before_;
        std::string after_;
    };

    struct Cost {
        // One component's time and work summed over every pass, how many passes ran it, and whether a budget
        // widened one of its facts to term().
        const Component *component_ = nullptr;
        Duration elapsed_{};
        std::size_t work_ = 0;
        std::size_t passes_ = 0;
        bool widened_ = false;

        // Recursive components only: how their fixed-point rounds went. Added for the recursive-component
        // measurements of --print-stage-times.
        struct Rounds {
            // Solves over all passes, rounds run, the per-solve round cap, solves that reached it or the
            // component's work share (every member widened to term()), solves that gave up at once because an
            // earlier pass did (58P), and solves that ended with the pass budget spent.
            std::size_t solves_ = 0;
            std::size_t rounds_ = 0;
            std::size_t round_cap_ = 0;
            std::size_t capped_ = 0;
            std::size_t over_share_ = 0;
            std::size_t remembered_ = 0;
            std::size_t exhausted_ = 0;
            // Member body walks and the walks that changed a member's summary: grown in place, function types
            // replaced because their inputs changed, or flipped back to an earlier round's summary (58Q).
            std::size_t walks_ = 0;
            std::size_t changes_ = 0;
            // Member walks a round skipped because nothing they read had changed (58R), and those whose last walk's
            // body a solve took again for the same reason (member-granular warm solves, CONV.8).
            std::size_t skipped_ = 0;
            std::size_t replayed_ = 0;
            std::size_t grown_ = 0;
            std::size_t replaced_ = 0;
            std::size_t flipped_ = 0;
            // Members changed in each round of the last solve.
            std::vector<std::size_t> last_changes_;
        } rounds_;
    };

    // The work budget of each pass, the passes in the order they ran, and the costs of the call graph's components,
    // indexed like them.
    std::size_t budget_ = 0;
    std::vector<Pass> passes_;
    std::vector<Cost> costs_;

    // The local input slots that changed, by function and argument, and how many local input slots there are. Added
    // for the input churn measurements of --print-stage-times.
    std::map<std::pair<const Function *, std::size_t>, Slot> slots_;
    std::size_t slots_total_ = 0;

    // Each pass's incremental potential, by pass (outside Pass: growing Pass changes the code the hot inference
    // walk's translation unit instantiates, which alone moved its speed by 2-3%). Added for the incremental-potential
    // measurements of --print-stage-times.
    std::vector<Incremental> incremental_;
    PassMemory memory_;
    std::vector<CleanChanged> clean_changed_;
    // By component index. Added for the solve repetition measurements of --print-stage-times.
    std::vector<Repetition> repetitions_;
    // By pass, for the passes noted. Added for the fact size measurements of --print-stage-times.
    std::vector<FactSizes> fact_sizes_;
};
} // namespace clause::semantic::types
