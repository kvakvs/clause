%% OTP 29 sets types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([set/0,set/1]).
-type seg() :: tuple().
-type segs(_Element) :: tuple().
-record(set,{size = 0 :: non_neg_integer(),
             n = 16 :: non_neg_integer(),
             maxn = 16 :: pos_integer(),
             bso = 16 div 2 :: non_neg_integer(),
             exp_size = 16 * 5 :: non_neg_integer(),
             con_size = 16 * 3 :: non_neg_integer(),
             empty :: seg(),
             segs :: segs(_)}).
-type set() :: set(_).
-opaque set(Element) :: set_v1(Element) | set_v2(Element).
-type set_v1(Element) :: #set{segs :: segs(Element)}.
-type set_v2(Element) :: #{Element => []}.
