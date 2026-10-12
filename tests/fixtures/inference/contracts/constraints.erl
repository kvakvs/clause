%% A type variable is checked through its bound.
%% warning: constraints.erl:5:1: inferred result contradicts specification for f: declared A when A :: integer(), inferred ok
-module(constraints).
-export([f/1]).
-spec f(A) -> A when A :: integer().
f(_) -> ok.
