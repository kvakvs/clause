%% Consistent with good.terms.
-module(good).
-export([
    one/0,
    two/1
]).

one() -> ok.
two(_) -> erlang:error(undef).
