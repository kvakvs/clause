%% OTP 29 atomics types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([atomics_ref/0]).
-opaque atomics_ref() :: reference().
