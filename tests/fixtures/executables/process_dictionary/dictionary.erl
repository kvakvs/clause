-module(dictionary).
-export([main/1]).

%% The process dictionary (plan step 41C): put/get/erase/get_keys with exact key equality, entries surviving
%% garbage collection (forced by churn/1), and one dictionary per process. Unordered results are sorted before printing.
main(_) ->
    io:format("~p~n", [{put(a, 1), put(a, 2), get(a), get(missing)}]),
    put({key, [1, 2]}, <<"value">>),
    put(1.0, float),
    put(1, int),
    io:format("~p~n", [{get({key, [1, 2]}), get(1), get(1.0)}]),
    io:format("~p~n", [lists:sort(get())]),
    io:format("~p~n", [lists:sort(get_keys())]),
    put(b, 2),
    io:format("~p~n", [lists:sort(get_keys(2))]),
    io:format("~p~n", [{erase(a), erase(a), get(a)}]),
    [put({n, N}, lists:seq(1, N rem 7)) || N <- lists:seq(1, 2000)],
    churn(300),
    io:format("~p~n", [{get({n, 1999}), length(get_keys()), get({key, [1, 2]})}]),
    Self = self(),
    spawn(fun() -> Self ! {child, get(b), put(b, child), get(b)} end),
    receive
        Message -> io:format("~p~n", [Message])
    end,
    io:format("~p~n", [get(b)]),
    All = erase(),
    io:format("~p~n", [{length(All), get(), get(b)}]),
    io:format("dictionary: ok~n").

% Allocate garbage until the heap has been collected many times.
churn(0) ->
    ok;
churn(N) ->
    _ = lists:reverse(lists:seq(1, 1000)),
    churn(N - 1).
