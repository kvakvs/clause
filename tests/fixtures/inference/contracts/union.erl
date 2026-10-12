%% A union sharing no member with the declared union contradicts it.
%% warning: union.erl:5:1: inferred result contradicts specification for f: declared ok | error, inferred no | yes
-module(union).
-export([f/1]).
-spec f(term()) -> ok | error.
f(X) ->
    case X of
        1 -> yes;
        _ -> no
    end.
