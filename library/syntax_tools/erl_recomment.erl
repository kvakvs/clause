%% Clause's erl_recomment (plan 21 LIB77E4b, docs/libraries/syntax_tools.md#erl_recomment): comments of
%% erl_comment_scan attached to syntax trees by their lines. All three functions are runtime builtins
%% (runtime/src/builtins/comments/recomment.cpp) with clause_builtin_placeholder bodies.
-module(erl_recomment).
-export([quick_recomment_forms/2, recomment_forms/2, recomment_tree/2]).

% Native quick_recomment_forms/2.
quick_recomment_forms(_, _) -> clause_builtin_placeholder.

% Native recomment_forms/2.
recomment_forms(_, _) -> clause_builtin_placeholder.

% Native recomment_tree/2.
recomment_tree(_, _) -> clause_builtin_placeholder.
