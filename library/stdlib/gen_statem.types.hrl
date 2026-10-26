%% OTP 29 gen_statem types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([action/0,
              callback_mode_result/0,
              enter_action/0,
              enter_loop_opt/0,
              event_handler_result/1,
              event_handler_result/2,
              event_type/0,
              format_status/0,
              from/0,
              handle_event_result/0,
              init_result/1,
              init_result/2,
              reply_action/0,
              reply_tag/0,
              request_id/0,
              request_id_collection/0,
              server_name/0,
              server_ref/0,
              start_mon_ret/0,
              start_opt/0,
              start_ret/0,
              state_enter_result/1,
              state_enter_result/2,
              state_function_result/0,
              transition_option/0]).
-type from() :: {To :: pid(), Tag :: reply_tag()}.
-opaque reply_tag() :: gen:reply_tag().
-type state() :: state_name() | term().
-type state_name() :: atom().
-type data() :: term().
-type event_type() ::
          external_event_type() | timeout_event_type() | internal.
-type external_event_type() :: {call, From :: from()} | cast | info.
-type timeout_event_type() ::
          timeout | {timeout, Name :: term()} | state_timeout.
-type event_content() :: term().
-type callback_mode_result() ::
          callback_mode() | [callback_mode() | state_enter()].
-type callback_mode() :: state_functions | handle_event_function.
-type state_enter() :: state_enter.
-type transition_option() ::
          postpone() |
          hibernate() |
          event_timeout() |
          generic_timeout() |
          state_timeout().
-type postpone() :: boolean().
-type hibernate() :: boolean().
-type event_timeout() :: Time :: timeout() | integer().
-type generic_timeout() :: Time :: timeout() | integer().
-type state_timeout() :: Time :: timeout() | integer().
-type timeout_option() :: {abs, Abs :: boolean()}.
-type action() ::
          postpone |
          {postpone, Postpone :: postpone()} |
          {next_event,
           EventType :: event_type(),
           EventContent :: event_content()} |
          {change_callback_module, NewModule :: module()} |
          {push_callback_module, NewModule :: module()} |
          pop_callback_module |
          enter_action().
-type enter_action() ::
          hibernate |
          {hibernate, Hibernate :: hibernate()} |
          timeout_action() |
          reply_action().
-type timeout_action() ::
          (Time :: event_timeout()) |
          {timeout,
           Time :: event_timeout(),
           EventContent :: event_content()} |
          {timeout,
           Time :: event_timeout(),
           EventContent :: event_content(),
           Options :: timeout_option() | [timeout_option()]} |
          {{timeout, Name :: term()},
           Time :: generic_timeout(),
           EventContent :: event_content()} |
          {{timeout, Name :: term()},
           Time :: generic_timeout(),
           EventContent :: event_content(),
           Options :: timeout_option() | [timeout_option()]} |
          {state_timeout,
           Time :: state_timeout(),
           EventContent :: event_content()} |
          {state_timeout,
           Time :: state_timeout(),
           EventContent :: event_content(),
           Options :: timeout_option() | [timeout_option()]} |
          timeout_cancel_action() |
          timeout_update_action().
-type timeout_cancel_action() ::
          {timeout, cancel} |
          {{timeout, Name :: term()}, cancel} |
          {state_timeout, cancel}.
-type timeout_update_action() ::
          {timeout, update, EventContent :: event_content()} |
          {{timeout, Name :: term()},
           update,
           EventContent :: event_content()} |
          {state_timeout, update, EventContent :: event_content()}.
-type reply_action() :: {reply, From :: from(), Reply :: term()}.
-type init_result(StateType) :: init_result(StateType, term()).
-type init_result(StateType, DataType) ::
          {ok, State :: StateType, Data :: DataType} |
          {ok,
           State :: StateType,
           Data :: DataType,
           Actions :: [action()] | action()} |
          ignore |
          {stop, Reason :: term()} |
          {error, Reason :: term()}.
-type state_function_result() :: event_handler_result(state_name()).
-type handle_event_result() :: event_handler_result(state()).
-type state_enter_result(State) :: state_enter_result(State, term()).
-type state_enter_result(State, DataType) ::
          {next_state, State, NewData :: DataType} |
          {next_state, State,
           NewData :: DataType,
           Actions :: [enter_action()] | enter_action()} |
          state_callback_result(enter_action(), DataType).
-type event_handler_result(StateType) ::
          event_handler_result(StateType, term()).
-type event_handler_result(StateType, DataType) ::
          {next_state, NextState :: StateType, NewData :: DataType} |
          {next_state,
           NextState :: StateType,
           NewData :: DataType,
           Actions :: [action()] | action()} |
          state_callback_result(action(), DataType).
-type state_callback_result(ActionType, DataType) ::
          {keep_state, NewData :: DataType} |
          {keep_state,
           NewData :: DataType,
           Actions :: [ActionType] | ActionType} |
          keep_state_and_data |
          {keep_state_and_data, Actions :: [ActionType] | ActionType} |
          {repeat_state, NewData :: DataType} |
          {repeat_state,
           NewData :: DataType,
           Actions :: [ActionType] | ActionType} |
          repeat_state_and_data |
          {repeat_state_and_data, Actions :: [ActionType] | ActionType} |
          stop |
          {stop, Reason :: term()} |
          {stop, Reason :: term(), NewData :: DataType} |
          {stop_and_reply,
           Reason :: term(),
           Replies :: [reply_action()] | reply_action()} |
          {stop_and_reply,
           Reason :: term(),
           Replies :: [reply_action()] | reply_action(),
           NewData :: DataType}.
-opaque request_id() :: gen:request_id().
-opaque request_id_collection() :: gen:request_id_collection().
-type format_status() ::
          #{state => state(),
            data => data(),
            reason => term(),
            queue => [{event_type(), event_content()}],
            postponed => [{event_type(), event_content()}],
            timeouts => [{timeout_event_type(), event_content()}],
            log => [sys:system_event()]}.
-type server_name() ::
          {local, atom()} |
          {global, GlobalName :: term()} |
          {via, RegMod :: module(), Name :: term()}.
-type server_ref() ::
          pid() |
          (LocalName :: atom()) |
          {Name :: atom(), Node :: atom()} |
          {global, GlobalName :: term()} |
          {via, RegMod :: module(), ViaName :: term()}.
-type start_opt() ::
          {timeout, Time :: timeout()} |
          {spawn_opt, [proc_lib:start_spawn_option()]} |
          enter_loop_opt().
-type enter_loop_opt() ::
          {hibernate_after, HibernateAfterTimeout :: timeout()} |
          {debug, Dbgs :: [sys:debug_option()]}.
-type start_ret() :: {ok, pid()} | ignore | {error, term()}.
-type start_mon_ret() ::
          {ok, {pid(), reference()}} | ignore | {error, term()}.
