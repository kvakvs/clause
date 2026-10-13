#pragma once
// Added for --print-stage-times: where whole-program inference spends its time, recorded only when requested.
#include "../calls.hpp"
#include <chrono>
#include <string_view>
#include <vector>

namespace clause::semantic::types {
struct InferenceProfile {
    using Duration = std::chrono::steady_clock::duration;

    struct Pass {
        // The pass's time and work steps, how it ended, and the component running when the work budget ran out.
        Duration elapsed_{};
        std::size_t work_ = 0;
        std::string_view outcome_;
        const Component *exhausted_at_ = nullptr;
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
            // Member walks a round skipped because nothing they read had changed (58R).
            std::size_t skipped_ = 0;
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
};
} // namespace clause::semantic::types
