-module(gen_sctp).
-export([
    abort/2,
    close/1,
    connect/3,
    connect/4,
    connect/5,
    connect_init/3,
    connect_init/4,
    connect_init/5,
    connectx_init/3,
    connectx_init/4,
    connectx_init/5,
    controlling_process/2,
    eof/2,
    error_string/1,
    listen/2,
    open/0,
    open/1,
    open/2,
    peeloff/2,
    recv/1,
    recv/2,
    send/3,
    send/4
]).
-include("gen_sctp.types.hrl").

% Clause's gen_sctp (plan 21 step LIB62, docs/library.md): the runtime has no SCTP, so opening answers
% {error, eprotonosupport} after OTP's option checks, as OTP where SCTP is absent, and no socket can be an SCTP one.

open() -> open([]).

open(Port) when is_integer(Port) -> open([{port, Port}]);
open(Options) when is_list(Options) -> open(0, Options);
open(Other) -> erlang:error(badarg, [Other]).

open(Port, Options) when is_integer(Port), Port >= 0, Port =< 65535, is_list(Options) ->
    case lists:all(fun valid_option/1, Options) of
        true -> {error, eprotonosupport};
        false -> erlang:error(badarg, [[{port, Port} | Options]])
    end;
open(Port, Options) ->
    erlang:error(badarg, [Port, Options]).

valid_option(Atom) when Atom =:= binary; Atom =:= list; Atom =:= inet; Atom =:= inet6 -> true;
valid_option({port, Port}) ->
    is_integer(Port) andalso Port >= 0 andalso Port =< 65535;
valid_option({Name, _}) when Name =:= ip; Name =:= ifaddr -> true;
valid_option({type, Type}) ->
    Type =:= seqpacket orelse Type =:= stream;
valid_option({active, Active}) ->
    lists:member(Active, [true, false, once]) orelse is_integer(Active);
valid_option({mode, Mode}) ->
    Mode =:= binary orelse Mode =:= list;
valid_option({Name, Value}) when Name =:= buffer; Name =:= recbuf; Name =:= sndbuf; Name =:= tos; Name =:= priority ->
    is_integer(Value);
valid_option({Name, Value}) when Name =:= reuseaddr; Name =:= dontroute; Name =:= ipv6_v6only -> is_boolean(Value);
valid_option({linger, {OnOff, Seconds}}) ->
    is_boolean(OnOff) andalso is_integer(Seconds);
valid_option({Name, _}) when Name =:= high_msgq_watermark; Name =:= low_msgq_watermark; Name =:= netns -> true;
valid_option({Name, _}) when is_atom(Name) -> lists:prefix("sctp_", atom_to_list(Name));
valid_option(_) ->
    false.

%% Every other call gets a socket that cannot be an SCTP socket: closed, or another kind (einval).
close(Socket) -> inet:close(Socket).

listen(Socket, _) -> not_sctp(Socket).

connect(Socket, _, _) -> not_sctp(Socket).

connect(Socket, _, _, _) -> not_sctp(Socket).

connect(Socket, _, _, _, _) -> not_sctp(Socket).

connect_init(Socket, _, _) -> not_sctp(Socket).

connect_init(Socket, _, _, _) -> not_sctp(Socket).

connect_init(Socket, _, _, _, _) -> not_sctp(Socket).

connectx_init(Socket, _, _) -> not_sctp(Socket).

connectx_init(Socket, _, _, _) -> not_sctp(Socket).

connectx_init(Socket, _, _, _, _) -> not_sctp(Socket).

send(Socket, _, _) -> not_sctp(Socket).

send(Socket, _, _, _) -> not_sctp(Socket).

recv(Socket) -> not_sctp(Socket).

recv(Socket, _) -> not_sctp(Socket).

peeloff(Socket, _) -> not_sctp(Socket).

controlling_process(Socket, Pid) when is_pid(Pid) -> not_sctp(Socket);
controlling_process(Socket, Pid) -> erlang:error(badarg, [Socket, Pid]).

%% abort/2 and eof/2 take the association change record of an association.
abort(Socket, Assoc) when is_tuple(Assoc), element(1, Assoc) =:= sctp_assoc_change -> not_sctp(Socket);
abort(Socket, Assoc) -> erlang:error(badarg, [Socket, Assoc]).

eof(Socket, Assoc) when is_tuple(Assoc), element(1, Assoc) =:= sctp_assoc_change -> not_sctp(Socket);
eof(Socket, Assoc) -> erlang:error(badarg, [Socket, Assoc]).

not_sctp(Socket) when is_port(Socket) ->
    case erlang:port_info(Socket, name) of
        undefined -> {error, closed};
        _ -> {error, einval}
    end;
not_sctp(_) ->
    {error, closed}.

%% The texts of SCTP's error causes (RFC 4960).
error_string(0) -> ok;
error_string(1) -> "Invalid Stream Identifier";
error_string(2) -> "Missing Mandatory Parameter";
error_string(3) -> "Stale Cookie Error";
error_string(4) -> "Out of Resource";
error_string(5) -> "Unresolvable Address";
error_string(6) -> "Unrecognized Chunk Type";
error_string(7) -> "Invalid Mandatory Parameter";
error_string(8) -> "Unrecognized Parameters";
error_string(9) -> "No User Data";
error_string(10) -> "Cookie Received While Shutting Down";
error_string(11) -> "Restart of an Association with New Addresses";
error_string(12) -> "User Initiated Abort";
error_string(13) -> "Protocol Violation";
error_string(N) when is_integer(N) -> unknown_error;
error_string(X) -> erlang:error(badarg, [X]).
