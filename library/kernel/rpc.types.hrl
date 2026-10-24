%% OTP 29 rpc types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([key/0]).
-opaque key() :: erpc:request_id().
