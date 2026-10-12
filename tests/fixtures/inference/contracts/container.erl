%% A tuple of other elements contradicts the declared tuple.
%% warning: container.erl:5:1: inferred result contradicts specification for f: declared {atom(), atom()}, inferred {1, 2}
-module(container).
-export([f/0]).
-spec f() -> {atom(), atom()}.
f() -> {1, 2}.
