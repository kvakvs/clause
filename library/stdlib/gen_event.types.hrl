%% OTP 29 gen_event types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([add_handler_ret/0,
              del_handler_ret/0,
              format_status/0,
              handler/0,
              handler_args/0,
              request_id/0,
              request_id_collection/0]).
-type format_status() ::
          #{state => term(),
            message => term(),
            reason => term(),
            log => [sys:system_event()]}.
-type handler() :: atom() | {atom(), term()}.
-type handler_args() :: term().
-type add_handler_ret() :: ok | term() | {'EXIT', term()}.
-type del_handler_ret() :: ok | term() | {'EXIT', term()}.
-opaque request_id() :: gen:request_id().
-opaque request_id_collection() :: gen:request_id_collection().
