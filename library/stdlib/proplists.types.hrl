%% OTP 29 proplists types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([property/0,proplist/0]).
-type property() :: atom() | tuple().
-type proplist() :: [property()].
