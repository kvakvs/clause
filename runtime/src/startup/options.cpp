#include "../memory/heap_collect.hpp"
#include "../scheduler/cpus.hpp"
#include "startup.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace clause::runtime::detail {
namespace {
// Environment variable whose words are parsed like the leading command-line arguments.
constexpr const char *FLAGS_VARIABLE = "CLAUSE_FLAGS";

// The value of an option and how many arguments carried it.
struct OptionValue {
    std::string_view value;
    std::size_t used;
};

// Read an environment variable through the platform CRT; an unset variable reads as empty.
std::string environment(const char *name) {
#ifdef _WIN32
    char *buffer = nullptr;
    std::size_t size = 0;
    const auto error = _dupenv_s(&buffer, &size, name);
    const std::unique_ptr<char, decltype(&std::free)> value(buffer, &std::free);
    return error == 0 && value ? std::string(value.get()) : std::string();
#else
    const auto *value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

// Split on spaces and tabs; there is no quoting.
std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> result;
    for (std::size_t at = 0; at < text.size();) {
        const auto begin = text.find_first_not_of(" \t", at);
        if (begin == std::string_view::npos) {
            break;
        }
        const auto end = std::min(text.find_first_of(" \t", begin), text.size());
        result.emplace_back(text.substr(begin, end - begin));
        at = end;
    }
    return result;
}

// Match `name=value` or `name value` at `at`; nothing when the argument is another one.
std::optional<OptionValue> match(std::span<const std::string> args, std::size_t at, std::string_view name) {
    const std::string_view argument = args[at];
    if (argument == name) {
        return at + 1 < args.size() ? OptionValue{args[at + 1], 2} : OptionValue{{}, 1};
    }
    if (argument.size() > name.size() && argument.starts_with(name) && argument[name.size()] == '=') {
        return OptionValue{argument.substr(name.size() + 1), 1};
    }
    return std::nullopt;
}

// Parse an atom table size: a decimal from 1 to AtomStorage::hard_limit.
std::expected<std::uint32_t, std::string> atom_limit(std::string_view text) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0 || value > AtomStorage::hard_limit) {
        return std::unexpected("invalid --max-atoms value '" + std::string(text) + "' (1 to " +
                               std::to_string(AtomStorage::hard_limit) + ")");
    }
    return static_cast<std::uint32_t>(value);
}

// Parse a scheduler count: a decimal from 1 to MAX_SCHEDULERS.
std::expected<std::size_t, std::string> scheduler_count(std::string_view text) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0 || value > MAX_SCHEDULERS) {
        return std::unexpected("invalid --schedulers value '" + std::string(text) + "' (1 to " +
                               std::to_string(MAX_SCHEDULERS) + ")");
    }
    return static_cast<std::size_t>(value);
}

// The scheduler count of a program without --schedulers: one per CPU of --scheduler-cpus, else one per CPU the
// program may run on, as OTP's online schedulers.
std::size_t default_schedulers(const std::vector<std::uint32_t> &scheduler_cpus) {
    const auto cpus = scheduler_cpus.empty() ? available_cpus().size() : scheduler_cpus.size();
    return std::clamp<std::size_t>(cpus, 1, MAX_SCHEDULERS);
}

// The CPUs of `cpus` the program may run on; added for --scheduler-cpus.
std::vector<std::uint32_t> available_of(const std::vector<std::uint32_t> &cpus) {
    const auto available = available_cpus();
    std::vector<std::uint32_t> result;
    std::ranges::set_intersection(cpus, available, std::back_inserter(result));
    return result;
}

// Parse a --scheduler-cpus CPU list, every CPU of which the program may run on; added for --scheduler-cpus.
std::expected<std::vector<std::uint32_t>, std::string> listed_cpus(std::string_view text) {
    const auto listed = parse_cpu_list(text);
    if (!listed) {
        return std::unexpected("invalid --scheduler-cpus value '" + std::string(text) +
                               "' (performance, all or CPUs such as 0-7,12)");
    }
    auto available = available_of(*listed);
    if (available.size() != listed->size()) {
        const auto missing = std::ranges::mismatch(*listed, available).in1;
        return std::unexpected("--scheduler-cpus: CPU " + std::to_string(*missing) +
                               " is not available to the program");
    }
    return available;
}

// Parse --scheduler-cpus (OTP +sbt/+sct): performance, all or a CPU list; added for --scheduler-cpus.
std::expected<std::vector<std::uint32_t>, std::string> scheduler_cpus(std::string_view text) {
    if (text == "all") {
        return available_cpus();
    }
    if (text != "performance") {
        return listed_cpus(text);
    }
    auto fastest = available_of(performance_cpus());
    if (fastest.empty()) {
        return std::unexpected(std::string("--scheduler-cpus: no performance CPU is available to the program"));
    }
    return fastest;
}

// Largest --min-heap-size in words, far beyond any host memory, so the size sequence never overflows.
constexpr std::uint64_t MAX_MIN_HEAP_WORDS = std::uint64_t{1} << 40;

// Parse --min-heap-size (ERTS +hms): words from 1, rounded up to a heap size as ERTS does (min_heap_size).
std::expected<std::size_t, std::string> min_heap_words(std::string_view text) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0 || value > MAX_MIN_HEAP_WORDS) {
        return std::unexpected("invalid --min-heap-size value '" + std::string(text) + "' (words, at least 1)");
    }
    return heap_size_at_least(static_cast<std::size_t>(value));
}

// A memory cap given in bytes: its option name, smallest accepted value and the setting it writes.
struct ByteOption {
    std::string_view name;
    std::size_t minimum;
    void (*set)(RuntimeOptions &options, std::size_t bytes);
};

