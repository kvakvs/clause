%% OTP 29 counters types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([counters_ref/0]).
-opaque counters_ref() ::
    {atomics, reference()} | {write_concurrency, reference()}.
