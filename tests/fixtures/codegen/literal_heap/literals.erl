%% Module literals of every kind, decoded into each runtime's literal heap at registration (literal heap).
-module(literals).
-export([value/0, prefixed/1]).

value() ->
    {alpha, [beta, {gamma, 1}, "text"], -5, true, {}, [delta | epsilon], 1.5,
        123456789012345678901234567890, -98765432109876543210, #{a => 1, b => [x]}, <<"short">>,
        <<"a binary longer than sixty-four bytes, kept off the heap in a buffer of its own">>}.

prefixed(Tail) -> [beta, gamma] ++ Tail.
