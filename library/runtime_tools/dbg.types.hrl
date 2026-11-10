%% OTP 29 dbg types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([match_spec/0,session/0]).
-opaque session() :: pid().
-type match_pattern() :: atom() | list().
-type match_spec() :: [{match_pattern(), [_], [_]}].
