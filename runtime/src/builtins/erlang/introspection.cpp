#include "../../scheduler/executor.hpp"
#include "../typed.hpp"
#include "terms.hpp"
#include <algorithm>
#include <array>
#include <vector>

// process_info/1,2, processes/0, group_leader/0,2, garbage_collect/0,1,2 and yield/0 (plan step 41G1,
// docs/processes.md#introspection). Another process is read under the executor's lock, never while it runs, and its
// terms are copied into the caller's heap.
namespace clause::runtime::builtins {
namespace {
using detail::Executor;

// The process an item is read from, the process asking, and the executor holding both.
struct Inspection {
    ProcessContext &caller;
    ProcessContext &target;
    Executor &executor;
};

// An atom of the caller.
Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

// A word of the target copied into the caller's heap.
Term copied(const Inspection &inspection, Word word) {
    return need(need(Term::from_word(word, inspection.target)).copy_to(inspection.caller.heap()));
}

// A word that needs no copy (an immediate pid, port or atom) as a term of the caller.
Term immediate(const Inspection &inspection, Word word) { return need(Term::from_word(word, inspection.caller)); }

Term list(const Inspection &inspection, const std::vector<Term> &items) {
    return need(TermFactory(inspection.caller).list(items));
}

Term pair(const Inspection &inspection, const Term &first, const Term &second) {
    const std::array parts{first, second};
    return need(TermFactory(inspection.caller).tuple(parts));
}

Term integer(const Inspection &inspection, std::size_t value) {
    return need(TermFactory(inspection.caller).integer(static_cast<std::int64_t>(value)));
}

// The group leader of a process: its own pid when it leads itself.
Word leader(ProcessContext &process) {
    const auto leader = process.signals().group_leader();
    return leader != 0 ? leader : detail::pid_of(process);
}

Term registered_name(const Inspection &inspection) {
    const auto name = inspection.target.signals().name();
    return name != 0 ? immediate(inspection, name) : list(inspection, {});
}

Term status(const Inspection &inspection) {
    return atom(inspection.caller, inspection.executor.status(inspection.caller, inspection.target));
}

Term message_queue_len(const Inspection &inspection) { return integer(inspection, inspection.target.mailbox().size()); }

Term messages(const Inspection &inspection) {
    std::vector<Word> words;
    inspection.target.mailbox().visit([&](Word &word) { words.push_back(word); });
    std::vector<Term> items;
    std::ranges::transform(words, std::back_inserter(items), [&](Word word) { return copied(inspection, word); });
    return list(inspection, items);
}

Term links(const Inspection &inspection) {
    std::vector<Term> items;
    for (const auto word : inspection.target.signals().links()) {
        items.push_back(immediate(inspection, word));
    }
    return list(inspection, items);
}

// {process, Pid}, or {process, {Name, nonode@nohost}} for a monitor made by name.
Term monitors(const Inspection &inspection) {
    std::vector<Term> items;
    for (const auto &[reference, monitor] : inspection.target.signals().monitors()) {
        auto subject = immediate(inspection, monitor.pid);
        if (monitor.name != 0) {
            subject = pair(inspection, immediate(inspection, monitor.name), atom(inspection.caller, "nonode@nohost"));
        }
        items.push_back(pair(inspection, atom(inspection.caller, "process"), subject));
    }
    return list(inspection, items);
}

Term monitored_by(const Inspection &inspection) {
    std::vector<Term> items;
    for (const auto &[reference, watcher] : inspection.target.signals().watchers()) {
        items.push_back(immediate(inspection, watcher.pid));
    }
    return list(inspection, items);
}

Term dictionary(const Inspection &inspection) {
    std::vector<Term> items;
    for (const auto &entry : inspection.target.dictionary().entries()) {
        items.push_back(pair(inspection, copied(inspection, entry.key), copied(inspection, entry.value)));
    }
    return list(inspection, items);
}

Term trap_exit(const Inspection &inspection) {
    return need(TermFactory(inspection.caller).boolean(inspection.target.signals().trap_exit()));
}

Term group_leader(const Inspection &inspection) { return immediate(inspection, leader(inspection.target)); }

Term heap_size(const Inspection &inspection) { return integer(inspection, inspection.target.heap().capacity_words()); }

Term stack_size(const Inspection &inspection) { return integer(inspection, inspection.target.stack().words()); }

Term reductions(const Inspection &inspection) { return integer(inspection, 0); }

Term memory(const Inspection &inspection) {
    auto &target = inspection.target;
    const auto words = target.heap().capacity_words() + target.heap().off_heap_words() + target.stack().capacity();
    return integer(inspection, words * sizeof(Word) + sizeof(ProcessContext));
}

Term priority(const Inspection &inspection) { return atom(inspection.caller, "normal"); }

Term error_handler(const Inspection &inspection) { return atom(inspection.caller, "error_handler"); }

Term garbage_collection(const Inspection &inspection) {
    const std::array settings{pair(inspection, atom(inspection.caller, "min_heap_size"),
                                   integer(inspection, inspection.target.heap().min_heap_words())),
                              pair(inspection, atom(inspection.caller, "fullsweep_after"), integer(inspection, 65535)),
                              pair(inspection, atom(inspection.caller, "minor_gcs"), integer(inspection, 0))};
    return list(inspection, {settings.begin(), settings.end()});
}

using ItemReader = Term (*)(const Inspection &);

// The items process_info/2 reads, in process_info/1's order.
constexpr std::array<std::pair<std::string_view, ItemReader>, 17> ITEMS{{
    {"status", status},
    {"message_queue_len", message_queue_len},
    {"links", links},
    {"dictionary", dictionary},
    {"trap_exit", trap_exit},
    {"error_handler", error_handler},
    {"priority", priority},
    {"group_leader", group_leader},
    {"total_heap_size", heap_size},
    {"heap_size", heap_size},
    {"stack_size", stack_size},
    {"reductions", reductions},
    {"garbage_collection", garbage_collection},
    {"registered_name", registered_name},
    {"messages", messages},
    {"monitors", monitors},
    {"monitored_by", monitored_by},
}};

// OTP items Clause does not keep: asking for one raises error:{notsup, Item}.
constexpr std::array<std::string_view, 12> UNKEPT{
    "current_function", "current_location",       "current_stacktrace", "initial_call", "binary", "catchlevel",
    "last_calls",       "sequential_trace_token", "suspending",         "trace",        "parent", "memory_info"};

// {First, Second} with an atom first, in the caller's heap.
Term pair_term(ProcessContext &context, std::string_view first, const Term &second) {
    const std::array parts{atom(context, first), second};
    return need(TermFactory(context).tuple(parts));
}

// The reader of an item atom: badarg for an unknown one, notsup for one Clause does not keep.
ItemReader reader(ProcessContext &context, const Term &item) {
    const auto name = item.is_atom() ? item.atom_spelling_unchecked() : std::string_view{};
    if (name == "memory") {
        return memory;
    }
    if (const auto found = std::ranges::find(ITEMS, name, &std::pair<std::string_view, ItemReader>::first);
        found != ITEMS.end()) {
        return found->second;
    }
    if (std::ranges::contains(UNKEPT, name)) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                             .payload = pair_term(context, "notsup", item).word()};
    }
    bad_argument();
}

