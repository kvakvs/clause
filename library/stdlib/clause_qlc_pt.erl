%% clause_qlc_pt: the builtins of qlc_pt (plan 21 step LIB73, runtime/src/builtins/qlc_pt/): parse_transform/3 and
%% transform_expression/3 run OTP's qlc transform, match specification attempts included (C++ ms_transform),
%% answering {Result, Transformed}. Each catalog builtin replaces its clause_builtin_placeholder body.
-module(clause_qlc_pt).
-export([parse_transform/3, transform_expression/3]).

parse_transform(_, _, _) -> clause_builtin_placeholder.
transform_expression(_, _, _) -> clause_builtin_placeholder.
