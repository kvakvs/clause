-module(socket).
-export([
    accept/1,
    accept/2,
    bind/2,
    bind/3,
    cancel/2,
    cancel_monitor/1,
    close/1,
    connect/1,
    connect/2,
    connect/3,
    debug/1,
    getopt/2,
    getopt/3,
    getopt_native/3,
    i/0,
    i/1,
    i/2,
    info/0,
    info/1,
    ioctl/2,
    ioctl/3,
    ioctl/4,
    is_supported/1,
    is_supported/2,
    is_supported/3,
    listen/1,
    listen/2,
    mk_sockaddr/2,
    mk_sockaddr/3,
    monitor/1,
    monitored_by/1,
    number_of/0,
    number_of_monitors/0,
    number_of_monitors/1,
    open/1,
    open/2,
    open/3,
    open/4,
    option/1,
    option/2,
    options/0,
    options/1,
    options/2,
    peeloff/2,
    peeloff/3,
    peername/1,
    peernames/2,
    protocol/1,
    protocols/0,
    recv/1,
    recv/2,
    recv/3,
    recv/4,
    recvfrom/1,
    recvfrom/2,
    recvfrom/3,
    recvfrom/4,
    recvmmsg/6,
    recvmsg/1,
    recvmsg/2,
    recvmsg/3,
    recvmsg/4,
    recvmsg/5,
    rest_iov/2,
    send/2,
    send/3,
    send/4,
    sendfile/2,
    sendfile/3,
    sendfile/4,
    sendfile/5,
    sendmmsg/4,
    sendmsg/2,
    sendmsg/3,
    sendmsg/4,
    sendto/3,
    sendto/4,
    sendto/5,
    sendv/2,
    sendv/3,
    sendv/4,
    setopt/3,
    setopt/4,
    setopt_native/3,
    shutdown/2,
    socket_debug/1,
    sockname/1,
    socknames/2,
    supports/0,
    supports/1,
    supports/2,
    table/1,
    tables/0,
    to_list/1,
    use_registry/1,
    which_monitors/1,
    which_socket_kind/1,
    which_sockets/0,
    which_sockets/1
]).
-include("socket.types.hrl").

% Clause's socket (plan 21 step LIB62, docs/libraries/library.md): OTP's socket API in Erlang over the runtime's socket
% driver ports (tcp_inet, udp_inet). A socket is {'$socket', Ref}; its port, kind and states are kept by the
% clause_socket_registry process in a public ETS table, which also serves monitors and listings. Operations block in
% the caller; nowait starts the operation in a helper process and answers as OTP does on the platform: completion
% messages on Windows, select messages elsewhere. Stream data a recv did not take waits in the socket's buffer.

-define(REGISTRY, clause_socket_registry).
-define(TABLE, clause_socket_registry).
-define(CONNECT, 1).
-define(LISTEN, 2).
-define(ACCEPT, 3).
-define(RECV, 4).
-define(SEND, 5).
-define(SETOPTS, 6).
-define(SOCKNAME, 7).
-define(PEERNAME, 8).
-define(SHUTDOWN, 9).
-define(UDP_OPEN, 10).
-define(UDP_SEND, 11).
-define(UDP_RECV, 12).
-define(CANCEL, 13).
-define(BIND, 22).
-define(UNCHANGED, <<255, 255, 255>>).
-define(SOCKET(Ref), {'$socket', Ref}).

%% --- The registry ---

% The registry process, started on first use: it owns the table, watches each socket's port and its owner, and
% delivers socket monitors' 'DOWN' messages.
registry() ->
    case whereis(?REGISTRY) of
        undefined ->
            Self = self(),
            Pid = spawn(fun() -> start_registry(Self) end),
            receive
                {Pid, started} -> registry()
            end;
        Pid ->
            Pid
    end.

