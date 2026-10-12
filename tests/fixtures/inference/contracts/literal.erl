%% A literal result contradicts the declared result.
%% warning: literal.erl:5:1: inferred result contradicts specification for f: declared atom(), inferred 1
-module(literal).
-export([f/0]).
-spec f() -> atom().
f() -> 1.
