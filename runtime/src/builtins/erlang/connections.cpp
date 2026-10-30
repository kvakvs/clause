#include "../../process/exits.hpp"
#include "../../scheduler/executor.hpp"
#include "../../terms/identities.hpp"
#include "../../terms/integers.hpp"
#include "../typed.hpp"
#include <array>
#include <clause/runtime/output.hpp>
#include <cstdint>
#include <limits>
#include <string>

// Connection entries of other nodes (plan 30 step DIST2b, docs/distribution.md#connections): OTP's process-controller
// interface erts_internal:new_connection/1, abort_pending_connection/2, create_dist_channel/3, get_dflags/0 and
// erlang:setnode/3, over the executor's connection table.
namespace clause::runtime::builtins {
namespace {
using detail::Executor;
using Node = Named<struct NodeRole>;
using Controller = Named<struct ControllerRole>;
using Options = Named<struct OptionsRole>;

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

constexpr std::array CONNECTION_BUILTINS{
    typed_entry<new_connection>("erts_internal", "new_connection"),
    typed_entry<abort_pending_connection>("erts_internal", "abort_pending_connection"),
    typed_entry<create_dist_channel>("erts_internal", "create_dist_channel"),
    typed_entry<get_dflags>("erts_internal", "get_dflags"),
    typed_entry<setnode3>("erlang", "setnode"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> connection_builtins() noexcept { return builtins::CONNECTION_BUILTINS; }
} // namespace clause::runtime
