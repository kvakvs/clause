#include "../typed.hpp"
#include "terms.hpp"
#include <array>

// nodes/0,1,2, is_alive/0, monitor_node/2,3 and disconnect_node/1 (plan step 41H, docs/processes.md#nodes): the
// answers of an OTP node that is not distributed. Only the local node, nonode@nohost, exists; monitoring another
// node raises notalive, disconnecting returns ignored.
namespace clause::runtime::builtins {
namespace {
// The name of the only node.
constexpr std::string_view LOCAL_NODE = "nonode@nohost";

Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

// Whether a node type of nodes/1,2 includes this node: this or known; any other valid type has no members.
bool includes_local(const Term &type) {
    static constexpr std::array<std::string_view, 5> TYPES{"visible", "hidden", "connected", "this", "known"};
    const auto name = type.is_atom() ? need(type.atom_spelling()) : std::string_view{};
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
    for (auto rest = types; !rest.is_nil(); rest = need(rest.tail())) {
        if (!rest.is_cons()) {
            bad_argument();
        }
        listed = includes_local(need(rest.head())) || listed;
    }
    return listed;
}

// [nonode@nohost] when listed, else [].
TermResult<Term> local_list(ProcessContext &context, bool listed) {
    std::vector<Term> items;
    if (listed) {
        items.push_back(atom(context, LOCAL_NODE));
    }
    return TermFactory(context).list(items);
}

TermResult<Term> nodes0(ProcessContext &context) { return local_list(context, false); }

TermResult<Term> nodes1(ProcessContext &context, const Term &types) { return local_list(context, local_listed(types)); }

// One {Key, Value} of nodes/2's info map for this node: connection_id is undefined and node_type this; none
// when the option is false. Badarg for any other key or a value that is not a boolean.
std::optional<std::pair<Term, Term>> info_entry(ProcessContext &context, const std::pair<Term, Term> &option) {
    const auto &[key, value] = option;
    const auto name = key.is_atom() ? need(key.atom_spelling()) : std::string_view{};
    if ((name != "connection_id" && name != "node_type") || !value.is_boolean()) {
        bad_argument();
    }
    if (!need(value.boolean_value())) {
        return std::nullopt;
    }
    return std::pair{key, atom(context, name == "node_type" ? "this" : "undefined")};
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

// nodes(Types, InfoOptions): [{nonode@nohost, Info}] when this node is listed.
TermResult<Term> nodes2(ProcessContext &context, const Term &types, const MapArgument &options) {
    const auto info = local_info(context, options);
    std::vector<Term> items;
    if (local_listed(types)) {
        const std::array parts{atom(context, LOCAL_NODE), info};
        items.push_back(need(TermFactory(context).tuple(parts)));
    }
    return TermFactory(context).list(items);
}

TermResult<Term> is_alive(ProcessContext &context) { return TermFactory(context).boolean(false); }

// monitor_node(Node, Flag[, Options]): this node is not alive, so error:notalive; badarg for invalid arguments.
TermResult<Term> monitor_node(ProcessContext &context, const AtomArgument &, const Term &flag,
                              const ListArgument *options) {
    if (!flag.is_boolean()) {
        bad_argument();
    }
    if (options) {
        for (const auto &option : options->elements) {
            if (!option.is_atom() || need(option.atom_spelling()) != "allow_passive_connect") {
                bad_argument();
            }
        }
    }
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = atom(context, "notalive").word()};
}

TermResult<Term> monitor_node2(ProcessContext &context, const AtomArgument &node, const Term &flag) {
    return monitor_node(context, node, flag, nullptr);
}

TermResult<Term> monitor_node3(ProcessContext &context, const AtomArgument &node, const Term &flag,
                               const ListArgument &options) {
    return monitor_node(context, node, flag, &options);
}

// disconnect_node(Node): ignored, since this node is not alive.
TermResult<Term> disconnect_node(ProcessContext &context, const Term &) { return TermFactory(context).atom("ignored"); }

constexpr std::array NODE_BUILTINS{
    typed_entry<nodes0>("erlang", "nodes"),
    typed_entry<nodes1>("erlang", "nodes"),
    typed_entry<nodes2>("erlang", "nodes"),
    typed_entry<is_alive>("erlang", "is_alive"),
    typed_entry<monitor_node2>("erlang", "monitor_node"),
    typed_entry<monitor_node3>("erlang", "monitor_node"),
    typed_entry<disconnect_node>("erlang", "disconnect_node"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> node_builtins() noexcept { return builtins::NODE_BUILTINS; }
} // namespace clause::runtime
