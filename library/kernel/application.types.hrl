%% OTP 29 application types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([restart_type/0,start_type/0]).
-type start_type() ::
          normal |
          {takeover, Node :: node()} |
          {failover, Node :: node()}.
-type restart_type() :: permanent | transient | temporary.
