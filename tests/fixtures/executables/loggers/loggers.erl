%% logger, logger_formatter, logger_std_h, logger_filters, error_logger, error_logger_file_h, log_mf_h and
%% error_handler (plan 21 step LIB23): configuration and its errors, levels, handlers, filters, formatting, files with
%% rotation, the old error_logger API with report handlers and a log file, wrap logs and the undefined call handler.
%% The default handler is removed first, so everything logged is printed by the handlers of this test.
-module(loggers).
-export([main/1]).
-export([adding_handler/1, removing_handler/1, changing_config/3, log/2]).

-include_lib("kernel/include/logger.hrl").

main(_) ->
    ok = logger:remove_handler(default),
    primary(),
    levels(),
    handlers(),
    filters(),
    metadata(),
    macros(),
    formatting(),
    reports(),
    files(),
    rotation(),
    old_api(),
    log_file(),
    wrap_logs(),
    undefined_calls(),
    io:format("loggers: ok~n").

show(Label, Value) -> io:format("~s: ~p~n", [Label, Value]).

attempt(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

%% A handler of this module prints each event with its formatter, tagged with the handler's id.
adding_handler(#{config := #{fail := true}}) -> {error, refused};
adding_handler(Config) -> {ok, Config#{config => maps:merge(#{added => true}, maps:get(config, Config, #{}))}}.

removing_handler(#{id := Id}) -> io:format("removing ~p~n", [Id]).

changing_config(_, _, #{config := #{fail := true}}) -> {error, refused};
changing_config(_, _, New) -> {ok, New}.

log(Event, #{id := Id, formatter := {Formatter, FConfig}}) ->
    io:format("[~p] ~ts", [Id, Formatter:format(Event, FConfig)]).

plain(Id) -> plain(Id, #{}).

plain(Id, Extra) ->
    ok = logger:add_handler(
        Id, ?MODULE, maps:merge(#{formatter => {logger_formatter, #{template => [level, " ", msg, "\n"]}}}, Extra)
    ).

%% Configuration without the funs of filters, as a sorted list: OTP prints map keys in its internal order.
config(Config) -> sorted(maps:without([filters], Config)).

sorted(Map) -> lists:sort(maps:to_list(Map)).

primary() ->
    show("primary", config(logger:get_primary_config())),
    show("set_level", logger:set_primary_config(level, info)),
    show("level", maps:get(level, logger:get_primary_config())),
    show("update", logger:update_primary_config(#{metadata => #{app => test}})),
    show("updated", config(logger:get_primary_config())),
    show("set", logger:set_primary_config(#{level => notice})),
    show("reset", config(logger:get_primary_config())),
    show("primary_errors", [
        logger:set_primary_config(level, loud),
        logger:set_primary_config(nothing, 1),
        logger:set_primary_config(filter_default, maybe_not),
        logger:set_primary_config(filters, [bad]),
        logger:set_primary_config(metadata, []),
        logger:update_primary_config(#{level => 7}),
        logger:set_primary_config(not_a_map)
    ]).

levels() ->
    show("compare", [
        logger:compare_levels(error, info),
        logger:compare_levels(info, error),
        logger:compare_levels(debug, debug),
        logger:compare_levels(all, none),
        attempt(fun() -> logger:compare_levels(loud, info) end)
    ]),
    show("allow", [logger:allow(error, ?MODULE), logger:allow(info, ?MODULE), logger:allow(debug, ?MODULE)]),
    show("module_level", logger:set_module_level(?MODULE, debug)),
    show("module_levels", [logger:get_module_level(?MODULE), logger:get_module_level([?MODULE, lists])]),
    show("allow_module", [logger:allow(debug, ?MODULE), logger:allow(debug, lists)]),
    show("unset", logger:unset_module_level(?MODULE)),
    show("after_unset", [logger:get_module_level(), logger:allow(debug, ?MODULE)]),
    show("level_errors", [
        logger:set_module_level(?MODULE, loud),
        logger:set_module_level("loggers", info),
        attempt(fun() -> logger:unset_module_level(42) end)
    ]),
    show("all_levels", [
        logger:set_module_level(lists, all),
        logger:get_module_level(lists),
        logger:unset_module_level(),
        logger:get_module_level()
    ]).

handlers() ->
    plain(one),
    show("handler", config(element(2, logger:get_handler_config(one)))),
    logger:error("an error ~p", [1]),
    logger:warning("a warning"),
    logger:notice("a notice ~s", ["text"]),
    logger:info("not shown"),
    logger:log(critical, "via log ~p", [critical]),
    logger:log(alert, #{what => report}),
    logger:error(#{key => [x, y]}, #{}),
    logger:emergency([{key, value}, {other, 2}]),
    logger:error(fun(N) -> {"lazy ~p", [N]} end, 7),
    logger:error(fun(_) -> #{lazy => report} end, x),
    logger:error(fun(_) -> {"lazy ~p", [meta]} end, x, #{some => meta}),
    show("errors", [
        logger:add_handler(one, ?MODULE, #{}),
        logger:add_handler(two, ?MODULE, #{level => loud}),
        logger:add_handler(two, ?MODULE, #{config => #{fail => true}}),
        logger:add_handler(two, no_such_handler_module, #{}),
        logger:add_handler("two", ?MODULE, #{}),
        logger:add_handler(two, ?MODULE, #{formatter => bad}),
        logger:add_handler(two, ?MODULE, not_a_map),
        logger:remove_handler(two),
        logger:get_handler_config(two),
        logger:set_handler_config(two, level, info)
    ]),
    plain(two, #{level => error}),
    show("ids", lists:sort(logger:get_handler_ids())),
    logger:warning("only one"),
    logger:error("both"),
    show("update_level", logger:update_handler_config(two, level, warning)),
    logger:warning("both again"),
    show("set_config", logger:set_handler_config(two, config, #{custom => 1})),
    show("config", maps:get(config, element(2, logger:get_handler_config(two)))),
    show("change_errors", [
        logger:set_handler_config(two, id, other),
        logger:update_handler_config(two, #{module => lists}),
        logger:set_handler_config(two, config, #{fail => true}),
        logger:update_handler_config(two, level, loud)
    ]),
    show("formatter", logger:update_formatter_config(two, template, [msg, "!\n"])),
    logger:error("new template"),
    show("formatter_errors", [
        logger:update_formatter_config(two, template, bad),
        logger:update_formatter_config(nobody, #{})
    ]),
    show("remove", logger:remove_handler(two)),
    show("handler_configs", lists:sort([Id || #{id := Id} <- logger:get_handler_config()])),
    show("config_all", lists:sort(maps:keys(logger:get_config()))),
    ok = logger:remove_handler(one).

filters() ->
    plain(fil),
    Domain = {fun logger_filters:domain/2, {stop, sub, [secret]}},
    show("add_filter", logger:add_handler_filter(fil, no_secret, Domain)),
    logger:error("public"),
    logger:error("secret", #{domain => [secret, more]}),
    logger:error("other domain", #{domain => [public]}),
    show("filter_exists", logger:add_handler_filter(fil, no_secret, Domain)),
    show("bad_filter", logger:add_handler_filter(fil, bad, {not_a_fun, x})),
    show("remove_filter", [logger:remove_handler_filter(fil, no_secret), logger:remove_handler_filter(fil, no_secret)]),
    Level = {fun logger_filters:level/2, {stop, lt, error}},
    ok = logger:add_primary_filter(no_warnings, Level),
    logger:warning("dropped warning"),
    logger:error("kept error"),
    ok = logger:remove_primary_filter(no_warnings),
    Rewrite = fun
        (#{msg := {F, A}} = Event, Tag) -> Event#{msg := {"~p: " ++ F, [Tag | A]}};
        (Event, _) -> Event
    end,
    ok = logger:add_handler_filter(fil, rewrite, {Rewrite, rewritten}),
    logger:error("changed ~p", [msg]),
    ok = logger:remove_handler_filter(fil, rewrite),
    ok = logger:add_handler_filter(fil, ignore, {fun(_, _) -> ignore end, x}),
    ok = logger:set_handler_config(fil, filter_default, stop),
    logger:error("ignored then stopped"),
    ok = logger:set_handler_config(fil, filter_default, log),
    logger:error("ignored then logged"),
    ok = logger:set_handler_config(fil, filters, [
        {progress, {fun logger_filters:progress/2, stop}},
        {stop_all, {fun(_, _) -> stop end, x}}
    ]),
    logger:error("stopped"),
    show("filter_ids", [Id || {Id, _} <- maps:get(filters, element(2, logger:get_handler_config(fil)))]),
    show("direct", [
        logger_filters:domain(#{meta => #{domain => [a, b]}}, {log, super, [a, b, c]}),
        logger_filters:domain(#{meta => #{domain => [a, b]}}, {stop, equal, [a, b]}),
        logger_filters:domain(#{meta => #{}}, {log, undefined, []}),
        logger_filters:domain(#{meta => #{domain => [x]}}, {log, not_equal, [y]}),
        logger_filters:level(#{level => info}, {log, gteq, warning}),
        logger_filters:level(#{level => info}, {log, neq, warning}),
        logger_filters:progress(
            #{meta => #{domain => [otp, sasl]}, msg => {report, #{label => {supervisor, progress}}}},
            stop
        ),
        logger_filters:remote_gl(#{meta => #{gl => group_leader()}}, stop)
    ]),
    show("filter_errors", [
        attempt(fun() -> logger_filters:domain(#{meta => #{}}, {bad, sub, []}) end),
        attempt(fun() -> logger_filters:level(#{level => info}, {log, gteq, loud}) end),
        attempt(fun() -> logger_filters:progress(#{}, maybe_not) end)
    ]),
    ok = logger:remove_handler(fil).

metadata() ->
    plain(meta, #{
        formatter => {logger_formatter, #{template => [{who, [who, " "], []}, {n, ["n=", n, " "], []}, msg, "\n"]}}
    }),
    show("no_metadata", logger:get_process_metadata()),
    ok = logger:set_process_metadata(#{who => me}),
    logger:error("with process metadata"),
    ok = logger:update_process_metadata(#{n => 1}),
    show("metadata", sorted(logger:get_process_metadata())),
    logger:error("call metadata wins", #{who => call}),
    ok = logger:update_primary_config(#{metadata => #{n => primary, who => primary}}),
    ok = logger:unset_process_metadata(),
    logger:error("primary metadata"),
    ok = logger:set_primary_config(metadata, #{}),
    show("metadata_errors", [
        attempt(fun() -> logger:set_process_metadata(not_a_map) end),
        attempt(fun() -> logger:update_process_metadata([]) end)
    ]),
    ok = logger:remove_handler(meta).

macros() ->
    plain(mac, #{formatter => {logger_formatter, #{template => [level, " ", mfa, ":", line, " ", msg, "\n"]}}}),
    ?LOG_ERROR("macro ~p", [error]),
    ?LOG_WARNING(#{macro => report}),
    ?LOG_NOTICE("notice with ~s", ["meta"], #{extra => 1}),
    ?LOG_INFO("not shown"),
    ?LOG(critical, "level ~p", [critical]),
    logger:set_module_level(?MODULE, debug),
    ?LOG_DEBUG("debug for this module"),
    logger:unset_module_level(?MODULE),
    ?LOG_DEBUG(begin
        io:format("never evaluated~n"),
        "x"
    end),
    ok = logger:remove_handler(mac).

fixed_time() -> calendar:rfc3339_to_system_time("2024-03-05T06:07:08.123456Z", [{unit, microsecond}]).

event(Level, Msg, Meta) ->
    #{level => Level, msg => Msg, meta => maps:merge(#{time => fixed_time(), pid => self()}, Meta)}.

formatting() ->
    Utc = #{time_offset => "Z"},
    Event = event(error, {"hello ~p~nworld", [[1, 2, 3]]}, #{}),
    Text = fun(E, Config) -> io:format("~ts|~n", [logger_formatter:format(E, Config)]) end,
    Text(Event, Utc),
    Text(Event, Utc#{single_line => false}),
    Text(Event, Utc#{legacy_header => true, single_line => false}),
    Text(Event, Utc#{template => [time, " ", level, ": ", msg, "\n"], time_designator => $\s}),
    Text(Event, Utc#{template => [{pid, ["has pid"], ["no pid"]}, " ", {missing, [missing], ["none"]}, "\n"]}),
    Text(Event, #{time_offset => "+02:00", template => [time]}),
    Text(Event, #{time_offset => -3600000000, template => [time]}),
    Text(event(info, {string, "plain text"}, #{}), Utc),
    Text(event(info, {string, <<"binary text">>}, #{}), Utc),
    Text(event(info, {report, #{list => [1, 2]}}, #{}), Utc),
    Text(event(info, {report, [{x, 1}, {y, "two"}]}, #{}), Utc#{single_line => false}),
    Text(event(info, {report, #{r => 1}}, #{report_cb => fun(R) -> {"cb1 ~p", [R]} end}), Utc),
    Text(
        event(info, {report, #{r => 2}}, #{
            report_cb => fun(R, C) -> io_lib:format("cb2 ~p ~p", [R, maps:get(depth, C)]) end
        }),
        Utc#{depth => 5}
    ),
    Long = lists:seq(1, 40),
    Text(event(notice, {"~p", [Long]}, #{}), Utc#{depth => 5}),
    Text(event(notice, {"~p", [Long]}, #{}), Utc#{chars_limit => 30}),
    Text(event(notice, {"~p", [Long]}, #{}), Utc#{max_size => 20}),
    Text(event(notice, {"bad ~p ~p", [one]}, #{}), Utc),
    Text(
        event(notice, {"meta ~p", [x]}, #{key => <<"bin">>, list => "str"}),
        #{template => [key, " ", list, " ", {key, [msg], []}, "\n"]}
    ),
    show("check_config", [
        logger_formatter:check_config(#{}),
        logger_formatter:check_config(#{template => [msg]}),
        logger_formatter:check_config(#{depth => 0}),
        logger_formatter:check_config(#{chars_limit => -1}),
        logger_formatter:check_config(#{template => [{a, b}]}),
        logger_formatter:check_config(#{unknown => 1}),
        logger_formatter:check_config(#{time_offset => "x"}),
        logger_formatter:check_config(not_a_map)
    ]).

reports() ->
    Report = fun({Format, Args}) -> lists:flatten(io_lib:format(Format, Args)) end,
    show("format_report", [
        Report(logger:format_report(#{a => 1})),
        Report(logger:format_report([{a, 1}, b])),
        Report(logger:format_report([1, 2])),
        Report(logger:format_report(atom))
    ]),
    show("otp_report", [
        Report(logger:format_otp_report(#{label => {x, y}, report => [{a, 1}]})),
        Report(logger:format_otp_report(#{format => "f ~p", args => [1]}))
    ]).

%% Files of logger_std_h: written, synced and read back.
files() ->
    _ = file:delete("std.log"),
    ok = logger:add_handler(file_h, logger_std_h, #{
        config => #{file => "std.log"},
        formatter => {logger_formatter, #{template => [level, " ", msg, "\n"]}}
    }),
    logger:error("to the file ~p", [1]),
    logger:warning("second line"),
    show("filesync", logger_std_h:filesync(file_h)),
    {ok, #{config := Config}} = logger:get_handler_config(file_h),
    show(
        "file_config",
        sorted(
            maps:with(
                [type, file_check, max_no_bytes, max_no_files, compress_on_rotate, sync_mode_qlen],
                Config
            )
        )
    ),
    show("file", file:read_file("std.log")),
    show(
        "std_errors",
        lists:map(fun brief/1, [
            logger:update_handler_config(file_h, config, #{type => standard_io}),
            logger:update_handler_config(file_h, config, #{file => "other.log"}),
            logger:update_handler_config(file_h, config, #{max_no_files => -1}),
            logger:update_handler_config(file_h, config, #{bogus => 1}),
            logger:add_handler(io_h, logger_std_h, #{config => #{type => standard_io, file => "x.log"}}),
            logger:add_handler(io_h, logger_std_h, #{config => #{type => bad}}),
            logger:add_handler(io_h, logger_std_h, #{config => #{sync_mode_qlen => 100, drop_mode_qlen => 10}})
        ])
    ),
    ok = logger:remove_handler(file_h),
    show(
        "io_handler",
        logger:add_handler(io_h, logger_std_h, #{
            config => #{type => standard_io},
            formatter => {logger_formatter, #{template => ["stdout: ", msg, "\n"]}}
        })
    ),
    logger:error("through standard_io"),
    show("filesync_io", logger_std_h:filesync(io_h)),
    ok = logger:remove_handler(io_h),
    show("filesync_missing", attempt(fun() -> logger_std_h:filesync(io_h) end)).

%% Errors with the absolute file names of logger_std_h left out.
brief({error, {illegal_config_change, Module, Old, New}}) ->
    {error, {illegal_config_change, Module, sorted(maps:remove(file, Old)), sorted(maps:remove(file, New))}};
brief({error, {handler_not_added, {invalid_config, Module, Config}}}) ->
    {error, {handler_not_added, {invalid_config, Module, maps:keys(Config)}}};
brief(Other) ->
    Other.

rotation() ->
    lists:foreach(fun(F) -> file:delete(F) end, ["rot.log", "rot.log.0", "rot.log.1", "rot.log.0.gz", "rot.log.1.gz"]),
    Add = fun(Compress) ->
        logger:add_handler(rot, logger_std_h, #{
            config => #{file => "rot.log", max_no_bytes => 40, max_no_files => 2, compress_on_rotate => Compress},
            formatter => {logger_formatter, #{template => [msg, "\n"]}}
        })
    end,
    ok = Add(false),
    [logger:error("line ~2..0w of rotation", [N]) || N <- lists:seq(1, 7)],
    ok = logger_std_h:filesync(rot),
    ok = logger:remove_handler(rot),
    show("rotated", [{F, file:read_file(F)} || F <- ["rot.log", "rot.log.0", "rot.log.1", "rot.log.2"]]),
    ok = Add(true),
    [logger:error("compressed ~2..0w of rotation", [N]) || N <- lists:seq(1, 5)],
    ok = logger:remove_handler(rot),
    Gunzip = fun(F) ->
        case file:read_file(F) of
            {ok, Bin} -> zlib:gunzip(Bin);
            Error -> Error
        end
    end,
    show("compressed", [{F, Gunzip(F)} || F <- ["rot.log.0.gz", "rot.log.1.gz"]]),
    show("current", file:read_file("rot.log")).

old_api() ->
    show("handlers_before", error_logger:which_report_handlers()),
    show("add_report", error_logger:add_report_handler(loggers_events, self())),
    show("handlers", error_logger:which_report_handlers()),
    show("logger_handler", lists:member(error_logger, logger:get_handler_ids())),
    error_logger:error_msg("error ~p~n", [1]),
    error_logger:error_msg("no args~n"),
    error_logger:format("format ~p~n", [2]),
    error_logger:warning_msg("warning ~p~n", [3]),
    error_logger:info_msg("info ~p~n", [4]),
    error_logger:error_report([{a, 1}]),
    error_logger:error_report(my_type, "typed"),
    error_logger:warning_report(#{w => 1}),
    error_logger:info_report(info_type, [x]),
    error_logger:info_report("info text"),
    logger:error("plain logger event"),
    logger:error("tagged logger event", #{error_logger => #{tag => error}}),
    logger:error(#{tagged => report}, #{error_logger => #{tag => error_report, type => std_error}}),
    error_logger:error_info(some_info),
    show("events", collect()),
    show("map", error_logger:warning_map()),
    show("depth", [error_logger:get_format_depth(), error_logger:limit_term({a, [b, c]})]),
    show("delete_report", error_logger:delete_report_handler(loggers_events)),
    show("delete_again", error_logger:delete_report_handler(loggers_events)),
    show("handlers_after", [error_logger:which_report_handlers(), lists:member(error_logger, logger:get_handler_ids())]).

collect() ->
    receive
        {event, Event} -> [Event | collect()]
    after 100 -> []
    end.

%% error_logger:logfile/1 and error_logger_file_h, with the time of each header left out.
log_file() ->
    _ = file:delete("old.log"),
    show("logfile", [error_logger:logfile(filename), error_logger:logfile({open, "old.log"})]),
    show("logfile_again", [error_logger:logfile({open, "old.log"}), error_logger:logfile(filename)]),
    error_logger:error_msg("file error ~p~n", [1]),
    error_logger:warning_report([{key, value}, plain]),
    error_logger:info_msg("file info~n"),
    error_logger:error_report(other_type, ignored),
    error_logger:info_report("text report"),
    show("close", [error_logger:logfile(close), error_logger:logfile(close), error_logger:logfile(filename)]),
    {ok, Bin} = file:read_file("old.log"),
    Text = re:replace(Bin, "==== [^=]* ===", "==== TIME ===", [global, {return, list}]),
    io:format("old.log:~n~ts", [Text]),
    error_logger:delete_report_handler(error_logger_file_h).

%% log_mf_h wrap files under a gen_event manager of this test: the index and the decoded events.
wrap_logs() ->
    Dir = "wraplogs",
    _ = [file:delete(filename:join(Dir, F)) || F <- ["index", "1", "2", "3"]],
    _ = file:make_dir(Dir),
    {ok, Manager} = gen_event:start(),
    Args = log_mf_h:init(Dir, 60, 3, fun(Event) -> Event =/= skipped end),
    show("init", element(1, Args) =:= Dir andalso element(2, Args) =:= 60),
    show("add", gen_event:add_handler(Manager, log_mf_h, Args)),
    [gen_event:notify(Manager, {event, N, lists:duplicate(5, N)}) || N <- lists:seq(1, 6)],
    gen_event:notify(Manager, skipped),
    show("call", gen_event:call(Manager, log_mf_h, null)),
    show("delete", element(1, gen_event:delete_handler(Manager, log_mf_h, stop))),
    show("index", file:read_file(filename:join(Dir, "index"))),
    show("wrap_files", [{F, events(filename:join(Dir, F))} || F <- ["1", "2", "3"]]),
    ok = gen_event:add_handler(Manager, log_mf_h, log_mf_h:init(Dir, 60, 3)),
    gen_event:notify(Manager, after_restart),
    ok = gen_event:stop(Manager),
    show("restart_index", file:read_file(filename:join(Dir, "index"))),
    show("restart_file", events(filename:join(Dir, "3"))).

events(File) ->
    case file:read_file(File) of
        {ok, Bin} -> decode(Bin);
        Error -> Error
    end.

decode(<<Size:16, Bin:Size/binary, Rest/binary>>) ->
    {{_Date, _Time}, Event} = binary_to_term(Bin),
    [Event | decode(Rest)];
decode(<<>>) ->
    [].

undefined_calls() ->
    show("undefined_function", [
        error_handler:undefined_function(lists, reverse, [[1, 2, 3]]),
        error_handler:undefined_function(loggers_undefined, no_such_function, [a, b]),
        attempt(fun() -> error_handler:undefined_function(lists, no_such_function, [a]) end),
        attempt(fun() -> error_handler:undefined_function(no_such_module_at_all, f, []) end)
    ]),
    show("undefined_lambda", error_handler:undefined_lambda(lists, fun(X) -> X * 2 end, [21])),
    show("raise", [
        try
            error_handler:raise_undef_exception(m, f, [1])
        catch
            error:undef:Stack -> hd(Stack)
        end,
        try
            error_handler:stub_function(m, f, [2])
        catch
            Class:Reason -> {Class, Reason}
        end
    ]).
