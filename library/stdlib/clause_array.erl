-module(clause_array).
-export([from_values/2, replace/4, sparse/3, values/3]).

% Arrays (plan 21 step LIB3B, docs/builtins.md#collections): array:from/2,3's array of generated values, the values
% or non-default {Index, Value} pairs of an index range, and the array with a range's values replaced as OTP's
% mapfolds rebuild it; runtime builtins that array's functions taking funs call.
from_values(_, _) -> clause_builtin_placeholder.
replace(_, _, _, _) -> clause_builtin_placeholder.
sparse(_, _, _) -> clause_builtin_placeholder.
values(_, _, _) -> clause_builtin_placeholder.
