#pragma once
#include <chrono>
#include <string_view>
#include <utility>

namespace clause::cli {
// Elapsed time of one stage, summed over the interleaved calls doing its work. Added for --print-stage-times.
class StageClock {
  public:
    // Run `work` and add its elapsed time to this stage, also when it throws.
    template <typename Work> decltype(auto) time(Work &&work) {
        const Lap lap{*this};
        return std::forward<Work>(work)();
    }

    // Time recorded so far.
    [[nodiscard]] std::chrono::steady_clock::duration elapsed() const { return elapsed_; }

  private:
    // Adds the time from its creation to its destruction to the clock.
    class Lap {
      public:
        explicit Lap(StageClock &clock) : clock_(clock) {}

        ~Lap() { clock_.elapsed_ += std::chrono::steady_clock::now() - start_; }

        Lap(const Lap &) = delete;
        Lap(Lap &&) = delete;
        Lap &operator=(const Lap &) = delete;
        Lap &operator=(Lap &&) = delete;

      private:
        // Clock receiving the lap, and when the lap began.
        StageClock &clock_;
        std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    };

    // Total of the finished laps.
    std::chrono::steady_clock::duration elapsed_{};
};

// Print "[time] <stage> <subject>: <milliseconds> ms" on stderr, followed by " (cached)" when the stage reused a
// cached result. Added for --print-stage-times.
void print_stage_time(std::string_view stage, std::string_view subject, std::chrono::steady_clock::duration elapsed,
                      bool cached = false);
} // namespace clause::cli
