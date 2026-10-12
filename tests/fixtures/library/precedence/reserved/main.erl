%% Calls prim_file, a module Clause leaves missing: a stand-in on a search path must not provide it (plan step 41K).
-module(main).
-export([main/1]).

main(_) -> io:format("~p~n", [prim_file:open("x", [read])]).
