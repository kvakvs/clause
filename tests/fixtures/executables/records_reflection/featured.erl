-module(featured).
-feature(compr_assign, enable).
-export([pairs/0]).

%% A module compiled with compr_assign enabled (unused here): erl_features:used/1 lists it with maybe_expr.
pairs() -> [{X, X * 10} || X <- [1, 2]].
