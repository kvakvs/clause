-module(net).
-export([
    gethostname/0,
    getaddrinfo/1,
    getaddrinfo/2,
    getnameinfo/1,
    getnameinfo/2,
    getifaddrs/0,
    getifaddrs/1,
    getifaddrs/2,
    getservbyname/1,
    getservbyname/2,
    getservbyport/1,
    getservbyport/2,
    if_name2index/1,
    if_index2name/1,
    if_names/0
]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
-export([
    broadcast/3,
    call/4,
    cast/4,
    debug/1,
    info/0,
    ping/1,
    sleep/1
]).
-include("net.types.hrl").

% The project-owned net module (plan 21 step LIB60, docs/builtins.md#network-names): name, service and interface
% lookups over the clause_net builtins, with prim_net's results and errors. Network namespaces are not supported.

% The host name of this machine.
gethostname() -> clause_net:gethostname().

getaddrinfo(Host) when is_list(Host) -> getaddrinfo(Host, undefined).

% The address infos of Host and Service (strings, one of them undefined).
getaddrinfo(Host, Service) when
    (is_list(Host) orelse Host =:= undefined) andalso
        (is_list(Service) orelse Service =:= undefined) andalso
        not (Host =:= undefined andalso Service =:= undefined)
->
    clause_net:getaddrinfo(text(Host), text(Service)).

getnameinfo(SockAddr) -> getnameinfo(SockAddr, undefined).

% {ok, #{host, service}} of a socket address, with name info Flags.
getnameinfo(SockAddr, Flags) when is_map(SockAddr), is_list(Flags); is_map(SockAddr), Flags =:= undefined ->
    Bits = name_flags(Flags),
    case name_address(SockAddr) of
        {Bytes, Port, Scope} -> clause_net:getnameinfo(Bytes, Port, Scope, Bits);
        error -> {error, einval}
    end.

% The NI_* bits of name info flags; badarg for one that is not, as prim_net raises.
name_flags(undefined) ->
    0;
name_flags(Flags) ->
    Bits = #{namereqd => 1, dgram => 2, nofqdn => 4, numerichost => 8, numericserv => 16, idn => 0},
    lists:foldl(
        fun(Flag, Acc) ->
            case Bits of
                #{Flag := Bit} -> Acc bor Bit;
                _ -> erlang:error(badarg)
            end
        end,
        0,
        Flags
    ).

% {AddressBytes, Port, ScopeId} of an inet or inet6 socket address, or error.
name_address(#{family := inet} = SockAddr) ->
    case {maps:get(addr, SockAddr, any), maps:get(port, SockAddr, 0)} of
        {Address, Port} when is_integer(Port) -> address_bytes(ip(Address, inet), Port, 0);
        _ -> error
    end;
name_address(#{family := inet6} = SockAddr) ->
    case {maps:get(addr, SockAddr, any), maps:get(port, SockAddr, 0), maps:get(scope_id, SockAddr, 0)} of
        {Address, Port, Scope} when is_integer(Port), is_integer(Scope) ->
            address_bytes(ip(Address, inet6), Port, Scope);
        _ ->
            error
    end;
name_address(_) ->
    error.

ip(any, inet) -> {0, 0, 0, 0};
ip(loopback, inet) -> {127, 0, 0, 1};
ip(any, inet6) -> {0, 0, 0, 0, 0, 0, 0, 0};
ip(loopback, inet6) -> {0, 0, 0, 0, 0, 0, 0, 1};
ip(Address, _) -> Address.

address_bytes(Address, Port, Scope) ->
    case clause_socket:address(Address) of
        <<_, Bytes/binary>> -> {Bytes, Port, Scope};
        error -> error
    end.

getifaddrs() -> getifaddrs(default).

% The interface addresses that Filter keeps (a family selector, a filter map or a fun), or of a namespace.
getifaddrs(Filter) when is_atom(Filter) orelse is_map(Filter) ->
    filtered(filter_map(Filter));
getifaddrs(Filter) when is_function(Filter, 1) ->
    filtered(Filter);
getifaddrs(Namespace) when is_list(Namespace) ->
    getifaddrs(default, Namespace).

% Network namespaces are not supported (docs/differences.md).
getifaddrs(Filter, Namespace) when
    (is_atom(Filter) orelse is_map(Filter) orelse is_function(Filter, 1)) andalso is_list(Namespace)
->
    {error, enotsup}.

filter_map(all) -> #{family => all, flags => any};
filter_map(default) -> #{family => default, flags => any};
filter_map(inet) -> #{family => inet, flags => any};
filter_map(inet6) -> #{family => inet6, flags => any};
filter_map(packet) -> #{family => packet, flags => any};
filter_map(link) -> #{family => link, flags => any};
filter_map(hwaddr) -> #{family => [link, packet], flags => any};
filter_map(Map) when is_map(Map) -> maps:merge(filter_map(default), Map).

filtered(Filter) ->
    case clause_net:getifaddrs() of
        {ok, Entries} when is_function(Filter) -> {ok, lists:filtermap(Filter, Entries)};
        {ok, Entries} -> {ok, [Entry || Entry <- Entries, keep(Filter, Entry)]};
        Error -> Error
    end.

% Whether a filter map keeps an entry: its family selects the entry's address family (default: inet, inet6, packet
% and link; all: every entry, with an address or not) and the entry has every flag it names.
keep(#{family := all, flags := Flags}, #{flags := Have}) ->
    has_flags(Flags, Have);
keep(#{family := Family, flags := Flags}, #{addr := #{family := Have}, flags := HaveFlags}) ->
    selects(Family, Have) andalso has_flags(Flags, HaveFlags);
keep(_, _) ->
    false.

selects(default, Family) -> lists:member(Family, [inet, inet6, link, packet]);
selects(Families, Family) when is_list(Families) -> lists:member(Family, Families);
selects(Family, Family) -> lists:member(Family, [inet, inet6, link, packet]);
selects(_, _) -> false.

has_flags(any, _) -> true;
has_flags(Flags, Have) -> Flags -- Have =:= [].

getservbyname(Name) -> getservbyname(Name, any).

% The port number of a service (an atom or a string) for Protocol (any, tcp, udp, ...).
getservbyname(Name, Protocol) when is_atom(Name) ->
    getservbyname(atom_to_list(Name), Protocol);
getservbyname(Name, Protocol) when is_list(Name) andalso is_atom(Protocol) ->
    clause_net:getservbyname(text(Name), protocol(Protocol)).

getservbyport(PortNumber) -> getservbyport(PortNumber, any).

% The service name of a port number for Protocol.
getservbyport(PortNumber, Protocol) when is_integer(PortNumber) andalso is_atom(Protocol) ->
    clause_net:getservbyport(PortNumber, protocol(Protocol)).

protocol(any) -> undefined;
protocol(Protocol) -> atom_to_binary(Protocol).

% The interface index of a name: {error, enodev} (einval on macOS) for no such interface.
if_name2index(Name) when is_list(Name) ->
    {ok, Names} = if_names(),
    case lists:keyfind(Name, 2, Names) of
        {Index, _} -> {ok, Index};
        false -> {error, unknown_interface()}
    end.

% The reason if_nametoindex(3) gives prim_net for an unknown name: einval on macOS, enodev elsewhere.
unknown_interface() ->
    case os:type() of
        {unix, darwin} -> einval;
        _ -> enodev
    end.

% The interface name of an index: {error, enxio} for no such interface.
if_index2name(Index) when is_integer(Index) ->
    {ok, Names} = if_names(),
    case lists:keyfind(Index, 1, Names) of
        {_, Name} -> {ok, Name};
        false -> {error, enxio}
    end.

% Every interface as {Index, Name}, ordered by index.
if_names() -> clause_net:if_names().

% A string as UTF-8, or undefined.
text(undefined) ->
    undefined;
text(String) ->
    case unicode:characters_to_binary(String) of
        Bytes when is_binary(Bytes) -> Bytes;
        _ -> erlang:error(badarg)
    end.

% OTP's deprecated forwarders.
call(Node, Module, Function, Arguments) -> rpc:call(Node, Module, Function, Arguments).
cast(Node, Module, Function, Arguments) -> rpc:cast(Node, Module, Function, Arguments).
broadcast(Module, Function, Arguments) -> rpc:eval_everywhere(Module, Function, Arguments).
ping(Node) -> net_adm:ping(Node).
sleep(Time) ->
    receive
    after Time -> ok
    end.

debug(Debug) when is_boolean(Debug) -> ok;
debug(Debug) -> erlang:error(badarg, [Debug]).

info() -> erlang:error({not_implemented, info, 0}).
