-module(clause_hashed).
-export([entries/1, keep/2, with_values/2]).

% Linear hash tables (plan 21 step LIB2, docs/builtins.md#collections): the entries of a dict or a version 1 set in
% the order OTP's map and filter visit them, and the table rebuilt with new values or only the kept entries; runtime
% builtins that dict and sets functions taking funs call.
entries(_) -> clause_builtin_placeholder.
keep(_, _) -> clause_builtin_placeholder.
with_values(_, _) -> clause_builtin_placeholder.
