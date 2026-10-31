#include "../memory/heap_collect.hpp"
#include "../scheduler/cpus.hpp"
#include "args_file.hpp"
#include "startup.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
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

// Parse --epmd (OTP's -start_epmd): bundled, external or none. Added for the bundled epmd; external since plan 30
// step DIST3d.
std::expected<EpmdMode, std::string> epmd_mode(std::string_view text) {
    if (text == "bundled") {
        return EpmdMode::bundled;
    }
    if (text == "none") {
        return EpmdMode::none;
    }
    if (text == "external") {
        return EpmdMode::external;
    }
    return std::unexpected("invalid --epmd value '" + std::string(text) + "' (bundled, external or none)");
}

// Keep init's flag `name` with `values`, replacing an earlier one of that name: the later option wins. Added for node
// start options.
void replace_flag(RuntimeOptions &options, std::string_view name, std::vector<std::string> values) {
    auto &flags = options.program.flags;
    std::erase_if(flags, [&](const ProgramFlag &flag) { return flag.name == name; });
    flags.push_back({std::string(name), std::move(values)});
}

// A node start option with a value and the init flag reporting it, as OTP spells it (docs/distribution.md).
struct NodeOption {
    std::string_view name;
    std::string_view flag;
};

// Node start options with a value; their values are checked by distribution when it starts (DIST4b).
constexpr std::array NODE_OPTIONS{
    NodeOption{"--sname", "sname"},
    NodeOption{"--name", "name"},
    NodeOption{"--setcookie", "setcookie"},
    NodeOption{"--proto-dist", "proto_dist"},
    NodeOption{"--dist-listen", "dist_listen"},
};

// Node start options without a value.
constexpr std::array NODE_SWITCHES{
    NodeOption{"--nocookie", "nocookie"},
    NodeOption{"--hidden", "hidden"},
};

// Check the value of a node start option: present, and true or false for --dist-listen.
std::expected<void, std::string> node_value(const NodeOption &option, std::string_view value) {
    if (value.empty()) {
        return std::unexpected(std::string(option.name) + " needs a value");
    }
    if (option.flag == "dist_listen" && value != "true" && value != "false") {
        return std::unexpected("invalid --dist-listen value '" + std::string(value) + "' (true or false)");
    }
    return {};
}

// Apply a node start option at `at` as its init flag and return how many arguments it used; 0 when it is none.
// Added for node start options.
std::expected<std::size_t, std::string> apply_node_option(std::span<const std::string> args, std::size_t at,
                                                          RuntimeOptions &options) {
    for (const auto &option : NODE_SWITCHES) {
        if (args[at] == option.name) {
            replace_flag(options, option.flag, {});
            return 1;
        }
    }
    for (const auto &option : NODE_OPTIONS) {
        if (const auto found = match(args, at, option.name)) {
            if (const auto valid = node_value(option, found->value); !valid) {
                return std::unexpected(valid.error());
            }
            replace_flag(options, option.flag, {std::string(found->value)});
            return found->used;
        }
    }
    return 0;
}

// Apply --env APP KEY VALUE (OTP's -App Key Value) as init's flag APP with [KEY, VALUE]; every one is kept, and
// application_controller parses the values (docs/distribution.md#node-start-options). Added for --env.
std::expected<std::size_t, std::string> apply_env(std::span<const std::string> args, std::size_t at,
                                                  RuntimeOptions &options) {
    if (args.size() - at < 4 || args[at + 1].empty() || args[at + 2].empty()) {
        return std::unexpected(std::string("--env needs an application, a key and a value"));
    }
    options.program.flags.push_back({args[at + 1], {args[at + 2], args[at + 3]}});
    return 4;
}

