%% OTP 29 orddict types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([orddict/0,orddict/2]).
-type orddict() :: orddict(_, _).
-type orddict(Key, Value) :: [{Key, Value}].
