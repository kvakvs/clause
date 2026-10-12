%% A fun contradicts a declared integer.
%% warning: funs.erl:5:1: inferred result contradicts specification for f: declared integer(), inferred fun(() -> 1)
-module(funs).
-export([f/0]).
-spec f() -> integer().
f() -> fun() -> 1 end.
