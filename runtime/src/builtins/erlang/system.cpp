#include "../../scheduler/executor.hpp"
#include "../typed.hpp"
#include "terms.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <clause/runtime/atoms.hpp>
#include <ctime>
#include <thread>

// statistics/1, system_info/1 and memory/0,1 (plan step 41G1, docs/builtins.md#system-information): the items OTP
// programs commonly read, from the executor's counts. Clause keeps no reduction, garbage collection or I/O counters,
// so those report zero; any other item is badarg.
namespace clause::runtime::builtins {
namespace {
using detail::Executor;

// The most processes, ports and atoms Clause reports as limits.
constexpr std::int64_t PROCESS_LIMIT = (std::int64_t{1} << 27) - 1;

Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

Term integer(ProcessContext &context, std::int64_t value) { return need(TermFactory(context).integer(value)); }

Term pair(ProcessContext &context, const Term &first, const Term &second) {
    const std::array parts{first, second};
    return need(TermFactory(context).tuple(parts));
}

// A proper list of the characters of `text`.
Term string(ProcessContext &context, std::string_view text) {
    std::vector<Term> codes;
    for (const auto character : text) {
        codes.push_back(integer(context, static_cast<unsigned char>(character)));
    }
    return need(TermFactory(context).list(codes));
}

// {Total, SinceLastCall} of a millisecond count; `last` keeps the previous total program-wide.
Term since_last(ProcessContext &context, std::int64_t total, std::atomic<std::int64_t> &last) {
    return pair(context, integer(context, total), integer(context, total - last.exchange(total)));
}

std::int64_t wall_clock_ms(ProcessContext &context) {
    const auto elapsed = std::chrono::steady_clock::now() - Executor::counts(context).started;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

std::int64_t runtime_ms() { return static_cast<std::int64_t>(std::clock()) * 1000 / CLOCKS_PER_SEC; }

// {0, 0} for the counters Clause does not keep.
Term zero_pair(ProcessContext &context) { return pair(context, integer(context, 0), integer(context, 0)); }

// The statistics/1 counters Clause does not keep: {0, 0}, {0, 0, 0} or {{input, 0}, {output, 0}}; none for other
// items.
std::optional<Term> unkept_statistics(ProcessContext &context, std::string_view name) {
    if (name == "reductions" || name == "exact_reductions") {
        return zero_pair(context);
    }
    if (name == "garbage_collection") {
        const std::array parts{integer(context, 0), integer(context, 0), integer(context, 0)};
        return need(TermFactory(context).tuple(parts));
    }
    if (name == "io") {
        return pair(context, pair(context, atom(context, "input"), integer(context, 0)),
                    pair(context, atom(context, "output"), integer(context, 0)));
    }
    return std::nullopt;
}

// statistics(Item).
TermResult<Term> statistics(ProcessContext &context, const AtomArgument &item) {
    static std::atomic<std::int64_t> wall{0};
    static std::atomic<std::int64_t> cpu{0};
    const auto &name = item.spelling;
    const auto counts = Executor::counts(context);
    if (name == "wall_clock") {
        return since_last(context, wall_clock_ms(context), wall);
    }
    if (name == "runtime") {
        return since_last(context, runtime_ms(), cpu);
    }
    if (name == "run_queue" || name == "total_run_queue_lengths" || name == "total_active_tasks") {
        return integer(context, static_cast<std::int64_t>(counts.run_queue));
    }
    if (name == "context_switches") {
        return pair(context, integer(context, static_cast<std::int64_t>(counts.slices)), integer(context, 0));
    }
    if (const auto unkept = unkept_statistics(context, name)) {
        return *unkept;
    }
    bad_argument();
}

// system_info items answered by a string.
std::optional<std::string> text_item(std::string_view name) {
    if (name == "otp_release") {
        return "29";
    }
    if (name == "version") {
        return "17.1";
    }
    if (name == "machine") {
        return "Clause";
    }
    if (name == "system_version") {
        return "Clause compiled Erlang/OTP 29 [erts-17.1]\n";
    }
    if (name == "system_architecture") {
        return sizeof(Word) == 8 ? "clause-64" : "clause-32";
    }
    return std::nullopt;
}

// system_info items answered by an integer.
std::optional<std::int64_t> count_item(ProcessContext &context, std::string_view name) {
    const auto counts = Executor::counts(context);
    const auto processors = static_cast<std::int64_t>(std::max(1U, std::thread::hardware_concurrency()));
    const std::array<std::pair<std::string_view, std::int64_t>, 13> items{{
        {"wordsize", static_cast<std::int64_t>(sizeof(Word))},
        {"process_count", static_cast<std::int64_t>(counts.processes)},
        {"process_limit", PROCESS_LIMIT},
        {"port_count", static_cast<std::int64_t>(counts.ports)},
        {"port_limit", PROCESS_LIMIT},
        {"atom_count", static_cast<std::int64_t>(context.atom_storage().size())},
        {"atom_limit", AtomStorage::default_limit},
        {"schedulers", static_cast<std::int64_t>(counts.workers)},
        {"schedulers_online", static_cast<std::int64_t>(counts.workers)},
        {"logical_processors", processors},
        {"logical_processors_available", processors},
        {"logical_processors_online", processors},
        {"thread_pool_size", 0},
    }};
    const auto found = std::ranges::find(items, name, &std::pair<std::string_view, std::int64_t>::first);
    return found == items.end() ? std::nullopt : std::optional{found->second};
}

// system_info items answered by an atom.
std::optional<std::string_view> atom_item(std::string_view name) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 9> ITEMS{{
        {"smp_support", "true"},
        {"threads", "true"},
        {"build_type", "opt"},
        {"heap_type", "private"},
        {"kernel_poll", "false"},
        {"debug_compiled", "false"},
        {"time_correction", "true"},
        {"time_warp_mode", "no_time_warp"},
        {"endian", std::endian::native == std::endian::little ? "little" : "big"},
    }};
    const auto found = std::ranges::find(ITEMS, name, &std::pair<std::string_view, std::string_view>::first);
    return found == ITEMS.end() ? std::nullopt : std::optional{found->second};
}

// The system_info items answered by a number or a tagged number: compat_rel, and min_heap_size as
// {min_heap_size, Words} (the --min-heap-size default); badarg for any other item.
TermResult<Term> numbered_item(ProcessContext &context, std::string_view name) {
    if (name == "compat_rel") {
        return integer(context, 29);
    }
    if (name != "min_heap_size") {
        bad_argument();
    }
    return pair(context, atom(context, name),
                integer(context, static_cast<std::int64_t>(context.heap().default_min_heap_words())));
}

// system_info(Item): an atom item, or {wordsize, internal | external}.
TermResult<Term> system_info(ProcessContext &context, const Term &item) {
    if (item.is_tuple() && item.tuple_size_unchecked() == 2 && item.tuple_element_unchecked(0).is_atom() &&
        item.tuple_element_unchecked(0).atom_spelling_unchecked() == "wordsize") {
        return integer(context, static_cast<std::int64_t>(sizeof(Word)));
    }
    const auto name = item.is_atom() ? item.atom_spelling_unchecked() : std::string_view{};
    if (const auto text = text_item(name)) {
        return string(context, *text);
    }
    if (const auto count = count_item(context, name)) {
        return integer(context, *count);
    }
    if (const auto value = atom_item(name)) {
        return atom(context, *value);
    }
    return numbered_item(context, name);
}

// The memory/0 sizes in bytes: processes from the runtime-wide account, atoms by their table size.
std::array<std::pair<std::string_view, std::int64_t>, 9> memory_sizes(ProcessContext &context) {
    const auto processes = static_cast<std::int64_t>(Executor::counts(context).memory_words * sizeof(Word));
    const auto atoms = static_cast<std::int64_t>(context.atom_storage().size() * 64);
    return {{{"total", processes + atoms},
             {"processes", processes},
             {"processes_used", processes},
             {"system", atoms},
             {"atom", atoms},
             {"atom_used", atoms},
             {"binary", 0},
             {"code", 0},
             {"ets", 0}}};
}

TermResult<Term> memory0(ProcessContext &context) {
    std::vector<Term> items;
    for (const auto &[name, size] : memory_sizes(context)) {
        items.push_back(pair(context, atom(context, name), integer(context, size)));
    }
    return TermFactory(context).list(items);
}

// The size of one memory/1 type; badarg for an unknown one.
std::int64_t memory_size(ProcessContext &context, const Term &type) {
    const auto name = type.is_atom() ? type.atom_spelling_unchecked() : std::string_view{};
    const auto sizes = memory_sizes(context);
    const auto found = std::ranges::find(sizes, name, &std::pair<std::string_view, std::int64_t>::first);
    if (found == sizes.end()) {
        bad_argument();
    }
    return found->second;
}

// memory(Type) or memory([Type]).
TermResult<Term> memory1(ProcessContext &context, const Term &types) {
    if (!types.is_list()) {
        return integer(context, memory_size(context, types));
    }
    std::vector<Term> items;
    for (auto rest = types; !rest.is_nil(); rest = rest.tail_unchecked()) {
        if (!rest.is_cons()) {
            bad_argument();
        }
        const auto type = rest.head_unchecked();
        items.push_back(pair(context, type, integer(context, memory_size(context, type))));
    }
    return TermFactory(context).list(items);
}

constexpr std::array SYSTEM_BUILTINS{
    typed_entry<statistics>("erlang", "statistics"),
    typed_entry<system_info>("erlang", "system_info"),
    typed_entry<memory0>("erlang", "memory"),
    typed_entry<memory1>("erlang", "memory"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> system_builtins() noexcept { return builtins::SYSTEM_BUILTINS; }
} // namespace clause::runtime
