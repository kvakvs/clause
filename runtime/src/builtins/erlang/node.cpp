#include "../../memory/heap_object.hpp"
#include "../../memory/persistent_terms.hpp"
#include "../../scheduler/executor.hpp"
#include "../../terms/identities.hpp"
#include "../typed.hpp"
#include "hashing.hpp"
#include "terms.hpp"
#include <algorithm>
#include <array>
#include <clause/runtime/code_server.hpp>
#include <cstdint>
#include <string_view>

// nodes/0,1,2, monitor_node/2,3, disconnect_node/1 and the cookies (plan step 41H, docs/processes.md#nodes), and
// this node's name: erlang:setnode/2 and erts_internal:get_creation/0 (plan 30 step DIST2a,
// docs/distribution.md#this-node). nodes/0,1,2 list the connection entries (plan 30 step DIST2b) and monitor_node/2,3
// monitors them (DIST2c); disconnecting returns ignored.
namespace clause::runtime::builtins {
namespace {
// The name of a node without one, which setnode/2 refuses.
constexpr std::string_view UNNAMED_NODE = "nonode@nohost";
// The least creation setnode/2 gives this node, as OTP's.
constexpr std::uint32_t MIN_CREATION = 4;

// What nodes/1,2 list: the table's kinds and whether this node is listed.
struct Listing final {
    detail::NodeKinds kinds;
    bool local = false;
};

// Add one node type of nodes/1,2 to a listing; badarg for an invalid one.
void add_type(const Term &type, Listing &listing) {
    const auto name = type.is_atom() ? type.atom_spelling_unchecked() : std::string_view{};
    auto &kinds = listing.kinds;
    if (name == "visible" || name == "connected") {
        kinds.visible = true;
    }
    if (name == "hidden" || name == "connected") {
        kinds.hidden = true;
    }
    if (name == "known") {
        kinds = {.visible = true, .hidden = true, .known = true};
    }
    if (name == "this" || name == "known") {
        listing.local = true;
    }
    static constexpr std::array<std::string_view, 5> TYPES{"visible", "hidden", "connected", "this", "known"};
    if (!std::ranges::contains(TYPES, name)) {
        bad_argument();
    }
}

// The listing of a node type or list of types; badarg for an invalid one.
Listing listing_of(const Term &types) {
    Listing listing;
    if (!types.is_list()) {
        add_type(types, listing);
        return listing;
    }
    for (auto rest = types; !rest.is_nil(); rest = rest.tail_unchecked()) {
        if (!rest.is_cons()) {
            bad_argument();
        }
        add_type(rest.head_unchecked(), listing);
    }
    return listing;
}

// One node nodes/1,2 lists: its name, its kind (visible, hidden, known or this) and its connection id while up.
struct Listed final {
    Term node;
    AtomName kind;
    std::optional<std::uint32_t> id;
};

// The node_type of a table entry nodes/2 reports.
AtomName kind_name(detail::ListedNode::Kind kind) {
    using Kind = detail::ListedNode::Kind;
    return kind == Kind::visible ? AtomName{"visible"_atom}
                                 : AtomName{kind == Kind::hidden ? "hidden"_atom : "known"_atom};
}

// The nodes of a listing in OTP's order: the table's entries, then this node.
std::vector<Listed> listed(ProcessContext &context, const Listing &listing) {
    std::vector<Listed> nodes;
    for (const auto &entry : detail::Executor::of(context).list_nodes(listing.kinds)) {
        nodes.push_back({need(Term::from_word(entry.node, context)), kind_name(entry.kind), entry.id});
    }
    if (listing.local) {
        nodes.push_back({need(detail::this_node_atom(context)), "this"_atom, std::nullopt});
    }
    return nodes;
}

TermResult<Term> node_names(ProcessContext &context, const Listing &listing) {
    std::vector<Term> items;
    for (const auto &node : listed(context, listing)) {
        items.push_back(node.node);
    }
    return TermFactory(context).list(items);
}

TermResult<Term> nodes0(ProcessContext &context) { return node_names(context, {.kinds = {.visible = true}}); }

TermResult<Term> nodes1(ProcessContext &context, const Term &types) { return node_names(context, listing_of(types)); }

// Whether erts_internal:dynamic_node_name(true) stored {erts_internal, dynamic_node_name} as a persistent term with a
// value other than false.
bool dynamic_name(ProcessContext &context) {
    const std::array parts{atom(context, "erts_internal"_atom), atom(context, "dynamic_node_name"_atom)};
    const auto key = need(TermFactory(context).tuple(parts));
    const auto same = [&key](Word stored) { return need(detail::TermAccess::make(stored).exactly_equal(key)); };
    const auto value = context.persistent_terms().find(term_hash(context, key), same);
    return value && detail::TermAccess::make(*value).word() != atom(context, "false"_atom).word();
}

// is_alive(): whether this node has a name or a dynamic name, as OTP's (plan 30 step DIST2a).
TermResult<Term> is_alive(ProcessContext &context) {
    return TermFactory(context).boolean(detail::this_node_named() || dynamic_name(context));
}

// Which keys nodes/2's info maps hold: connection_id and node_type. Badarg for any other key or a value that is not a
// boolean.
std::pair<bool, bool> info_keys(const MapArgument &options) {
    std::pair<bool, bool> keys;
    for (const auto &[key, value] : need(options.term.map_entries())) {
        const auto name = key.is_atom() ? key.atom_spelling_unchecked() : std::string_view{};
        if ((name != "connection_id" && name != "node_type") || !value.is_boolean()) {
            bad_argument();
        }
        (name == "connection_id" ? keys.first : keys.second) = value.boolean_value_unchecked();
    }
    return keys;
}

// nodes(Types, InfoOptions): [{Node, Info}] in nodes/1's order; Info holds the connection id (undefined unless up)
// and the node type when asked.
TermResult<Term> nodes2(ProcessContext &context, const Term &types, const MapArgument &options) {
    const auto listing = listing_of(types);
    const auto [with_id, with_type] = info_keys(options);
    TermFactory factory(context);
    std::vector<Term> items;
    for (const auto &node : listed(context, listing)) {
        std::vector<std::pair<Term, Term>> info;
        if (with_id) {
            const auto id = node.id ? factory.integer(*node.id) : factory.atom("undefined"_atom);
            info.emplace_back(atom(context, "connection_id"_atom), need(id));
        }
        if (with_type) {
            info.emplace_back(atom(context, "node_type"_atom), atom(context, node.kind));
        }
        items.push_back(need(factory.tuple(std::array{node.node, need(factory.map(info))})));
    }
    return factory.list(items);
}

// Whether monitor_node/3's options ask for a passive connect; badarg unless every option is allow_passive_connect.
bool passive_connect(const ListArgument &options) {
    for (const auto &option : options.elements) {
        if (!option.is_atom() || option.atom_spelling_unchecked() != "allow_passive_connect") {
            bad_argument();
        }
    }
    return !options.elements.empty();
}

// Continue as clause_dist:passive_monitor(Node, Flag, Options), which asks net_kernel for a passive connect (plan 30
// step DIST2c).
Word passive_monitor(ProcessContext &context, const std::array<Word, 3> &arguments) {
    const auto *frame = static_cast<const abi::v1::FrameDescriptor *>(context.code_server().function_frame(
        atom(context, "clause_dist"_atom), atom(context, "passive_monitor"_atom), arguments.size()));
    if (!frame) {
        bad_argument();
    }
    context.stack().trap(*frame, arguments);
    return 0;
}

// monitor_node(Node, Flag[, Options]) in OTP's order of checks: badarg for an unknown option, error:notalive for a
// node other than nonode@nohost while this node has no name, badarg for a flag that is not a boolean. Monitoring this
// node does nothing; another node's monitor is held by its connection entry, which auto-connects when idle (plan 30
// step DIST2c, docs/distribution.md#node-monitors). A passive one of a connection that is not up asks net_kernel.
Word monitor_node(ProcessContext &context, const AtomArgument &node, const Term &flag, const ListArgument &options) {
    const auto passive = passive_connect(options);
    if (node.spelling != UNNAMED_NODE && !detail::this_node_named()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                             .payload = atom(context, "notalive"_atom).word()};
    }
    if (!flag.is_boolean()) {
        bad_argument();
    }
    if (!detail::is_this_node(node.term) &&
        !detail::Executor::of(context).monitor_node(context, node.term, flag.boolean_value_unchecked(), passive)) {
        return passive_monitor(context, {node.term.word(), flag.word(), options.term.word()});
    }
    return atom(context, "true"_atom).word();
}

Word monitor_node2(ProcessContext &context, const AtomArgument &node, const Term &flag) {
    return monitor_node(context, node, flag, {.term = need(TermFactory(context).nil()), .elements = {}});
}

Word monitor_node3(ProcessContext &context, const AtomArgument &node, const Term &flag, const ListArgument &options) {
    return monitor_node(context, node, flag, options);
}

// erlang:dmonitor_node(Node, Flag, Options) (plan 30 step DIST2c): with allow_passive_connect among the options it asks
// net_kernel as monitor_node/3 does; otherwise the caller gets {nodedown, Node} at once, whatever the arguments.
Word dmonitor_node(ProcessContext &context, const Term &node, const Term &flag, const ListArgument &options) {
    const auto passive = std::ranges::any_of(options.elements, [](const Term &option) {
        return option.is_atom() && option.atom_spelling_unchecked() == "allow_passive_connect";
    });
    if (passive) {
        return passive_monitor(context, {node.word(), flag.word(), options.term.word()});
    }
    const std::array parts{atom(context, "nodedown"_atom), node};
    need(detail::Executor::send(context, detail::pid_of(context), need(TermFactory(context).tuple(parts))));
    return atom(context, "true"_atom).word();
}

// Whether this node is alive, named or waiting for a dynamic name, so that the auth server keeps its cookies.
bool alive(ProcessContext &context) { return detail::this_node_named() || dynamic_name(context); }

// Continue as auth:Name(Arguments...), the auth server's cookies of an alive node (plan 30 step DIST4b1c,
// docs/distribution.md#cookies).
Word auth_call(ProcessContext &context, const CachedAtom &name, const std::span<const Word> arguments) {
    const auto *frame = static_cast<const abi::v1::FrameDescriptor *>(
        context.code_server().function_frame(atom(context, "auth"_atom), atom(context, name), arguments.size()));
    if (!frame) {
        bad_argument();
    }
    context.stack().trap(*frame, arguments);
    return 0;
}

// get_cookie() and get_cookie(Node): the auth server's cookies, nocookie while this node is not alive (plan 21 step
// LIB9A).
Word get_cookie0(ProcessContext &context) {
    return alive(context) ? auth_call(context, "get_cookie"_atom, {}) : atom(context, "nocookie"_atom).word();
}

Word get_cookie1(ProcessContext &context, const AtomArgument &node) {
    return alive(context) ? auth_call(context, "get_cookie"_atom, std::array{node.term.word()})
                          : atom(context, "nocookie"_atom).word();
}

// set_cookie(Cookie) and set_cookie(Node, Cookie) while this node is not alive: error:distribution_not_started for
// atoms, badarg otherwise.
[[noreturn]] void not_started(ProcessContext &context) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                         .payload = atom(context, "distribution_not_started"_atom).word()};
}

