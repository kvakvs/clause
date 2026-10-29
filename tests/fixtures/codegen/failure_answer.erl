-module(failure_answer).
-export([chain/0, leaf/0, later/0, take/2]).
%% The remote call keeps leaf/0 a call at O2 (no leaf inlining): the test replaces its body with a fault seam.
chain() -> ?MODULE:leaf().
leaf() -> 42.
later() -> 7.
take(X, _) -> X.
