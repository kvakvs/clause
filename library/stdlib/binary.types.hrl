%% OTP 29 binary types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([cp/0,part/0]).
-opaque cp() :: {am | bm, reference()}.
-type part() :: {Start :: non_neg_integer(), Length :: integer()}.
