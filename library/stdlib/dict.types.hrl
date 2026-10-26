%% OTP 29 dict types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([dict/0,dict/2]).
-type segs(_Key, _Value) :: tuple().
-record(dict,{size = 0 :: non_neg_integer(),
              n = 16 :: non_neg_integer(),
              maxn = 16 :: non_neg_integer(),
              bso = 16 div 2 :: non_neg_integer(),
              exp_size = 16 * 5 :: non_neg_integer(),
              con_size = 16 * 3 :: non_neg_integer(),
              empty :: tuple(),
              segs :: segs(_, _)}).
-type dict() :: dict(_, _).
-opaque dict(Key, Value) :: #dict{segs :: segs(Key, Value)}.
