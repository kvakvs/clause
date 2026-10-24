%% OTP 29 log_mf_h types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([args/0]).
-type b() :: non_neg_integer().
-type f() :: 1..255.
-type pred() :: fun((term()) -> boolean()).
-opaque args() :: {file:filename(), b(), f(), pred()}.
