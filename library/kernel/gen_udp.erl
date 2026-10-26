-module(gen_udp).
-export([
    open/1,
    open/2,
    connect/2,
    connect/3,
    send/2,
    send/3,
    send/4,
    send/5,
    recv/2,
    recv/3,
    controlling_process/2,
    close/1
]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
-export([
    fdopen/2
]).
-include("gen_udp.types.hrl").

% The project-owned gen_udp module (docs/libraries/library.md, docs/ports.md#sockets): UDP sockets are ports of the runtime's
% socket driver, driven through clause_socket.

-define(UDP_OPEN, 10).
-define(UDP_SEND, 11).
-define(UDP_RECV, 12).
-define(UDP_CONNECT, 20).
-define(UDP_SEND_NOW, 21).

% OTP's defaults: list data, active messages, IPv4.
defaults() -> #{family => 4, binary => false, active => true, packet => 0}.

open(Port) -> open(Port, []).

% A socket bound to Port (0 for any free port) of its ip option, or {error, Reason}.
open(Port, Options) when is_integer(Port), Port >= 0, Port =< 65535 ->
    Map = clause_socket:options(Options, defaults()),
    Socket = clause_socket:open("udp_inet"),
    Data = [clause_socket:local(Map), <<Port:16>>, clause_socket:mode(Map), clause_socket:records(Map)],
    case clause_socket:control(Socket, ?UDP_OPEN, Data) of
        {ok, _} ->
            {ok, Socket};
        Error ->
            clause_socket:close(Socket),
            Error
    end;
open(_, _) ->
    exit(badarg).

% Send Packet on a connected socket; an unconnected one sends to the any address and port 0, as OTP's.
send(Socket, Packet) -> send_to(Socket, {0, 0, 0, 0}, 0, Packet).

% Send Packet to Destination: {Address, Port}, {Family, {Address, Port}} or a socket address map.
send(Socket, Destination, Packet) -> send(Socket, Destination, [], Packet).

% send(Socket, Host, Port, Packet): Packet (iodata) to Port at Host (a tuple, a host name string or an atom);
% send(Socket, Destination, AncData, Packet): to a destination of send/3 with ancillary data.
send(Socket, #{family := Family} = Destination, AncData, Packet) when
    (Family =:= inet orelse Family =:= inet6) andalso is_list(AncData)
->
    #{addr := Address, port := Port} = inet:ensure_sockaddr(Destination),
    send(Socket, Address, Port, AncData, Packet);
send(Socket, {Family, {Address, Port}}, AncData, Packet) when
    (Family =:= inet orelse Family =:= inet6) andalso is_list(AncData)
->
    send(Socket, Address, Port, AncData, Packet);
send(Socket, {Address, Port}, AncData, Packet) when is_list(AncData) ->
    send(Socket, Address, Port, AncData, Packet);
send(Socket, {_, _} = Destination, 0, Packet) ->
    send(Socket, Destination, [], Packet);
send(Socket, Host, Port, Packet) ->
    send(Socket, Host, Port, [], Packet).

% Send Packet to Port (a number or a service name) at Host with ancillary data ({tos | tclass | ttl, Byte}) for
% this datagram alone.
send(Socket, Host, Port, AncData, Packet) when is_port(Socket), is_list(AncData) ->
    Address = host(Host),
    case clause_socket:resolve(Socket, Address, family(Address)) of
        {ok, Target} when AncData =:= [] -> send_to(Socket, Target, service(Port), Packet);
        {ok, Target} -> send_now(Socket, Target, service(Port), AncData, Packet);
        Error -> Error
    end.

% A port number, or the UDP port of a service name (an atom); exit(badarg) for an unknown service and
% function_clause for another port, as OTP's inet_udp.
service(Port) when is_integer(Port), Port >= 0, Port =< 65535 ->
    Port;
service(Name) when is_atom(Name) ->
    case inet:getservbyname(Name, udp) of
        {ok, Port} -> Port;
        _ -> exit(badarg)
    end;
service(Port) ->
    erlang:error(function_clause, [Port]).

host(any) -> {0, 0, 0, 0};
host(Host) -> Host.

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

send_now(Socket, Target, Port, AncData, Packet) ->
    case {clause_socket_options:ancillary(AncData), packet_bytes(Packet)} of
        {{ok, Records}, Bytes} when is_binary(Bytes) ->
            Data = [<<Port:16>>, clause_socket:address(Target), <<(byte_size(Records)):16>>, Records, Bytes],
            case clause_socket:control(Socket, ?UDP_SEND_NOW, Data) of
                {ok, _} -> ok;
                Error -> Error
            end;
        _ ->
            {error, einval}
    end.

% The bytes of iodata, or error.
packet_bytes(Packet) ->
    try
        iolist_to_binary(Packet)
    catch
        error:badarg -> error
    end.

% Connect Socket to a socket address map, so that it sends there and receives from there only.
connect(Socket, #{family := Family} = SockAddr) when Family =:= inet; Family =:= inet6 ->
    #{addr := Address, port := Port} = inet:ensure_sockaddr(SockAddr),
    connect(Socket, host(Address), Port).

% Connect Socket to Port at Address (a tuple, a host name string or an atom).
connect(Socket, Address, Port) when is_integer(Port), Port >= 0, Port =< 65535 ->
    case clause_socket:resolve(Socket, Address, family(Address)) of
        {ok, Target} ->
            case clause_socket:control(Socket, ?UDP_CONNECT, [clause_socket:address(Target), <<Port:16>>]) of
                {ok, _} -> ok;
                Error -> Error
            end;
        Error ->
            Error
    end;
connect(_, _, _) ->
    exit(badarg).

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

fdopen(_, _) -> erlang:error({not_implemented, fdopen, 2}).
