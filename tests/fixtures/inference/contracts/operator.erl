%% An operator's folded result contradicts the declared result.
%% warning: operator.erl:5:1: inferred result contradicts specification for f: declared atom(), inferred 3
-module(operator).
-export([f/0]).
-spec f() -> atom().
f() -> 1 + 2.
