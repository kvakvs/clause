%% OTP 29 string types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([grapheme_cluster/0]).
-type grapheme_cluster() :: char() | [char()].
