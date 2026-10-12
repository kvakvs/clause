%% An integer outside the declared range contradicts it.
%% warning: range.erl:5:1: inferred result contradicts specification for f: declared 1..10, inferred 20
-module(range).
-export([f/0]).
-spec f() -> 1..10.
f() -> 20.
