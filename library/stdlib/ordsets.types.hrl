%% OTP 29 ordsets types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([ordset/1]).
-type ordset(T) :: [T].
