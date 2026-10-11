#include "stage_times.hpp"
#include <format>
#include <iostream>

namespace clause::cli {
void print_stage_time(const std::string_view stage, const std::string_view subject,
                      const std::chrono::steady_clock::duration elapsed) {
    const std::chrono::duration<double, std::milli> milliseconds = elapsed;
    std::cerr << std::format("[time] {} {}: {:.3f} ms\n", stage, subject, milliseconds.count());
}
} // namespace clause::cli
