#include "../../memory/heap_object.hpp"
#include "../../memory/persistent_terms.hpp"
#include "../../scheduler/executor.hpp"
#include "../../terms/identities.hpp"
#include "../typed.hpp"
#include "hashing.hpp"
#include "terms.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

// nodes/0,1,2, monitor_node/2,3, disconnect_node/1 and the cookies (plan step 41H, docs/processes.md#nodes), and
// this node's name: erlang:setnode/2 and erts_internal:get_creation/0 (plan 30 step DIST2a,
// docs/distribution.md#this-node). This node has no connections yet: monitoring another node raises notalive unless
// the node has a name, disconnecting returns ignored.
namespace clause::runtime::builtins {
namespace {
// The name of a node without one, which setnode/2 refuses.
constexpr std::string_view UNNAMED_NODE = "nonode@nohost";
// The least creation setnode/2 gives this node, as OTP's.
constexpr std::uint32_t MIN_CREATION = 4;

// Whether a node type of nodes/1,2 includes this node: this or known; any other valid type has no members.
bool includes_local(const Term &type) {
    static constexpr std::array<std::string_view, 5> TYPES{"visible", "hidden", "connected", "this", "known"};
    const auto name = type.is_atom() ? type.atom_spelling_unchecked() : std::string_view{};
    if (!std::ranges::contains(TYPES, name)) {
        bad_argument();
    }
    return name == "this" || name == "known";
}

// Whether a node type or list of types includes this node; badarg for an invalid one.
bool local_listed(const Term &types) {
    if (!types.is_list()) {
        return includes_local(types);
    }
    bool listed = false;
    for (auto rest = types; !rest.is_nil(); rest = rest.tail_unchecked()) {
        if (!rest.is_cons()) {
            bad_argument();
        }
        listed = includes_local(rest.head_unchecked()) || listed;
    }
    return listed;
}

// [Node] with this node's name when listed, else [].
TermResult<Term> local_list(ProcessContext &context, bool listed) {
    std::vector<Term> items;
    if (listed) {
        items.push_back(need(detail::this_node_atom(context)));
    }
    return TermFactory(context).list(items);
}

TermResult<Term> nodes0(ProcessContext &context) { return local_list(context, false); }

TermResult<Term> nodes1(ProcessContext &context, const Term &types) { return local_list(context, local_listed(types)); }

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

// One {Key, Value} of nodes/2's info map for this node: connection_id is undefined and node_type this; none
// when the option is false. Badarg for any other key or a value that is not a boolean.
std::optional<std::pair<Term, Term>> info_entry(ProcessContext &context, const std::pair<Term, Term> &option) {
    const auto &[key, value] = option;
    const auto name = key.is_atom() ? key.atom_spelling_unchecked() : std::string_view{};
    if ((name != "connection_id" && name != "node_type") || !value.is_boolean()) {
        bad_argument();
    }
    if (!value.boolean_value_unchecked()) {
        return std::nullopt;
    }
    return std::pair{key, atom(context, name == "node_type" ? "this"_atom : "undefined"_atom)};
}

// The info map of nodes/2 for this node, for the keys asked.
Term local_info(ProcessContext &context, const MapArgument &options) {
    std::vector<std::pair<Term, Term>> entries;
    for (const auto &option : need(options.term.map_entries())) {
        if (auto entry = info_entry(context, option)) {
            entries.push_back(*entry);
        }
    }
    return need(TermFactory(context).map(entries));
}

// nodes(Types, InfoOptions): [{Node, Info}] with this node's name when this node is listed.
TermResult<Term> nodes2(ProcessContext &context, const Term &types, const MapArgument &options) {
    const auto info = local_info(context, options);
    std::vector<Term> items;
    if (local_listed(types)) {
        const std::array parts{need(detail::this_node_atom(context)), info};
        items.push_back(need(TermFactory(context).tuple(parts)));
    }
    return TermFactory(context).list(items);
}

// Badarg unless every monitor_node/3 option is allow_passive_connect.
void check_monitor_options(const ListArgument &options) {
    for (const auto &option : options.elements) {
        if (!option.is_atom() || option.atom_spelling_unchecked() != "allow_passive_connect") {
            bad_argument();
        }
    }
}

// monitor_node(Node, Flag[, Options]) in OTP's order of checks: badarg for an unknown option, error:notalive for a
// node other than nonode@nohost while this node has no name, badarg for a flag that is not a boolean. Monitoring this
// node does nothing; another node has no connection, so its monitor ends at once with {nodedown, Node}, as OTP's
// when connecting fails (plan 30 step DIST2a; connecting is DIST2b).
TermResult<Term> monitor_node(ProcessContext &context, const AtomArgument &node, const Term &flag,
                              const ListArgument *options) {
    if (options) {
        check_monitor_options(*options);
    }
    if (node.spelling != UNNAMED_NODE && !detail::this_node_named()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                             .payload = atom(context, "notalive"_atom).word()};
    }
    if (!flag.is_boolean()) {
        bad_argument();
    }
    if (flag.boolean_value_unchecked() && !detail::is_this_node(node.term)) {
        const std::array parts{atom(context, "nodedown"_atom), node.term};
        need(detail::Executor::send(context, detail::pid_of(context), need(TermFactory(context).tuple(parts))));
    }
    return TermFactory(context).boolean(true);
}

TermResult<Term> monitor_node2(ProcessContext &context, const AtomArgument &node, const Term &flag) {
    return monitor_node(context, node, flag, nullptr);
}

TermResult<Term> monitor_node3(ProcessContext &context, const AtomArgument &node, const Term &flag,
                               const ListArgument &options) {
    return monitor_node(context, node, flag, &options);
}

// get_cookie() and get_cookie(Node): nocookie, since this node is not alive (plan 21 step LIB9A).
TermResult<Term> get_cookie0(ProcessContext &context) { return TermFactory(context).atom("nocookie"_atom); }

TermResult<Term> get_cookie1(ProcessContext &context, const AtomArgument & /*node*/) {
    return TermFactory(context).atom("nocookie"_atom);
}

// set_cookie(Cookie) and set_cookie(Node, Cookie): error:distribution_not_started for atoms, badarg otherwise.
[[noreturn]] void not_started(ProcessContext &context) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                         .payload = atom(context, "distribution_not_started"_atom).word()};
}

