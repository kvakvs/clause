#include "system_flags.hpp"
#include "../../scheduler/executor.hpp"
#include "../typed.hpp"
#include <array>
#include <clause/runtime/callable.hpp>
#include <mutex>

// system_flag/2 (plan 21 step LIB9A, docs/builtins.md#system-information): backtrace_depth bounds stack traces and
// min_heap_size the heap of new processes; the other documented flags that can apply to a program are accepted and
// read back, without an effect Clause has no machinery for (docs/differences.md).
namespace clause::runtime::builtins {
namespace {
// The roles of the arguments (typed.hpp's Named).
struct FlagRole;
struct ValueRole;
using FlagArg = Named<FlagRole>;
using ValueArg = Named<ValueRole>;

// The settings Clause only keeps for system_flag/2 to return, for the whole program.
struct Settings {
    std::int64_t fullsweep_after = 65535;
    std::int64_t min_bin_vheap_size = 46422;
    std::int64_t max_heap_words = 0;
    bool max_heap_kill = true;
    bool max_heap_error_logger = true;
    bool max_heap_shared_binaries = false;
    bool microstate_accounting = false;
    bool scheduler_wall_time = false;
    std::int64_t trace_control_word = 0;
    std::optional<std::int64_t> outstanding_requests;
    std::optional<std::int64_t> schedulers_online;
    std::optional<std::int64_t> dirty_schedulers_online;
    bool multi_scheduling_blocked = false;
    Word system_logger = 0;
};

std::mutex settings_mutex;
Settings settings;

Term integer(ProcessContext &context, std::int64_t value) { return need(TermFactory(context).integer(value)); }

// A non-negative small integer argument; badarg otherwise.
std::int64_t count_of(const ValueArg &value, std::int64_t limit = std::numeric_limits<std::int64_t>::max()) {
    const auto number = small(value.term.word());
    if (!number || *number < 0 || *number > limit) {
        bad_argument();
    }
    return *number;
}

bool boolean_of(const ValueArg &value) {
    if (!value.term.is_boolean()) {
        bad_argument();
    }
    return value.term.boolean_value_unchecked();
}

// Replace an integer setting, returning its previous value.
Term exchange(ProcessContext &context, std::int64_t &setting, std::int64_t value) {
    const std::scoped_lock lock(settings_mutex);
    return integer(context, std::exchange(setting, value));
}

Term exchange_flag(ProcessContext &context, bool &setting, bool value) {
    const std::scoped_lock lock(settings_mutex);
    return need(TermFactory(context).boolean(std::exchange(setting, value)));
}

Term backtrace_depth(ProcessContext &context, const ValueArg &value) {
    return integer(context, static_cast<std::int64_t>(set_backtrace_depth(static_cast<std::size_t>(count_of(value)))));
}

Term min_heap_size(ProcessContext &context, const ValueArg &value) {
    const auto words = static_cast<std::size_t>(count_of(value));
    return integer(context, static_cast<std::int64_t>(detail::Executor::set_min_heap_words(context, words)));
}

Term fullsweep_after(ProcessContext &context, const ValueArg &value) {
    return exchange(context, settings.fullsweep_after, count_of(value));
}

Term min_bin_vheap_size(ProcessContext &context, const ValueArg &value) {
    return exchange(context, settings.min_bin_vheap_size, count_of(value));
}

// #{size, kill, error_logger, include_shared_binaries} of the max_heap_size setting.
Term max_heap_map(ProcessContext &context, const Settings &kept) {
    const std::array entries{
        std::pair{atom(context, "error_logger"_atom), need(TermFactory(context).boolean(kept.max_heap_error_logger))},
        std::pair{atom(context, "include_shared_binaries"_atom),
                  need(TermFactory(context).boolean(kept.max_heap_shared_binaries))},
        std::pair{atom(context, "kill"_atom), need(TermFactory(context).boolean(kept.max_heap_kill))},
        std::pair{atom(context, "size"_atom), integer(context, kept.max_heap_words)}};
    return need(TermFactory(context).map(entries));
}

// One key of a max_heap_size map applied to `kept`; badarg for an unknown key or a bad value.
void max_heap_entry(Settings &kept, const std::pair<Term, Term> &entry) {
    const auto &[key, value] = entry;
    const auto name = key.is_atom() ? key.atom_spelling_unchecked() : std::string_view{};
    if (name == "size") {
        kept.max_heap_words = count_of({value});
    } else if (name == "kill" || name == "error_logger" || name == "include_shared_binaries") {
        auto &flag = name == "kill"           ? kept.max_heap_kill
                     : name == "error_logger" ? kept.max_heap_error_logger
                                              : kept.max_heap_shared_binaries;
        flag = boolean_of({value});
    } else {
        bad_argument();
    }
}

// max_heap_size: a size or a map of the settings; the previous settings as a map.
Term max_heap_size(ProcessContext &context, const ValueArg &value) {
    const std::scoped_lock lock(settings_mutex);
    auto kept = settings;
    if (value.term.is_map()) {
        for (const auto &entry : need(value.term.map_entries())) {
            max_heap_entry(kept, entry);
        }
    } else {
        kept.max_heap_words = count_of(value);
    }
    const auto previous = max_heap_map(context, settings);
    settings = kept;
    return previous;
}

Term microstate_accounting(ProcessContext &context, const ValueArg &value) {
    const bool reset = value.term.is_atom() && value.term.atom_spelling_unchecked() == "reset";
    if (reset) {
        const std::scoped_lock lock(settings_mutex);
        return need(TermFactory(context).boolean(settings.microstate_accounting));
    }
    return exchange_flag(context, settings.microstate_accounting, boolean_of(value));
}

Term scheduler_wall_time(ProcessContext &context, const ValueArg &value) {
    return exchange_flag(context, settings.scheduler_wall_time, boolean_of(value));
}

Term trace_control_word(ProcessContext &context, const ValueArg &value) {
    return exchange(context, settings.trace_control_word, count_of(value, 0xFFFFFFFFLL));
}

std::int64_t workers(ProcessContext &context) {
    return static_cast<std::int64_t>(detail::Executor::counts(context).workers);
}

// A count setting's new value and the default it has until first set.
struct CountChange {
    std::int64_t value;
    std::int64_t fallback;
};

// A setting with a default computed from the workers, replaced by a checked value.
Term exchange_count(ProcessContext &context, std::optional<std::int64_t> &setting, CountChange change) {
    const std::scoped_lock lock(settings_mutex);
    const auto previous = setting.value_or(change.fallback);
    setting = change.value;
    return integer(context, previous);
}

Term outstanding_system_requests_limit(ProcessContext &context, const ValueArg &value) {
    const auto limit = count_of(value, 134217727);
    if (limit == 0) {
        bad_argument();
    }
    return exchange_count(context, settings.outstanding_requests, {.value = limit, .fallback = 2 * workers(context)});
}

// schedulers_online and dirty_cpu_schedulers_online: 1 up to the workers; Clause keeps running on every worker.
Term schedulers_online(ProcessContext &context, const ValueArg &value) {
    const auto online = count_of(value, workers(context));
    if (online == 0) {
        bad_argument();
    }
    return exchange_count(context, settings.schedulers_online, {.value = online, .fallback = workers(context)});
}

Term dirty_cpu_schedulers_online(ProcessContext &context, const ValueArg &value) {
    const auto online = count_of(value, workers(context));
    if (online == 0) {
        bad_argument();
    }
    return exchange_count(context, settings.dirty_schedulers_online, {.value = online, .fallback = workers(context)});
}

// multi_scheduling: block and unblock (and their _normal forms); disabled with a single worker.
Term multi_scheduling(ProcessContext &context, const ValueArg &value) {
    const auto name = value.term.is_atom() ? value.term.atom_spelling_unchecked() : std::string_view{};
    const bool block = name == "block" || name == "block_normal";
    if (!block && name != "unblock" && name != "unblock_normal") {
        bad_argument();
    }
    if (workers(context) == 1) {
        return atom(context, "disabled"_atom);
    }
    const std::scoped_lock lock(settings_mutex);
    settings.multi_scheduling_blocked = block;
    return atom(context, block ? "blocked"_atom : "enabled"_atom);
}

// system_logger: logger, undefined or a pid; the previous one (undefined before any is set: there is no logger).
Term system_logger(ProcessContext &context, const ValueArg &value) {
    const auto &term = value.term;
    const bool named =
        term.is_atom() && (term.atom_spelling_unchecked() == "logger" || term.atom_spelling_unchecked() == "undefined");
    if (!named && !term.is_pid()) {
        bad_argument();
    }
    const std::scoped_lock lock(settings_mutex);
    const auto previous = std::exchange(settings.system_logger, term.word());
    return previous == 0 ? atom(context, "undefined"_atom) : need(Term::from_word(previous, context));
}

// time_offset: finalize; a program runs in no_time_warp mode, so the offset is final.
Term time_offset(ProcessContext &context, const ValueArg &value) {
    if (!value.term.is_atom() || value.term.atom_spelling_unchecked() != "finalize") {
        bad_argument();
    }
    return atom(context, "final"_atom);
}

using FlagSetter = Term (*)(ProcessContext &, const ValueArg &);

constexpr std::array<std::pair<std::string_view, FlagSetter>, 14> FLAGS{{
    {"backtrace_depth", backtrace_depth},
    {"min_heap_size", min_heap_size},
    {"fullsweep_after", fullsweep_after},
    {"min_bin_vheap_size", min_bin_vheap_size},
    {"max_heap_size", max_heap_size},
    {"microstate_accounting", microstate_accounting},
    {"scheduler_wall_time", scheduler_wall_time},
    {"trace_control_word", trace_control_word},
    {"outstanding_system_requests_limit", outstanding_system_requests_limit},
    {"schedulers_online", schedulers_online},
    {"dirty_cpu_schedulers_online", dirty_cpu_schedulers_online},
    {"multi_scheduling", multi_scheduling},
    {"system_logger", system_logger},
    {"time_offset", time_offset},
}};

// system_flag(Flag, Value): the previous value of the flag; badarg for a flag Clause does not take.
Term system_flag(ProcessContext &context, const FlagArg &flag, const ValueArg &value) {
    const auto name = flag.term.is_atom() ? flag.term.atom_spelling_unchecked() : std::string_view{};
    const auto found = std::ranges::find(FLAGS, name, &std::pair<std::string_view, FlagSetter>::first);
    if (found == FLAGS.end()) {
        bad_argument();
    }
    return found->second(context, value);
}

constexpr std::array SYSTEM_FLAG_BUILTINS{
    typed_entry<system_flag>("erlang", "system_flag"),
};
} // namespace

std::int64_t system_trace_control_word() noexcept {
    const std::scoped_lock lock(settings_mutex);
    return settings.trace_control_word;
}
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> system_flag_builtins() noexcept { return builtins::SYSTEM_FLAG_BUILTINS; }
} // namespace clause::runtime
