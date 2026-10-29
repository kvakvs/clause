%% clause_ms_transform: the builtins of ms_transform (C++ ms_transform, runtime/src/builtins/front_end/ms_*.cpp):
%% transform_forms/1 translates every ets:fun2ms/1 and dbg:fun2ms/1 of a literal fun in a module's forms, answering
%% {ok, Forms, File, Warnings, Used} or {error, File, Anno, Reason}; clause_list/2 translates a shell fun's clauses,
%% answering {ok, Spec} or {error, Anno, Reason}. Each catalog builtin replaces its clause_builtin_placeholder body.
-module(clause_ms_transform).
-export([transform_forms/1, clause_list/2]).

transform_forms(_) -> clause_builtin_placeholder.
clause_list(_, _) -> clause_builtin_placeholder.