start_registry(Starter) ->
    try register(?REGISTRY, self()) of
        true ->
            ets:new(?TABLE, [named_table, public, set, {read_concurrency, true}]),
            Starter ! {self(), started},
            registry_loop(#{ports => #{}, monitors => #{}, owners => #{}})
    catch
        error:badarg -> Starter ! {self(), started}
    end.

registry_loop(State) ->
    receive
        {call, From, Tag, Request} ->
            {Reply, Next} = registry_request(Request, State),
            From ! {Tag, Reply},
            registry_loop(Next);
        {'DOWN', Mref, port, _, _} ->
            registry_loop(port_down(Mref, State));
        {'DOWN', Mref, process, _, _} ->
            registry_loop(process_down(Mref, State))
    end.

registry_call(Request) ->
    Pid = registry(),
    Tag = erlang:monitor(process, Pid),
    Pid ! {call, self(), Tag, Request},
    receive
        {Tag, Reply} ->
            erlang:demonitor(Tag, [flush]),
            Reply;
        {'DOWN', Tag, _, _, Reason} ->
            erlang:error(Reason)
    end.

% A new socket's port is watched; its owner too, so the rows of its closed sockets go when it ends.
registry_request({add, Ref, Port, Owner}, #{ports := Ports} = State) ->
    Mref = erlang:monitor(port, Port),
    {ok, watch_owner(Owner, State#{ports := Ports#{Mref => Ref}})};
registry_request({owner, Owner}, State) ->
    {ok, watch_owner(Owner, State)};
registry_request({monitor, Watcher, Ref}, #{monitors := Monitors} = State) ->
    Mref = make_ref(),
    case lookup(Ref) of
        {ok, #{state := open}} ->
            Wref = erlang:monitor(process, Watcher),
            {Mref, State#{monitors := Monitors#{Mref => {Watcher, Ref, Wref}}}};
        _ ->
            Watcher ! {'DOWN', Mref, socket, ?SOCKET(Ref), closed},
            {Mref, State}
    end;
registry_request({cancel_monitor, Mref}, #{monitors := Monitors} = State) ->
    case maps:take(Mref, Monitors) of
        {{_, _, Wref}, Rest} ->
            erlang:demonitor(Wref, [flush]),
            {true, State#{monitors := Rest}};
        error ->
            {false, State}
    end.

watch_owner(Owner, #{owners := Owners} = State) ->
    case lists:member(Owner, maps:values(Owners)) of
        true -> State;
        false -> State#{owners := Owners#{erlang:monitor(process, Owner) => Owner}}
    end.

% A socket's port closed: the socket is closed and its monitors fire.
port_down(Mref, #{ports := Ports, monitors := Monitors} = State) ->
    case maps:take(Mref, Ports) of
        {Ref, Rest} ->
            close_row(Ref),
            Fired = [M || M := {_, R, _} <- Monitors, R =:= Ref],
            [fire(M, maps:get(M, Monitors)) || M <- Fired],
            State#{ports := Rest, monitors := maps:without(Fired, Monitors)};
        error ->
            State
    end.

% A closed socket keeps its row while its owner lives, for info/1 and getopt/2.
close_row(Ref) ->
    case lookup(Ref) of
        {ok, #{owner := Owner}} ->
            case is_process_alive(Owner) of
                true -> update(Ref, fun closed/1);
                false -> ets:delete(?TABLE, Ref)
            end;
        _ ->
            ok
    end.

fire(Mref, {Watcher, Ref, Wref}) ->
    erlang:demonitor(Wref, [flush]),
    Watcher ! {'DOWN', Mref, socket, ?SOCKET(Ref), closed}.

% An owner or a watcher ended: the rows of the owner's closed sockets go, the watcher's monitors too.
process_down(Mref, #{owners := Owners, monitors := Monitors} = State) ->
    case maps:take(Mref, Owners) of
        {Owner, Rest} ->
            [ets:delete(?TABLE, Ref) || {Ref, #{owner := O, state := closed}} <- ets:tab2list(?TABLE), O =:= Owner],
            State#{owners := Rest};
        error ->
            State#{monitors := maps:filter(fun(_, {_, _, W}) -> W =/= Mref end, Monitors)}
    end.

closed(#{state := closed} = Info) ->
    Info;
closed(#{rstates := R, wstates := W} = Info) ->
    Info#{state := closed, rstates := R ++ [closing, closed], wstates := W ++ [closing, closed]}.

% The row of a socket.
lookup(Ref) when is_reference(Ref) ->
    try ets:lookup(?TABLE, Ref) of
        [{Ref, Info}] -> {ok, Info};
        [] -> {error, closed}
    catch
        error:badarg -> {error, closed}
    end;
lookup(_) ->
    {error, closed}.

update(Ref, Fun) ->
    case lookup(Ref) of
        {ok, Info} ->
            ets:insert(?TABLE, {Ref, Fun(Info)}),
            ok;
        Error ->
            Error
    end.

% The open socket's row, or {error, closed}.
open_info(?SOCKET(Ref)) ->
    case lookup(Ref) of
        {ok, #{state := open} = Info} -> {ok, Info};
        _ -> {error, closed}
    end;
open_info(Socket) ->
    erlang:error(badarg, [Socket]).

%% --- Opening and closing ---

open(FD) -> open(FD, #{}).

open(FD, Options) when is_integer(FD), is_map(Options) ->
    % Adopting an operating system descriptor is not supported (docs/differences.md).
    erlang:error(notsup, [FD, Options]);
open(Domain, Type) ->
    open(Domain, Type, ip, #{}).

open(Domain, Type, Options) when is_map(Options) ->
    open(Domain, Type, ip, Options);
open(Domain, Type, Protocol) ->
    open(Domain, Type, Protocol, #{}).

%% A socket of Domain, Type and Protocol, owned by the caller: it closes when the caller ends.
open(Domain, Type, Protocol0, Options) when is_map(Options) ->
    case kind(Domain, Type, Protocol0) of
        {ok, Protocol, Driver} ->
            Port = open_port({spawn_driver, Driver}, [binary]),
            {ok, _} = clause_socket:control(Port, ?SETOPTS, <<0, 0, 1>>),
            Ref = make_ref(),
            ets_insert(Ref, #{
                port => Port,
                domain => Domain,
                type => Type,
                protocol => Protocol,
                owner => self(),
                state => open,
                rstates => [],
                wstates => [],
                meta => undefined,
                buffer => <<>>,
                helpers => #{},
                ready => #{},
                counters => counters()
            }),
            ok = registry_call({add, Ref, Port, self()}),
            {ok, ?SOCKET(Ref)};
        {error, _} = Error ->
            Error
    end;
open(Domain, Type, Protocol, Options) ->
    erlang:error(badarg, [Domain, Type, Protocol, Options]).

ets_insert(Ref, Info) ->
    _ = registry(),
    ets:insert(?TABLE, {Ref, Info}).

% The protocol and driver of a domain, type and protocol, or OTP's error.
kind(Domain, _, _) when Domain =/= inet, Domain =/= inet6, Domain =/= local, not is_integer(Domain) ->
    {error, {invalid, {domain, Domain}}};
kind(Domain, _, _) when Domain =/= inet, Domain =/= inet6 ->
    {error, eafnosupport};
kind(_, Type, _) when Type =/= stream, Type =/= dgram, Type =/= seqpacket, Type =/= raw, not is_integer(Type) ->
    {error, {invalid, {type, Type}}};
kind(_, stream, Protocol) when Protocol =:= ip; Protocol =:= tcp; Protocol =:= default ->
    {ok, protocol_name(Protocol), "tcp_inet"};
kind(_, dgram, Protocol) when Protocol =:= ip; Protocol =:= udp; Protocol =:= default ->
    {ok, protocol_name(Protocol), "udp_inet"};
kind(_, _, Protocol) when Protocol =:= ip; Protocol =:= tcp; Protocol =:= udp; Protocol =:= default ->
    {error, eprotonosupport};
kind(_, _, Protocol) when is_integer(Protocol) ->
    {error, eprotonosupport};
kind(_, _, Protocol) ->
    {error, {invalid, {protocol, Protocol}}}.

protocol_name(default) -> ip;
protocol_name(Protocol) -> Protocol.

%% Close the socket; {error, closed} once it is.
close(Socket) ->
    case open_info(Socket) of
        {ok, #{port := Port}} ->
            ?SOCKET(Ref) = Socket,
            update(Ref, fun closed/1),
            clause_socket:close(Port),
            ok;
        Error ->
            Error
    end.

shutdown(Socket, How) when How =:= read; How =:= write; How =:= read_write ->
    with_port(Socket, fun(#{port := Port}) ->
        Code =
            case How of
                read -> 0;
                write -> 1;
                read_write -> 2
            end,
        plain(clause_socket:control(Port, ?SHUTDOWN, <<Code>>))
    end);
shutdown(Socket, How) ->
    erlang:error(badarg, [Socket, How]).

with_port(Socket, Fun) ->
    case open_info(Socket) of
        {ok, Info} -> Fun(Info);
        Error -> Error
    end.

plain({ok, _}) -> ok;
plain(Error) -> Error.

%% --- Addresses ---

% A socket address (a map or any/loopback/broadcast) as {Address, Port}, or badarg.
endpoint(#{family := Family} = SockAddr, _) when Family =:= inet; Family =:= inet6 ->
    Port = maps:get(port, SockAddr, 0),
    {address(maps:get(addr, SockAddr, any), Family), Port};
endpoint(Name, Domain) when Name =:= any; Name =:= loopback; Name =:= broadcast ->
    {address(Name, Domain), 0};
endpoint(Other, _) ->
    erlang:error(badarg, [Other]).

address(any, inet) -> {0, 0, 0, 0};
address(any, inet6) -> {0, 0, 0, 0, 0, 0, 0, 0};
address(loopback, inet) -> {127, 0, 0, 1};
address(loopback, inet6) -> {0, 0, 0, 0, 0, 0, 0, 1};
address(broadcast, inet) -> {255, 255, 255, 255};
address(Address, _) when is_tuple(Address) -> Address;
address(Other, _) -> erlang:error(badarg, [Other]).

sockaddr({Address, Port}) when tuple_size(Address) =:= 4 -> #{family => inet, addr => Address, port => Port};
sockaddr({Address, Port}) -> #{family => inet6, addr => Address, port => Port}.

%% Bind to a local address: a stream socket before it listens or connects, a datagram socket for receiving.
bind(Socket, Addr) ->
    with_port(Socket, fun(#{port := Port, type := Type, domain := Domain}) ->
        {Address, Number} = endpoint(Addr, Domain),
        Operation =
            case Type of
                stream -> ?BIND;
                dgram -> ?UDP_OPEN
            end,
        Mode =
            case Type of
                stream -> ?UNCHANGED;
                dgram -> <<0, 0, 1>>
            end,
        case clause_socket:control(Port, Operation, [clause_socket:address(Address), <<Number:16>>, Mode]) of
            {ok, _} -> add_state(Socket, wstates, bound);
            Error -> Error
        end
    end).

% SCTP's bindx, peeloff and association names are not supported (as OTP on Windows).
bind(Socket, Addrs, Action) -> erlang:error(notsup, [Socket, Addrs, Action]).

peeloff(Socket, Assoc) -> erlang:error(notsup, [Socket, Assoc]).

peeloff(Socket, Assoc, Options) -> erlang:error(notsup, [Socket, Assoc, Options]).

socknames(Socket, Assoc) -> erlang:error(notsup, [Socket, Assoc]).

peernames(Socket, Assoc) -> erlang:error(notsup, [Socket, Assoc]).

add_state(?SOCKET(Ref), Key, Value) ->
    update(Ref, fun(Info) -> Info#{Key := lists:usort([Value | maps:get(Key, Info)])} end),
    ok.

sockname(Socket) ->
    with_port(Socket, fun(#{port := Port}) ->
        case clause_socket:names(Port, false) of
            {ok, Name} -> {ok, sockaddr(Name)};
            {error, enotconn} -> {error, einval};
            Error -> Error
        end
    end).

peername(Socket) ->
    with_port(Socket, fun(#{port := Port}) ->
        case clause_socket:names(Port, true) of
            {ok, Name} -> {ok, sockaddr(Name)};
            {error, einval} -> {error, enotconn};
            Error -> Error
        end
    end).

%% --- Connections ---

listen(Socket) -> listen(Socket, 5).

listen(Socket, Backlog) when is_integer(Backlog), Backlog >= 0 ->
    with_port(Socket, fun
        (#{type := stream, port := Port, domain := Domain}) ->
            Data = [clause_socket:address(address(any, Domain)), <<0:16, Backlog:32>>, ?UNCHANGED],
            case clause_socket:control(Port, ?LISTEN, Data) of
                {ok, _} ->
                    add_state(Socket, wstates, bound),
                    add_state(Socket, rstates, listening);
                Error ->
                    Error
            end;
        (#{wstates := WStates}) ->
            case os:type() =:= {win32, nt} andalso not lists:member(bound, WStates) of
                true -> {error, not_bound};
                false -> {error, eopnotsupp}
            end
    end);
listen(Socket, Backlog) ->
    erlang:error(badarg, [Socket, Backlog]).

accept(Socket) -> accept(Socket, infinity).

%% The next connection, as a socket owned by the caller.
accept(Socket, Timeout) ->
    case open_info(Socket) of
        {ok, Info} -> maybe_async(Socket, accept, Timeout, fun(T, Caller) -> accept_now(Socket, Info, T, Caller) end);
        Error -> Error
    end.

accept_now(_, #{port := Port} = Info, Timeout, Caller) ->
    accepted(request(Port, ?ACCEPT, <<>>, Timeout), Info, Caller).

accepted({ok, Port}, #{domain := Domain, type := Type, protocol := Protocol}, Caller) ->
    Caller =:= self() orelse
        begin
            erlang:port_connect(Port, Caller),
            unlink(Port)
        end,
    Ref = make_ref(),
    ets_insert(Ref, #{
        port => Port,
        domain => Domain,
        type => Type,
        protocol => Protocol,
        owner => Caller,
        state => open,
        rstates => [],
        wstates => [bound, connected],
        meta => undefined,
        buffer => <<>>,
        helpers => #{},
        ready => #{},
        counters => counters()
    }),
    ok = registry_call({add, Ref, Port, Caller}),
    {ok, ?SOCKET(Ref)};
accepted(Error, _, _) ->
    Error.

connect(Socket) ->
    case ready(Socket, connect) of
        {ok, Result} -> Result;
        none -> erlang:error({invalid, state}, [Socket])
    end.

connect(Socket, Addr) -> connect(Socket, Addr, infinity).

%% Connect a stream socket to Addr (on Windows it must be bound first, as OTP's); a datagram socket only sends there.
connect(Socket, Addr, Timeout) ->
    case open_info(Socket) of
        {ok, #{wstates := WStates, type := Type} = Info} ->
            case os:type() =:= {win32, nt} andalso Type =:= stream andalso not lists:member(bound, WStates) of
                true -> {error, not_bound};
                false -> maybe_async(Socket, connect, Timeout, fun(T, _) -> connect_now(Socket, Info, Addr, T) end)
            end;
        Error ->
            Error
    end.

connect_now(Socket, #{port := Port, domain := Domain, type := stream}, Addr, Timeout) ->
    {Address, Number} = endpoint(Addr, Domain),
    Data = [clause_socket:address(Address), <<Number:16>>, ?UNCHANGED],
    case request(Port, ?CONNECT, Data, Timeout) of
        ok -> add_state(Socket, wstates, connected);
        Error -> Error
    end;
connect_now(Socket, #{domain := Domain}, Addr, _) ->
    ?SOCKET(Ref) = Socket,
    update(Ref, fun(Info) -> Info#{peer => endpoint(Addr, Domain)} end),
    add_state(Socket, wstates, connected).

% Start Operation on the driver and wait for its reply; on time-out cancel it: what arrived before, or timeout. A
% helper running a nowait operation is asked to cancel it here, as the driver cancels its caller's requests.
request(Port, Operation, Data, Timeout) ->
    case clause_socket:control(Port, Operation, Data) of
        {ok, _} ->
            receive
                {clause_socket, Port, Reply} ->
                    Reply;
                {cancel_request, From, Tag} ->
                    _ = cancel_request(Port),
                    From ! {Tag, ok},
                    exit(normal)
            after Timeout -> cancel_request(Port)
            end;
        Error ->
            Error
    end.

% The reply that arrived before the cancellation, or {error, timeout}.
cancel_request(Port) ->
    case clause_socket:control(Port, ?CANCEL, <<>>) of
        {ok, _} ->
            receive
                {clause_socket, Port, cancelled} ->
                    {error, timeout};
                {clause_socket, Port, Reply} ->
                    receive
                        {clause_socket, Port, cancelled} -> Reply
                    end
            end;
        {error, closed} ->
            receive
                {clause_socket, Port, Reply} -> Reply
            end
    end.

%% --- Sending ---

send(Socket, Data) -> send(Socket, Data, [], infinity).

send(Socket, Data, Flags) when is_list(Flags) -> send(Socket, Data, Flags, infinity);
send(Socket, Data, Timeout) -> send(Socket, Data, [], Timeout).

send(Socket, Data, Flags, Timeout) when is_list(Flags) ->
    case open_info(Socket) of
        {ok, #{type := stream, port := Port}} ->
            Bytes = iolist_to_binary(Data),
            maybe_async(Socket, send, Timeout, fun(_, _) ->
                sent(Socket, Bytes, clause_socket:control(Port, ?SEND, Bytes))
            end);
        {ok, #{peer := Peer} = Info} ->
            datagram(Socket, Info, Data, Peer);
        {ok, _} ->
            {error, enotconn};
        Error ->
            Error
    end;
send(Socket, Data, Flags, Timeout) ->
    erlang:error(badarg, [Socket, Data, Flags, Timeout]).

sent(Socket, Bytes, {ok, _}) ->
    count(Socket, [{write_byte, byte_size(Bytes)}, {write_pkg, 1}, {write_tries, 1}]),
    ok;
sent(Socket, _, Error) ->
    count(Socket, [{write_fails, 1}, {write_tries, 1}]),
    Error.

sendto(Socket, Data, Dest) -> sendto(Socket, Data, Dest, [], infinity).

sendto(Socket, Data, Dest, Flags) when is_list(Flags) -> sendto(Socket, Data, Dest, Flags, infinity);
sendto(Socket, Data, Dest, Timeout) -> sendto(Socket, Data, Dest, [], Timeout).

%% A datagram to Dest; a stream socket sends Data on its connection.
sendto(Socket, Data, Dest, Flags, Timeout) when is_list(Flags) ->
    case open_info(Socket) of
        {ok, #{type := dgram, domain := Domain} = Info} -> datagram(Socket, Info, Data, endpoint(Dest, Domain));
        {ok, _} -> send(Socket, Data, Flags, Timeout);
        Error -> Error
    end;
sendto(Socket, Data, Dest, Flags, Timeout) ->
    erlang:error(badarg, [Socket, Data, Dest, Flags, Timeout]).

% Send a datagram, binding an unbound socket to any free port first, as the system does.
datagram(
    ?SOCKET(Ref) = Socket,
    #{port := Port, wstates := WStates, domain := Domain} = Info,
    Data,
    {Address, Number}
) ->
    Bound =
        case lists:member(bound, WStates) orelse maps:get(implicit, Info, false) of
            true ->
                ok;
            % The system binds a socket that sends first; OTP does not count it as bound.
            false ->
                plain(
                    clause_socket:control(Port, ?UDP_OPEN, [
                        clause_socket:address(address(any, Domain)), <<0:16, 0, 0, 1>>
                    ])
                )
        end,
    case Bound of
        ok ->
            update(Ref, fun(I) -> I#{implicit => true} end),
            Bytes = iolist_to_binary(Data),
            Request = [<<Number:16>>, clause_socket:address(Address), Bytes],
            sent(Socket, Bytes, clause_socket:control(Port, ?UDP_SEND, Request));
        Error ->
            Error
    end.

sendmsg(Socket, Msg) -> sendmsg(Socket, Msg, [], infinity).

sendmsg(Socket, Msg, Flags) when is_list(Flags) -> sendmsg(Socket, Msg, Flags, infinity);
sendmsg(Socket, Msg, Timeout) -> sendmsg(Socket, Msg, [], Timeout).

%% The message's iov, to its addr when it has one.
sendmsg(Socket, #{iov := IOV} = Msg, Flags, Timeout) when is_list(Flags) ->
    case Msg of
        #{addr := Dest} -> sendto(Socket, IOV, Dest, Flags, Timeout);
        _ -> send(Socket, IOV, Flags, Timeout)
    end;
sendmsg(Socket, Msg, Flags, Timeout) ->
    erlang:error(badarg, [Socket, Msg, Flags, Timeout]).

sendv(Socket, IOV) -> sendv(Socket, IOV, infinity).

sendv(Socket, IOV, Timeout) -> send(Socket, IOV, [], Timeout).

sendv(Socket, IOV, Flags, Timeout) -> send(Socket, IOV, Flags, Timeout).

%% {ok, N}: every message sent in turn.
sendmmsg(Socket, Msgs, Flags, Timeout) when is_list(Msgs) ->
    Sent = lists:takewhile(fun(Msg) -> sendmsg(Socket, Msg, Flags, Timeout) =:= ok end, Msgs),
    {ok, length(Sent)}.

%% The bytes of IOV after the first Written.
rest_iov(0, IOV) ->
    IOV;
rest_iov(Written, [Bin | IOV]) when is_binary(Bin) ->
    case byte_size(Bin) of
        Size when Size =< Written -> rest_iov(Written - Size, IOV);
        _ -> [binary:part(Bin, Written, byte_size(Bin) - Written) | IOV]
    end;
rest_iov(_, []) ->
    [].

sendfile(Socket, File) -> sendfile(Socket, File, 0, 0, infinity).

sendfile(Socket, File, Timeout) -> sendfile(Socket, File, 0, 0, Timeout).

sendfile(Socket, File, Offset, Count) -> sendfile(Socket, File, Offset, Count, infinity).

%% {ok, Sent}: Count bytes of File from Offset (0: to its end) sent on the connection.
sendfile(Socket, File, Offset, Count, Timeout) ->
    Size =
        case Count of
            0 -> 1 bsl 40;
            _ -> Count
        end,
    case file:pread(File, Offset, Size) of
        {ok, Bytes} ->
            case send(Socket, Bytes, [], Timeout) of
                ok -> {ok, byte_size(Bytes)};
                Error -> Error
            end;
        eof ->
            {ok, 0};
        Error ->
            Error
    end.

%% --- Receiving ---

recv(Socket) -> recv(Socket, 0, [], infinity).

recv(Socket, Flags) when is_list(Flags) -> recv(Socket, 0, Flags, infinity);
recv(Socket, Length) -> recv(Socket, Length, [], infinity).

recv(Socket, Flags, Timeout) when is_list(Flags) -> recv(Socket, 0, Flags, Timeout);
recv(Socket, Length, Flags) when is_list(Flags) -> recv(Socket, Length, Flags, infinity);
recv(Socket, Length, Timeout) -> recv(Socket, Length, [], Timeout).

%% Length bytes of a connection (0: what has arrived), or a datagram truncated to Length.
recv(Socket, Length, Flags, Timeout) when is_integer(Length), Length >= 0, is_list(Flags) ->
    case open_info(Socket) of
        {ok, #{type := stream}} ->
            maybe_async(Socket, recv, Timeout, fun(T, _) -> stream_recv(Socket, Length, deadline(T)) end);
        {ok, #{type := dgram}} ->
            maybe_async(Socket, recv, Timeout, fun(T, _) ->
                case datagram_recv(Socket, Length, T) of
                    {ok, {_, Data}} -> {ok, Data};
                    Error -> Error
                end
            end);
        Error ->
            Error
    end;
recv(Socket, Length, Flags, Timeout) ->
    erlang:error(badarg, [Socket, Length, Flags, Timeout]).

deadline(infinity) -> infinity;
deadline(Timeout) -> erlang:monotonic_time(millisecond) + Timeout.

left(infinity) -> infinity;
left(Deadline) -> max(0, Deadline - erlang:monotonic_time(millisecond)).

% Take Length bytes from the buffer, reading more until there are enough; what was read stays on an error.
stream_recv(?SOCKET(Ref) = Socket, Length, Deadline) ->
    {ok, #{buffer := Buffer, port := Port}} = lookup(Ref),
    if
        Length =:= 0, Buffer =/= <<>>; Length > 0, byte_size(Buffer) >= Length ->
            Taken = take(Ref, Buffer, Length),
            count(Socket, [{read_byte, byte_size(Taken)}, {read_pkg, 1}, {read_tries, 1}]),
            {ok, Taken};
        true ->
            case request(Port, ?RECV, <<0:32>>, left(Deadline)) of
                {ok, Data} ->
                    update(Ref, fun(#{buffer := B} = Info) -> Info#{buffer := <<B/binary, Data/binary>>} end),
                    stream_recv(Socket, Length, Deadline);
                {error, Reason} ->
                    count(Socket, [{read_fails, 1}, {read_tries, 1}]),
                    partial_error(Ref, Reason)
            end
    end.

take(Ref, Buffer, 0) ->
    update(Ref, fun(Info) -> Info#{buffer := <<>>} end),
    Buffer;
take(Ref, Buffer, Length) ->
    <<Taken:Length/binary, Rest/binary>> = Buffer,
    update(Ref, fun(Info) -> Info#{buffer := Rest} end),
    Taken.

% An error with the bytes received before it, which it consumes.
partial_error(Ref, Reason) ->
    case lookup(Ref) of
        {ok, #{buffer := <<>>}} ->
            {error, Reason};
        {ok, #{buffer := Buffer}} ->
            update(Ref, fun(Info) -> Info#{buffer := <<>>} end),
            {error, {Reason, Buffer}};
        _ ->
            {error, Reason}
    end.

% {ok, {{Address, Port}, Data}} of the next datagram, its data truncated to Length (0: whole).
datagram_recv(Socket, Length, Timeout) ->
    {ok, #{port := Port}} = open_info(Socket),
    case request(Port, ?UDP_RECV, <<>>, Timeout) of
        {ok, {Address, Number, Data}} ->
            count(Socket, [{read_byte, byte_size(Data)}, {read_pkg, 1}, {read_tries, 1}]),
            {ok, {{Address, Number}, truncate(Data, Length)}};
        Error ->
            count(Socket, [{read_fails, 1}, {read_tries, 1}]),
            Error
    end.

truncate(Data, 0) -> Data;
truncate(Data, Length) when byte_size(Data) > Length -> binary:part(Data, 0, Length);
truncate(Data, _) -> Data.

recvfrom(Socket) -> recvfrom(Socket, 0, [], infinity).

recvfrom(Socket, Flags) when is_list(Flags) -> recvfrom(Socket, 0, Flags, infinity);
recvfrom(Socket, BufSz) -> recvfrom(Socket, BufSz, [], infinity).

recvfrom(Socket, Flags, Timeout) when is_list(Flags) -> recvfrom(Socket, 0, Flags, Timeout);
recvfrom(Socket, BufSz, Flags) when is_list(Flags) -> recvfrom(Socket, BufSz, Flags, infinity);
recvfrom(Socket, BufSz, Timeout) -> recvfrom(Socket, BufSz, [], Timeout).

%% {ok, {Source, Data}}: a datagram and its source address; a connection gives its data from its peer.
recvfrom(Socket, BufSz, Flags, Timeout) when is_integer(BufSz), BufSz >= 0, is_list(Flags) ->
    case open_info(Socket) of
        {ok, #{type := dgram}} ->
            maybe_async(Socket, recvfrom, Timeout, fun(T, _) ->
                case datagram_recv(Socket, BufSz, T) of
                    {ok, {From, Data}} -> {ok, {sockaddr(From), Data}};
                    Error -> Error
                end
            end);
        {ok, _} ->
            case recv(Socket, BufSz, Flags, Timeout) of
                {ok, Data} -> {ok, {element(2, peername(Socket)), Data}};
                Other -> Other
            end;
        Error ->
            Error
    end;
recvfrom(Socket, BufSz, Flags, Timeout) ->
    erlang:error(badarg, [Socket, BufSz, Flags, Timeout]).

recvmsg(Socket) -> recvmsg(Socket, 0, 0, [], infinity).

recvmsg(Socket, Flags) when is_list(Flags) -> recvmsg(Socket, 0, 0, Flags, infinity);
recvmsg(Socket, Timeout) -> recvmsg(Socket, 0, 0, [], Timeout).

recvmsg(Socket, Flags, Timeout) when is_list(Flags) -> recvmsg(Socket, 0, 0, Flags, Timeout);
recvmsg(Socket, BufSz, CtrlSz) -> recvmsg(Socket, BufSz, CtrlSz, [], infinity).

recvmsg(Socket, BufSz, CtrlSz, Flags) when is_list(Flags) -> recvmsg(Socket, BufSz, CtrlSz, Flags, infinity);
recvmsg(Socket, BufSz, CtrlSz, Timeout) -> recvmsg(Socket, BufSz, CtrlSz, [], Timeout).

%% {ok, #{iov, ctrl, flags}} with the source addr of a datagram; no control messages are received.
recvmsg(Socket, BufSz, CtrlSz, Flags, Timeout) when is_integer(CtrlSz), is_list(Flags) ->
    case open_info(Socket) of
        {ok, #{type := dgram}} ->
            case recvfrom(Socket, BufSz, Flags, Timeout) of
                {ok, {From, Data}} -> {ok, #{addr => From, iov => [Data], ctrl => [], flags => []}};
                Other -> Other
            end;
        {ok, _} ->
            case recv(Socket, BufSz, Flags, Timeout) of
                {ok, Data} -> {ok, #{iov => [Data], ctrl => [], flags => []}};
                Other -> Other
            end;
        Error ->
            Error
    end;
recvmsg(Socket, BufSz, CtrlSz, Flags, Timeout) ->
    erlang:error(badarg, [Socket, BufSz, CtrlSz, Flags, Timeout]).

%% {ok, Msgs}: up to N datagrams, the first waited for, the others those already there.
recvmmsg(Socket, N, BufSz, CtrlSz, Flags, Timeout) when is_integer(N), N > 0 ->
    case recvmsg(Socket, BufSz, CtrlSz, Flags, Timeout) of
        {ok, First} -> {ok, [First | more_msgs(Socket, N - 1, BufSz, CtrlSz, Flags)]};
        Error -> Error
    end.

more_msgs(_, 0, _, _, _) ->
    [];
more_msgs(Socket, N, BufSz, CtrlSz, Flags) ->
    case recvmsg(Socket, BufSz, CtrlSz, Flags, 0) of
        {ok, Msg} -> [Msg | more_msgs(Socket, N - 1, BufSz, CtrlSz, Flags)];
        _ -> []
    end.

%% --- Asynchronous operations ---

% A time-out runs Fun in the caller; nowait or a handle runs it in a helper process, whose answer comes as OTP's
% completion message (Windows) or is kept for the next call after a select message (elsewhere).
maybe_async(Socket, Operation, Timeout, Fun) when Operation =/= connect ->
    % What a select-style helper got answers the next call, as the system's readiness does.
    case ready(Socket, Operation) of
        {ok, Result} -> Result;
        none -> start(Socket, Operation, Timeout, Fun)
    end;
maybe_async(Socket, Operation, Timeout, Fun) ->
    start(Socket, Operation, Timeout, Fun).

start(?SOCKET(Ref) = Socket, recv, nowait, Fun) ->
    % Buffered stream bytes are read at once where readiness is selected (not Windows).
    case {os:type(), lookup(Ref)} of
        {{unix, _}, {ok, #{type := stream, buffer := Buffer}}} when Buffer =/= <<>> -> Fun(0, self());
        _ -> start_async(Socket, recv, nowait, Fun)
    end;
start(Socket, Operation, Handle, Fun) when Handle =:= nowait; is_reference(Handle) ->
    start_async(Socket, Operation, Handle, Fun);
start(_, _, Timeout, Fun) when Timeout =:= infinity; is_integer(Timeout), Timeout >= 0 ->
    Fun(Timeout, self());
start(Socket, _, Timeout, _) ->
    erlang:error(badarg, [Socket, Timeout]).

start_async(Socket, Operation, Handle0, Fun) ->
    Handle =
        case Handle0 of
            nowait -> make_ref();
            _ -> Handle0
        end,
    Caller = self(),
    Helper = spawn(fun() -> helper(Socket, Operation, Handle, Caller, Fun) end),
    ?SOCKET(Ref) = Socket,
    update(Ref, fun(#{helpers := H} = Info) -> Info#{helpers := H#{Handle => {Operation, Helper}}} end),
    case os:type() of
        {win32, _} -> {completion, {completion_info, Operation, Handle}};
        _ -> {select, {select_info, Operation, Handle}}
    end.

helper(?SOCKET(Ref) = Socket, Operation, Handle, Caller, Fun) ->
    Result = Fun(infinity, Caller),
    update(Ref, fun(#{helpers := H} = Info) -> Info#{helpers := maps:remove(Handle, H)} end),
    case os:type() of
        {win32, _} ->
            Caller ! {'$socket', Socket, completion, {Handle, Result}};
        _ ->
            keep_ready(Ref, Operation, Result),
            Caller ! {'$socket', Socket, select, Handle}
    end.

% A result that a select-style helper got, for the next call of its operation.
keep_ready(Ref, Operation, Result) ->
    update(Ref, fun(#{ready := R} = Info) -> Info#{ready := R#{Operation => Result}} end).

ready(?SOCKET(Ref), Operation) ->
    case lookup(Ref) of
        {ok, #{ready := #{Operation := Result} = Ready}} ->
            update(Ref, fun(Info) -> Info#{ready := maps:remove(Operation, Ready)} end),
            {ok, Result};
        _ ->
            none
    end.

%% Stop an operation started with nowait; {error, not_found} when there is none.
cancel(?SOCKET(Ref) = Socket, {Kind, Operation, Handle}) when Kind =:= completion_info; Kind =:= select_info ->
    case lookup(Ref) of
        {ok, #{helpers := #{Handle := {Operation, Helper}}}} ->
            Tag = erlang:monitor(process, Helper),
            Helper ! {cancel_request, self(), Tag},
            receive
                {Tag, ok} -> erlang:demonitor(Tag, [flush]);
                {'DOWN', Tag, _, _, _} -> ok
            end,
            update(Ref, fun(#{helpers := H} = Info) -> Info#{helpers := maps:remove(Handle, H)} end),
            ok;
        {ok, _} ->
            {error, not_found};
        Error ->
            Error
    end;
cancel(Socket, Info) ->
    erlang:error(badarg, [Socket, Info]).

%% --- Options ---

getopt(Socket, Level, Opt) -> getopt(Socket, {Level, Opt}).

setopt(Socket, Level, Opt, Value) -> setopt(Socket, {Level, Opt}, Value).

%% {ok, Value} of a socket option {Level, Opt}, or OTP's invalid error.
getopt(Socket, {otp, Opt}) ->
    with_info(Socket, fun(Info) -> otp_option(Opt, Info) end);
getopt(Socket, {socket, Opt} = Option) when Opt =:= type; Opt =:= domain; Opt =:= protocol; Opt =:= acceptconn ->
    with_info(Socket, fun(Info) -> socket_option(Option, Info) end);
getopt(Socket, Option) ->
    case option_name(Option) of
        {ok, Name} ->
            with_port(Socket, fun(#{port := Port}) ->
                case clause_socket:getopts(Port, [Name]) of
                    {ok, [{Name, Value}]} -> {ok, option_value(Option, Value)};
                    {ok, []} -> {error, {invalid, {socket_option, Option}}};
                    Error -> Error
                end
            end);
        error ->
            {error, {invalid, {socket_option, Option}}}
    end.

with_info(?SOCKET(Ref), Fun) ->
    case lookup(Ref) of
        {ok, Info} -> Fun(Info);
        Error -> Error
    end;
with_info(Socket, _) ->
    erlang:error(badarg, [Socket]).

otp_option(controlling_process, #{owner := Owner}) -> {ok, Owner};
otp_option(meta, #{meta := Meta}) -> {ok, Meta};
otp_option(rcvbuf, _) -> {ok, 8192};
otp_option(rcvctrlbuf, _) -> {ok, 1024};
otp_option(sndctrlbuf, _) -> {ok, 1024};
otp_option(debug, _) -> {ok, false};
otp_option(iow, _) -> {ok, false};
otp_option(use_registry, _) -> {ok, true};
otp_option(domain, #{domain := Domain}) -> {ok, Domain};
otp_option(fd, _) -> {ok, -1};
otp_option(Opt, _) -> {error, {invalid, {socket_option, {otp, Opt}}}}.

% The options the socket itself knows; Windows has no domain or protocol option, as OTP there.
socket_option({socket, type}, #{type := Type}) ->
    {ok, Type};
socket_option({socket, acceptconn}, #{rstates := RStates}) ->
    {ok, lists:member(listening, RStates)};
socket_option({socket, Opt} = Option, Info) ->
    case os:type() of
        {win32, _} -> {error, {invalid, {socket_option, Option}}};
        _ -> {ok, maps:get(Opt, Info)}
    end.

% The driver's name of a socket option.
option_name({socket, reuseaddr}) -> {ok, reuseaddr};
option_name({socket, reuseport}) -> {ok, reuseport};
option_name({socket, keepalive}) -> {ok, keepalive};
option_name({socket, dontroute}) -> {ok, dontroute};
option_name({socket, linger}) -> {ok, linger};
option_name({socket, broadcast}) -> {ok, broadcast};
option_name({socket, sndbuf}) -> {ok, sndbuf};
option_name({socket, rcvbuf}) -> {ok, recbuf};
option_name({socket, priority}) -> {ok, priority};
option_name({socket, debug}) -> {ok, debug};
option_name({tcp, nodelay}) -> {ok, nodelay};
option_name({ip, tos}) -> {ok, tos};
option_name({ip, multicast_ttl}) -> {ok, multicast_ttl};
option_name({ip, multicast_loop}) -> {ok, multicast_loop};
option_name({ip, multicast_if}) -> {ok, multicast_if};
option_name({ip, add_membership}) -> {ok, add_membership};
option_name({ip, drop_membership}) -> {ok, drop_membership};
option_name({ipv6, v6only}) -> {ok, ipv6_v6only};
option_name({ipv6, tclass}) -> {ok, tclass};
option_name(_) -> error.

% A driver value as socket's: linger as a map.
option_value({socket, linger}, {OnOff, Seconds}) -> #{onoff => OnOff, linger => Seconds};
option_value(_, Value) -> Value.

driver_value({socket, linger}, #{onoff := OnOff, linger := Seconds}) ->
    {ok, {OnOff, Seconds}};
driver_value({socket, linger}, _) ->
    error;
driver_value({ip, Membership}, #{multiaddr := Group, interface := Interface}) when
    Membership =:= add_membership; Membership =:= drop_membership
->
    {ok, {Group, Interface}};
driver_value(_, Value) ->
    {ok, Value}.

%% Set a socket option; {otp, controlling_process} gives the socket another owner, as only its owner may.
setopt(Socket, {otp, Opt} = Option, Value) ->
    with_port(Socket, fun(Info) -> set_otp_option(Socket, Option, Opt, Value, Info) end);
setopt(Socket, Option, Value) ->
    case {option_name(Option), driver_value(Option, Value)} of
        {{ok, Name}, {ok, Driver}} ->
            with_port(Socket, fun(#{port := Port}) ->
                case clause_socket:setopts(Port, [{Name, Driver}]) of
                    ok -> ok;
                    {error, einval} -> {error, {invalid, {socket_option, Option, Value}}};
                    Error -> Error
                end
            end);
        {error, _} ->
            {error, {invalid, {socket_option, Option}}};
        _ ->
            {error, {invalid, {socket_option, Option, Value}}}
    end.

set_otp_option(?SOCKET(Ref), _, controlling_process, Pid, #{owner := Owner, port := Port}) when is_pid(Pid) ->
    case self() of
        Owner ->
            erlang:port_connect(Port, Pid),
            unlink(Port),
            update(Ref, fun(Info) -> Info#{owner := Pid} end),
            registry_call({owner, Pid});
        _ ->
            {error, {invalid, not_owner}}
    end;
set_otp_option(?SOCKET(Ref), _, meta, Value, _) ->
    update(Ref, fun(Info) -> Info#{meta := Value} end);
set_otp_option(_, _, Opt, Value, _) when
    (Opt =:= debug orelse Opt =:= iow orelse Opt =:= use_registry) andalso is_boolean(Value)
->
    ok;
set_otp_option(_, _, Opt, Value, _) when
    (Opt =:= rcvbuf orelse Opt =:= rcvctrlbuf orelse Opt =:= sndctrlbuf) andalso is_integer(Value) andalso Value > 0
->
    ok;
set_otp_option(_, Option, _, Value, _) ->
    {error, {invalid, {socket_option, Option, Value}}}.

%% Native options by level and number are not supported (docs/differences.md).
getopt_native(_, Option, _) -> {error, {invalid, {socket_option, Option}}}.

setopt_native(_, Option, Value) -> {error, {invalid, {socket_option, Option, Value}}}.

%% Interface requests are not supported (as OTP on Windows).
ioctl(_, Request) -> {error, {invalid, {ioctl_request, Request}}}.

ioctl(_, Request, _) -> {error, {invalid, {ioctl_request, Request}}}.

ioctl(_, Request, _, _) -> {error, {invalid, {ioctl_request, Request}}}.

%% --- Information ---

counters() ->
    maps:from_keys(
        [
            read_byte,
            read_fails,
            read_pkg,
            read_pkg_max,
            read_tries,
            read_waits,
            write_byte,
            write_fails,
            write_pkg,
            write_pkg_max,
            write_tries,
            write_waits,
            acc_success,
            acc_fails,
            acc_tries,
            acc_waits
        ],
        0
    ).

count(?SOCKET(Ref), Increments) ->
    update(Ref, fun(#{counters := C} = Info) ->
        Info#{counters := lists:foldl(fun add_count/2, C, Increments)}
    end).

add_count({Key, N}, Counters) -> maps:update_with(Key, fun(V) -> V + N end, Counters).

%% The socket's kind, owner, states and counters.
info(?SOCKET(Ref)) ->
    case lookup(Ref) of
        {ok, Info} ->
            info_extra(maps:with([counters, owner, type, protocol, domain, rstates, wstates], Info), Info);
        {error, _} ->
            erlang:error(badarg, [?SOCKET(Ref)])
    end;
info(Socket) ->
    erlang:error(badarg, [Socket]).

info_extra(Map, #{helpers := Helpers}) ->
    Count = fun(Ops) -> length([H || _ := {Op, H} <- Helpers, lists:member(Op, Ops)]) end,
    Map#{
        ctype => normal,
        num_acceptors => Count([accept]),
        num_readers => Count([recv, recvfrom, recvmsg]),
        num_writers => Count([send, sendto, sendmsg])
    }.

%% The library's own information.
info() ->
    Sockets = open_rows(),
    Count = fun(Key, Value) -> length([I || I <- Sockets, maps:get(Key, I) =:= Value]) end,
    #{
        counters => #{
            num_sockets => length(Sockets),
            num_dom_inet => Count(domain, inet),
            num_dom_inet6 => Count(domain, inet6),
            num_dom_local => 0,
            num_type_stream => Count(type, stream),
            num_type_dgram => Count(type, dgram),
            num_type_seqpacket => 0,
            num_proto_tcp => Count(protocol, tcp),
            num_proto_udp => Count(protocol, udp),
            num_proto_sctp => 0
        },
        debug => false,
        use_registry => true,
        socket_debug => false,
        iow => false,
        eei => false,
        iov_max => 1024,
        io_backend => #{name => clause}
    }.

open_rows() ->
    _ = registry(),
    [Info#{ref => Ref} || {Ref, #{state := open} = Info} <- ets:tab2list(?TABLE)].

number_of() -> length(open_rows()).

which_sockets() -> [?SOCKET(maps:get(ref, I)) || I <- open_rows()].

%% The sockets of a domain, type, protocol, owner or the ones a fun of their info keeps.
which_sockets(Filter) when is_function(Filter, 1) ->
    [?SOCKET(Ref) || #{ref := Ref} <- open_rows(), Filter(info(?SOCKET(Ref)))];
which_sockets(Owner) when is_pid(Owner) ->
    [?SOCKET(Ref) || #{ref := Ref, owner := O} <- open_rows(), O =:= Owner];
which_sockets(Domain) when Domain =:= inet; Domain =:= inet6; Domain =:= local ->
    [?SOCKET(Ref) || #{ref := Ref, domain := D} <- open_rows(), D =:= Domain];
which_sockets(Type) when Type =:= stream; Type =:= dgram; Type =:= seqpacket ->
    [?SOCKET(Ref) || #{ref := Ref, type := T} <- open_rows(), T =:= Type];
which_sockets(Protocol) when is_atom(Protocol) ->
    [?SOCKET(Ref) || #{ref := Ref, protocol := P} <- open_rows(), P =:= Protocol];
which_sockets(Filter) ->
    erlang:error(badarg, [Filter]).

monitor(?SOCKET(Ref)) -> registry_call({monitor, self(), Ref});
monitor(Socket) -> erlang:error(badarg, [Socket]).

cancel_monitor(Mref) when is_reference(Mref) -> registry_call({cancel_monitor, Mref});
cancel_monitor(Mref) -> erlang:error(badarg, [Mref]).

use_registry(Flag) when is_boolean(Flag) -> ok.

%% Platform features in OTP's shape.
supports() ->
    [
        {ioctl_requests, []},
        {ioctl_flags, []},
        {options, supports(options)},
        {msg_flags, []},
        {protocols, supports(protocols)},
        {sctp, false},
        {ipv6, true},
        {local, false},
        {netns, false},
        {sendfile, true}
    ].

supports(options) ->
    Options = [
        {socket, acceptconn},
        {socket, broadcast},
        {socket, debug},
        {socket, domain},
        {socket, dontroute},
        {socket, keepalive},
        {socket, linger},
        {socket, priority},
        {socket, protocol},
        {socket, rcvbuf},
        {socket, reuseaddr},
        {socket, reuseport},
        {socket, sndbuf},
        {socket, type},
        {ip, add_membership},
        {ip, drop_membership},
        {ip, multicast_if},
        {ip, multicast_loop},
        {ip, multicast_ttl},
        {ip, tos},
        {ipv6, tclass},
        {ipv6, v6only},
        {tcp, nodelay}
    ],
    [{Option, true} || Option <- Options];
supports(protocols) ->
    [{ipv6, true}, {ip, true}, {udp, true}, {tcp, true}];
supports(Key) when Key =:= ioctl_requests; Key =:= ioctl_flags; Key =:= msg_flags ->
    [];
supports(_) ->
    [].

supports(Key1, Key2) ->
    case lists:keyfind(Key2, 1, supports(Key1)) of
        {_, Value} -> Value;
        false -> false
    end.

is_supported(Key) ->
    case lists:keyfind(Key, 1, supports()) of
        {_, true} -> true;
        _ -> false
    end.

is_supported(Key1, Key2) -> supports(Key1, Key2) =:= true.

is_supported(Key1, Key2, Key3) ->
    case supports(Key1, Key2) of
        List when is_list(List) -> proplists:get_value(Key3, List, false) =:= true;
        _ -> false
    end.

%% A table of the sockets as OTP's socket:i/0,1,2 prints it.
i() -> i(default_columns()).

i(Columns) when is_list(Columns) -> i(which_sockets(), Columns);
i(Filter) -> i(which_sockets(Filter), default_columns()).

i(Filter, Columns) when not is_list(Filter) -> i(which_sockets(Filter), Columns);
i(Sockets, Columns) ->
    Rows = [[cell(Column, S) || Column <- Columns] || S <- Sockets],
    Header = [header(Column) || Column <- Columns],
    Widths = [lists:max([length(C) || C <- Col]) || Col <- transpose([Header | Rows])],
    [
        io:format("~ts~n", [lists:flatten([string:pad(C, W + 1) || {C, W} <- lists:zip(Row, Widths)])])
     || Row <- [Header | Rows]
    ],
    ok.

transpose([[] | _]) -> [];
transpose(Rows) -> [[hd(R) || R <- Rows] | transpose([tl(R) || R <- Rows])].

default_columns() ->
    [domain, type, protocol, fd, owner, local_address, remote_address, recv, sent, state].

header(Column) ->
    Words = string:split(atom_to_list(Column), "_", all),
    lists:join($\s, [string:titlecase(W) || W <- Words]).

cell(Column, ?SOCKET(Ref) = Socket) ->
    case lookup(Ref) of
        {ok, Info} -> cell_text(Column, Socket, Info);
        _ -> ""
    end.

cell_text(domain, _, #{domain := D}) ->
    atom_to_list(D);
cell_text(type, _, #{type := T}) ->
    string:uppercase(atom_to_list(T));
cell_text(protocol, _, #{protocol := P}) ->
    string:uppercase(atom_to_list(P));
cell_text(fd, _, #{port := Port}) ->
    string:trim(lists:last(string:split(erlang:port_to_list(Port), ".")), trailing, ">");
cell_text(owner, _, #{owner := O}) ->
    pid_to_list(O);
cell_text(local_address, S, _) ->
    address_text(sockname(S));
cell_text(remote_address, S, _) ->
    address_text(peername(S));
cell_text(recv, _, #{counters := #{read_byte := N}}) ->
    integer_to_list(N);
cell_text(sent, _, #{counters := #{write_byte := N}}) ->
    integer_to_list(N);
cell_text(state, _, #{rstates := R, wstates := W}) ->
    states_text(R) ++ " , " ++ states_text(W);
cell_text(_, _, _) ->
    "".

address_text({ok, #{addr := {127, 0, 0, 1}, port := Port}}) -> "localhost:" ++ integer_to_list(Port);
address_text({ok, #{addr := Addr, port := Port}}) -> inet:ntoa(Addr) ++ ":" ++ integer_to_list(Port);
address_text(_) -> "".

states_text([]) -> "-";
states_text(States) -> lists:append([state_abbreviation(S) || S <- States]).

state_abbreviation(bound) -> "BD";
state_abbreviation(connected) -> "CD";
state_abbreviation(listening) -> "LG";
state_abbreviation(closing) -> "CG";
state_abbreviation(closed) -> "CL";
state_abbreviation(_) -> "".

%% The undocumented helpers: socket addresses, debug switches and monitor listings.
mk_sockaddr(Domain, Addr) -> mk_sockaddr(Domain, Addr, 0).

mk_sockaddr(Domain, Addr, Port) when Domain =:= inet; Domain =:= inet6 ->
    #{family => Domain, addr => Addr, port => Port}.

debug(Flag) when is_boolean(Flag) -> ok.

socket_debug(Flag) when is_boolean(Flag) -> ok.

which_socket_kind(Socket) ->
    case open_info(Socket) of
        {ok, _} -> plain;
        _ -> undefined
    end.

to_list(?SOCKET(Ref)) -> "#Socket" ++ lists:last(string:split(erlang:ref_to_list(Ref), "."));
to_list(Socket) -> erlang:error(badarg, [Socket]).

monitored_by(_) -> erlang:error({not_implemented, monitored_by, 1}).
number_of_monitors() -> erlang:error({not_implemented, number_of_monitors, 0}).
number_of_monitors(_) -> erlang:error({not_implemented, number_of_monitors, 1}).
option(_) -> erlang:error({not_implemented, option, 1}).
option(_, _) -> erlang:error({not_implemented, option, 2}).
options() -> erlang:error({not_implemented, options, 0}).
options(_) -> erlang:error({not_implemented, options, 1}).
options(_, _) -> erlang:error({not_implemented, options, 2}).
protocol(_) -> erlang:error({not_implemented, protocol, 1}).
protocols() -> erlang:error({not_implemented, protocols, 0}).
table(_) -> erlang:error({not_implemented, table, 1}).
tables() -> erlang:error({not_implemented, tables, 0}).
which_monitors(_) -> erlang:error({not_implemented, which_monitors, 1}).
