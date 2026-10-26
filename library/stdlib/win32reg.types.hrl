%% OTP 29 win32reg types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([reg_handle/0]).
-opaque reg_handle() :: {win32reg, port()}.
