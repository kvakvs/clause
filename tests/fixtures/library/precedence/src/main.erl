%% Calls Clause's lists, which a same-named module on a search path must not replace (plan step 41K).
-module(main).
-export([main/1]).

main(_) -> io:format("~p~n", [lists:reverse([1, 2])]).