// The local pid argument of process_info and garbage_collect; badarg otherwise.
Word local_pid(const Term &pid) {
    if (!pid.is_pid()) {
        bad_argument();
    }
    return pid.word();
}

// {Item, Value} of one item.
Term item_pair(const Inspection &inspection, const Term &item, ItemReader read) {
    return pair(inspection, immediate(inspection, item.word()), read(inspection));
}

// The readers of an item list, in order; badarg for an improper list or an invalid item.
std::vector<std::pair<Term, ItemReader>> readers(ProcessContext &context, const Term &items) {
    std::vector<std::pair<Term, ItemReader>> result;
    for (auto rest = items; !rest.is_nil(); rest = rest.tail_unchecked()) {
        if (!rest.is_cons()) {
            bad_argument();
        }
        const auto item = rest.head_unchecked();
        result.emplace_back(item, reader(context, item));
    }
    return result;
}

// process_info(Pid, [Item]): a list of {Item, Value}.
TermResult<Term> process_info_list(ProcessContext &context, const PidArgument &pid, const Term &items) {
    const auto read = readers(context, items);
    auto result = Executor::inspect(context, pid.term.word(), [&](ProcessContext &target, Executor &executor) {
        const Inspection inspection{context, target, executor};
        std::vector<Term> values;
        values.reserve(read.size());
        for (const auto &[item, reader] : read) {
            values.push_back(item_pair(inspection, item, reader));
        }
        return list(inspection, values);
    });
    return result ? *result : atom(context, "undefined");
}

// process_info(Pid, Item | [Item]): undefined for a process that is not alive.
TermResult<Term> process_info2(ProcessContext &context, const PidArgument &pid, const Term &items) {
    if (items.is_list()) {
        return process_info_list(context, pid, items);
    }
    const auto read = reader(context, items);
    auto result = Executor::inspect(context, pid.term.word(), [&](ProcessContext &target, Executor &executor) {
        const Inspection inspection{context, target, executor};
        auto value = read(inspection);
        // An unregistered process answers registered_name with [] rather than a pair, as in OTP.
        return read == registered_name && value.is_nil() ? value : item_pair(inspection, items, read);
    });
    return result ? *result : atom(context, "undefined");
}

