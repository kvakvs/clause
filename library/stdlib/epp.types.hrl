%% OTP 29 epp types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([source_encoding/0]).
-type source_encoding() :: latin1 | utf8.
