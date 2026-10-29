-module(fairness).
-export([main/1]).

% A process that never waits and never ends.
spin(N) -> spin(N + 1).

% Like spin/1, entering another function every step, so its self call spends no reduction of its
% own (batched loop reductions): the entered function's reductions must still end its time slice.
spin_calls(N) -> spin_calls(next(N)).

next(N) -> N + 1.

% Answers each {From, N} with N + 1 until it gets stop.
echo() ->
    receive
        {From, N} ->
            From ! {self(), N + 1},
            echo();
        stop ->
            ok
    end.

% Exchange Count messages with Echo, each answer feeding the next request; the last answer.
exchange(_, 0, N) ->
    N;
exchange(Echo, Count, N) ->
    Echo ! {self(), N},
    receive
        {Echo, M} -> exchange(Echo, Count - 1, M)
    end.

% Busy loops on every scheduler, or all sharing one, cannot starve a process waiting for messages: each exchange
% needs both the echo process and main to get a time slice between the spinners'.
main(_) ->
    Spinners = [spawn(fun() -> spin(0) end), spawn(fun() -> spin_calls(0) end)],
    Echo = spawn(fun echo/0),
    io:format("~p answers~n", [exchange(Echo, 20, 0)]),
    Echo ! stop,
    [exit(Spinner, kill) || Spinner <- Spinners],
    io:format("~p~n", [[is_process_alive(Spinner) || Spinner <- Spinners]]).
