%% OTP 29 trace types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([session/0,session_strong_ref/0,session_weak_ref/0]).
-type session() ::
          {session_strong_ref(), session_weak_ref()} |
          session_weak_ref().
-opaque session_strong_ref() :: reference().
-opaque session_weak_ref() :: {atom(), integer()}.
