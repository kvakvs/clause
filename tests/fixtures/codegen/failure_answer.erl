-module(failure_answer).
-export([chain/0, later/0, take/2]).
%% The catch keeps leaf/0 a call at O2 (no leaf inlining): the test replaces its body with a fault seam.
chain() -> leaf().
leaf() -> catch 42.
later() -> 7.
take(X, _) -> X.
