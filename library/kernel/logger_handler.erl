%% Clause's logger_handler (plan 21 step LIB23, docs/library.md): the behaviour of logger handlers, callbacks only.
-module(logger_handler).
-include("logger_handler.types.hrl").

-callback adding_handler(Config1) -> {ok, Config2} | {error, Reason} when
    Config1 :: config(), Config2 :: config(), Reason :: term().
-callback changing_config(SetOrUpdate, OldConfig, NewConfig) -> {ok, Config} | {error, Reason} when
    SetOrUpdate :: set | update, OldConfig :: config(), NewConfig :: config(), Config :: config(), Reason :: term().
-callback filter_config(Config) -> FilteredConfig when Config :: config(), FilteredConfig :: config().
-callback log(LogEvent, Config) -> term() when LogEvent :: logger:log_event(), Config :: config().
-callback removing_handler(Config) -> ok when Config :: config().
-optional_callbacks([adding_handler/1, changing_config/3, filter_config/1, removing_handler/1]).
