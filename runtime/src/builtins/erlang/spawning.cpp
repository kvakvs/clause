#include "spawning.hpp"
#include "../../memory/message_buffer.hpp"
#include "../../scheduler/executor.hpp"
#include "../portions.hpp"
#include "../typed.hpp"
#include "terms.hpp"
#include <array>
#include <clause/runtime/code_server.hpp>

// spawn_opt/2..5, spawn_request/1..5, spawn_request_abandon/1, alias/0,1, unalias/1 and hibernate/3 (plan step 41G2,
// docs/processes.md#spawn-options). min_heap_size is applied; other heap and scheduling tuning options are checked and
// accepted, not applied.
namespace clause::runtime::builtins {
namespace {
using detail::Executor;
using detail::SpawnOptions;

// The node a spawn on this node names; anything else is badarg.
void local_node(const Term &node) {
    if (!node.is_atom() || need(node.atom_spelling()) != "nonode@nohost") {
        bad_argument();
    }
}

// Whether an atom option value is one of `allowed`.
template <std::size_t N> bool one_of(const Term &value, const std::array<std::string_view, N> &allowed) {
    return value.is_atom() && std::ranges::contains(allowed, need(value.atom_spelling()));
}

// Whether a value is a non-negative small integer.
bool count(const Term &value) {
    const auto size = small(value.word());
    return size && *size >= 0;
}

// Whether a {Name, Value} spawn option only tunes the process (accepted, not applied).
bool tuning_option(std::string_view name, const Term &value) {
    if (name == "priority") {
        return one_of<4>(value, {"low", "normal", "high", "max"});
    }
    if (name == "fullsweep_after" || name == "min_bin_vheap_size") {
        return count(value);
    }
    if (name == "message_queue_data") {
        return one_of<2>(value, {"off_heap", "on_heap"});
    }
    if (name == "async_dist") {
        return value.is_boolean();
    }
    return name == "max_heap_size" && (count(value) || value.is_map());
}

// Apply one {Name, Value} spawn option; false for an unknown one.
bool pair_option(const Term &option, SpawnOptions &options) {
    if (!option.is_tuple() || need(option.tuple_size()) != 2) {
        return false;
    }
    const auto name = need(option.tuple_element(0)).atom_spelling().value_or("");
    const auto value = need(option.tuple_element(1));
    if (name == "monitor") {
        options.monitor = true;
        options.monitor_options = monitor_options(value);
        return true;
    }
    if (name == "min_heap_size" && count(value)) {
        options.min_heap_words = static_cast<std::size_t>(*small(value.word()));
        return true;
    }
    return tuning_option(name, value);
}

// Apply the atom option link or monitor; false for anything else.
bool flag_option(const Term &option, SpawnOptions &options) {
    const auto name = option.is_atom() ? need(option.atom_spelling()) : std::string_view{};
    if (name != "link" && name != "monitor") {
        return false;
    }
    (name == "link" ? options.link : options.monitor) = true;
    return true;
}

// The spawn_opt options of a list; `extra` accepts spawn_request's own options. Badarg for anything else.
template <typename Extra> SpawnOptions spawn_options(const ListArgument &list, Extra extra) {
    SpawnOptions options;
    for (const auto &option : list.elements) {
        if (!flag_option(option, options) && !pair_option(option, options) && !extra(option)) {
            bad_argument();
        }
    }
    return options;
}

SpawnOptions spawn_options(const ListArgument &list) {
    return spawn_options(list, [](const Term &) { return false; });
}

// spawn(Fun) with options; badarg unless Fun is a fun of arity 0.
TermResult<Term> spawn_fun(ProcessContext &context, const Term &fun, const SpawnOptions &options) {
    if (!fun.is_function(0)) {
        bad_argument();
    }
    return Executor::of(context).spawn(context, fun, options);
}

TermResult<Term> spawn_opt2(ProcessContext &context, const Term &fun, const ListArgument &options) {
    return spawn_fun(context, fun, spawn_options(options));
}

TermResult<Term> spawn_opt3(ProcessContext &context, const AtomArgument &node, const Term &fun,
                            const ListArgument &options) {
    local_node(node.term);
    return spawn_opt2(context, fun, options);
}

// The call of spawn_opt/4,5 and spawn_request: Module, Function, Args.
struct Call {
    const AtomArgument &module;
    const AtomArgument &function;
    const ListArgument &arguments;
};

TermResult<Term> spawn_call(ProcessContext &context, const Call &call, const SpawnOptions &options) {
    return Executor::of(context).spawn(context, {call.module.term, call.function.term, call.arguments.term}, options);
}

TermResult<Term> spawn_opt4(ProcessContext &context, const AtomArgument &module, const AtomArgument &function,
                            const ListArgument &arguments, const ListArgument &options) {
    return spawn_call(context, {module, function, arguments}, spawn_options(options));
}

TermResult<Term> spawn_opt5(ProcessContext &context, const Term &node, const AtomArgument &module,
                            const AtomArgument &function, const ListArgument &arguments, const ListArgument &options) {
    local_node(node);
    return spawn_opt4(context, module, function, arguments, options);
}

// spawn_request's own options: the reply tag and which replies to send.
struct Reply {
    Term tag;
    bool success = true;
    bool error = true;
};

// Apply {reply_tag, Tag} or {reply, yes | no | error_only | success_only}; false for anything else.
bool reply_option(const Term &option, Reply &reply) {
    if (!option.is_tuple() || need(option.tuple_size()) != 2) {
        return false;
    }
    const auto name = need(option.tuple_element(0)).atom_spelling().value_or("");
    const auto value = need(option.tuple_element(1));
    if (name == "reply_tag") {
        reply.tag = value;
        return true;
    }
    const auto mode = value.is_atom() ? need(value.atom_spelling()) : std::string_view{};
    if (name != "reply" ||
        !std::ranges::contains(std::array<std::string_view, 4>{"yes", "no", "error_only", "success_only"}, mode)) {
        return false;
    }
    reply.success = mode == "yes" || mode == "success_only";
    reply.error = mode == "yes" || mode == "error_only";
    return true;
}

// Send the spawn reply {Tag, ReqId, ok, Pid} or {Tag, ReqId, error, Reason} to the caller as `reply` asks.
void send_reply(ProcessContext &context, const Reply &reply, const Term &request, bool ok, Term value) {
    if (ok ? !reply.success : !reply.error) {
        return;
    }
    TermFactory factory(context);
    if (value.is_tuple()) {
        value = need(value.tuple_element(0));
    }
    const std::array parts{reply.tag, request, need(factory.atom(ok ? "ok" : "error")), value};
    if (const auto sent = Executor::send(context, detail::pid_of(context), need(factory.tuple(parts))); !sent) {
        throw BuiltinFailure{.term = sent.error()};
    }
}

// The options of a spawn_request option list; none when one is invalid (the request then replies badopt).
std::optional<SpawnOptions> request_options(const ListArgument *list, Reply &reply) {
    if (!list) {
        return SpawnOptions{};
    }
    try {
        return spawn_options(*list, [&](const Term &option) { return reply_option(option, reply); });
    } catch (const BuiltinFailure &) {
        return std::nullopt;
    }
}

// Spawn for spawn_request with its options, reply to the caller, and return the request id. Invalid options reply
// {Tag, ReqId, error, badopt}, as in OTP.
template <typename Spawn> TermResult<Term> request(ProcessContext &context, const ListArgument *list, Spawn spawn) {
    TermFactory factory(context);
    Reply reply{.tag = need(factory.atom("spawn_reply"))};
    auto options = request_options(list, reply);
    auto id = need(factory.make_reference());
    if (!options) {
        send_reply(context, reply, id, false, need(factory.atom("badopt")));
        return id;
    }
    options->monitor_reference = need(id.reference_value());
    const auto spawned = spawn(*options);
    send_reply(context, reply, id, spawned.has_value(), spawned ? *spawned : need(factory.atom("system_limit")));
    return id;
}

TermResult<Term> request_fun(ProcessContext &context, const Term &fun, const ListArgument *options) {
    if (!fun.is_function(0)) {
        bad_argument();
    }
    return request(context, options,
                   [&](const SpawnOptions &spawn) { return Executor::of(context).spawn(context, fun, spawn); });
}

TermResult<Term> request_call(ProcessContext &context, const Call &call, const ListArgument *options) {
    return request(context, options, [&](const SpawnOptions &spawn) { return spawn_call(context, call, spawn); });
}

// spawn_request(Fun).
TermResult<Term> spawn_request1(ProcessContext &context, const Term &fun) { return request_fun(context, fun, nullptr); }

// spawn_request(Fun, Options) or spawn_request(Node, Fun).
TermResult<Term> spawn_request2(ProcessContext &context, const Term &first, const Term &second) {
    if (first.is_atom()) {
        local_node(first);
        return request_fun(context, second, nullptr);
    }
    const auto options = need(Argument<ListArgument>::convert(second));
    if (!options) {
        bad_argument();
    }
    return request_fun(context, first, &*options);
}

// spawn_request(Node, Fun, Options) or spawn_request(Module, Function, Args).
TermResult<Term> spawn_request3(ProcessContext &context, const AtomArgument &first, const Term &second,
                                const ListArgument &third) {
    if (second.is_function()) {
        local_node(first.term);
        return request_fun(context, second, &third);
    }
    const auto function = need(Argument<AtomArgument>::convert(second));
    if (!function) {
        bad_argument();
    }
    return request_call(context, {first, *function, third}, nullptr);
}

// spawn_request(Node, Module, Function, Args) or spawn_request(Module, Function, Args, Options).
TermResult<Term> spawn_request4(ProcessContext &context, const AtomArgument &first, const AtomArgument &second,
                                const Term &third, const ListArgument &fourth) {
    if (third.is_atom()) {
        local_node(first.term);
        const auto function = need(Argument<AtomArgument>::convert(third));
        return request_call(context, {second, *function, fourth}, nullptr);
    }
    const auto arguments = need(Argument<ListArgument>::convert(third));
    if (!arguments) {
        bad_argument();
    }
    return request_call(context, {first, second, *arguments}, &fourth);
}

// spawn_request(Node, Module, Function, Args, Options).
TermResult<Term> spawn_request5(ProcessContext &context, const Term &node, const AtomArgument &module,
                                const AtomArgument &function, const ListArgument &arguments,
                                const ListArgument &options) {
    local_node(node);
    return request_call(context, {module, function, arguments}, &options);
}

// spawn_request_abandon(ReqId): the reply was already sent when the request returned, so false.
TermResult<Term> spawn_request_abandon(ProcessContext &context, const Term &request) {
    if (!request.is_reference()) {
        bad_argument();
    }
    return TermFactory(context).boolean(false);
}

// alias() and alias(Options): explicit_unalias (the default) or reply.
TermResult<Term> make_alias(ProcessContext &context, detail::AliasMode mode) {
    auto reference = need(TermFactory(context).make_reference());
    Executor::alias(context, need(reference.reference_value()), mode);
    return reference;
}

TermResult<Term> alias0(ProcessContext &context) { return make_alias(context, detail::AliasMode::explicit_unalias); }

TermResult<Term> alias1(ProcessContext &context, const ListArgument &options) {
    auto mode = detail::AliasMode::explicit_unalias;
    for (const auto &option : options.elements) {
        const auto name = option.is_atom() ? need(option.atom_spelling()) : std::string_view{};
        if (name == "reply") {
            mode = detail::AliasMode::reply;
        } else if (name != "explicit_unalias") {
            bad_argument();
        }
    }
    return make_alias(context, mode);
}

// unalias(Alias): true when it was an active alias of the caller.
TermResult<Term> unalias(ProcessContext &context, const Term &alias) {
    if (!alias.is_reference()) {
        bad_argument();
    }
    return TermFactory(context).boolean(Executor::unalias(context, need(alias.reference_value())));
}

// hibernate(Module, Function, Args): wait until a message is in the mailbox, then call Module:Function(Args...)
// in place of the call (its result returns to hibernate's caller; the stack is kept).
Word hibernate(ProcessContext &context, Arguments arguments) {
    const auto module = term_of(context, arguments[0]);
    const auto function = term_of(context, arguments[1]);
    const auto list = need(Argument<ListArgument>::convert(term_of(context, arguments[2])));
    if (!module.is_atom() || !function.is_atom() || !list || list->elements.size() > 255) {
        bad_argument();
    }
    const auto *frame = static_cast<const abi::v1::FrameDescriptor *>(
        context.code_server().function_frame(module, function, list->elements.size()));
    if (!frame) {
        return raise(context, abi::v1::ErrorReason::undef);
    }
    std::vector<Word> state;
    std::ranges::transform(list->elements, std::back_inserter(state), &Term::word);
    if (context.mailbox().size() == 0) {
        context.stack().wait(*frame, state);
    } else {
        context.stack().trap(*frame, state);
    }
    return 0;
}

constexpr std::array SPAWNING_BUILTINS{
    typed_entry<spawn_opt2>("erlang", "spawn_opt"),
    typed_entry<spawn_opt3>("erlang", "spawn_opt"),
    typed_entry<spawn_opt4>("erlang", "spawn_opt"),
    typed_entry<spawn_opt5>("erlang", "spawn_opt"),
    typed_entry<spawn_request1>("erlang", "spawn_request"),
    typed_entry<spawn_request2>("erlang", "spawn_request"),
    typed_entry<spawn_request3>("erlang", "spawn_request"),
    typed_entry<spawn_request4>("erlang", "spawn_request"),
    typed_entry<spawn_request5>("erlang", "spawn_request"),
    typed_entry<spawn_request_abandon>("erlang", "spawn_request_abandon"),
    typed_entry<alias0>("erlang", "alias"),
    typed_entry<alias1>("erlang", "alias"),
    typed_entry<unalias>("erlang", "unalias"),
    BuiltinEntry{"erlang", "hibernate", 3, &guarded<hibernate>},
};

// The mode of a monitor's {alias, Mode}: explicit_unalias, demonitor or reply_demonitor; badarg otherwise.
detail::AliasMode alias_mode(const Term &value) {
    const auto mode = value.is_atom() ? need(value.atom_spelling()) : std::string_view{};
    if (mode == "explicit_unalias") {
        return detail::AliasMode::explicit_unalias;
    }
    if (mode == "demonitor") {
        return detail::AliasMode::demonitor;
    }
    if (mode != "reply_demonitor") {
        bad_argument();
    }
    return detail::AliasMode::reply_demonitor;
}
} // namespace

detail::MonitorOptions monitor_options(const Term &list) {
    const auto options = need(Argument<ListArgument>::convert(list));
    if (!options) {
        bad_argument();
    }
    detail::MonitorOptions result;
    for (const auto &option : options->elements) {
        if (!option.is_tuple() || need(option.tuple_size()) != 2) {
            bad_argument();
        }
        const auto name = need(option.tuple_element(0)).atom_spelling().value_or("");
        const auto value = need(option.tuple_element(1));
        if (name == "tag") {
            result.tag = need(detail::MessageBuffer::copy_of(value));
        } else if (name == "alias") {
            result.alias = alias_mode(value);
        } else {
            bad_argument();
        }
    }
    return result;
}
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> spawning_builtins() noexcept { return builtins::SPAWNING_BUILTINS; }
} // namespace clause::runtime