// process_info(Pid): the default items.
TermResult<Term> process_info1(ProcessContext &context, const PidArgument &pid) {
    auto result = Executor::inspect(context, pid.term.word(), [&](ProcessContext &target, Executor &executor) {
        const Inspection inspection{context, target, executor};
        std::vector<Term> values;
        for (const auto &[name, read] : std::span(ITEMS).first(13)) {
            values.push_back(pair(inspection, atom(context, name), read(inspection)));
        }
        return list(inspection, values);
    });
    return result ? *result : atom(context, "undefined");
}

TermResult<Term> processes(ProcessContext &context) {
    const auto pids = Executor::pids(context);
    std::vector<Term> items;
    items.reserve(pids.size());
    for (const auto word : pids) {
        items.push_back(need(Term::from_word(word, context)));
    }
    return TermFactory(context).list(items);
}

TermResult<Term> group_leader0(ProcessContext &context) { return Term::from_word(leader(context), context); }

// group_leader(Leader, Pid): badarg unless both are pids and Pid is alive.
TermResult<Term> group_leader2(ProcessContext &context, const PidArgument &leader_pid, const Term &pid) {
    const auto set = Executor::inspect(context, local_pid(pid), [&](ProcessContext &target, Executor &) {
        target.signals().set_group_leader(leader_pid.term.word());
        return true;
    });
    if (!set) {
        bad_argument();
    }
    return TermFactory(context).boolean(true);
}

TermResult<Term> garbage_collect0(ProcessContext &context) {
    context.heap().request_collection();
    return TermFactory(context).boolean(true);
}

// garbage_collect(Pid): false for a process that is not alive.
TermResult<Term> garbage_collect1(ProcessContext &context, const Term &pid) {
    const auto requested = Executor::inspect(context, local_pid(pid), [](ProcessContext &target, Executor &) {
        target.heap().request_collection();
        return true;
    });
    return TermFactory(context).boolean(requested.has_value());
}

// Whether a garbage_collect/2 option is {type, major} or {type, minor}.
bool type_option(std::string_view name, const Term &value) {
    const auto kind = value.is_atom() ? value.atom_spelling_unchecked() : std::string_view{};
    return name == "type" && (kind == "major" || kind == "minor");
}

// The RequestId of a garbage_collect/2 option list ({async, RequestId}, {type, major | minor}); badarg for others.
std::optional<Term> gc_request(const ListArgument &options) {
    std::optional<Term> request;
    for (const auto &option : options.elements) {
        if (!option.is_tuple() || option.tuple_size_unchecked() != 2) {
            bad_argument();
        }
        const auto name = option.tuple_element_unchecked(0).atom_spelling().value_or("");
        const auto value = option.tuple_element_unchecked(1);
        if (name == "async") {
            request = value;
        } else if (!type_option(name, value)) {
            bad_argument();
        }
    }
    return request;
}

// garbage_collect(Pid, Options): with {async, RequestId} it returns async and answers with the message
// {garbage_collect, RequestId, Result}.
TermResult<Term> garbage_collect2(ProcessContext &context, const Term &pid, const ListArgument &options) {
    const auto request = gc_request(options);
    auto result = need(garbage_collect1(context, pid));
    if (!request) {
        return result;
    }
    TermFactory factory(context);
    const std::array parts{atom(context, "garbage_collect"), *request, result};
    if (const auto sent = Executor::send(context, detail::pid_of(context), need(factory.tuple(parts))); !sent) {
        throw BuiltinFailure{.term = sent.error()};
    }
    return factory.atom("async");
}

// yield/0: end the time slice at the next function entry.
TermResult<Term> yield(ProcessContext &context) {
    context.stack().spend(context.stack().budget());
    return TermFactory(context).boolean(true);
}

constexpr std::array INTROSPECTION_BUILTINS{
    typed_entry<process_info1>("erlang", "process_info"),
    typed_entry<process_info2>("erlang", "process_info"),
    typed_entry<processes>("erlang", "processes"),
    typed_entry<group_leader0>("erlang", "group_leader"),
    typed_entry<group_leader2>("erlang", "group_leader"),
    typed_entry<garbage_collect0>("erlang", "garbage_collect"),
    typed_entry<garbage_collect1>("erlang", "garbage_collect"),
    typed_entry<garbage_collect2>("erlang", "garbage_collect"),
    typed_entry<yield>("erlang", "yield"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> introspection_builtins() noexcept { return builtins::INTROSPECTION_BUILTINS; }
} // namespace clause::runtime