Word set_cookie1(ProcessContext &context, const AtomArgument &cookie) {
    if (!alive(context)) {
        not_started(context);
    }
    return auth_call(context, "set_cookie"_atom, std::array{cookie.term.word()});
}

// set_cookie(Node, Cookie): naming this node is badarg while it is not alive; once alive it sets this node's cookie.
Word set_cookie2(ProcessContext &context, const AtomArgument &node, const AtomArgument &cookie) {
    if (alive(context)) {
        return auth_call(context, "set_cookie"_atom, std::array{node.term.word(), cookie.term.word()});
    }
    if (node.spelling == detail::this_node_name()) {
        bad_argument();
    }
    not_started(context);
}

// disconnect_node(Node): ignored, since this node is not alive.
TermResult<Term> disconnect_node(ProcessContext &context, const Term &) {
    return TermFactory(context).atom("ignored"_atom);
}

// erlang:setnode(Node, Creation) (plan 30 step DIST2a): name this node as OTP's does. Node must be a node name other
// than nonode@nohost and Creation a non-negative small integer, cut to 32 bits and raised to at least 4; a live
// process must be registered as net_kernel and the node must have no name. Badarg otherwise.
TermResult<Term> setnode2(ProcessContext &context, const AtomArgument &node, std::int64_t creation) {
    if (creation < 0 || node.spelling == UNNAMED_NODE || !detail::is_node_name(node.spelling)) {
        bad_argument();
    }
    const auto cut = std::max(static_cast<std::uint32_t>(creation), MIN_CREATION);
    if (!detail::Executor::of(context).name_node(context, node.term, cut)) {
        bad_argument();
    }
    return TermFactory(context).boolean(true);
}

