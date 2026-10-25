-module(tracing_unsupported).
-export([main/1]).

%% Tracing (plan 21 step LIB71) is not supported: the erlang tracing BIFs,
%% the trace module and erl_tracer's callbacks raise error:notsup; the
%% erl_tracer behaviour still lists its callbacks.
main([]) ->
    Self = self(),
    try_call(trace, fun() -> erlang:trace(Self, true, [send]) end),
    try_call(trace_pattern2, fun() -> erlang:trace_pattern({lists, seq, 2}, true) end),
    try_call(trace_pattern3, fun() -> erlang:trace_pattern({lists, seq, 2}, true, [local]) end),
    try_call(trace_info, fun() -> erlang:trace_info(Self, flags) end),
    try_call(trace_delivered, fun() -> erlang:trace_delivered(Self) end),
    try_call(system_monitor0, fun() -> erlang:system_monitor() end),
    try_call(system_monitor1, fun() -> erlang:system_monitor(undefined) end),
    try_call(system_monitor2, fun() -> erlang:system_monitor(Self, [busy_port]) end),
    try_call(system_profile0, fun() -> erlang:system_profile() end),
    try_call(system_profile2, fun() -> erlang:system_profile(Self, [runnable_procs]) end),
    try_call(seq_trace, fun() -> erlang:seq_trace(label, 1) end),
    try_call(seq_trace_info, fun() -> erlang:seq_trace_info(label) end),
    try_call(session_create, fun() -> trace:session_create(probe, Self, []) end),
    try_call(session_info, fun() -> trace:session_info(all) end),
    try_call(session_destroy, fun() -> trace:session_destroy(probe) end),
    try_call(trace_process, fun() -> trace:process(probe, Self, true, [send]) end),
    try_call(trace_port, fun() -> trace:port(probe, all, true, [send]) end),
    try_call(trace_function, fun() -> trace:function(probe, {lists, seq, 2}, true, []) end),
    try_call(trace_send, fun() -> trace:send(probe, [], []) end),
    try_call(trace_recv, fun() -> trace:recv(probe, [], []) end),
    try_call(trace_info3, fun() -> trace:info(probe, Self, flags) end),
    try_call(trace_delivered2, fun() -> trace:delivered(probe, Self) end),
    try_call(trace_system, fun() -> trace:system(probe, long_gc, 100) end),
    try_call(tracer_enabled, fun() -> erl_tracer:enabled(trace_status, Self, Self) end),
    try_call(tracer_trace, fun() -> erl_tracer:trace(send, Self, Self, msg, #{}) end),
    io:format("callbacks ~p~n", [lists:sort(erl_tracer:behaviour_info(callbacks))]),
    io:format("tracing_unsupported: ok~n").

try_call(Label, Fun) ->
    try Fun() of
        Result -> io:format("~s returned ~p~n", [Label, Result])
    catch
        Class:Reason -> io:format("~s ~p:~p~n", [Label, Class, Reason])
    end.
