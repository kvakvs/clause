%% erl_pp's runtime builtin (plan 21 step LIB34C, docs/builtins.md#erl-pp), called by library/stdlib/erl_pp.erl;
%% runtime/src/builtins/front_end/pp*.cpp implements it.
-module(clause_erl_pp).
-export([print/2]).

%% print({What, Form, Indent, Precedence, Options}, Answers): the characters of Form, What being form, attribute,
%% function, guard, exprs or expr; or {hook, Expr, Column, Precedence} when the next hook's text is not among the
%% Answers given so far.
print(_, _) -> clause_builtin_placeholder.
