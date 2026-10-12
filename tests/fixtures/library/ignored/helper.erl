%% A module the project ignores; its type is named by main.erl's specs (plan step 41K2).
-module(helper).
-export([value/0]).
-export_type([result/0]).

-type result() :: ok.

-spec value() -> result().
value() -> ok.
