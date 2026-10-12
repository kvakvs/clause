-module(gen_udp).
-export([open/1, open/2, send/4, recv/2, recv/3, controlling_process/2, close/1]).
%% OTP 29 exports Clause does not implement yet: stubs raising error({not_implemented, F, A}),
%% or catalog builtins the runtime puts in their place (docs/library.md#runtime-module-replacements).
-export([
    connect/2,
    connect/3,
    fdopen/2,
    send/2,
    send/3,
    send/5
]).

% The project-owned gen_udp module (docs/library.md, docs/ports.md#sockets): UDP sockets are ports of the runtime's
% socket driver, driven through clause_socket.

-define(UDP_OPEN, 10).
-define(UDP_SEND, 11).
-define(UDP_RECV, 12).

% OTP's defaults: list data, active messages, IPv4.
defaults() -> #{family => 4, binary => false, active => true, packet => 0}.

open(Port) -> open(Port, []).

% A socket bound to Port (0 for any free port) of its ip option, or {error, Reason}.
open(Port, Options) when is_integer(Port), Port >= 0, Port =< 65535 ->
    Map = clause_socket:options(Options, defaults()),
    Socket = clause_socket:open("udp_inet"),
    Data = [clause_socket:local(Map), <<Port:16>>, clause_socket:mode(Map)],
    case clause_socket:control(Socket, ?UDP_OPEN, Data) of
        {ok, _} ->
            {ok, Socket};
        Error ->
            clause_socket:close(Socket),
            Error
    end;
open(_, _) ->
    exit(badarg).

% Send Packet (iodata) to Port at Address (a tuple, a host name string or an atom).
send(Socket, Address, Port, Packet) when is_integer(Port), Port >= 0, Port =< 65535 ->
    case clause_socket:resolve(Socket, Address, family(Address)) of
        {ok, Target} -> send_to(Socket, Target, Port, Packet);
        Error -> Error
    end;
send(_, _, _, _) ->
    exit(badarg).

send_to(Socket, Target, Port, Packet) ->
    try iolist_to_binary(Packet) of
        Bytes ->
            Data = [<<Port:16>>, clause_socket:address(Target), Bytes],
            case clause_socket:control(Socket, ?UDP_SEND, Data) of
                {ok, _} -> ok;
                Error -> Error
            end
    catch
        error:badarg -> {error, einval}
    end.

family(Address) when tuple_size(Address) =:= 8 -> 6;
family(_) -> 4.

recv(Socket, Length) -> recv(Socket, Length, infinity).

% The next datagram of a passive socket, {ok, {Address, Port, Packet}}, waiting at most Timeout.
recv(Socket, Length, Timeout) when is_integer(Length), Length >= 0 ->
    clause_socket:request(Socket, ?UDP_RECV, <<>>, Timeout);
recv(_, _, _) ->
    exit(badarg).

controlling_process(Socket, Owner) -> clause_socket:controlling_process(Socket, Owner).

close(Socket) -> clause_socket:close(Socket).

connect(_, _) -> erlang:error({not_implemented, connect, 2}).
connect(_, _, _) -> erlang:error({not_implemented, connect, 3}).
fdopen(_, _) -> erlang:error({not_implemented, fdopen, 2}).
send(_, _) -> erlang:error({not_implemented, send, 2}).
send(_, _, _) -> erlang:error({not_implemented, send, 3}).
send(_, _, _, _, _) -> erlang:error({not_implemented, send, 5}).
