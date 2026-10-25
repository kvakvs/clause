%% erl_lint's runtime builtins (plan 21 step LIB34C, docs/builtins.md#erl-lint), called by library/stdlib/erl_lint.erl;
%% runtime/src/builtins/front_end/lint*.cpp implements them.
-module(clause_erl_lint).
-export([module/4, format_spec/1, local_calls/1, is_guard_test/3]).

%% module(Forms, File, Options, Behaviours): {ok, Warnings} or {error, Errors, Warnings}, Behaviours being
%% {Module, Callbacks, OptionalCallbacks} with {ok, List} or error for each.
module(_, _, _, _) -> clause_builtin_placeholder.

%% format_spec(Error): {plain, Format, Args}, {nested, Format, Parts} or a binary text, for format_error/1.
format_spec(_) -> clause_builtin_placeholder.

%% local_calls(Expr): the {Name, Arity} of the local calls in Expr.
local_calls(_) -> clause_builtin_placeholder.

%% is_guard_test(Expr, Forms, Overridden): a guard test with the records of Forms, the Overridden local calls never
%% being guard BIFs.
is_guard_test(_, _, _) -> clause_builtin_placeholder.
