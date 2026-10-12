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
    };

    // The work budget of each pass, the passes in the order they ran, and the costs of the call graph's components,
    // indexed like them.
    std::size_t budget_ = 0;
    std::vector<Pass> passes_;
    std::vector<Cost> costs_;
};
} // namespace clause::semantic::types
