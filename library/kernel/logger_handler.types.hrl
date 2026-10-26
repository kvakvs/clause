%% OTP 29 logger_handler types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/libraries/library.md#types).
-export_type([config/0,id/0,olp_config/0]).
-type config() ::
          #{id => id(),
            config => term(),
            level => logger:level() | all | none,
            module => module(),
            filter_default => log | stop,
            filters => [{logger:filter_id(), logger:filter()}],
            formatter => {module(), logger:formatter_config()}}.
-type id() :: atom().
-type olp_config() ::
          #{sync_mode_qlen => non_neg_integer(),
            drop_mode_qlen => pos_integer(),
            flush_qlen => pos_integer(),
            burst_limit_enable => boolean(),
            burst_limit_max_count => pos_integer(),
            burst_limit_window_time => pos_integer(),
            overload_kill_enable => boolean(),
            overload_kill_qlen => pos_integer(),
            overload_kill_mem_size => pos_integer(),
            overload_kill_restart_after => non_neg_integer() | infinity}.
