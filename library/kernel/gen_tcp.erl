-module(gen_tcp).
-export([
    listen/2,
    accept/1,
    accept/2,
    connect/3,
    connect/4,
    send/2,
    recv/2,
    recv/3,
    shutdown/2,
    controlling_process/2,
    close/1
]).
%% OTP 29 exports Clause does not implement yet: stubs raising error({not_implemented, F, A}),
%% or catalog builtins the runtime puts in their place (docs/library.md#runtime-module-replacements).
-export([
    connect/2,
    fdopen/2,
    unrecv/2
]).

% The project-owned gen_tcp module (docs/library.md, docs/ports.md#sockets): TCP sockets are ports of the runtime's
% socket driver, driven through clause_socket.

-define(CONNECT, 1).
-define(LISTEN, 2).
-define(ACCEPT, 3).
-define(RECV, 4).
-define(SEND, 5).
-define(SHUTDOWN, 9).

% OTP's defaults: list data, active messages, no packet header, IPv4.
defaults() ->
    #{family => 4, binary => false, active => true, packet => 0, reuseaddr => false, backlog => 5}.

% A socket listening on Port (0 for any free port) of its ip option, or {error, Reason}.
listen(Port, Options) when is_integer(Port), Port >= 0, Port =< 65535 ->
    Map = clause_socket:options(Options, defaults()),
    #{reuseaddr := Reuse, backlog := Backlog} = Map,
    Socket = clause_socket:open("tcp_inet"),
    Data = [clause_socket:local(Map), <<Port:16, Backlog:32, (flag(Reuse))>>, mode(Map)],
    case clause_socket:control(Socket, ?LISTEN, Data) of
        {ok, _} ->
            {ok, Socket};
        Error ->
            clause_socket:close(Socket),
            Error
    end;
listen(_, _) ->
    exit(badarg).

accept(Listen) -> accept(Listen, infinity).

% The next connection of Listen as a new socket owned by the caller, waiting at most Timeout.
accept(Listen, Timeout) -> clause_socket:request(Listen, ?ACCEPT, <<>>, Timeout).

connect(Address, Port, Options) -> connect(Address, Port, Options, infinity).

% A socket connected to Port at Address (a tuple, a host name string or an atom), or {error, Reason}.
connect(Address, Port, Options, Timeout) when is_integer(Port), Port >= 0, Port =< 65535 ->
    Map = clause_socket:options(Options, defaults()),
    Socket = clause_socket:open("tcp_inet"),
    case connect_to(Socket, Address, Port, Map, Timeout) of
        ok ->
            {ok, Socket};
        Error ->
            clause_socket:close(Socket),
            Error
    end;
connect(_, _, _, _) ->
    exit(badarg).

connect_to(Socket, Address, Port, #{family := Family} = Map, Timeout) ->
    case clause_socket:resolve(Socket, Address, Family) of
        {ok, Target} ->
            Data = [clause_socket:address(Target), <<Port:16>>, mode(Map)],
            clause_socket:request(Socket, ?CONNECT, Data, Timeout);
        Error ->
            Error
    end.

% Send Data (iodata) on Socket.
send(Socket, Data) ->
    try iolist_to_binary(Data) of
        Bytes -> ok(clause_socket:control(Socket, ?SEND, Bytes))
    catch
        error:badarg -> {error, einval}
    end.

recv(Socket, Length) -> recv(Socket, Length, infinity).

% Length bytes of a passive socket (0: what there is, or one packet), waiting at most Timeout.
recv(Socket, Length, Timeout) when is_integer(Length), Length >= 0 ->
    clause_socket:request(Socket, ?RECV, <<Length:32>>, Timeout);
recv(_, _, _) ->
    exit(badarg).

% Shut down reading, writing or both of a connected socket.
shutdown(Socket, read) -> ok(clause_socket:control(Socket, ?SHUTDOWN, <<0>>));
shutdown(Socket, write) -> ok(clause_socket:control(Socket, ?SHUTDOWN, <<1>>));
shutdown(Socket, read_write) -> ok(clause_socket:control(Socket, ?SHUTDOWN, <<2>>)).

controlling_process(Socket, Owner) -> clause_socket:controlling_process(Socket, Owner).

close(Socket) -> clause_socket:close(Socket).

mode(Map) -> clause_socket:mode(Map).

flag(true) -> 1;
flag(false) -> 0.

ok({ok, _}) -> ok;
ok(Error) -> Error.

connect(_, _) -> erlang:error({not_implemented, connect, 2}).
fdopen(_, _) -> erlang:error({not_implemented, fdopen, 2}).
unrecv(_, _) -> erlang:error({not_implemented, unrecv, 2}).
