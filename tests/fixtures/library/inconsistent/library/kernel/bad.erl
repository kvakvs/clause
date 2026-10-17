%% Wrong against bad.terms in every way the check reports.
-module(bad).
-export([
    alpha/1,
    beta/0,
    gamma/2,
    epsilon/0,
    zeta/1
]).

alpha(X) -> X.
beta() -> erlang:error({not_implemented, beta, 0}).
gamma(_, X) -> zeta(X).
epsilon() -> ok.
zeta(_) -> clause_builtin_placeholder.
