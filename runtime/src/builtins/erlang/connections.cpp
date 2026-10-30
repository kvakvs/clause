#include "../../process/exits.hpp"
#include "../../scheduler/executor.hpp"
#include "../../terms/identities.hpp"
#include "../../terms/integers.hpp"
#include "../typed.hpp"
#include <array>
#include <clause/runtime/code_server.hpp>
#include <clause/runtime/output.hpp>
#include <cstdint>
#include <expected>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

// Connection entries of other nodes (plan 30 step DIST2b, docs/distribution.md#connections): OTP's process-controller
// interface erts_internal:new_connection/1, abort_pending_connection/2, create_dist_channel/3, get_dflags/0 and
// erlang:setnode/3, over the executor's connection table. The controller data path is DIST2d's: dist_ctrl_get_data/1,
// dist_ctrl_get_data_notification/1, dist_ctrl_put_data/2, dist_ctrl_input_handler/2, dist_ctrl_get_opt/2,
// dist_ctrl_set_opt/3 and dist_get_stat/1 (docs/distribution.md#controller-data-path).
namespace clause::runtime::builtins {
namespace {
using detail::ConnectionTable;
using detail::DataPath;
using detail::DataPathError;
using detail::Executor;
using Node = Named<struct NodeRole>;
using Controller = Named<struct ControllerRole>;
using Options = Named<struct OptionsRole>;
using Handle = Named<struct HandleRole>;
using Option = Named<struct OptionRole>;

// erts_internal:new_connection(Node): the dist handle {ConnId, Ref} of Node's entry, which turns pending when idle.
// Badarg unless Node is a node name other than this node's.
TermResult<Term> new_connection(ProcessContext &context, const AtomArgument &node) {
    const auto handle =
        detail::is_node_name(node.spelling) ? Executor::of(context).new_connection(context, node.term) : std::nullopt;
    if (!handle) {
        bad_argument();
    }
    return *handle;
}

// A dist handle {ConnId, Ref} with a local reference; none for anything else.
std::optional<detail::DistHandle> dist_handle(const Term &term) {
    if (!term.is_tuple() || term.tuple_size_unchecked() != 2) {
        return std::nullopt;
    }
    const auto id = small(term.tuple_element_unchecked(0).word());
    const auto reference = detail::reference_number(term.tuple_element_unchecked(1));
    if (!id || *id < 0 || *id > std::numeric_limits<std::uint32_t>::max() || !reference) {
        return std::nullopt;
    }
    return detail::DistHandle{.id = static_cast<std::uint32_t>(*id), .reference = *reference};
}

// erts_internal:abort_pending_connection(Node, DHandle): false when Node's connection is up, else true after aborting
// a pending connection of the handle's id. Badarg unless the handle names Node's entry.
TermResult<Term> abort_pending_connection(ProcessContext &context, const AtomArgument &node, const Term &handle) {
    const auto parsed = dist_handle(handle);
    const auto aborted = parsed ? Executor::of(context).abort_connection(context, node.term, *parsed) : std::nullopt;
    if (!aborted) {
        bad_argument();
    }
    return TermFactory(context).boolean(*aborted);
}

// A non-negative integer of at most 64 bits, as OTP's term_to_Uint64 reads distribution flags.
std::optional<std::uint64_t> unsigned64(const Term &term) {
    if (!term.is_integer()) {
        return std::nullopt;
    }
    const auto value = detail::integer_read(term);
    if (!value || *value < 0 || *value > std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(*value);
}

// {Flags, Creation} of setnode/3: flags of at most 64 bits, a non-negative small creation cut to 32 bits (OTP's
// term_to_Uint32).
std::optional<detail::ChannelOptions> channel_options(const Term &options) {
    if (!options.is_tuple() || options.tuple_size_unchecked() != 2) {
        return std::nullopt;
    }
    const auto flags = unsigned64(options.tuple_element_unchecked(0));
    const auto creation = small(options.tuple_element_unchecked(1).word());
    if (!flags || !creation || *creation < 0) {
        return std::nullopt;
    }
    return detail::ChannelOptions{.flags = *flags, .creation = static_cast<std::uint32_t>(*creation)};
}

// The error report OTP logs when setnode/3 is given flags without every mandatory one.
void report_missing_flags(ProcessContext &context, const Term &node) {
    const auto shown = [](const Term &term) { return format_term(term, TermStyle::display).value_or(""); };
    auto text = shown(need(TermFactory(context).pid(context.identity())));
    if (const auto name = context.signals().name(); name != 0) {
        text += " (" + shown(need(Term::from_word(name, context))) + ")";
    }
    text += " attempted to enable connection to node " + shown(node) +
            " which does not support all mandatory capabilities.\n";
    detail::report_error(context, text);
}

// Whether create_dist_channel/3 may connect `node` under `controller`: a node name, and a pid or port of this node.
bool channel_ends(const Node &node, const Controller &controller) {
    const auto &term = controller.term;
    const auto local = (term.is_pid() && !term.is_external_pid()) || (term.is_port() && !term.is_external_port());
    return local && node.term.is_atom() && detail::is_node_name(node.term.atom_spelling_unchecked());
}

// The checks of create_dist_channel/3 in OTP's order, then the connection: its dist handle, or none for badarg.
// Port controllers are refused: Clause's carriers run controller processes.
std::optional<Term> open_channel(ProcessContext &context, const Node &node, const Controller &controller,
                                 const Options &options) {
    auto &executor = Executor::of(context);
    const auto parsed = channel_options(options.term);
    if (!executor.kernel_ready(context) || !channel_ends(node, controller) || !parsed) {
        return std::nullopt;
    }
    if ((~parsed->flags & detail::dflags::MANDATORY) != 0) {
        report_missing_flags(context, node.term);
        return std::nullopt;
    }
    return controller.term.is_pid() ? executor.connect(context, node.term, controller.term.word(), *parsed)
                                    : std::nullopt;
}

// erts_internal:create_dist_channel(Node, DistCtrlr, {Flags, Creation}): {ok, DHandle}, or badarg as an atom.
TermResult<Term> create_dist_channel(ProcessContext &context, const Node &node, const Controller &controller,
                                     const Options &options) {
    TermFactory factory(context);
    const auto handle = open_channel(context, node, controller, options);
    if (!handle) {
        return factory.atom("badarg"_atom);
    }
    return factory.tuple(std::array{need(factory.atom("ok"_atom)), *handle});
}

// erlang:setnode(Node, DistCtrlr, {Flags, Creation}): bring Node's pending connection up under the controller; its
// dist handle. Badarg otherwise.
TermResult<Term> setnode3(ProcessContext &context, const Node &node, const Controller &controller,
                          const Options &options) {
    const auto handle = open_channel(context, node, controller, options);
    if (!handle) {
        bad_argument();
    }
    return *handle;
}

// erts_internal:get_dflags(): {erts_dflags, Default, Mandatory, Addable, Rejectable, StrictOrder} of what Clause
// offers (docs/distribution.md#protocol).
TermResult<Term> get_dflags(ProcessContext &context) {
    TermFactory factory(context);
    const auto flags = [&](std::uint64_t value) {
        return need(detail::IntegerAccess::make(context.heap(), detail::Integer{value}));
    };
    namespace dflags = detail::dflags;
    const std::array parts{need(factory.atom("erts_dflags"_atom)),
                           flags(dflags::DEFAULT),
                           flags(dflags::MANDATORY),
                           flags(dflags::DEFAULT),
                           flags(dflags::REJECTABLE),
                           flags(dflags::STRICT_ORDER)};
    return factory.tuple(parts);
}

// The controller data path (plan 30 step DIST2d): a result or the refusal of a dist_ctrl_* call.
template <typename T> using DataPathResult = std::expected<T, DataPathError>;

// Raise a refused call's error:notsup or badarg.
[[noreturn]] void refuse(ProcessContext &context, DataPathError error) {
    if (error == DataPathError::notsup) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                             .payload = atom(context, "notsup"_atom).word()};
    }
    bad_argument();
}

// Run `operation` on the data path of the up connection the caller controls, when `handle` is its dist handle; notsup
// for a caller controlling none, else badarg for another handle or when `operation` refuses its arguments.
template <typename Operation> auto on_controlled(ProcessContext &context, const Term &handle, Operation operation) {
    const auto parsed = dist_handle(handle);
    const auto pid = detail::pid_of(context);
    auto result = Executor::of(context).with_connections([&](ConnectionTable &table) {
        return table.controlled(pid, parsed).and_then([&](detail::Connection *connection) {
            return operation(connection->data);
        });
    });
    if (!result) {
        refuse(context, result.error());
    }
    return std::move(*result);
}

// Whether `term` is the atom `name`: get_size, the one option of dist_ctrl_get_opt/2 and dist_ctrl_set_opt/3, or a
// boolean.
bool is_named(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

// A packet dist_ctrl_get_data/1 takes, and whether to give its size.
struct TakenPacket {
    std::optional<std::vector<std::byte>> bytes;
    bool sized = false;
};

// dist_ctrl_get_data(DHandle): the oldest queued packet as a list of binaries, {Size, Packet} with get_size, or none.
TermResult<Term> dist_ctrl_get_data(ProcessContext &context, const Term &handle) {
    const auto taken = on_controlled(context, handle, [](DataPath &path) -> DataPathResult<TakenPacket> {
        if (path.output.empty()) {
            return TakenPacket{};
        }
        TakenPacket packet{.bytes = std::move(path.output.front()), .sized = path.get_size};
        path.output.pop_front();
        ++path.sent;
        return packet;
    });
    TermFactory factory(context);
    if (!taken.bytes) {
        return factory.atom("none"_atom);
    }
    const auto packet = need(factory.list(std::array{need(factory.binary(*taken.bytes))}));
    if (!taken.sized) {
        return packet;
    }
    return factory.tuple(std::array{need(factory.integer(static_cast<std::int64_t>(taken.bytes->size()))), packet});
}

// dist_ctrl_get_data_notification(DHandle): ok; the controller gets dist_data at once when a packet is queued, else
// when the next one is.
TermResult<Term> dist_ctrl_get_data_notification(ProcessContext &context, const Term &handle) {
    const auto queued = on_controlled(context, handle, [](DataPath &path) -> DataPathResult<bool> {
        path.notify = path.output.empty();
        return !path.notify;
    });
    if (queued) {
        need(Executor::send(context, detail::pid_of(context), atom(context, "dist_data"_atom)));
    }
    return atom(context, "ok"_atom);
}

// dist_ctrl_get_opt(DHandle, get_size): whether dist_ctrl_get_data/1 gives sizes.
TermResult<Term> dist_ctrl_get_opt(ProcessContext &context, const Handle &handle, const Option &option) {
    const auto known = is_named(option.term, "get_size");
    const auto value = on_controlled(context, handle.term, [known](DataPath &path) -> DataPathResult<bool> {
        if (!known) {
            return std::unexpected(DataPathError::badarg);
        }
        return path.get_size;
    });
    return TermFactory(context).boolean(value);
}

// dist_ctrl_set_opt(DHandle, get_size, Bool): the value before.
TermResult<Term> dist_ctrl_set_opt(ProcessContext &context, const Handle &handle, const Option &option,
                                   const Term &value) {
    const auto on = is_named(value, "true");
    const auto known = is_named(option.term, "get_size") && (on || is_named(value, "false"));
    const auto before = on_controlled(context, handle.term, [known, on](DataPath &path) -> DataPathResult<bool> {
        if (!known) {
            return std::unexpected(DataPathError::badarg);
        }
        return std::exchange(path.get_size, on);
    });
    return TermFactory(context).boolean(before);
}

// A dist handle {Id, Ref} as dist_ctrl_put_data/2 and dist_ctrl_input_handler/2 read it: any small integer id.
struct LooseHandle {
    std::int64_t id = 0;
    std::uint64_t reference = 0;
};

std::optional<LooseHandle> loose_handle(const Term &term) {
    if (!term.is_tuple() || term.tuple_size_unchecked() != 2) {
        return std::nullopt;
    }
    const auto id = small(term.tuple_element_unchecked(0).word());
    const auto reference = detail::reference_number(term.tuple_element_unchecked(1));
    if (!id || !reference) {
        return std::nullopt;
    }
    return LooseHandle{.id = *id, .reference = *reference};
}

// Continue as erts_internal:dist_ctrl_put_data(DHandle, IoList), which puts the list as one binary.
Word put_iodata(ProcessContext &context, const std::array<Word, 2> &arguments) {
    const auto *frame = static_cast<const abi::v1::FrameDescriptor *>(context.code_server().function_frame(
        atom(context, "erts_internal"_atom), atom(context, "dist_ctrl_put_data"_atom), arguments.size()));
    if (!frame) {
        bad_argument();
    }
    context.stack().trap(*frame, arguments);
    return 0;
}

// dist_ctrl_put_data(DHandle, Data): data from the other node, taken from the connection's input handler only and
// counted as received before Data is checked. Ticks (no data) are ok; other data is decoded from DIST5 on.
Word dist_ctrl_put_data(ProcessContext &context, const Term &handle, const Term &data) {
    if (data.is_list()) {
        return put_iodata(context, {handle.word(), data.word()});
    }
    const auto parsed = loose_handle(handle);
    if (!parsed) {
        bad_argument();
    }
    const auto pid = detail::pid_of(context);
    const auto counted = Executor::of(context).with_connections([&](ConnectionTable &table) {
        return table.input_of(table.of_reference(parsed->reference), pid).transform([](detail::Connection *connection) {
            return ++connection->data.received;
        });
    });
    if (!counted) {
        refuse(context, counted.error());
    }
    if (!data.is_binary()) {
        bad_argument();
    }
    return atom(context, "ok"_atom).word();
}

// dist_ctrl_input_handler(DHandle, InputHandler): the controller makes InputHandler, any pid, the process
// dist_ctrl_put_data/2 takes data from. notsup for a handle of no entry, else badarg.
TermResult<Term> dist_ctrl_input_handler(ProcessContext &context, const Term &handle, const Term &handler) {
    const auto parsed = loose_handle(handle);
    const auto pid = detail::pid_of(context);
    const auto input = handler.is_pid() && !handler.is_external_pid() ? handler.word() : Word{0};
    const auto set = Executor::of(context).with_connections([&](ConnectionTable &table) -> DataPathResult<bool> {
        auto *connection = parsed ? table.of_reference(parsed->reference) : nullptr;
        if (connection == nullptr) {
            return std::unexpected(DataPathError::notsup);
        }
        if (!handler.is_pid() || connection->controller != pid || std::cmp_not_equal(connection->id, parsed->id)) {
            return std::unexpected(DataPathError::badarg);
        }
        connection->data.input_handler = input;
        return true;
    });
    if (!set) {
        refuse(context, set.error());
    }
    return atom(context, "ok"_atom);
}

// dist_get_stat(DHandle): {ok, Received, Sent, PendingBytes} of the handle's connection for any caller, zeros while
// it is not up. Badarg for a handle that is not its entry's current one.
TermResult<Term> dist_get_stat(ProcessContext &context, const Term &handle) {
    const auto parsed = dist_handle(handle);
    const auto counts = Executor::of(context).with_connections(
        [&](ConnectionTable &table) -> std::optional<std::array<std::uint64_t, 3>> {
            const auto *connection = parsed ? table.of_handle(*parsed, true) : nullptr;
            if (connection == nullptr) {
                return std::nullopt;
            }
            const auto &path = connection->data;
            return std::array{path.received, path.sent, std::uint64_t{path.queued()}};
        });
    if (!counts) {
        bad_argument();
    }
    TermFactory factory(context);
    const auto count = [&](std::uint64_t value) {
        return need(detail::IntegerAccess::make(context.heap(), detail::Integer{value}));
    };
    return factory.tuple(
        std::array{need(factory.atom("ok"_atom)), count((*counts)[0]), count((*counts)[1]), count((*counts)[2])});
}

constexpr std::array CONNECTION_BUILTINS{
    typed_entry<new_connection>("erts_internal", "new_connection"),
    typed_entry<abort_pending_connection>("erts_internal", "abort_pending_connection"),
    typed_entry<create_dist_channel>("erts_internal", "create_dist_channel"),
    typed_entry<get_dflags>("erts_internal", "get_dflags"),
    typed_entry<setnode3>("erlang", "setnode"),
    typed_entry<dist_ctrl_get_data>("erlang", "dist_ctrl_get_data"),
    typed_entry<dist_ctrl_get_data_notification>("erlang", "dist_ctrl_get_data_notification"),
    typed_entry<dist_ctrl_get_opt>("erlang", "dist_ctrl_get_opt"),
    typed_entry<dist_ctrl_set_opt>("erlang", "dist_ctrl_set_opt"),
    typed_entry<dist_ctrl_put_data>("erlang", "dist_ctrl_put_data"),
    typed_entry<dist_ctrl_input_handler>("erlang", "dist_ctrl_input_handler"),
    typed_entry<dist_get_stat>("erlang", "dist_get_stat"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> connection_builtins() noexcept { return builtins::CONNECTION_BUILTINS; }
} // namespace clause::runtime
