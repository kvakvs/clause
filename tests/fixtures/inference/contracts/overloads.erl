%% A result no overload admits, and a call whose arguments fit no overload, contradict the specification.
%% warning: overloads.erl:6:1: inferred result contradicts specification for f: declared integer() | atom(), inferred {_}
%% warning: overloads.erl:15:9: inferred arguments contradict specification for g: declared integer(), inferred []
-module(overloads).
-export([f/1, g/1, h/0]).
-spec f
    (integer()) -> integer();
    (atom()) -> atom().
f(X) -> {X}.

-spec g
    (integer()) -> ok;
    (atom()) -> ok.
g(_) -> ok.
h() -> g([]).
