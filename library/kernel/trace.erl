%% Clause's trace (plan 21 step LIB71, docs/libraries/library.md): tracing is not supported, so every function raises
%% error:notsup, as erlang's tracing functions do.
-module(trace).
-export([
    delivered/2,
    function/4,
    info/3,
    port/4,
    process/4,
    recv/3,
    send/3,
    session_create/3,
    session_destroy/1,
    session_info/1,
    system/3
]).
-include("trace.types.hrl").

delivered(_, _) -> erlang:error(notsup).
function(_, _, _, _) -> erlang:error(notsup).
info(_, _, _) -> erlang:error(notsup).
port(_, _, _, _) -> erlang:error(notsup).
process(_, _, _, _) -> erlang:error(notsup).
recv(_, _, _) -> erlang:error(notsup).
send(_, _, _) -> erlang:error(notsup).
session_create(_, _, _) -> erlang:error(notsup).
session_destroy(_) -> erlang:error(notsup).
session_info(_) -> erlang:error(notsup).
system(_, _, _) -> erlang:error(notsup).