// Apply an application environment option at `at`, --env or --config, and return how many arguments it used; else
// the node start options and then the scheduler options are tried. Added for --env and -config (DIST4a1, DIST4a4).
std::expected<std::size_t, std::string> apply_application_option(std::span<const std::string> args, std::size_t at,
                                                                 RuntimeOptions &options) {
    if (args[at] == "--env") {
        return apply_env(args, at, options);
    }
    // OTP's -config: every one is kept, and application_controller reads the files.
    if (const auto config = match(args, at, "--config")) {
        if (config->value.empty()) {
            return std::unexpected(std::string("--config needs a file"));
        }
        options.program.flags.push_back({"config", {std::string(config->value)}});
        return config->used;
    }
    const auto node = apply_node_option(args, at, options);
    if (!node || *node != 0) {
        return node;
    }
    return apply_scheduling(args, at, options);
}

// Apply a distribution option at `at` and return how many arguments it used; 0 when it is none, then the application
// environment options are tried. Added for the bundled epmd.
std::expected<std::size_t, std::string> apply_distribution(std::span<const std::string> args, std::size_t at,
                                                           RuntimeOptions &options) {
    if (const auto epmd = match(args, at, "--epmd")) {
        const auto mode = epmd_mode(epmd->value);
        if (!mode) {
            return std::unexpected(mode.error());
        }
        options.epmd = *mode;
        // OTP's -start_epmd: false only when no epmd may start. Added for init's flags (DIST4a1).
        replace_flag(options, "start_epmd", {*mode == EpmdMode::none ? "false" : "true"});
        return epmd->used;
    }
    return apply_application_option(args, at, options);
}

// Most args files nested in one another; OTP sets no limit, so a file naming itself never ends there.
constexpr std::size_t MAX_ARGS_FILE_DEPTH = 32;

std::expected<std::size_t, std::string> apply(std::span<const std::string> args, std::size_t at,
                                              RuntimeOptions &options);

// Apply Clause option words an OTP flag stands for; every one must be an option. Added for args files.
std::expected<void, std::string> apply_words(std::span<const std::string> words, RuntimeOptions &options) {
    for (std::size_t at = 0; at < words.size();) {
        const auto used = apply(words, at, options);
        if (!used || *used == 0) {
            return std::unexpected(used ? "not a runtime option: " + words[at] : used.error());
        }
        at += *used;
    }
    return {};
}

// Apply the OTP flag at `at` of an args file and return how many words it used; ignored words join `ignored`.
// Added for args files.
std::expected<std::size_t, std::string> apply_otp_flag(std::span<const std::string> words, std::size_t at,
                                                       RuntimeOptions &options, std::vector<std::string> &ignored) {
    auto flag = otp_flag(words, at);
    if (!flag) {
        return std::unexpected(flag.error());
    }
    if (const auto applied = apply_words(flag->options, options); !applied) {
        return std::unexpected(applied.error());
    }
    if (flag->variable) {
        set_environment(flag->variable->first, flag->variable->second);
    }
    if (flag->ignored) {
        ignored.insert(ignored.end(), words.begin() + static_cast<std::ptrdiff_t>(at),
                       words.begin() + static_cast<std::ptrdiff_t>(at + flag->used));
    }
    std::ranges::move(flag->flags, std::back_inserter(options.program.flags));
    return flag->used;
}

std::expected<void, std::string> apply_args_file(const std::string &path, RuntimeOptions &options, std::size_t depth);

// An args file being applied: its path, how deep it is nested and the ignored words of its warning line.
struct ArgsFile {
    const std::string &path;
    std::size_t depth;
    std::vector<std::string> ignored;
};

// Apply a Clause runtime option or an OTP flag at `at` of an args file; how many words it used.
std::expected<std::size_t, std::string> apply_own_word(std::span<const std::string> words, std::size_t at,
                                                       RuntimeOptions &options, ArgsFile &file) {
    if (!words[at].starts_with("--")) {
        return apply_otp_flag(words, at, options, file.ignored);
    }
    const auto used = apply(words, at, options);
    if (used && *used == 0) {
        return std::unexpected("not a runtime option: " + words[at]);
    }
    return used;
}

