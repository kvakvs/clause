#include "net.hpp"
#include <algorithm>
#include <array>

// The interface builtins of clause_net (plan 21 step LIB60): the records the operating system's part gathered
// (interfaces_windows.cpp, interfaces_posix.cpp), shaped as inet:getifaddrs/0 and net:getifaddrs/0 answer.
namespace clause::runtime::builtins::net {
namespace {
// The options of one name and flags, as prim_inet merges records.
struct FlagGroup final {
    std::vector<std::string_view> flags;
    std::vector<InetOption> options;
};

Term atoms(ProcessContext &context, const std::vector<std::string_view> &names) {
    std::vector<Term> list;
    list.reserve(names.size());
    for (const auto name : names) {
        list.push_back(need(TermFactory(context).atom(AtomName{name})));
    }
    return need(TermFactory(context).list(list));
}

Term bytes_list(ProcessContext &context, const std::vector<std::uint8_t> &bytes) {
    std::vector<Term> list;
    list.reserve(bytes.size());
    for (const auto byte : bytes) {
        list.push_back(need(TermFactory(context).integer(byte)));
    }
    return need(TermFactory(context).list(list));
}

Term binary_of(ProcessContext &context, const std::vector<std::uint8_t> &bytes) {
    std::vector<std::byte> data(bytes.size());
    std::ranges::transform(bytes, data.begin(), [](std::uint8_t b) { return static_cast<std::byte>(b); });
    return need(TermFactory(context).binary(data));
}

Term pair(ProcessContext &context, std::string_view name, const Term &value) {
    const std::array parts{need(TermFactory(context).atom(AtomName{name})), value};
    return need(TermFactory(context).tuple(parts));
}

// The options of one flags group: {flags, Flags}, the addresses in order and the hardware addresses last.
void group_options(ProcessContext &context, const FlagGroup &group, std::vector<Term> &options) {
    options.push_back(pair(context, "flags", atoms(context, group.flags)));
    for (const bool hardware : {false, true}) {
        for (const auto &option : group.options) {
            if (option.name == "hwaddr" && hardware) {
                options.push_back(pair(context, option.name, bytes_list(context, option.hardware)));
            } else if (option.name != "hwaddr" && !hardware) {
                options.push_back(pair(context, option.name, address_tuple(context, option.address)));
            }
        }
    }
}

// A socket address of net:getifaddrs/0 as its map (or binary).
Term net_address(ProcessContext &context, const NetAddress &address) {
    if (address.kind == NetAddress::Kind::ip) {
        return sockaddr_map(context, address.ip, 0);
    }
    if (address.kind == NetAddress::Kind::bytes) {
        return binary_of(context, address.data);
    }
    TermFactory factory(context);
    std::vector<std::pair<Term, Term>> entries{
        {atom(context, "family"_atom),
         atom(context, address.kind == NetAddress::Kind::packet ? "packet"_atom : "link"_atom)},
        {atom(context, address.kind == NetAddress::Kind::packet ? "addr"_atom : "data"_atom),
         binary_of(context, address.data)},
    };
    for (const auto &[name, value] : address.fields) {
        entries.emplace_back(need(factory.atom(AtomName{name})), need(factory.integer(value)));
    }
    for (const auto &[name, value] : address.names) {
        entries.emplace_back(need(factory.atom(AtomName{name})), need(factory.atom(AtomName{value})));
    }
    return need(factory.map(entries));
}

Term net_entry(ProcessContext &context, const NetEntry &entry) {
    std::vector<std::pair<Term, Term>> entries{
        {atom(context, "name"_atom), chars(context, entry.name)},
        {atom(context, "flags"_atom), atoms(context, entry.flags)},
    };
    const std::array optional{std::pair{std::string_view("addr"), &entry.addr},
                              std::pair{std::string_view("netmask"), &entry.netmask},
                              std::pair{std::string_view("broadaddr"), &entry.broadaddr},
                              std::pair{std::string_view("dstaddr"), &entry.dstaddr}};
    for (const auto &[name, address] : optional) {
        if (*address) {
            entries.emplace_back(need(TermFactory(context).atom(AtomName{name})), net_address(context, **address));
        }
    }
    return need(TermFactory(context).map(entries));
}
} // namespace

// clause_net:inet_getifaddrs(): {ok, [{Name, Options}]} as inet:getifaddrs/0, each name once and its records merged
// by flags in the order the system lists them.
Term inet_getifaddrs(ProcessContext &context) {
    std::string failure;
    const auto records = inet_records(failure);
    if (!failure.empty()) {
        return error(context, failure);
    }
    std::vector<std::pair<std::string, std::vector<FlagGroup>>> names;
    for (const auto &record : records) {
        auto found = std::ranges::find(names, record.name, &std::pair<std::string, std::vector<FlagGroup>>::first);
        if (found == names.end()) {
            found = names.insert(names.end(), {record.name, {}});
        }
        auto group = std::ranges::find(found->second, record.flags, &FlagGroup::flags);
        if (group == found->second.end()) {
            group = found->second.insert(found->second.end(), FlagGroup{record.flags, {}});
        }
        group->options.insert(group->options.end(), record.options.begin(), record.options.end());
    }
    std::vector<Term> interfaces;
    for (const auto &[name, groups] : names) {
        std::vector<Term> options;
        for (const auto &group : groups) {
            group_options(context, group, options);
        }
        const std::array parts{chars(context, name), need(TermFactory(context).list(options))};
        interfaces.push_back(need(TermFactory(context).tuple(parts)));
    }
    return ok(context, need(TermFactory(context).list(interfaces)));
}

// clause_net:getifaddrs(): {ok, [IfAddrs]} as net:getifaddrs(all).
Term getifaddrs(ProcessContext &context) {
    std::string failure;
    const auto entries = net_entries(failure);
    if (!failure.empty()) {
        return error(context, failure);
    }
    std::vector<Term> list;
    list.reserve(entries.size());
    for (const auto &entry : entries) {
        list.push_back(net_entry(context, entry));
    }
    return ok(context, need(TermFactory(context).list(list)));
}

// clause_net:if_names(): {ok, [{Index, Name}]} ordered by index.
Term if_names(ProcessContext &context) {
    auto names = interface_names();
    std::ranges::sort(names, {}, &std::pair<std::uint32_t, std::string>::first);
    std::vector<Term> list;
    for (const auto &[index, name] : names) {
        const std::array parts{need(TermFactory(context).integer(index)), chars(context, name)};
        list.push_back(need(TermFactory(context).tuple(parts)));
    }
    return ok(context, need(TermFactory(context).list(list)));
}
} // namespace clause::runtime::builtins::net
