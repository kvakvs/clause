%% OTP 29 erl_eval types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([binding_struct/0]).
-type binding_struct() :: orddict:orddict() | map().
