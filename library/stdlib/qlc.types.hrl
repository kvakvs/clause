%% OTP 29 qlc types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([query_cursor/0,query_handle/0]).
-opaque query_cursor() :: {qlc_cursor, term()}.
-opaque query_handle() :: {qlc_handle, term()}.