// Memory caps (docs/executables.md#runtime-options); values round down to whole words.
constexpr std::array BYTE_OPTIONS{
    ByteOption{"--max-heap", HeapOptions{}.min_heap_words * sizeof(Word),
               [](RuntimeOptions &options, std::size_t bytes) { options.process_heap.limit_bytes = bytes; }},
    ByteOption{
        "--max-stack", sizeof(Word),
        [](RuntimeOptions &options, std::size_t bytes) { options.process_stack.limit_words = bytes / sizeof(Word); }},
    ByteOption{"--max-memory", sizeof(Word),
               [](RuntimeOptions &options, std::size_t bytes) { options.memory_limit_bytes = bytes; }},
};

// Parse a byte count of a memory cap: a decimal from the option's minimum to UNLIMITED_HEAP_BYTES, in whole words.
std::expected<std::size_t, std::string> byte_count(const ByteOption &option, std::string_view text) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < option.minimum ||
        value > UNLIMITED_HEAP_BYTES) {
        return std::unexpected("invalid " + std::string(option.name) + " value '" + std::string(text) +
                               "' (bytes, at least " + std::to_string(option.minimum) + ")");
    }
    return static_cast<std::size_t>(value) / sizeof(Word) * sizeof(Word);
}

// Apply a memory size option at `at` (the minimum heap or a cap) and return how many arguments it used; 0 when it is
// not one.
std::expected<std::size_t, std::string> apply_sizes(std::span<const std::string> args, std::size_t at,
                                                    RuntimeOptions &options) {
    if (const auto heap = match(args, at, "--min-heap-size")) {
        const auto words = min_heap_words(heap->value);
        if (!words) {
            return std::unexpected(words.error());
        }
        options.process_heap.min_heap_words = *words;
        return heap->used;
    }
    for (const auto &option : BYTE_OPTIONS) {
        if (const auto found = match(args, at, option.name)) {
            const auto bytes = byte_count(option, found->value);
            if (!bytes) {
                return std::unexpected(bytes.error());
            }
            option.set(options, *bytes);
            return found->used;
        }
    }
    return 0;
}

// Apply a scheduler option at `at` and return how many arguments it used; 0 when it is none, then the size options
// are tried.
std::expected<std::size_t, std::string> apply_scheduling(std::span<const std::string> args, std::size_t at,
                                                         RuntimeOptions &options) {
    if (const auto schedulers = match(args, at, "--schedulers")) {
        const auto count = scheduler_count(schedulers->value);
        if (!count) {
            return std::unexpected(count.error());
        }
        options.schedulers = *count;
        return schedulers->used;
    }
    if (const auto cpus = match(args, at, "--scheduler-cpus")) {
        auto chosen = scheduler_cpus(cpus->value);
        if (!chosen) {
            return std::unexpected(chosen.error());
        }
        options.scheduler_cpus = std::move(*chosen);
        return cpus->used;
    }
    return apply_sizes(args, at, options);
}

// Apply the option at `at` and return how many arguments it used; 0 when it is not a runtime option.
std::expected<std::size_t, std::string> apply(std::span<const std::string> args, std::size_t at,
                                              RuntimeOptions &options) {
    if (const auto atoms = match(args, at, "--max-atoms")) {
        const auto limit = atom_limit(atoms->value);
        if (!limit) {
            return std::unexpected(limit.error());
        }
        options.max_atoms = *limit;
        return atoms->used;
    }
    if (const auto profile = match(args, at, "--profile")) {
        if (profile->value.empty()) {
            return std::unexpected(std::string("--profile needs the file the report is written to"));
        }
        options.profile = profile->value;
        return profile->used;
    }
    // Entry point for a vm.args-like options file; reading it is not implemented yet.
    if (match(args, at, "--args-file")) {
        return std::unexpected(std::string("runtime option --args-file is not implemented"));
    }
    return apply_scheduling(args, at, options);
}

// Apply leading runtime options until another argument; `--` ends them and is used too.
std::expected<std::size_t, std::string> apply_all(std::span<const std::string> args, RuntimeOptions &options) {
    std::size_t at = 0;
    while (at < args.size() && args[at] != "--") {
        const auto used = apply(args, at, options);
        if (!used) {
            return std::unexpected(used.error());
        }
        if (*used == 0) {
            return at;
        }
        at += *used;
    }
    return at < args.size() ? at + 1 : at;
}
} // namespace

std::expected<ProgramOptions, std::string> program_options(int argc, char **argv) {
    ProgramOptions result;
    // 0 until --schedulers sets it; the default depends on --scheduler-cpus.
    result.runtime.schedulers = 0;
    const auto flags = words(environment(FLAGS_VARIABLE));
    const auto from_environment = apply_all(flags, result.runtime);
    if (!from_environment) {
        return std::unexpected(from_environment.error());
    }
    if (*from_environment != flags.size()) {
        return std::unexpected(std::string(FLAGS_VARIABLE) + ": not a runtime option: " + flags[*from_environment]);
    }
    std::vector<std::string> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int i = 1; i < argc && argv; ++i) {
        arguments.emplace_back(argv[i] ? argv[i] : "");
    }
    const auto used = apply_all(arguments, result.runtime);
    if (!used) {
        return std::unexpected(used.error());
    }
    result.consumed = *used;
    if (result.runtime.schedulers == 0) {
        result.runtime.schedulers = default_schedulers(result.runtime.scheduler_cpus);
    }
    // A process heap must fit its budget (valid_heap_options).
    const auto &heap = result.runtime.process_heap;
    if (heap.min_heap_words > heap.limit_bytes / sizeof(Word)) {
        return std::unexpected(std::string("--min-heap-size exceeds --max-heap"));
    }
    return result;
}
} // namespace clause::runtime::detail
