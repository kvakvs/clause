%% A behaviour with ill-defined callback lists, for erl_lint_checks.
-module(erl_lint_checks_behaviour).
-export([behaviour_info/1]).

behaviour_info(callbacks) -> [{init, 1}, bad];
behaviour_info(optional_callbacks) -> undefined.