// Apply the word at `at` of an args file: a nested file, a Clause runtime option or an OTP flag; how many words it
// used. Errors of the file's own words name it. Added for args files.
std::expected<std::size_t, std::string> apply_file_word(std::span<const std::string> words, std::size_t at,
                                                        RuntimeOptions &options, ArgsFile &file) {
    const auto &word = words[at];
    const bool nests = word == "-args_file" || word == "--args-file";
    if (nests && at + 1 < words.size()) {
        const auto nested = apply_args_file(words[at + 1], options, file.depth + 1);
        return nested ? std::expected<std::size_t, std::string>(2) : std::unexpected(nested.error());
    }
    const auto used = nests ? std::expected<std::size_t, std::string>(std::unexpected(word + " needs a file"))
                            : apply_own_word(words, at, options, file);
    return used ? used : std::unexpected("--args-file " + file.path + ": " + used.error());
}

// Apply an args file's words as OTP's erlexec does (docs/executables.md#args-files): the words after -extra are
// plain arguments, and one warning line names the ignored flags. Added for args files.
std::expected<void, std::string> apply_args_file(const std::string &path, RuntimeOptions &options, std::size_t depth) {
    if (depth == MAX_ARGS_FILE_DEPTH) {
        return std::unexpected("args files nested too deeply at '" + path + "'");
    }
    const auto words = args_file_words(path);
    if (!words) {
        return std::unexpected(words.error());
    }
    ArgsFile file{path, depth, {}};
    for (std::size_t at = 0; at < words->size();) {
        if ((*words)[at] == "-extra") {
            options.program.plain.insert(options.program.plain.end(),
                                         words->begin() + static_cast<std::ptrdiff_t>(at) + 1, words->end());
            break;
        }
        const auto used = apply_file_word(*words, at, options, file);
        if (!used) {
            return std::unexpected(used.error());
        }
        at += *used;
    }
    if (!file.ignored.empty()) {
        std::string text;
        for (const auto &word : file.ignored) {
            text += " " + word;
        }
        static_cast<void>(
            std::fprintf(stderr, "clau: warning: --args-file %s: ignored:%s\n", path.c_str(), text.c_str()));
    }
    return {};
}

// Apply --args-file at `at`, a release's vm.args whose words stand in place of the option
// (docs/executables.md#args-files), and return how many arguments it used; else the distribution options are tried.
// Added for args files.
std::expected<std::size_t, std::string> apply_args_option(std::span<const std::string> args, std::size_t at,
                                                          RuntimeOptions &options) {
    const auto file = match(args, at, "--args-file");
    if (!file) {
        return apply_distribution(args, at, options);
    }
    if (file->value.empty()) {
        return std::unexpected(std::string("--args-file needs a file"));
    }
    const auto applied = apply_args_file(std::string(file->value), options, 0);
    return applied ? std::expected<std::size_t, std::string>(file->used) : std::unexpected(applied.error());
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
    return apply_args_option(args, at, options);
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

// Apply words that must all be runtime options (the built-in defaults, CLAUSE_FLAGS); an error names their `source`.
std::expected<void, std::string> apply_every(std::span<const std::string> words, std::string_view source,
                                             RuntimeOptions &options) {
    const auto used = apply_all(words, options);
    if (!used) {
        return std::unexpected(used.error());
    }
    if (*used != words.size()) {
        return std::unexpected(std::string(source) + ": not a runtime option: " + words[*used]);
    }
    return {};
}
} // namespace

std::expected<ProgramOptions, std::string> program_options(std::span<const char *const> defaults, int argc,
                                                           char **argv) {
    ProgramOptions result;
    // 0 until --schedulers sets it; the default depends on --scheduler-cpus.
    result.runtime.schedulers = 0;
    const std::vector<std::string> built_in(defaults.begin(), defaults.end());
    if (const auto applied = apply_every(built_in, "runtime_options", result.runtime); !applied) {
        return std::unexpected(applied.error());
    }
    if (const auto applied = apply_every(words(environment(FLAGS_VARIABLE)), FLAGS_VARIABLE, result.runtime);
        !applied) {
        return std::unexpected(applied.error());
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
