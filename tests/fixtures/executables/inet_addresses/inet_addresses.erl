-module(inet_addresses).
-export([main/1]).

%% Addresses as text in Clause's inet module (plan step 41R): relaxed and
%% strict IPv4 and IPv6 parsing, ntoa/1 and the address tests, compared
%% exactly; host lookups depend on the machine and are checked for shape.
main([]) ->
    io:format("~p~n", [[parsed(Text) || Text <- texts()]]),
    io:format("~p~n", [[inet:ntoa(Address) || Address <- addresses()]]),
    io:format("~p~n", [
        [
            {inet:is_ipv4_address(A), inet:is_ipv6_address(A), inet:is_ip_address(A)}
         || A <- addresses()
        ]
    ]),
    io:format("~p~n", [errors()]),
    io:format("~p~n", [lookups()]),
    io:format("inet_addresses: ok~n").

texts() ->
    [
        "127.0.0.1",
        "1.2.3",
        "1.2",
        "16909060",
        "0x7f.1",
        "0X7F.0.0.1",
        "0177.0.0.1",
        "00.1.1.1",
        "08.1.1.1",
        "1.2.3.4.5",
        "1.2.3.",
        ".1.2.3",
        "256.1.1.1",
        "1.2.3.256",
        "4294967295",
        "4294967296",
        "01.02.03.04",
        "1.2.3.04",
        "0.0.0.0",
        "",
        "::",
        "::1",
        "1::",
        "1::2",
        "1:2:3:4:5:6:7:8",
        "1:2:3:4:5:6:7:8:9",
        "1:2:3:4:5:6:7",
        "1::2::3",
        "FFFF::abCD",
        "12345::",
        "::ffff:1.2.3.4",
        "::1.2.3.4",
        "1:2:3:4:5:6:1.2.3.4",
        "1:2:3:4:5:6:7:1.2.3.4",
        "::1.2.3",
        "fe80::1%01",
        "fe80::1%0",
        "fe80::1%",
        "fe80::1%eth0",
        "ff02::2%05",
        "fe80:5::1%01",
        "2001:db8::1%01",
        "fe80::1%065536",
        "::%01",
        "1:2:3:4:5:6:7:8%01",
        ":1",
        "1:",
        "g::"
    ].

%% Every parser's answer for one text.
parsed(Text) ->
    {Text, inet:parse_address(Text), inet:parse_strict_address(Text), inet:parse_ipv4_address(Text),
        inet:parse_ipv4strict_address(Text), inet:parse_ipv6_address(Text),
        inet:parse_ipv6strict_address(Text), inet:parse_address(Text, inet6),
        inet:parse_strict_address(Text, inet)}.

addresses() ->
    [
        {127, 0, 0, 1},
        {255, 255, 255, 255},
        {256, 0, 0, 1},
        {0, 0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0, 1},
        {0, 0, 0, 0, 0, 0, 16#102, 16#304},
        {0, 0, 0, 0, 0, 0, 0, 2},
        {0, 0, 0, 0, 0, 16#ffff, 16#102, 16#304},
        {1, 0, 0, 0, 0, 0, 0, 1},
        {1, 0, 0, 2, 0, 0, 0, 3},
        {1, 0, 0, 2, 0, 0, 3, 4},
        {1, 0, 2, 0, 3, 0, 4, 0},
        {16#2001, 16#db8, 0, 0, 0, 0, 0, 16#ABCD},
        {16#fe80, 1, 0, 0, 0, 0, 0, 1},
        {16#fe80, 0, 0, 0, 0, 0, 0, 1},
        {16#ff02, 5, 0, 0, 0, 0, 0, 2},
        {1, 2, 3, 4, 5, 6, 7, 8},
        {65536, 0, 0, 0, 0, 0, 0, 0},
        {a, b, c, d},
        {1, 2, 3},
        not_an_address
    ].

errors() ->
    [
        inet:parse_address(x),
        inet:parse_strict_address(x),
        inet:parse_ipv6strict_address(x),
        outcome(fun() -> inet:parse_ipv4_address(x) end),
        outcome(fun() -> inet:parse_ipv4strict_address(x) end),
        outcome(fun() -> inet:parse_address("1.2.3.4", other) end)
    ].

lookups() ->
    {ok, Name} = inet:gethostname(),
    {ok, Host} = inet:gethostbyname("localhost"),
    [
        is_list(Name) andalso Name =/= "",
        inet:getaddr("localhost", inet),
        inet:getaddr({1, 2, 3, 4}, inet),
        inet:getaddr({1, 2, 3, 4}, inet6),
        inet:getaddrs({0, 0, 0, 0, 0, 0, 0, 1}, inet6),
        inet:getaddr(localhost, inet),
        inet:getaddrs("localhost", inet),
        inet:getaddr("nonexistent.invalid", inet),
        inet:getaddr("localhost", other),
        element(1, Host),
        element(4, Host),
        element(5, Host),
        lists:member({127, 0, 0, 1}, element(6, Host))
    ].

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.
