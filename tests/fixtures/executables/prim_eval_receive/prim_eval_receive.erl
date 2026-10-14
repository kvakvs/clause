-module(prim_eval_receive).
-export([main/1]).

%% prim_eval:'receive'/2, the receive of erl_eval (plan step 41S): the first
%% message the fun accepts is taken in mailbox order and the rest stay, a
%% timeout answers timeout, a message arriving while it waits is matched, and
%% an exception from the fun leaves the mailbox as it was.
main([]) ->
    io:format("~p~n", [selecting()]),
    io:format("~p~n", [timeouts()]),
    io:format("~p~n", [waiting()]),
    io:format("~p~n", [failing()]),
    io:format("prim_eval_receive: ok~n").

selecting() ->
    [self() ! M || M <- [a, {b, 1}, c, {b, 2}, d]],
    First = prim_eval:'receive'(fun accept_b/1, infinity),
    Second = prim_eval:'receive'(fun accept_b/1, 0),
    Third = prim_eval:'receive'(fun accept_b/1, 0),
    [First, Second, Third, drain()].

accept_b({b, N}) -> {got, N};
accept_b(_) -> nomatch.

timeouts() ->
    Never = fun(_) -> nomatch end,
    self() ! kept,
    [
        prim_eval:'receive'(Never, 0),
        prim_eval:'receive'(Never, 20),
        outcome(fun() -> prim_eval:'receive'(Never, -1) end),
        outcome(fun() -> prim_eval:'receive'(Never, bad) end),
        drain(),
        compiled_bad_timeout()
    ].

%% A compiled receive with a bad timeout, for comparison: the mailbox stays whole.
compiled_bad_timeout() ->
    self() ! kept,
    Failure = outcome(fun() ->
        receive
            never -> ok
        after bad_timeout() -> ok
        end
    end),
    [Failure, drain()].

bad_timeout() -> bad.

waiting() ->
    Self = self(),
    spawn(fun() ->
        pause(30),
        Self ! skipped,
        pause(30),
        Self ! {late, 7}
    end),
    Answer = prim_eval:'receive'(
        fun
            ({late, N}) -> {late, N * 6};
            (_) -> nomatch
        end,
        5000
    ),
    [Answer, drain()].

failing() ->
    [self() ! M || M <- [x, y]],
    Failure = outcome(fun() ->
        prim_eval:'receive'(
            fun
                (y) -> erlang:error(boom);
                (_) -> nomatch
            end,
            0
        )
    end),
    After = prim_eval:'receive'(
        fun
            (y) -> took_y;
            (_) -> nomatch
        end,
        0
    ),
    [Failure, After, drain()].

drain() ->
    receive
        M -> [M | drain()]
    after 0 -> []
    end.

outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

pause(Milliseconds) ->
    receive
    after Milliseconds -> ok
    end.
