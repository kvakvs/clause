%% Wrong against bad.terms in every way the check reports.
-module(bad).
-export([
    alpha/1,
    beta/0,
    gamma/2,
    epsilon/0
]).

alpha(X) -> X.
beta() -> erlang:error(undef).
gamma(_, _) -> ok.
epsilon() -> ok.
