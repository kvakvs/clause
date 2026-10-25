%% What Clause's logger handlers share (plan 21 steps LIB23, LIB52; docs/library.md), as OTP's logger_h_common: the
%% common and overload options and their checks, adding and changing a handler's configuration around the handler
%% module's own check_config/4, formatting events in the logging process, and synchronous requests to a handler's
%% process. logger_std_h and logger_disk_log_h each run their own handler process.
-module(clause_logger_h).
-export([adding_handler/3, changing_config/4, diff_maps/2, filter_config/1, reg_name/2, request/2, stop/1, to_binary/2]).

-define(OLP_KEYS, [
    sync_mode_qlen,
    drop_mode_qlen,
    flush_qlen,
    burst_limit_enable,
    burst_limit_max_count,
    burst_limit_window_time,
    overload_kill_enable,
    overload_kill_qlen,
    overload_kill_mem_size,
    overload_kill_restart_after
]).
-define(COMMON_KEYS, [filesync_repeat_interval]).
-define(READ_ONLY_KEYS, [olp]).
-define(CALL_TIMEOUT, 5000).

% Check a new handler's configuration (Module's own keys, then the common and overload ones) and start its process
% with Start(Name, HConfig): the configuration with the process as olp.
adding_handler(Module, #{id := Name} = Config, Start) ->
    HConfig0 = maps:get(config, Config, #{}),
    case Module:check_config(Name, set, undefined, maps:without(?OLP_KEYS ++ ?COMMON_KEYS, HConfig0)) of
        {ok, Own} ->
            Common = maps:merge(
                maps:merge(#{filesync_repeat_interval => 5000}, maps:with(?COMMON_KEYS, HConfig0)),
                maps:with(?COMMON_KEYS, Own)
            ),
            Olp = maps:merge(olp_defaults(), maps:with(?OLP_KEYS, HConfig0)),
            case {check_common(Common), check_olp(Olp)} of
                {ok, ok} ->
                    HConfig = maps:merge(maps:merge(Common, Own), Olp),
                    case Start(Name, HConfig) of
                        {ok, Pid} -> {ok, Config#{config => HConfig#{olp => Pid}}};
                        Error -> Error
                    end;
                {{error, Faulty}, _} ->
                    {error, {invalid_config, Module, Faulty}};
                {_, Error} ->
                    Error
            end;
        Error ->
            Error
    end.

% Check a changed configuration as adding_handler/3 does; the handler process gets {config_changed, Common, Own}.
changing_config(Module, SetOrUpdate, #{id := Name, config := OldHConfig}, New) ->
    NewHConfig0 = maps:get(config, New, #{}),
    Excluded = ?OLP_KEYS ++ ?COMMON_KEYS ++ ?READ_ONLY_KEYS,
    case
        Module:check_config(Name, SetOrUpdate, maps:without(Excluded, OldHConfig), maps:without(Excluded, NewHConfig0))
    of
        {ok, Own} ->
            CommonBase =
                case SetOrUpdate of
                    set -> #{filesync_repeat_interval => 5000};
                    update -> maps:with(?COMMON_KEYS, OldHConfig)
                end,
            Common = maps:merge(
                maps:merge(CommonBase, maps:with(?COMMON_KEYS, NewHConfig0)), maps:with(?COMMON_KEYS, Own)
            ),
            OlpBase =
                case SetOrUpdate of
                    set -> olp_defaults();
                    update -> maps:with(?OLP_KEYS, OldHConfig)
                end,
            Olp = maps:merge(OlpBase, maps:with(?OLP_KEYS, NewHConfig0)),
            case {check_common(Common), check_olp(Olp)} of
                {ok, ok} ->
                    maps:get(olp, OldHConfig) ! {config_changed, Common, Own},
                    HConfig = maps:merge(
                        maps:merge(maps:merge(Common, Own), maps:with(?READ_ONLY_KEYS, OldHConfig)), Olp
                    ),
                    {ok, New#{config => HConfig}};
                {{error, Faulty}, _} ->
                    {error, {invalid_config, Module, Faulty}};
                {_, Error} ->
                    Error
            end;
        Error ->
            Error
    end.

filter_config(#{config := HConfig} = Config) -> Config#{config => maps:without(?READ_ONLY_KEYS, HConfig)}.

check_common(Common) ->
    case
        [
            {K, V}
         || K := V <- Common, not (K =:= filesync_repeat_interval andalso (is_integer(V) orelse V =:= no_repeat))
        ]
    of
        [] -> ok;
        [{K, V} | _] -> {error, #{K => V}}
    end.

olp_defaults() ->
    #{
        sync_mode_qlen => 10,
        drop_mode_qlen => 200,
        flush_qlen => 1000,
        burst_limit_enable => true,
        burst_limit_max_count => 500,
        burst_limit_window_time => 1000,
        overload_kill_enable => false,
        overload_kill_qlen => 20000,
        overload_kill_mem_size => 3000000,
        overload_kill_restart_after => 5000
    }.

check_olp(Options) ->
    Bad = [
        {K, V}
     || K := V <- Options,
        not (is_integer(V) orelse
            (K =:= burst_limit_enable orelse K =:= overload_kill_enable) andalso is_boolean(V) orelse
            K =:= overload_kill_restart_after andalso V =:= infinity)
    ],
    case Bad of
        [{K, V} | _] ->
            {error, {invalid_olp_config, #{K => V}}};
        [] ->
            #{sync_mode_qlen := Sync, drop_mode_qlen := Drop, flush_qlen := Flush} = Options,
            case Drop > 1 andalso Sync =< Drop andalso Drop =< Flush of
                true -> ok;
                false -> {error, {invalid_olp_levels, maps:with([sync_mode_qlen, drop_mode_qlen, flush_qlen], Options)}}
            end
    end.

% The keys whose values differ between two maps, with the values of each, as OTP's logger_server:diff_maps/2.
diff_maps(M1, M2) ->
    Keys = lists:usort(maps:keys(M1) ++ maps:keys(M2)),
    Changed = [K || K <- Keys, maps:find(K, M1) =/= maps:find(K, M2)],
    {maps:with(Changed, M1), maps:with(Changed, M2)}.

% The registered name of a handler's process: Module_Name.
reg_name(Module, Name) -> list_to_atom(lists:concat([Module, "_", Name])).

% --- Formatting ---

% The formatter's text as a binary; a failing formatter falls back to the default one.
to_binary(#{msg := {report, _}, meta := Meta} = Event, Config) when not is_map_key(report_cb, Meta) ->
    binary_text(Event#{meta => Meta#{report_cb => fun logger:format_otp_report/1}}, Config);
to_binary(Event, Config) ->
    binary_text(Event, Config).

binary_text(Event, Config) ->
    {Formatter, FConfig} = maps:get(
        formatter, Config, {logger_formatter, #{legacy_header => true, single_line => false}}
    ),
    Text = format(Event, Formatter, FConfig),
    case unicode:characters_to_binary(Text) of
        Bin when is_binary(Bin) -> Bin;
        _ -> <<"FORMATTER ERROR: bad return value\n">>
    end.

format(Event, Formatter, FConfig) ->
    try
        formatted(Formatter, Event, FConfig)
    catch
        _:_ ->
            case {Formatter, FConfig} of
                {logger_formatter, #{}} when map_size(FConfig) =:= 0 -> "DEFAULT FORMATTER CRASHED\n";
                _ -> format(Event#{msg => {"FORMATTER CRASH: ~tp", [maps:get(msg, Event)]}}, logger_formatter, #{})
            end
    end.

% Names the default formatter literally, so programs that log link it.
formatted(logger_formatter, Event, FConfig) -> logger_formatter:format(Event, FConfig);
formatted(Formatter, Event, FConfig) -> Formatter:format(Event, FConfig).

% --- The handler process ---

% A synchronous request: the process's reply, or {error, Reason} when it ended.
request(Pid, Request) when Pid =:= self() ->
    {error, {attempting_syncronous_call_to_self, Request}};
request(Pid, Request) ->
    Ref = monitor(process, Pid),
    Pid ! {Request, {self(), Ref}},
    receive
        {Ref, Result} ->
            demonitor(Ref, [flush]),
            Result;
        {'DOWN', Ref, _, _, Reason} ->
            {error, Reason}
    end.

% Stop a handler's process and wait for it (killed after five seconds).
stop(Pid) ->
    Ref = monitor(process, Pid),
    unlink(Pid),
    Pid ! stop,
    receive
        {'DOWN', Ref, _, _, _} -> ok
    after ?CALL_TIMEOUT ->
        exit(Pid, kill),
        ok
    end.
