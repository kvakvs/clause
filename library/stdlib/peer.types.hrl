%% OTP 29 peer types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([disconnect_timeout/0,
              exec/0,
              peer_state/0,
              server_ref/0,
              start_options/0]).
-type server_ref() :: pid().
-type connection() ::
          Port :: 0..65535 | {inet:ip_address(), 0..65535} | standard_io.
-type wait_boot() :: timeout() | {pid(), Tag :: term()} | false.
-type exec() :: file:name() | {file:name(), [string()]}.
-type start_options() ::
          #{name => atom() | string(),
            longnames => boolean(),
            host => string(),
            peer_down => stop | continue | crash,
            connection => connection(),
            exec => exec(),
            detached => boolean(),
            args => [string()],
            post_process_args => fun(([string()]) -> [string()]),
            env => [{string(), string()}],
            wait_boot => wait_boot(),
            shutdown =>
                close | halt |
                {halt, disconnect_timeout()} |
                disconnect_timeout()}.
-type peer_state() :: booting | running | {down, Reason :: term()}.
-type disconnect_timeout() :: 1000..4294967295 | infinity.
