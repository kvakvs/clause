%% no_return() admits only a function that never returns.
%% warning: no_return.erl:5:1: inferred result contradicts specification for f: declared none(), inferred ok
-module(no_return).
-export([f/0]).
-spec f() -> no_return().
f() -> ok.
