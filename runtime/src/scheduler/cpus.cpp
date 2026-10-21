#include "cpus.hpp"
#include <algorithm>
#include <charconv>
#include <numeric>
#include <thread>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <sys/sysctl.h>
#else
#include <fstream>
#include <pthread.h>
#include <sched.h>
#include <string>
#endif

namespace clause::runtime::detail {
namespace {
// CPUs 0 to count - 1: the set of a host that does not report one.
std::vector<std::uint32_t> first_cpus(std::size_t count) {
    std::vector<std::uint32_t> cpus(std::max<std::size_t>(count, 1));
    std::iota(cpus.begin(), cpus.end(), std::uint32_t{0});
    return cpus;
}

// One decimal CPU number of a CPU list; nothing when malformed or above MAX_CPU.
std::optional<std::uint32_t> cpu_number(std::string_view text) {
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value > MAX_CPU) {
        return std::nullopt;
    }
    return value;
}

// Append the CPUs of one list item, `N` or `A-B` with A <= B; false when malformed.
bool append_range(std::string_view item, std::vector<std::uint32_t> &cpus) {
    const auto dash = item.find('-');
    const auto first = cpu_number(item.substr(0, dash));
    const auto last = dash == std::string_view::npos ? first : cpu_number(item.substr(dash + 1));
    if (!first || !last || *first > *last) {
        return false;
    }
    for (auto cpu = *first; cpu <= *last; ++cpu) {
        cpus.push_back(cpu);
    }
    return true;
}

#if defined(_WIN32)
// Bits per processor group mask; Clause uses the CPUs of the process's processor group only.
constexpr std::uint32_t GROUP_CPUS = 64;

// The CPUs of a processor group affinity mask, ascending.
std::vector<std::uint32_t> mask_cpus(std::uint64_t mask) {
    std::vector<std::uint32_t> cpus;
    for (std::uint32_t cpu = 0; cpu < GROUP_CPUS; ++cpu) {
        if (((mask >> cpu) & 1U) != 0) {
            cpus.push_back(cpu);
        }
    }
    return cpus;
}

// The CPU mask of group 0's cores of the highest EfficiencyClass (the fastest); 0 when none is listed.
std::uint64_t fastest_cores_mask(const std::byte *records, DWORD bytes) {
    std::uint64_t mask = 0;
    BYTE fastest = 0;
    for (DWORD at = 0; at < bytes;) {
        const auto *core = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(records + at);
        const auto &processor = core->Processor;
        if (processor.GroupMask[0].Group == 0 && processor.EfficiencyClass >= fastest) {
            mask = processor.EfficiencyClass > fastest ? 0 : mask;
            fastest = processor.EfficiencyClass;
            mask |= processor.GroupMask[0].Mask;
        }
        at += core->Size;
    }
    return mask;
}

// Bind the calling thread to one CPU of the process's processor group.
void pin_thread(std::uint32_t cpu) noexcept {
    if (cpu < GROUP_CPUS) {
        SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR{1} << cpu);
    }
}
#elif defined(__APPLE__)
// macOS has no thread affinity: a bound worker asks for the QoS class the P-cores serve first.
void pin_thread(std::uint32_t /*cpu*/) noexcept { pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0); }
#else
// The first line of a sysfs file; empty when it cannot be read.
std::string first_line(const std::string &path) {
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    return line;
}

// The CPUs of `cpus` with the highest cpu_capacity (big cores of ARM big.LITTLE); all of them when none reports it.
std::vector<std::uint32_t> largest_capacity_cpus(const std::vector<std::uint32_t> &cpus) {
    std::vector<std::uint32_t> largest;
    long best = -1;
    for (const auto cpu : cpus) {
        const auto text = first_line("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpu_capacity");
        long capacity = -1;
        std::from_chars(text.data(), text.data() + text.size(), capacity);
        if (capacity > best) {
            best = capacity;
            largest.clear();
        }
        if (capacity == best) {
            largest.push_back(cpu);
        }
    }
    return largest;
}

// Bind the calling thread to one CPU.
void pin_thread(std::uint32_t cpu) noexcept {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}
#endif
} // namespace

#if defined(_WIN32)
std::vector<std::uint32_t> available_cpus() {
    DWORD_PTR process = 0;
    DWORD_PTR system = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process, &system) == 0 || process == 0) {
        return first_cpus(std::thread::hardware_concurrency());
    }
    return mask_cpus(process);
}

std::vector<std::uint32_t> performance_cpus() {
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    // Whole words keep the variable-size records aligned.
    std::vector<std::uint64_t> records((bytes + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t));
    auto *first = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(records.data());
    if (bytes == 0 || GetLogicalProcessorInformationEx(RelationProcessorCore, first, &bytes) == 0) {
        return available_cpus();
    }
    const auto mask = fastest_cores_mask(reinterpret_cast<const std::byte *>(records.data()), bytes);
    return mask == 0 ? available_cpus() : mask_cpus(mask);
}
#elif defined(__APPLE__)
std::vector<std::uint32_t> available_cpus() { return first_cpus(std::thread::hardware_concurrency()); }

// The count of the first performance level (P-cores) numbered from 0; macOS CPU numbers only count CPUs.
std::vector<std::uint32_t> performance_cpus() {
    int count = 0;
    std::size_t size = sizeof(count);
    if (sysctlbyname("hw.perflevel0.logicalcpu", &count, &size, nullptr, 0) != 0 || count <= 0) {
        return available_cpus();
    }
    return first_cpus(std::min<std::size_t>(static_cast<std::size_t>(count), std::thread::hardware_concurrency()));
}
#else
std::vector<std::uint32_t> available_cpus() {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) != 0) {
        return first_cpus(std::thread::hardware_concurrency());
    }
    std::vector<std::uint32_t> cpus;
    for (std::uint32_t cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &set)) {
            cpus.push_back(cpu);
        }
    }
    return cpus.empty() ? first_cpus(std::thread::hardware_concurrency()) : cpus;
}

// Intel hybrid CPUs list their P-cores in the cpu_core PMU; other hosts are ranked by cpu_capacity.
std::vector<std::uint32_t> performance_cpus() {
    if (const auto listed = parse_cpu_list(first_line("/sys/devices/cpu_core/cpus"))) {
        return *listed;
    }
    return largest_capacity_cpus(available_cpus());
}
#endif

std::optional<std::vector<std::uint32_t>> parse_cpu_list(std::string_view text) {
    std::vector<std::uint32_t> cpus;
    for (std::size_t at = 0; at <= text.size();) {
        const auto end = std::min(text.find(',', at), text.size());
        if (!append_range(text.substr(at, end - at), cpus)) {
            return std::nullopt;
        }
        at = end + 1;
    }
    std::ranges::sort(cpus);
    const auto duplicates = std::ranges::unique(cpus);
    cpus.erase(duplicates.begin(), duplicates.end());
    return cpus;
}

void bind_worker(std::span<const std::uint32_t> cpus, std::size_t index) noexcept {
    if (!cpus.empty()) {
        pin_thread(cpus[index % cpus.size()]);
    }
}
} // namespace clause::runtime::detail
