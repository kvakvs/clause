%% Message passing benchmarks (plan step 69A): ping-pong, fan-in and a process ring.
-module(bench_msg).
-export([main/1]).

main(_Args) ->
    bench_util:run("msg.ping_pong", 2, fun() -> ping_pong(50000) end),
    bench_util:run("msg.fan_in", 2, fun() -> fan_in(100, 1000) end),
    bench_util:run("msg.ring", 2, fun() -> ring(1000, 50) end),
    bench_util:run("msg.large_message", 3, fun() -> large_message(lists:seq(1, 100000)) end),
    bench_util:done("bench_msg").

%% Bounce one message between two processes Count times.
ping_pong(Count) ->
    Pong = spawn(fun pong/0),
    ping(Count, Pong).

ping(0, Pong) ->
    Pong ! stop,
    ok;
ping(N, Pong) ->
    Pong ! {ping, self()},
    receive
        pong -> ping(N - 1, Pong)
    end.

pong() ->
    receive
        {ping, From} ->
            From ! pong,
            pong();
        stop ->
            ok
    end.

%% Senders processes each send Count messages to this process.
fan_in(Senders, Count) ->
    Parent = self(),
    start_senders(Senders, Count, Parent),
    collect(Senders * Count).

start_senders(0, _Count, _Parent) ->
    ok;
start_senders(N, Count, Parent) ->
    spawn(fun() -> send_n(Count, Parent) end),
    start_senders(N - 1, Count, Parent).

send_n(0, _To) ->
    ok;
send_n(N, To) ->
    To ! {msg, N},
    send_n(N - 1, To).

collect(0) ->
    ok;
collect(N) ->
    receive
        {msg, _} -> collect(N - 1)
    end.

%% Pass a token Rounds times around a ring of Size processes.
ring(Size, Rounds) ->
    First = start_ring(Size - 1, self()),
    First ! {token, Size * Rounds},
    ring_node(First).

start_ring(0, Next) ->
    Next;
start_ring(N, Next) ->
    start_ring(N - 1, spawn(fun() -> ring_node(Next) end)).

ring_node(Next) ->
    receive
        {token, 0} ->
            Next ! {token, 0},
            ok;
        {token, N} ->
            Next ! {token, N - 1},
            ring_node(Next)
    end.

%% Copy a large term to another process and back.
large_message(Term) ->
    Echo = spawn(fun() ->
        receive
            {From, T} -> From ! {self(), T}
        end
    end),
    Echo ! {self(), Term},
    receive
        {Echo, Back} -> length(Back)
    end.
