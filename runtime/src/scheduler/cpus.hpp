#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Logical CPU sets of the host and scheduler worker binding; added for --scheduler-cpus.
namespace clause::runtime::detail {
// Highest logical CPU number a CPU list may name (Linux CPU_SETSIZE - 1).
inline constexpr std::uint32_t MAX_CPU = 1023;

// The logical CPUs the program may run on (its process affinity), ascending; never empty.
std::vector<std::uint32_t> available_cpus();
// The logical CPUs of the fastest core class (P-cores of a hybrid CPU), ascending; every CPU when all are alike.
std::vector<std::uint32_t> performance_cpus();
// Parse a CPU list such as `0-15` or `2,4,6` into ascending unique CPUs; nothing when malformed.
std::optional<std::vector<std::uint32_t>> parse_cpu_list(std::string_view text);
// Bind the calling worker to the `index`-th CPU of `cpus`, round-robin; nothing when `cpus` is empty. macOS has no
// thread affinity and raises the thread's QoS class instead.
void bind_worker(std::span<const std::uint32_t> cpus, std::size_t index) noexcept;
} // namespace clause::runtime::detail
