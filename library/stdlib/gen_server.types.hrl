%% OTP 29 gen_server types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([action/0,
              enter_loop_opt/0,
              format_status/0,
              from/0,
              reply_tag/0,
              request_id/0,
              request_id_collection/0,
              server_name/0,
              server_ref/0,
              start_mon_ret/0,
              start_opt/0,
              start_ret/0]).
-type format_status() ::
          #{state => term(),
            message => term(),
            reason => term(),
            log => [sys:system_event()]}.
-type timeout_option() :: {abs, Abs :: boolean()}.
-type action() ::
          (Time :: timeout()) |
          hibernate |
          {timeout, Time :: timeout(), Message :: term()} |
          {timeout,
           Time :: timeout(),
           Message :: term(),
           Options :: timeout_option() | [timeout_option()]} |
          {hibernate, Time :: timeout(), Message :: term()} |
          {hibernate,
           Time :: timeout(),
           Message :: term(),
           Options :: timeout_option() | [timeout_option()]} |
          {continue, Continue :: term()}.
-type from() :: {Client :: pid(), Tag :: reply_tag()}.
-opaque reply_tag() :: gen:reply_tag().
-opaque request_id() :: gen:request_id().
-opaque request_id_collection() :: gen:request_id_collection().
-type server_name() ::
          {local, LocalName :: atom()} |
          {global, GlobalName :: term()} |
          {via, RegMod :: module(), ViaName :: term()}.
-type server_ref() ::
          pid() |
          (LocalName :: atom()) |
          {Name :: atom(), Node :: atom()} |
          {global, GlobalName :: term()} |
          {via, RegMod :: module(), ViaName :: term()}.
-type start_opt() ::
          {timeout, Timeout :: timeout()} |
          {spawn_opt, SpawnOptions :: [proc_lib:start_spawn_option()]} |
          enter_loop_opt().
-type enter_loop_opt() ::
          {hibernate_after, HibernateAfterTimeout :: timeout()} |
          {debug, Dbgs :: [sys:debug_option()]}.
-type start_ret() ::
          {ok, Pid :: pid()} | ignore | {error, Reason :: term()}.
-type start_mon_ret() ::
          {ok, {Pid :: pid(), MonRef :: reference()}} |
          ignore |
          {error, Reason :: term()}.
