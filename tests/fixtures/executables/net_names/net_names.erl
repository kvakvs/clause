-module(net_names).
-moduledoc "net's name, service and interface lookups and inet's over them (plan 21 LIB60).".
-export([main/1]).

main(_) ->
    hosts(),
    services(),
    interfaces(),
    inet_lookups(),
    io:format("net_names: ok~n").

hosts() ->
    {ok, Name} = net:gethostname(),
    show(gethostname, is_list(Name) andalso Name =/= ""),
    {ok, Infos} = net:getaddrinfo("localhost"),
    show(
        localhost_has_ipv4,
        lists:member({127, 0, 0, 1}, [
            Address
         || #{addr := #{family := inet, addr := Address}} <- Infos
        ])
    ),
    show(info_keys, lists:usort([lists:sort(maps:keys(Info)) || Info <- Infos])),
    show(numeric, numeric(net:getaddrinfo("127.0.0.1", "80"))),
    show(
        service_only,
        lists:member(
            {inet, 80, {127, 0, 0, 1}}, element(2, numeric(net:getaddrinfo(undefined, "80")))
        )
    ),
    show(unknown_host, element(1, net:getaddrinfo("no-such-host.invalid"))),
    show(unknown_service, net:getaddrinfo("localhost", "no-such-service")),
    [
        show({getaddrinfo, Bad}, catch_error(fun() -> net:getaddrinfo(id(Bad)) end))
     || Bad <- [localhost, 1, undefined]
    ],
    show(both_undefined, catch_error(fun() -> net:getaddrinfo(id(undefined), id(undefined)) end)),
    Loopback = #{family => inet, addr => {127, 0, 0, 1}, port => 80},
    show(numeric_name, net:getnameinfo(Loopback, [numerichost, numericserv])),
    show(service_name, net:getnameinfo(Loopback, [numerichost])),
    show(
        ipv6_name,
        net:getnameinfo(#{family => inet6, addr => {0, 0, 0, 0, 0, 0, 0, 1}, port => 443}, [
            numerichost
        ])
    ),
    show(
        datagram_name,
        net:getnameinfo(#{family => inet, addr => loopback, port => 53}, [dgram, numerichost])
    ),
    {ok, #{host := Host}} = net:getnameinfo(Loopback),
    show(reverse_name, is_list(Host)),
    show(bad_flag, catch_error(fun() -> net:getnameinfo(Loopback, [id(bogus)]) end)),
    show(bad_address, catch_error(fun() -> net:getnameinfo(id(x)) end)).

% The family, port and address of each address info, sorted without repeats.
numeric({ok, Infos}) ->
    {ok,
        lists:usort([
            {Family, Port, Address}
         || #{family := Family, addr := #{port := Port, addr := Address}} <- Infos
        ])};
numeric(Error) ->
    Error.

services() ->
    show(by_name, [net:getservbyname(Name) || Name <- ["http", http]]),
    show(by_name_protocol, [net:getservbyname(http, tcp), net:getservbyname("domain", udp)]),
    show(unknown_name, net:getservbyname("no-such-service")),
    show(name_string_protocol, catch_error(fun() -> net:getservbyname("http", id("tcp")) end)),
    show(by_port, [net:getservbyport(80), net:getservbyport(80, tcp), net:getservbyport(53, udp)]),
    show(unknown_port, [net:getservbyport(Port) || Port <- [61234, -1]]),
    show(port_string_protocol, catch_error(fun() -> net:getservbyport(80, id("tcp")) end)),
    show(inet_by_port, [
        inet:getservbyport(80, tcp), inet:getservbyport(53, udp), inet:getservbyport(61234, tcp)
    ]),
    show(inet_by_name, [
        inet:getservbyname(http, tcp),
        inet:getservbyname("domain", udp),
        inet:getservbyname(no_such, tcp)
    ]).

interfaces() ->
    {ok, Names} = net:if_names(),
    show(
        if_names,
        Names =/= [] andalso
            lists:all(fun({Index, Name}) -> is_integer(Index) andalso is_list(Name) end, Names)
    ),
    [{Index, Name} | _] = Names,
    show(index_to_name, net:if_index2name(Index) =:= {ok, Name}),
    show(name_to_index, net:if_name2index(Name) =:= {ok, Index}),
    show(unknown_index, net:if_index2name(99999)),
    show(unknown_name, net:if_name2index("no-such-interface")),
    {ok, All} = net:getifaddrs(all),
    show(
        entry_keys,
        lists:all(fun(#{name := N, flags := Flags}) -> is_list(N) andalso is_list(Flags) end, All)
    ),
    show(loopback_entry, lists:any(fun is_loopback/1, All)),
    Families = fun(Entries) -> lists:usort([F || #{addr := #{family := F}} <- Entries]) end,
    {ok, Inet} = net:getifaddrs(inet),
    show(inet_families, Families(Inet)),
    {ok, Inet6} = net:getifaddrs(#{family => inet6}),
    show(inet6_families, Families(Inet6) -- [inet6]),
    {ok, Default} = net:getifaddrs(),
    show(default_families, Families(Default) -- [inet, inet6, packet, link]),
    {ok, Loopbacks} = net:getifaddrs(#{family => inet, flags => [loopback]}),
    show(loopback_filter, Loopbacks =/= [] andalso lists:all(fun is_loopback/1, Loopbacks)),
    {ok, Kept} = net:getifaddrs(fun(Entry) -> is_loopback(Entry) end),
    show(fun_filter, Kept =/= [] andalso lists:all(fun is_loopback/1, Kept)),
    {ok, Hardware} = net:getifaddrs(hwaddr),
    show(hwaddr_families, Families(Hardware) -- [packet, link]),
    show(bad_filter, catch_error(fun() -> net:getifaddrs(id(bogus)) end)).

% A loopback interface's inet entry (WSL also gives lo an address outside 127.0.0.0/8).
is_loopback(#{addr := #{family := inet}, flags := Flags}) ->
    lists:member(loopback, Flags);
is_loopback(_) ->
    false.

inet_lookups() ->
    {ok, Interfaces} = inet:getifaddrs(),
    show(
        inet_interfaces, lists:all(fun({Name, [{flags, _} | _]}) -> is_list(Name) end, Interfaces)
    ),
    show(inet_loopback, lists:any(fun inet_loopback/1, Interfaces)),
    {ok, Socket} = inet:getifaddrs([{inet_backend, socket}]),
    show(socket_backend_loopback, lists:any(fun inet_loopback/1, Socket)),
    show(inet_bad_option, catch_error(fun() -> inet:getifaddrs(id([bogus])) end)),
    {ok, Hostent} = inet:gethostbyaddr({127, 0, 0, 1}),
    show(gethostbyaddr, setelement(2, Hostent, is_list(element(2, Hostent)))),
    {ok, Text} = inet:gethostbyaddr("127.0.0.1"),
    show(gethostbyaddr_text, setelement(2, Text, is_list(element(2, Text)))),
    {ok, Six} = inet:gethostbyaddr({0, 0, 0, 0, 0, 0, 0, 1}),
    show(gethostbyaddr_ipv6, setelement(2, Six, is_list(element(2, Six)))),
    show(gethostbyaddr_bad, [inet:gethostbyaddr(Bad) || Bad <- ["nonsense", {1, 2, 3}, localhost]]).

inet_loopback({_, Options}) ->
    lists:member({addr, {127, 0, 0, 1}}, Options) andalso
        lists:any(
            fun
                ({flags, Flags}) -> lists:member(loopback, Flags);
                (_) -> false
            end,
            Options
        ).

show(Label, Value) -> io:format("~p ~p~n", [Label, sorted(Value)]).

% Maps as sorted lists, so that the printed key order is OTP's and Clause's alike.
sorted(Map) when is_map(Map) -> lists:sort([{Key, sorted(Value)} || Key := Value <- Map]);
sorted(List) when is_list(List) -> [sorted(Item) || Item <- List];
sorted(Tuple) when is_tuple(Tuple) -> list_to_tuple(sorted(tuple_to_list(Tuple)));
sorted(Term) -> Term.

catch_error(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, Reason}
    end.

id(Value) -> Value.
