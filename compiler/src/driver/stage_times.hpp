#pragma once
#include <chrono>
#include <string_view>
#include <utility>
#include <vector>

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

// The named phases of one batch's compile in the order they ran, timed only when enabled. Added for the compile
// phases of --print-stage-times.
class PhaseTimes {
  public:
    using Phase = std::pair<std::string_view, std::chrono::steady_clock::duration>;

    // Times one phase from its creation to its destruction; does nothing when timing is off.
    class Lap {
      public:
        Lap(PhaseTimes *times, std::string_view phase) : times_(times), phase_(phase) {}

        ~Lap() {
            if (times_) {
                times_->phases_.emplace_back(phase_, std::chrono::steady_clock::now() - start_);
            }
        }

        Lap(const Lap &) = delete;
        Lap(Lap &&) = delete;
        Lap &operator=(const Lap &) = delete;
        Lap &operator=(Lap &&) = delete;

      private:
        // The phases receiving the time (none when off), the phase's name, and when it began.
        PhaseTimes *times_;
        std::string_view phase_;
        std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    };

    PhaseTimes() = default;

    explicit PhaseTimes(const bool enabled) : enabled_(enabled) {}

    // Time `phase` until the returned lap is destroyed.
    [[nodiscard]] Lap lap(const std::string_view phase) { return {enabled_ ? this : nullptr, phase}; }

    [[nodiscard]] bool enabled() const { return enabled_; }

    [[nodiscard]] const std::vector<Phase> &phases() const { return phases_; }

  private:
    // Whether phases are timed, and the phases timed so far.
    bool enabled_ = false;
    std::vector<Phase> phases_;
};

// Print "[time] <stage> <subject>: <milliseconds> ms" on stderr, followed by " (<note>)" when there is a note, such
// as "cached" for a stage that reused a cached result. Added for --print-stage-times.
void print_stage_time(std::string_view stage, std::string_view subject, std::chrono::steady_clock::duration elapsed,
                      std::string_view note = {});
} // namespace clause::cli
