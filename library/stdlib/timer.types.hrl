%% OTP 29 timer types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([time/0,tref/0]).
-opaque tref() :: {type(), reference()}.
-type type() :: once | interval | instant | send_local.
-nominal time() :: non_neg_integer().
