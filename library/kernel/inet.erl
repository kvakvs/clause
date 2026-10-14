-module(inet).
-export([port/1, sockname/1, peername/1, setopts/2, close/1]).
%% OTP 29 exports Clause does not implement yet: stubs raising error({not_implemented, F, A}),
%% or catalog builtins the runtime puts in their place (docs/library.md#runtime-module-replacements).
-export([
    cancel_monitor/1,
    connect_options/2,
    ensure_sockaddr/1,
    fdopen/6,
    format_error/1,
    gen_tcp_module/1,
    gen_udp_module/1,
    get_rc/0,
    getaddr/2,
    getaddr/3,
    getaddr_tm/3,
    getaddrs/2,
    getaddrs/3,
    getaddrs_tm/3,
    getfd/1,
    gethostbyaddr/1,
    gethostbyaddr/2,
    gethostbyaddr_tm/2,
    gethostbyname/1,
    gethostbyname/2,
    gethostbyname/3,
    gethostbyname_self/2,
    gethostbyname_string/2,
    gethostbyname_tm/3,
    gethostname/0,
    gethostname/1,
    getif/0,
    getif/1,
    getifaddrs/0,
    getifaddrs/1,
    getiflist/0,
    getiflist/1,
    getll/1,
    getopts/2,
    getservbyname/2,
    getservbyport/2,
    getstat/1,
    getstat/2,
    i/0,
    i/1,
    i/2,
    ifget/2,
    ifget/3,
    ifset/2,
    ifset/3,
    inet_backend/0,
    info/1,
    ip/1,
    ipv4_mapped_ipv6_address/1,
    is_ip_address/1,
    is_ipv4_address/1,
    is_ipv6_address/1,
    listen_options/2,
    lock_socket/2,
    monitor/1,
    ntoa/1,
    open/8,
    open_bind/8,
    options/0,
    parse_address/1,
    parse_address/2,
    parse_ipv4_address/1,
    parse_ipv4strict_address/1,
    parse_ipv6_address/1,
    parse_ipv6strict_address/1,
    parse_strict_address/1,
    parse_strict_address/2,
    peernames/1,
    peernames/2,
    popf/1,
    pushf/3,
    sctp_module/1,
    sctp_options/2,
    send/2,
    setpeername/2,
    setsockname/2,
    socket_to_list/1,
    socknames/1,
    socknames/2,
    start_timer/1,
    stats/0,
    stop_timer/1,
    tcp_close/1,
    tcp_controlling_process/2,
    tcp_module/1,
    tcp_module/2,
    timeout/1,
    timeout/2,
    translate_ip/2,
    udp_close/1,
    udp_controlling_process/2,
    udp_module/1,
    udp_module/2,
    udp_options/2
]).
-include("inet.types.hrl").

% The project-owned subset of inet (docs/library.md, docs/ports.md#sockets) for sockets of gen_tcp and gen_udp.

% The local port number of Socket.
port(Socket) ->
    case sockname(Socket) of
        {ok, {_, Port}} -> {ok, Port};
        Error -> Error
    end.

sockname(Socket) -> clause_socket:names(Socket, false).

peername(Socket) -> clause_socket:names(Socket, true).

% Change active, packet and binary or list; other options are accepted and not applied.
setopts(Socket, Options) -> clause_socket:setopts(Socket, Options).

close(Socket) -> clause_socket:close(Socket).

cancel_monitor(_) -> erlang:error({not_implemented, cancel_monitor, 1}).
connect_options(_, _) -> erlang:error({not_implemented, connect_options, 2}).
ensure_sockaddr(_) -> erlang:error({not_implemented, ensure_sockaddr, 1}).
fdopen(_, _, _, _, _, _) -> erlang:error({not_implemented, fdopen, 6}).
format_error(_) -> erlang:error({not_implemented, format_error, 1}).
gen_tcp_module(_) -> erlang:error({not_implemented, gen_tcp_module, 1}).
gen_udp_module(_) -> erlang:error({not_implemented, gen_udp_module, 1}).
get_rc() -> erlang:error({not_implemented, get_rc, 0}).
getaddr(_, _, _) -> erlang:error({not_implemented, getaddr, 3}).
getaddr_tm(_, _, _) -> erlang:error({not_implemented, getaddr_tm, 3}).
getaddrs(_, _, _) -> erlang:error({not_implemented, getaddrs, 3}).
getaddrs_tm(_, _, _) -> erlang:error({not_implemented, getaddrs_tm, 3}).
getfd(_) -> erlang:error({not_implemented, getfd, 1}).
gethostbyaddr(_) -> erlang:error({not_implemented, gethostbyaddr, 1}).
gethostbyaddr(_, _) -> erlang:error({not_implemented, gethostbyaddr, 2}).
gethostbyaddr_tm(_, _) -> erlang:error({not_implemented, gethostbyaddr_tm, 2}).
gethostbyname(_, _, _) -> erlang:error({not_implemented, gethostbyname, 3}).
gethostbyname_self(_, _) -> erlang:error({not_implemented, gethostbyname_self, 2}).
gethostbyname_string(_, _) -> erlang:error({not_implemented, gethostbyname_string, 2}).
gethostbyname_tm(_, _, _) -> erlang:error({not_implemented, gethostbyname_tm, 3}).
gethostname(_) -> erlang:error({not_implemented, gethostname, 1}).
getif() -> erlang:error({not_implemented, getif, 0}).
getif(_) -> erlang:error({not_implemented, getif, 1}).
getifaddrs() -> erlang:error({not_implemented, getifaddrs, 0}).
getifaddrs(_) -> erlang:error({not_implemented, getifaddrs, 1}).
getiflist() -> erlang:error({not_implemented, getiflist, 0}).
getiflist(_) -> erlang:error({not_implemented, getiflist, 1}).
getll(_) -> erlang:error({not_implemented, getll, 1}).
getopts(_, _) -> erlang:error({not_implemented, getopts, 2}).
getservbyname(_, _) -> erlang:error({not_implemented, getservbyname, 2}).
getservbyport(_, _) -> erlang:error({not_implemented, getservbyport, 2}).
getstat(_) -> erlang:error({not_implemented, getstat, 1}).
getstat(_, _) -> erlang:error({not_implemented, getstat, 2}).
i() -> erlang:error({not_implemented, i, 0}).
i(_) -> erlang:error({not_implemented, i, 1}).
i(_, _) -> erlang:error({not_implemented, i, 2}).
ifget(_, _) -> erlang:error({not_implemented, ifget, 2}).
ifget(_, _, _) -> erlang:error({not_implemented, ifget, 3}).
ifset(_, _) -> erlang:error({not_implemented, ifset, 2}).
ifset(_, _, _) -> erlang:error({not_implemented, ifset, 3}).
inet_backend() -> erlang:error({not_implemented, inet_backend, 0}).
info(_) -> erlang:error({not_implemented, info, 1}).
ip(_) -> erlang:error({not_implemented, ip, 1}).
ipv4_mapped_ipv6_address(_) -> erlang:error({not_implemented, ipv4_mapped_ipv6_address, 1}).
listen_options(_, _) -> erlang:error({not_implemented, listen_options, 2}).
lock_socket(_, _) -> erlang:error({not_implemented, lock_socket, 2}).
monitor(_) -> erlang:error({not_implemented, monitor, 1}).
open(_, _, _, _, _, _, _, _) -> erlang:error({not_implemented, open, 8}).
open_bind(_, _, _, _, _, _, _, _) -> erlang:error({not_implemented, open_bind, 8}).
options() -> erlang:error({not_implemented, options, 0}).
peernames(_) -> erlang:error({not_implemented, peernames, 1}).
peernames(_, _) -> erlang:error({not_implemented, peernames, 2}).
popf(_) -> erlang:error({not_implemented, popf, 1}).
pushf(_, _, _) -> erlang:error({not_implemented, pushf, 3}).
sctp_module(_) -> erlang:error({not_implemented, sctp_module, 1}).
sctp_options(_, _) -> erlang:error({not_implemented, sctp_options, 2}).
send(_, _) -> erlang:error({not_implemented, send, 2}).
setpeername(_, _) -> erlang:error({not_implemented, setpeername, 2}).
setsockname(_, _) -> erlang:error({not_implemented, setsockname, 2}).
socket_to_list(_) -> erlang:error({not_implemented, socket_to_list, 1}).
socknames(_) -> erlang:error({not_implemented, socknames, 1}).
socknames(_, _) -> erlang:error({not_implemented, socknames, 2}).
start_timer(_) -> erlang:error({not_implemented, start_timer, 1}).
stats() -> erlang:error({not_implemented, stats, 0}).
stop_timer(_) -> erlang:error({not_implemented, stop_timer, 1}).
tcp_close(_) -> erlang:error({not_implemented, tcp_close, 1}).
tcp_controlling_process(_, _) -> erlang:error({not_implemented, tcp_controlling_process, 2}).
tcp_module(_) -> erlang:error({not_implemented, tcp_module, 1}).
tcp_module(_, _) -> erlang:error({not_implemented, tcp_module, 2}).
timeout(_) -> erlang:error({not_implemented, timeout, 1}).
timeout(_, _) -> erlang:error({not_implemented, timeout, 2}).
translate_ip(_, _) -> erlang:error({not_implemented, translate_ip, 2}).
udp_close(_) -> erlang:error({not_implemented, udp_close, 1}).
udp_controlling_process(_, _) -> erlang:error({not_implemented, udp_controlling_process, 2}).
udp_module(_) -> erlang:error({not_implemented, udp_module, 1}).
udp_module(_, _) -> erlang:error({not_implemented, udp_module, 2}).
udp_options(_, _) -> erlang:error({not_implemented, udp_options, 2}).

% --- Addresses as text and host lookups (plan step 41R) ---

-define(IS_IP4(A, B, C, D), (((A) bor (B) bor (C) bor (D)) band (bnot 16#ff)) =:= 0).
-define(IS_IP6(A, B, C, D, E, F, G, H),
    (((A) bor (B) bor (C) bor (D) bor (E) bor (F) bor (G) bor (H)) band (bnot 16#ffff)) =:= 0
).

is_ipv4_address({A, B, C, D}) when ?IS_IP4(A, B, C, D) -> true;
is_ipv4_address(_) -> false.

is_ipv6_address({A, B, C, D, E, F, G, H}) when ?IS_IP6(A, B, C, D, E, F, G, H) -> true;
is_ipv6_address(_) -> false.

is_ip_address(Address) -> is_ipv4_address(Address) orelse is_ipv6_address(Address).

% An address as text: dotted decimal, or IPv6 with the first longest run of two or more zero fields as "::".
ntoa({A, B, C, D}) when ?IS_IP4(A, B, C, D) ->
    lists:append([
        integer_to_list(A),
        ".",
        integer_to_list(B),
        ".",
        integer_to_list(C),
        ".",
        integer_to_list(D)
    ]);
ntoa({0, 0, 0, 0, 0, 0, 0, 0}) ->
    "::";
ntoa({0, 0, 0, 0, 0, 0, 0, 1}) ->
    "::1";
ntoa({0, 0, 0, 0, 0, 0, A, B}) when ?IS_IP6(0, 0, 0, 0, 0, 0, A, B) ->
    "::" ++ dotted_pair(A) ++ "." ++ dotted_pair(B);
ntoa({0, 0, 0, 0, 0, 16#ffff, A, B}) when ?IS_IP6(0, 0, 0, 0, 0, 16#ffff, A, B) ->
    "::ffff:" ++ dotted_pair(A) ++ "." ++ dotted_pair(B);
ntoa({A, B, C, D, E, F, G, H}) when ?IS_IP6(A, B, C, D, E, F, G, H) ->
    case (A =:= 16#fe80 orelse A =:= 16#ff02) andalso B =/= 0 of
        true -> fields_text([A, 0, C, D, E, F, G, H]) ++ "%0" ++ integer_to_list(B);
        false -> fields_text([A, B, C, D, E, F, G, H])
    end;
ntoa(_) ->
    {error, einval}.

% The two bytes of a 16-bit field as "High.Low".
dotted_pair(Field) -> integer_to_list(Field bsr 8) ++ "." ++ integer_to_list(Field band 16#ff).

% Eight fields in lowercase hexadecimal joined by ":", the first longest zero run of two or more as "::".
fields_text(Fields) ->
    case longest_zeros(Fields, 0, 0, 0, 0) of
        {_, Length} when Length < 2 ->
            hex_fields(Fields);
        {Start, Length} ->
            {Before, Rest} = lists:split(Start, Fields),
            hex_fields(Before) ++ "::" ++ hex_fields(lists:nthtail(Length, Rest))
    end.

% {Start, Length} of the first longest run of zero fields.
longest_zeros([], _, _, BestStart, BestLength) ->
    {BestStart, BestLength};
longest_zeros([0 | _] = Fields, At, _, BestStart, BestLength) ->
    Length = zero_run(Fields, 0),
    case Length > BestLength of
        true -> longest_zeros(lists:nthtail(Length, Fields), At + Length, 0, At, Length);
        false -> longest_zeros(lists:nthtail(Length, Fields), At + Length, 0, BestStart, BestLength)
    end;
longest_zeros([_ | Fields], At, _, BestStart, BestLength) ->
    longest_zeros(Fields, At + 1, 0, BestStart, BestLength).

zero_run([0 | Fields], Length) -> zero_run(Fields, Length + 1);
zero_run(_, Length) -> Length.

hex_fields(Fields) -> lists:append(lists:join(":", [lower_hex(Field) || Field <- Fields])).

lower_hex(Field) -> [lower(C) || C <- integer_to_list(Field, 16)].

lower(C) when C >= $A, C =< $F -> C + 32;
lower(C) -> C.

parse_address(Text) when is_list(Text) ->
    case parse_ipv4_address(Text) of
        {ok, Address} -> {ok, Address};
        _ -> parse_ipv6strict_address(Text)
    end;
parse_address(_) ->
    {error, einval}.

parse_strict_address(Text) when is_list(Text) ->
    case parse_ipv4strict_address(Text) of
        {ok, Address} -> {ok, Address};
        _ -> parse_ipv6strict_address(Text)
    end;
parse_strict_address(_) ->
    {error, einval}.

parse_address(Text, inet) -> parse_ipv4_address(Text);
parse_address(Text, inet6) -> parse_ipv6_address(Text).

parse_strict_address(Text, inet) -> parse_ipv4strict_address(Text);
parse_strict_address(Text, inet6) -> parse_ipv6strict_address(Text).

% {ok, Address}, or {error, einval} when Parse raises badarg.
parsed(Parse, Text) ->
    try Parse(Text) of
        Address -> {ok, Address}
    catch
        error:badarg -> {error, einval}
    end.

% A relaxed IPv4 address: 1 to 4 fields, each decimal, octal (leading 0) or hexadecimal (0x); the last field fills
% the remaining bytes.
parse_ipv4_address(Text) -> parsed(fun relaxed_ipv4/1, Text).

relaxed_ipv4(Text) ->
    case relaxed_fields(Text, []) of
        [D] when D < 1 bsl 32 -> bytes4(D, []);
        [D, D1] when D < 1 bsl 24, D1 < 256 -> bytes4(D, [D1]);
        [D, D2, D1] when D < 1 bsl 16, (D2 bor D1) < 256 -> bytes4(D, [D1, D2]);
        [D4, D3, D2, D1] when (D4 bor D3 bor D2 bor D1) < 256 -> {D1, D2, D3, D4};
        _ -> erlang:error(badarg)
    end.

% The four bytes of an address whose leading fields are Leading and whose last field is Last.
bytes4(Last, Leading) ->
    Count = 4 - length(Leading),
    Low = [(Last bsr (8 * Shift)) band 16#ff || Shift <- lists:seq(Count - 1, 0, -1)],
    list_to_tuple(Leading ++ Low).

% The fields of a relaxed address, last first.
relaxed_fields([_ | _], [_, _, _, _]) ->
    erlang:error(badarg);
relaxed_fields([$0, X | Text], Fields) when X =:= $x; X =:= $X ->
    relaxed_field(drop_zeros(Text), Fields, [], 16, 8);
relaxed_fields([$0 | Text], Fields) ->
    relaxed_field(drop_zeros(Text), Fields, [$0], 8, 11);
relaxed_fields(Text, Fields) when is_list(Text) ->
    relaxed_field(Text, Fields, [], 10, 10).

relaxed_field(Text, Fields, Digits, Base, Most) ->
    case field(Text, Most, Digits, Base) of
        {Value, ""} -> [Value | Fields];
        {Value, [$. | [_ | _] = Rest]} -> relaxed_fields(Rest, [Value | Fields]);
        _ -> erlang:error(badarg)
    end.

drop_zeros([$0 | Text]) -> drop_zeros(Text);
drop_zeros(Text) when is_list(Text) -> Text.

% One field: at most Most digits up to a dot or the end, in Base, after Digits already taken; a field may not start
% with a zero after its prefix.
field("", _, Digits, Base) -> {field_value(Digits, Base), ""};
field([$. | _] = Rest, _, Digits, Base) -> {field_value(Digits, Base), Rest};
field([$0 | _], _, [], _) -> erlang:error(badarg);
field([C | Text], Most, Digits, Base) when Most > 0 -> field(Text, Most - 1, [C | Digits], Base);
field(Text, _, _, _) when is_list(Text) -> erlang:error(badarg).

field_value(Digits, Base) ->
    case list_to_integer(lists:reverse(Digits), Base) of
        Value when Value >= 0 -> Value;
        _ -> erlang:error(badarg)
    end.

% A strict IPv4 address: four decimal fields without leading zeros.
parse_ipv4strict_address(Text) -> parsed(fun strict_ipv4/1, Text).

strict_ipv4(Text) ->
    case strict_fields(Text, []) of
        [D4, D3, D2, D1] when (D4 bor D3 bor D2 bor D1) < 256 -> {D1, D2, D3, D4};
        _ -> erlang:error(badarg)
    end.

strict_fields([_ | _], [_, _, _, _]) ->
    erlang:error(badarg);
strict_fields("0", Fields) ->
    [0 | Fields];
strict_fields([$0, $. | Text], Fields) ->
    strict_fields(Text, [0 | Fields]);
strict_fields(Text, Fields) when is_list(Text) ->
    case field(Text, 3, [], 10) of
        {Value, ""} -> [Value | Fields];
        {Value, [$. | [_ | _] = Rest]} -> strict_fields(Rest, [Value | Fields]);
        _ -> erlang:error(badarg)
    end.

% A relaxed IPv6 address: a relaxed IPv4 address becomes its IPv4-mapped IPv6 address.
parse_ipv6_address(Text) ->
    case parse_ipv4_address(Text) of
        {ok, {D1, D2, D3, D4}} ->
            {ok, {0, 0, 0, 0, 0, 16#ffff, (D1 bsl 8) bor D2, (D3 bsl 8) bor D4}};
        _ ->
            parse_ipv6strict_address(Text)
    end.

% An IPv6 address of RFC 4291: hexadecimal fields, "::" once, an IPv4 tail, a %scope for fe80:: and ff02::.
parse_ipv6strict_address(Text) -> parsed(fun strict_ipv6/1, Text).

strict_ipv6("::") -> filled([], [], 0);
strict_ipv6("::" ++ Text) -> ipv6_after(hex_digits(Text), [], [], 0);
strict_ipv6(Text) -> ipv6_before(hex_digits(Text), [], 0).

% Fields before "::", last first in Before; N fields so far.
ipv6_before({Digits, "%" ++ Scope}, Before, 7) ->
    scoped(Scope, [hex(Digits) | Before], [], 8);
ipv6_before({Digits, []}, Before, 7) ->
    list_to_tuple(lists:reverse([hex(Digits) | Before]));
ipv6_before({Digits, "::%" ++ Scope}, Before, N) when N =< 6 ->
    scoped(Scope, [hex(Digits) | Before], [], N + 1);
ipv6_before({Digits, "::"}, Before, N) when N =< 6 ->
    filled([hex(Digits) | Before], [], N + 1);
ipv6_before({Digits, "::" ++ Text}, Before, N) when N =< 5 ->
    ipv6_after(hex_digits(Text), [hex(Digits) | Before], [], N + 1);
ipv6_before({Digits, ":" ++ Text}, Before, N) when N =< 6 ->
    ipv6_before(hex_digits(Text), [hex(Digits) | Before], N + 1);
ipv6_before({Digits, "." ++ _ = Text}, Before, 6) ->
    with_ipv4(Before, [], 6, strict_ipv4(Digits ++ Text));
ipv6_before(_, _, _) ->
    erlang:error(badarg).

% Fields after "::", last first in After.
ipv6_after({Digits, "%" ++ Scope}, Before, After, N) when N =< 6 ->
    scoped(Scope, Before, [hex(Digits) | After], N + 1);
ipv6_after({Digits, []}, Before, After, N) when N =< 6 ->
    filled(Before, [hex(Digits) | After], N + 1);
ipv6_after({Digits, ":" ++ Text}, Before, After, N) when N =< 5 ->
    ipv6_after(hex_digits(Text), Before, [hex(Digits) | After], N + 1);
ipv6_after({Digits, "." ++ _ = Text}, Before, After, N) when N =< 5 ->
    with_ipv4(Before, After, N, strict_ipv4(Digits ++ Text));
ipv6_after(_, _, _, _) ->
    erlang:error(badarg).

with_ipv4(Before, After, N, {D1, D2, D3, D4}) ->
    filled(Before, [(D3 bsl 8) bor D4, (D1 bsl 8) bor D2 | After], N + 2).

% The address with the zeros of "::" between the fields before and after it (both last first).
filled(Before, After, N) ->
    list_to_tuple(lists:reverse(After ++ lists:duplicate(8 - N, 0) ++ Before)).

% A link-local or multicast address with its %scope: "0" and a decimal id, anything else 0, as its second field.
scoped(Scope, Before, After, N) ->
    Id =
        case Scope of
            "0" ++ Decimal -> decimal16(Decimal, 0);
            _ -> 0
        end,
    case lists:reverse(After ++ lists:duplicate(8 - N, 0) ++ Before) of
        [Prefix, 0 | Rest] when Prefix =:= 16#fe80; Prefix =:= 16#ff02 ->
            list_to_tuple([Prefix, Id | Rest]);
        _ ->
            erlang:error(badarg)
    end.

decimal16([], Value) ->
    Value;
decimal16([C | Text], Value) when C >= $0, C =< $9, 10 * Value + C - $0 =< 16#ffff ->
    decimal16(Text, 10 * Value + C - $0);
decimal16(_, _) ->
    erlang:error(badarg).

% One to four hexadecimal digits and the text after them.
hex_digits(Text) -> hex_digits(Text, [], 4).

hex_digits([C | Text], Digits, Most) when
    Most > 0,
    (C >= $0 andalso C =< $9 orelse C >= $a andalso C =< $f orelse C >= $A andalso C =< $F)
->
    hex_digits(Text, [C | Digits], Most - 1);
hex_digits(Text, [_ | _] = Digits, _) when is_list(Text) ->
    {lists:reverse(Digits), Text};
hex_digits(_, _, _) ->
    erlang:error(badarg).

hex(Digits) -> list_to_integer(Digits, 16).

% The host name of this machine.
gethostname() -> socket_query(fun clause_socket:hostname/1).

getaddr(Host, Family) ->
    case getaddrs(Host, Family) of
        {ok, [Address | _]} -> {ok, Address};
        Error -> Error
    end.

% Every address of a host, an address tuple or loopback of the family.
getaddrs(Host, Family) when Family =:= inet; Family =:= inet6 ->
    Number =
        case Family of
            inet -> 4;
            inet6 -> 6
        end,
    case is_tuple(Host) of
        true ->
            case {is_family_address(Host, Number), is_ip_address(Host)} of
                {true, _} -> {ok, [Host]};
                {false, true} -> {error, eafnosupport};
                {false, false} -> {error, einval}
            end;
        false ->
            socket_query(fun(Socket) -> clause_socket:addresses(Socket, Host, Number) end)
    end;
getaddrs(_, _) ->
    {error, einval}.

is_family_address(Address, 4) -> is_ipv4_address(Address);
is_family_address(Address, 6) -> is_ipv6_address(Address).

gethostbyname(Host) -> gethostbyname(Host, inet).

% A #hostent{} for a host name and family.
gethostbyname(Host, Family) ->
    case getaddrs(Host, Family) of
        {ok, Addresses} ->
            Name =
                case Host of
                    _ when is_atom(Host) -> atom_to_list(Host);
                    _ when is_list(Host) -> Host;
                    _ -> ntoa(Host)
                end,
            Length =
                case Family of
                    inet -> 4;
                    inet6 -> 16
                end,
            {ok, #hostent{
                h_name = Name,
                h_aliases = [],
                h_addrtype = Family,
                h_length = Length,
                h_addr_list = Addresses
            }};
        Error ->
            Error
    end.

% Run Query on a socket port opened for it alone.
socket_query(Query) ->
    Socket = clause_socket:open("udp_inet"),
    try
        Query(Socket)
    after
        catch port_close(Socket)
    end.
