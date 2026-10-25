-module(clause_socket).
-export([
    open/1,
    options/2,
    mode/1,
    local/1,
    address/1,
    control/3,
    request/4,
    resolve/3,
    addresses/3,
    hostname/1,
    names/2,
    setopts/2,
    getopts/2,
    getstat/2,
    status/1,
    query/3,
    records/1,
    controlling_process/2,
    close/1
]).

% The socket plumbing shared by gen_tcp, gen_udp and inet (docs/ports.md#sockets): a socket is a port of the
% runtime's socket driver ({spawn_driver, "tcp_inet" | "udp_inet"}), driven with port_control/3. Operations that
% wait (connect, accept, recv) answer with a message {clause_socket, Socket, Reply} to their caller.

-define(SETOPTS, 6).
-define(SOCKNAME, 7).
-define(PEERNAME, 8).
-define(CANCEL, 13).
-define(RESOLVE, 14).
-define(HOSTNAME, 15).
-define(GETSTAT, 16).
-define(GETSTATUS, 17).
-define(GETOPT, 18).
-define(SETOPT, 19).
-define(UNCHANGED, 255).

% A new socket port of Driver ("tcp_inet" or "udp_inet") owned by and linked to the caller.
open(Driver) -> open_port({spawn_driver, Driver}, [binary]).

% Options applied to the map Defaults; exit(badarg) for an option that is not one, as OTP does.
options(Options, Defaults) when is_list(Options) -> lists:foldl(fun option/2, Defaults, Options);
options(_, _) -> exit(badarg).

option(binary, Map) ->
    Map#{binary => true};
option(list, Map) ->
    Map#{binary => false};
option(inet, Map) ->
    Map#{family => 4};
option(inet6, Map) ->
    Map#{family => 6};
option({active, Active}, Map) when Active =:= true; Active =:= false; Active =:= once ->
    Map#{active => Active};
option({mode, binary}, Map) ->
    Map#{binary => true};
option({mode, list}, Map) ->
    Map#{binary => false};
option({packet, raw}, Map) ->
    Map#{packet => 0};
option({packet, Size}, Map) when Size =:= 0; Size =:= 1; Size =:= 2; Size =:= 4 ->
    Map#{packet => Size};
option({ip, Address}, Map) ->
    ip(Address, Map);
option({ifaddr, Address}, Map) ->
    ip(Address, Map);
option({backlog, Backlog}, Map) when is_integer(Backlog), Backlog >= 0 ->
    Map#{backlog => Backlog};
option({Name, Value}, Map) ->
    % The options beyond the data mode, as driver records (inet:getopts/2).
    case clause_socket_options:record(Name, Value) of
        {ok, Record} -> Map#{records => [Record | maps:get(records, Map, [])]};
        error -> exit(badarg)
    end;
option(_, _) ->
    exit(badarg).

% The option records of an options map, in the order given, for the driver to set (inet:getopts/2).
records(Map) -> lists:reverse(maps:get(records, Map, [])).

ip(Address, Map) ->
    case address(Address) of
        error -> exit(badarg);
        <<Family, _/binary>> -> Map#{ip => Address, family => Family}
    end.

% The data mode of an options map as the driver's bytes: active, packet and binary; unset ones unchanged.
mode(Map) ->
    <<
        (active_byte(field(active, Map))),
        (packet_byte(field(packet, Map))),
        (binary_byte(field(binary, Map)))
    >>.

% The value of option Name in Map, or unchanged.
field(Name, Map) ->
    case Map of
        #{Name := Value} -> Value;
        _ -> unchanged
    end.

active_byte(false) -> 0;
active_byte(true) -> 1;
active_byte(once) -> 2;
active_byte(unchanged) -> ?UNCHANGED.

packet_byte(unchanged) -> ?UNCHANGED;
packet_byte(Size) -> Size.

binary_byte(false) -> 0;
binary_byte(true) -> 1;
binary_byte(unchanged) -> ?UNCHANGED.

% The local address of an options map: its ip, or the any address of its family.
local(#{ip := Address}) -> address(Address);
local(#{family := 6}) -> address({0, 0, 0, 0, 0, 0, 0, 0});
local(_) -> address({0, 0, 0, 0}).

% An address tuple as the driver's family byte and address bytes, or error.
address({A, B, C, D} = Address) ->
    case bytes(tuple_to_list(Address), 255) of
        true -> <<4, A, B, C, D>>;
        false -> error
    end;
address({A, B, C, D, E, F, G, H} = Address) ->
    case bytes(tuple_to_list(Address), 65535) of
        true -> <<6, A:16, B:16, C:16, D:16, E:16, F:16, G:16, H:16>>;
        false -> error
    end;
address(_) ->
    error.

bytes([], _) -> true;
bytes([Part | Parts], Max) when is_integer(Part), Part >= 0, Part =< Max -> bytes(Parts, Max);
bytes(_, _) -> false.

% The tuple of the driver's family byte and address bytes, and the bytes after it.
address_tuple(<<4, A, B, C, D, Rest/binary>>) ->
    {{A, B, C, D}, Rest};
address_tuple(<<6, A:16, B:16, C:16, D:16, E:16, F:16, G:16, H:16, Rest/binary>>) ->
    {{A, B, C, D, E, F, G, H}, Rest}.

% Run Operation on Socket: {ok, Result} or {error, Reason}; {error, closed} once the port has closed.
control(Socket, Operation, Data) ->
    try port_control(Socket, Operation, Data) of
        <<0, Result/binary>> -> {ok, Result};
        <<1, Reason/binary>> -> {error, list_to_atom(binary_to_list(Reason))}
    catch
        error:badarg -> {error, closed}
    end.

% Run a query or setting on Socket as control/3, {error, einval} once the port has closed, as OTP's inet_drv answers
% (inet:getopts/2, inet:getstat/2).
query(Socket, Operation, Data) ->
    case control(Socket, Operation, Data) of
        {error, closed} -> {error, einval};
        Result -> Result
    end.

% Start Operation, which answers with a message, and wait at most Timeout for it.
request(Socket, Operation, Data, Timeout) ->
    case control(Socket, Operation, Data) of
        {ok, _} ->
            receive
                {clause_socket, Socket, Reply} -> Reply
            after Timeout -> cancel(Socket)
            end;
        Error ->
            Error
    end.

% Stop waiting: the reply that arrived before the cancellation, or {error, timeout}.
cancel(Socket) ->
    case control(Socket, ?CANCEL, <<>>) of
        {ok, _} ->
            receive
                {clause_socket, Socket, cancelled} ->
                    {error, timeout};
                {clause_socket, Socket, Reply} ->
                    flush_cancelled(Socket),
                    Reply
            end;
        {error, closed} ->
            % Closing the socket answers every waiting caller.
            receive
                {clause_socket, Socket, Reply} -> Reply
            end
    end.

flush_cancelled(Socket) ->
    receive
        {clause_socket, Socket, cancelled} -> ok
    end.

% The address of Host (an address tuple, a string or an atom) of Family (4 or 6), looked up through Socket.
resolve(_, Host, _) when is_tuple(Host) ->
    case address(Host) of
        error -> {error, einval};
        _ -> {ok, Host}
    end;
resolve(_, loopback, 4) ->
    {ok, {127, 0, 0, 1}};
resolve(_, loopback, 6) ->
    {ok, {0, 0, 0, 0, 0, 0, 0, 1}};
resolve(Socket, Host, Family) when is_atom(Host) ->
    resolve(Socket, atom_to_list(Host), Family);
resolve(Socket, Host, Family) when is_list(Host) ->
    case control(Socket, ?RESOLVE, [Family, Host]) of
        {ok, Addresses} -> {ok, element(1, address_tuple(Addresses))};
        Error -> Error
    end;
resolve(_, _, _) ->
    {error, einval}.

% {ok, [Address]}: every address of a host name of family 4 or 6, loopback too (plan step 41R).
addresses(_, loopback, Family) ->
    {ok, [element(2, resolve(none, loopback, Family))]};
addresses(Socket, Host, Family) when is_atom(Host) ->
    addresses(Socket, atom_to_list(Host), Family);
addresses(Socket, Host, Family) when is_list(Host) ->
    case control(Socket, ?RESOLVE, [Family, Host]) of
        {ok, Bytes} -> {ok, address_list(Bytes)};
        Error -> Error
    end;
addresses(_, _, _) ->
    {error, einval}.

address_list(<<>>) ->
    [];
address_list(Bytes) ->
    {Address, Rest} = address_tuple(Bytes),
    [Address | address_list(Rest)].

% {ok, Name}: this machine's host name (plan step 41R).
hostname(Socket) ->
    case control(Socket, ?HOSTNAME, []) of
        {ok, Name} -> {ok, binary_to_list(Name)};
        Error -> Error
    end.

% {ok, {Address, Port}} of the socket's own end, or of its peer when Peer.
names(Socket, Peer) ->
    Operation =
        case Peer of
            true -> ?PEERNAME;
            false -> ?SOCKNAME
        end,
    case query(Socket, Operation, <<>>) of
        {ok, <<Family, Port:16, Address/binary>>} ->
            {ok, {element(1, address_tuple(<<Family, Address/binary>>)), Port}};
        Error ->
            Error
    end.

% Change the options of Socket: every option is checked before any is set, as OTP does.
setopts(_, Options) when not is_list(Options) ->
    erlang:error({bad_generator, Options});
setopts(Socket, Options) ->
    try options(Options, #{}) of
        Map -> set_records(Socket, records(Map), mode(Map))
    catch
        exit:badarg -> {error, einval}
    end.

set_records(Socket, [], Mode) ->
    set_mode(Socket, Mode);
set_records(Socket, Records, Mode) ->
    case query(Socket, ?SETOPT, Records) of
        {ok, _} -> set_mode(Socket, Mode);
        Error -> Error
    end.

set_mode(Socket, Mode) ->
    case query(Socket, ?SETOPTS, Mode) of
        {ok, _} -> ok;
        Error -> Error
    end.

% {ok, [{Name, Value}]} of the options Names in order, leaving out those the socket type lacks; {error, einval} for
% an option getopts cannot read (inet:getopts/2).
getopts(Socket, Names) when is_list(Names) ->
    case status(Socket) of
        {ok, {_, Type, Family}} ->
            Requests = [{Name, option_request(Name, Type, Family)} || Name <- Names],
            case lists:keymember(einval, 2, Requests) of
                true -> {error, einval};
                false -> fetch_options(Socket, Requests)
            end;
        Error ->
            Error
    end.

option_request(Name, _, _) when Name =:= active; Name =:= packet; Name =:= mode -> mode;
option_request(Name, Type, Family) -> clause_socket_options:request(Name, Type, Family).

fetch_options(Socket, Requests) ->
    Codes = <<<<Code>> || {_, {code, Code, _}} <- Requests>>,
    Unchanged = <<?UNCHANGED, ?UNCHANGED, ?UNCHANGED>>,
    case {query(Socket, ?SETOPTS, Unchanged), query(Socket, ?GETOPT, Codes)} of
        {{ok, Mode}, {ok, Values}} -> {ok, option_values(Requests, Mode, Values)};
        {{error, _} = Error, _} -> Error;
        {_, Error} -> Error
    end.

option_values([], _, _) ->
    [];
option_values([{_, skip} | Requests], Mode, Values) ->
    option_values(Requests, Mode, Values);
option_values([{Name, mode} | Requests], Mode, Values) ->
    [{Name, mode_value(Name, Mode)} | option_values(Requests, Mode, Values)];
option_values([{Name, {code, Code, Type}} | Requests], Mode, <<Code, Size, Value:Size/binary, Rest/binary>>) ->
    [{Name, clause_socket_options:decode(Type, Value)} | option_values(Requests, Mode, Rest)].

mode_value(active, <<0, _, _>>) -> false;
mode_value(active, <<1, _, _>>) -> true;
mode_value(active, <<_, _, _>>) -> once;
mode_value(packet, <<_, Packet, _>>) -> Packet;
mode_value(mode, <<_, _, 0>>) -> list;
mode_value(mode, <<_, _, _>>) -> binary.

% {ok, {Flags, Type, Family}} of an open socket: inet_drv's status flags (sorted), stream or dgram, and 4 or 6.
status(Socket) ->
    case query(Socket, ?GETSTATUS, <<>>) of
        {ok, <<Flags, Kind, Family>>} ->
            Names = [accepting, connected, connecting, listen, open],
            Bits = [16, 2, 4, 8, 1],
            Set = [Name || {Name, Bit} <- lists:zip(Names, Bits), Flags band Bit =/= 0],
            {ok, {Set, element(Kind + 1, {stream, dgram}), Family}};
        Error ->
            Error
    end.

% {ok, [{Name, Count}]} of the statistics Names in order (inet:getstat/2).
getstat(Socket, Names) when is_list(Names) ->
    All = [recv_oct, recv_cnt, recv_max, recv_avg, recv_dvi, send_oct, send_cnt, send_max, send_avg, send_pend],
    case lists:all(fun(Name) -> lists:member(Name, All) end, Names) of
        true -> statistics(query(Socket, ?GETSTAT, <<>>), All, Names);
        false -> {error, einval}
    end.

statistics({ok, Bytes}, All, Names) ->
    Values = maps:from_list(lists:zip(All, [Value || <<Value:64>> <= Bytes])),
    {ok, [{Name, maps:get(Name, Values)} || Name <- Names]};
statistics(Error, _, _) ->
    Error.

% Make Owner the process the socket's messages go to; only its current owner may, as in OTP.
controlling_process(Socket, Owner) when is_port(Socket), is_pid(Owner) ->
    case erlang:port_info(Socket, connected) of
        {connected, Owner} -> ok;
        {connected, Current} when Current =/= self() -> {error, not_owner};
        undefined -> {error, einval};
        _ -> transfer(Socket, Owner)
    end;
controlling_process(_, _) ->
    {error, badarg}.

% Stop messages, move those already sent to the new owner, connect it and resume messages.
transfer(Socket, Owner) ->
    case control(Socket, ?SETOPTS, <<0, ?UNCHANGED, ?UNCHANGED>>) of
        {ok, <<Active, _, _>>} ->
            {ok, _} = control(Socket, ?CANCEL, <<>>),
            flush_cancelled(Socket),
            move(Socket, Owner),
            port_connect(Socket, Owner),
            unlink(Socket),
            control(Socket, ?SETOPTS, <<Active, ?UNCHANGED, ?UNCHANGED>>),
            ok;
        Error ->
            Error
    end.

move(Socket, Owner) ->
    receive
        {tcp, Socket, _} = Message -> forward(Socket, Owner, Message);
        {tcp_closed, Socket} = Message -> forward(Socket, Owner, Message);
        {tcp_error, Socket, _} = Message -> forward(Socket, Owner, Message);
        {udp, Socket, _, _, _} = Message -> forward(Socket, Owner, Message)
    after 0 -> ok
    end.

forward(Socket, Owner, Message) ->
    Owner ! Message,
    move(Socket, Owner).

% Close Socket: ok, also when it is already closed.
close(Socket) ->
    try port_close(Socket) of
        _ -> ok
    catch
        error:badarg -> ok
    end.
