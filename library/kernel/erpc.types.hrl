%% OTP 29 erpc types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([call_options/0,
              request_id/0,
              request_id_collection/0,
              timeout_time/0]).
-type timeout_time() :: 0..4294967295 | infinity | {abs, integer()}.
-type call_options() ::
          #{timeout => Timeout :: timeout_time(),
            always_spawn => AlwaysSpawn :: boolean()}.
-opaque request_id() :: nonempty_improper_list(reference(), reference()).
-opaque request_id_collection() ::
            #{reference() => [reference() | term()]}.