// erts_internal:get_creation(): this node's creation, or undefined while it has no name.
TermResult<Term> get_creation(ProcessContext &context) {
    const auto creation = detail::this_node_creation();
    if (creation == 0) {
        return TermFactory(context).atom("undefined"_atom);
    }
    return TermFactory(context).integer(static_cast<std::int64_t>(creation));
}

constexpr std::array NODE_BUILTINS{
    typed_entry<nodes0>("erlang", "nodes"),
    typed_entry<nodes1>("erlang", "nodes"),
    typed_entry<nodes2>("erlang", "nodes"),
    typed_entry<is_alive>("erlang", "is_alive"),
    typed_entry<monitor_node2>("erlang", "monitor_node"),
    typed_entry<monitor_node3>("erlang", "monitor_node"),
    typed_entry<dmonitor_node>("erlang", "dmonitor_node"),
    typed_entry<disconnect_node>("erlang", "disconnect_node"),
    typed_entry<get_cookie0>("erlang", "get_cookie"),
    typed_entry<get_cookie1>("erlang", "get_cookie"),
    typed_entry<set_cookie1>("erlang", "set_cookie"),
    typed_entry<set_cookie2>("erlang", "set_cookie"),
    typed_entry<setnode2>("erlang", "setnode"),
    typed_entry<get_creation>("erts_internal", "get_creation"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> node_builtins() noexcept { return builtins::NODE_BUILTINS; }
} // namespace clause::runtime
