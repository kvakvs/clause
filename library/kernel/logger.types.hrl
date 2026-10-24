%% OTP 29 logger types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([config_handler/0,
              filter/0,
              filter_arg/0,
              filter_id/0,
              filter_return/0,
              formatter_config/0,
              handler_config/0,
              handler_id/0,
              level/0,
              log_event/0,
              metadata/0,
              msg_fun/0,
              olp_config/0,
              primary_config/0,
              report/0,
              report_cb/0,
              report_cb_config/0,
              timestamp/0]).
-type log_event() ::
          #{level := level(),
            msg :=
                {io:format(), [term()]} |
                {report, report()} |
                {string, unicode:chardata()},
            meta := metadata()}.
-type level() ::
          emergency | alert | critical | error | warning | notice |
          info | debug.
-type report() :: map() | [{atom(), term()}, ...].
-type report_cb() ::
          fun((report()) -> {io:format(), [term()]}) |
          fun((report(), report_cb_config()) -> unicode:chardata()).
-type report_cb_config() ::
          #{depth := pos_integer() | unlimited,
            chars_limit := pos_integer() | unlimited,
            single_line := boolean()}.
-type msg_fun() ::
          fun((term()) ->
                  msg_fun_return() | {msg_fun_return(), metadata()}).
-type msg_fun_return() ::
          {io:format(), [term()]} |
          report() |
          unicode:chardata() |
          ignore.
-type metadata() ::
          #{pid => pid(),
            gl => pid(),
            time => timestamp(),
            mfa => {module(), atom(), non_neg_integer()},
            file => file:filename(),
            line => non_neg_integer(),
            domain => [atom()],
            report_cb => report_cb(),
            atom() => term()}.
-type filter_id() :: atom().
-type filter() ::
          {fun((log_event(), filter_arg()) -> filter_return()),
           filter_arg()}.
-type filter_arg() :: term().
-type filter_return() :: stop | ignore | log_event().
-type primary_config() ::
          #{level => level() | all | none,
            metadata => metadata(),
            filter_default => log | stop,
            filters => [{filter_id(), filter()}]}.
-type timestamp() :: integer().
-type formatter_config() :: #{atom() => term()}.
-type config_handler() ::
          {handler,
           logger_handler:id(),
           module(),
           logger_handler:config()}.
-type olp_config() :: logger_handler:olp_config().
-type handler_id() :: logger_handler:id().
-type handler_config() :: logger_handler:config().
