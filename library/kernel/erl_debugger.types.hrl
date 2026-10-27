%% OTP 29 erl_debugger types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([event/0,
              event_message/0,
              instrumentation/0,
              reg_val/0,
              session/0,
              stack_frame/0,
              stack_frame_fun/0,
              stack_frame_info/0,
              stack_frame_slot/0]).
-opaque session() :: integer().
-type event_message() :: {debugger_event, session(), event()}.
-type event() ::
          {breakpoint,
           pid(),
           mfa(),
           Line :: pos_integer(),
           Resume :: fun(() -> ok)}.
-type stack_frame() ::
          {FrameNo :: non_neg_integer(),
           stack_frame_fun(),
           stack_frame_info()}.
-type stack_frame_fun() ::
          #{function := mfa(), line := pos_integer() | undefined} |
          '<terminate process>' | '<continue terminate process>' |
          '<terminate process normally>' | '<breakpoint>' |
          'unknown function'.
-type stack_frame_info() ::
          #{slots := [stack_frame_slot()], code := pos_integer()}.
-type stack_frame_slot() :: reg_val() | {'catch', stack_frame_fun()}.
-type reg_val() :: {value, term()} | {too_large, Size :: pos_integer()}.
-type instrumentation() :: line_breakpoint.