TermResult<Term> set_cookie1(ProcessContext &context, const AtomArgument & /*cookie*/) { not_started(context); }

// set_cookie(Node, Cookie): the local node's cookie is set with set_cookie/1, so naming it here is badarg.
TermResult<Term> set_cookie2(ProcessContext &context, const AtomArgument &node, const AtomArgument & /*cookie*/) {
    if (node.spelling == detail::this_node_name()) {
        bad_argument();
    }
    not_started(context);
}

// disconnect_node(Node): ignored, since this node is not alive.
TermResult<Term> disconnect_node(ProcessContext &context, const Term &) {
    return TermFactory(context).atom("ignored"_atom);
}

// Whether a character may be in the name part of a node name: a letter, a digit, '-' or '_'.
bool name_char(char c) {
    return c == '-' || c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// Whether a spelling is a node name as OTP's is_node_name checks: name characters before an '@', then a host without
// another '@'.
bool node_name(std::string_view spelling) {
    const auto at = spelling.find('@');
    if (at == 0 || at == std::string_view::npos || at + 1 == spelling.size()) {
        return false;
    }
    return std::ranges::all_of(spelling.substr(0, at), name_char) &&
           spelling.find('@', at + 1) == std::string_view::npos;
}

// erlang:setnode(Node, Creation) (plan 30 step DIST2a): name this node as OTP's does. Node must be a node name other
// than nonode@nohost and Creation a non-negative small integer, cut to 32 bits and raised to at least 4; a live
// process must be registered as net_kernel and the node must have no name. Badarg otherwise.
TermResult<Term> setnode2(ProcessContext &context, const AtomArgument &node, std::int64_t creation) {
    if (creation < 0 || node.spelling == UNNAMED_NODE || !node_name(node.spelling)) {
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
