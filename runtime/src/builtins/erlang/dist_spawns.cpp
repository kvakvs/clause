#include "dist_spawns.hpp"
#include "../../scheduler/executor.hpp"
#include "../../terms/identities.hpp"
#include "dist_messages.hpp"
#include "spawning.hpp"
#include <array>
#include <clause/runtime/code_server.hpp>
#include <optional>
#include <string_view>
#include <vector>

// Spawning on other nodes (plan 30 step DIST5b2, docs/distribution.md#spawning), as OTP 29's dist.c and erlang.erl
// spawn: the requester's reply and monitor options stay with it and the others go to the node, which creates the
// child and answers with SPAWN_REPLY.
namespace clause::runtime::builtins {
namespace {
using detail::Executor;
using detail::PendingSpawn;

// The control message operations of spawns (erl_dist_protocol).
constexpr std::int64_t SPAWN_REQUEST = 29;
constexpr std::int64_t SPAWN_REPLY = 31;

// A spawn request's options as OTP's dist_spawn_request splits them: the requester's pending state, the options
// sent to the node (link and monitor first for their tuple forms, then the rest in order), and whether one is
// invalid (badopt).
struct SplitOptions {
    PendingSpawn pending;
    std::vector<Term> sent;
    bool link_first = false;
    bool monitor_first = false;
    bool badopt = false;
};

// The atom spelling of a term, or empty.
std::string_view spelling(const Term &term) { return term.is_atom() ? term.atom_spelling_unchecked() : ""; }

// Apply {reply, Mode}: false for a mode OTP does not know.
bool reply_mode(const Term &mode, PendingSpawn &pending) {
    const auto name = spelling(mode);
    if (name != "yes" && name != "no" && name != "error_only" && name != "success_only") {
        return false;
    }
    pending.success_message = name == "yes" || name == "success_only";
    pending.error_message = name == "yes" || name == "error_only";
    return true;
}

// Apply {monitor, MonitorOptions} or {link, LinkOptions} ([] or [priority]); false when they are invalid.
bool monitor_or_link(ProcessContext &context, std::string_view name, const Term &value, SplitOptions &split) {
    if (name == "link") {
        const auto list = need(Argument<ListArgument>::convert(value));
        split.pending.link = split.link_first =
            list && std::ranges::all_of(list->elements, [](const Term &item) { return spelling(item) == "priority"; });
        return split.link_first;
    }
    try {
        split.pending.monitor_options = monitor_options(context, value);
        split.pending.monitor = split.monitor_first = true;
        return true;
    } catch (const BuiltinFailure &) {
        return false;
    }
}

// Take one of the requester's own options ({reply_tag, T}, {reply, M}, {monitor, Os}, {link, Os}); false for any
// other option, which goes to the node.
bool own_option(ProcessContext &context, const Term &option, SplitOptions &split) {
    if (!option.is_tuple() || option.tuple_size_unchecked() != 2) {
        return false;
    }
    const auto name = spelling(option.tuple_element_unchecked(0));
    const auto value = option.tuple_element_unchecked(1);
    if (name == "reply_tag") {
        split.pending.tag = need(detail::MessageBuffer::copy_of(value, context));
        return true;
    }
    if (name == "reply" || name == "monitor" || name == "link") {
        split.badopt =
            name == "reply" ? !reply_mode(value, split.pending) : !monitor_or_link(context, name, value, split);
        return true;
    }
    return false;
}

// Take one option of a spawn request: the requester's own, or one sent to the node.
void take_option(ProcessContext &context, const Term &option, SplitOptions &split) {
    split.pending.link = split.pending.link || spelling(option) == "link";
    split.pending.monitor = split.pending.monitor || spelling(option) == "monitor";
    if (!own_option(context, option, split)) {
        split.sent.push_back(option);
    }
}

SplitOptions split_options(ProcessContext &context, const ListArgument *options) {
    SplitOptions split;
    split.pending.tag = need(detail::MessageBuffer::copy_of(atom(context, "spawn_reply"_atom), context));
    // OTP stops at the first invalid option.
    for (std::size_t index = 0; options != nullptr && index < options->elements.size() && !split.badopt; ++index) {
        take_option(context, options->elements[index], split);
    }
    if (split.monitor_first) {
        split.sent.insert(split.sent.begin(), atom(context, "monitor"_atom));
    }
    if (split.link_first) {
        split.sent.insert(split.sent.begin(), atom(context, "link"_atom));
    }
    return split;
}

// The caller's group leader as a term of its heap, as group_leader/0 reports it; request_spawn holds the lock.
Term group_leader_of(ProcessContext &context) {
    const auto user = Executor::of(context).whereis_locked(context, atom(context, "user"_atom).word());
    return reported_leader(context, context, user);
}

// The SPAWN_REQUEST packet of the caller's request `reference`.
std::vector<std::byte> request_packet(ProcessContext &context, const Term &reference, const RemoteSpawnCall &call,
                                      const std::vector<Term> &sent) {
    TermFactory factory(context);
    const auto arguments = need(Argument<ListArgument>::convert(call.arguments));
    const auto arity = arguments ? arguments->elements.size() : 0;
    const auto mfa =
        need(factory.tuple(std::array{call.module, call.function, need(factory.integer(std::int64_t(arity)))}));
    const std::array parts{need(factory.integer(SPAWN_REQUEST)),
                           reference,
                           need(factory.pid(context.identity())),
                           group_leader_of(context),
                           mfa,
                           need(factory.list(sent))};
    return dist_packet(context, need(factory.tuple(parts)), call.arguments);
}

// Send the caller the error reply {Tag, ReqId, error, Reason} unless its options asked for none.
void reply_error(ProcessContext &context, const PendingSpawn &pending, const Term &reference, AtomName reason) {
    if (!pending.error_message) {
        return;
    }
    TermFactory factory(context);
    const std::array parts{need(pending.tag->copy_to(context)), reference, atom(context, "error"_atom),
                           atom(context, reason)};
    if (const auto sent = Executor::send(context, detail::pid_of(context), need(factory.tuple(parts))); !sent) {
        throw BuiltinFailure{.term = sent.error()};
    }
}

// Answer another node's request with an error SPAWN_REPLY {31, ReqId, From, 0, Reason}.
void refuse_request(ProcessContext &context, Word node, const Term &control, AtomName reason) {
    TermFactory factory(context);
    const std::array parts{need(factory.integer(SPAWN_REPLY)), control.tuple_element_unchecked(1),
                           control.tuple_element_unchecked(2), need(factory.integer(0)), atom(context, reason)};
    Executor::of(context).dist_output(context, need(Term::from_word(node, context)),
                                      dist_packet(context, need(factory.tuple(parts)), std::nullopt), false);
}
} // namespace

Term request_remote(ProcessContext &context, const Term &node, const RemoteSpawnCall &call,
                    const ListArgument *options) {
    if (!need(Argument<ListArgument>::convert(call.arguments))) {
        bad_argument();
    }
    auto split = split_options(context, options);
    const auto reference = need(TermFactory(context).make_reference());
    if (split.badopt) {
        reply_error(context, split.pending, reference, "badopt"_atom);
        return reference;
    }
    const auto pending = split.pending;
    const auto sent = Executor::of(context).request_spawn(
        context, node, reference.reference_value_unchecked(),
        [&] { return request_packet(context, reference, call, split.sent); }, std::move(split.pending));
    if (sent != detail::SpawnSent::queued) {
        reply_error(context, pending, reference,
                    sent == detail::SpawnSent::notsup ? "notsup"_atom : "noconnection"_atom);
    }
    return reference;
}

Word spawn_remote(ProcessContext &context, const Term &node, const RemoteSpawnCall &call, const Term &options) {
    const auto *frame = static_cast<const abi::v1::FrameDescriptor *>(
        context.code_server().function_frame(atom(context, "clause_dist_spawn"_atom), atom(context, "spawn"_atom), 5));
    if (frame == nullptr) {
        bad_argument();
    }
    context.stack().trap(*frame, std::array{node.word(), call.module.word(), call.function.word(),
                                            call.arguments.word(), options.word()});
    return 0;
}

RemoteSpawnCall fun_call(ProcessContext &context, const Term &fun) {
    TermFactory factory(context);
    const std::array arguments{fun, need(factory.nil())};
    return {.module = atom(context, "erlang"_atom),
            .function = atom(context, "apply"_atom),
            .arguments = need(factory.list(arguments))};
}

void take_spawn_request(ProcessContext &context, const Term &control, Word node, const Term &arguments) {
    detail::RemoteSpawnRequest request{.node = node,
                                       .reference = control.tuple_element_unchecked(1),
                                       .requester = control.tuple_element_unchecked(2),
                                       .leader = control.tuple_element_unchecked(3),
                                       .link = false,
                                       .monitor = false};
    auto options = served_spawn_options(context, control.tuple_element_unchecked(5));
    if (!options) {
        refuse_request(context, node, control, "badopt"_atom);
        return;
    }
    request.link = std::exchange(options->link, false);
    request.monitor = std::exchange(options->monitor, false);
    options->request = &request;
    const auto mfa = control.tuple_element_unchecked(4);
    // An argument list that is no proper list fails in the child, as OTP's apply/3 there fails.
    detail::InitialCall call{mfa.tuple_element_unchecked(0), mfa.tuple_element_unchecked(1), arguments};
    if (!need(Argument<ListArgument>::convert(arguments))) {
        TermFactory factory(context);
        const std::array parts{call.module, call.function, arguments};
        call = {atom(context, "erlang"_atom), atom(context, "apply"_atom), need(factory.list(parts))};
    }
    if (!Executor::of(context).spawn(context, call, *options)) {
        refuse_request(context, node, control, "system_limit"_atom);
    }
}

void take_spawn_reply(ProcessContext &context, Word node, const Term &control) {
    const auto element = [&](std::size_t index) { return control.tuple_element_unchecked(index); };
    Executor::of(context).spawn_replied(context, node, {element(1), element(2), element(3), element(4)});
}
} // namespace clause::runtime::builtins
