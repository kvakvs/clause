%% clause_qlc_pt: the builtins of qlc_pt (plan 21 step LIB73, runtime/src/builtins/qlc_pt/): parse_transform/4 and
%% transform_expression/4 run OTP's qlc transform with the match specification answers so far, answering
%% {done, Result, Transformed} or {try_ms, Forms} for qlc_pt.erl to run ms_transform over the forms. Each catalog
%% builtin replaces its clause_builtin_placeholder body.
-module(clause_qlc_pt).
-export([parse_transform/4, transform_expression/4]).

parse_transform(_, _, _, _) -> clause_builtin_placeholder.
transform_expression(_, _, _, _) -> clause_builtin_placeholder.
