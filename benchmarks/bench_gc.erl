%% Garbage collection benchmarks (plan step 69A): short-lived processes and a heap growing then shrinking.
-module(bench_gc).
-export([main/1]).

main(_Args) ->
    bench_util:run("gc.short_processes", 69, fun() -> short_processes(5000) end),
    bench_util:run("gc.grow_shrink", 150, fun() -> grow_shrink(2) end),
    bench_util:run("gc.long_lived_churn", 370, fun() -> churn(100, []) end),
    bench_util:run("gc.explicit_collect", 5100, fun() -> collect_with_live_data() end),
    bench_util:done("bench_gc").

%% Spawn Count processes that each allocate a garbage list, and wait for all of them.
short_processes(Count) ->
    Parent = self(),
    spawn_all(Count, Parent),
    await(Count).

spawn_all(0, _Parent) ->
    ok;
spawn_all(N, Parent) ->
    spawn(fun() -> Parent ! {done, length(build(1000, []))} end),
    spawn_all(N - 1, Parent).

await(0) ->
    ok;
await(N) ->
    receive
        {done, _} -> await(N - 1)
    end.

%% Build a large live list, drop it, repeat: the heap grows and must shrink again.
grow_shrink(0) ->
    ok;
grow_shrink(N) ->
    _ = length(build(200000, [])),
    grow_shrink(N - 1).

%% Keep a sliding window of live data while producing garbage, so collections copy surviving terms.
churn(0, Window) ->
    length(Window);
churn(N, Window) ->
    Survivors = take(100, build(1000, []), Window),
    churn(N - 1, take(10000, Survivors, [])).

%% Prepend the first Count elements of List to Tail, in reverse order.
take(0, _List, Tail) -> Tail;
take(_Count, [], Tail) -> Tail;
take(Count, [X | Rest], Tail) -> take(Count - 1, Rest, [X | Tail]).

collect_with_live_data() ->
    Live = build(10000, []),
    erlang:garbage_collect(),
    length(Live).

build(0, Acc) -> Acc;
build(N, Acc) -> build(N - 1, [{N, N} | Acc]).
