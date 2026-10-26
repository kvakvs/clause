%% Clause's logger (plan 21 step LIB23, docs/libraries/library.md): OTP's logging API over clause_logger, which keeps the
%% configuration. A log event passes the module or primary level, then the primary filters, then each handler's
%% level and filters, and is handed to the handler's log/2 in the calling process; a failing filter or handler is
%% removed. Configuration functions check their arguments as OTP does and return its error terms.
-module(logger).
-export([
    add_default_metadata/1,
    add_handler/3,
    add_handler_filter/3,
    add_handlers/1,
    add_primary_filter/2,
    alert/1,
    alert/2,
    alert/3,
    allow/2,
    compare_levels/2,
    critical/1,
    critical/2,
    critical/3,
    debug/1,
    debug/2,
    debug/3,
    emergency/1,
    emergency/2,
    emergency/3,
    error/1,
    error/2,
    error/3,
    filter_stacktrace/2,
    format_otp_report/1,
    format_report/1,
    get_config/0,
    get_handler_config/0,
    get_handler_config/1,
    get_handler_ids/0,
    get_module_level/0,
    get_module_level/1,
    get_primary_config/0,
    get_process_metadata/0,
    get_proxy_config/0,
    i/0,
    i/1,
    info/1,
    info/2,
    info/3,
    internal_init_logger/0,
    internal_log/2,
    log/2,
    log/3,
    log/4,
    macro_log/3,
    macro_log/4,
    macro_log/5,
    notice/1,
    notice/2,
    notice/3,
    reconfigure/0,
    remove_handler/1,
    remove_handler_filter/2,
    remove_primary_filter/1,
    set_application_level/2,
    set_handler_config/2,
    set_handler_config/3,
    set_module_level/2,
    set_primary_config/1,
    set_primary_config/2,
    set_process_metadata/1,
    set_proxy_config/1,
    timestamp/0,
    unset_application_level/1,
    unset_module_level/0,
    unset_module_level/1,
    unset_process_metadata/0,
    update_formatter_config/2,
    update_formatter_config/3,
    update_handler_config/2,
    update_handler_config/3,
    update_primary_config/1,
    update_process_metadata/1,
    update_proxy_config/1,
    warning/1,
    warning/2,
    warning/3
]).
-include("logger.types.hrl").

-define(META_KEY, '$logger_metadata$').
-define(LEVELS, [none, emergency, alert, critical, error, warning, notice, info, debug, all]).
-define(IS_LEVEL(L),
    (L =:= emergency orelse L =:= alert orelse L =:= critical orelse L =:= error orelse L =:= warning orelse
        L =:= notice orelse L =:= info orelse L =:= debug)
).
-define(IS_STRING(S), (is_list(S) orelse is_binary(S))).
-define(IS_FORMAT(F), (?IS_STRING(F) orelse is_atom(F))).
-define(IS_REPORT(R), (is_map(R) orelse (is_list(R) andalso is_tuple(hd(R))))).
-define(OWN_KEYS, [level, filters, filter_default, handlers]).

%% Logging by level.

emergency(A) -> log(emergency, A).
emergency(A, B) -> log(emergency, A, B).
emergency(A, B, C) -> log(emergency, A, B, C).
alert(A) -> log(alert, A).
alert(A, B) -> log(alert, A, B).
alert(A, B, C) -> log(alert, A, B, C).
critical(A) -> log(critical, A).
critical(A, B) -> log(critical, A, B).
critical(A, B, C) -> log(critical, A, B, C).
error(A) -> log(error, A).
error(A, B) -> log(error, A, B).
error(A, B, C) -> log(error, A, B, C).
warning(A) -> log(warning, A).
warning(A, B) -> log(warning, A, B).
warning(A, B, C) -> log(warning, A, B, C).
notice(A) -> log(notice, A).
notice(A, B) -> log(notice, A, B).
notice(A, B, C) -> log(notice, A, B, C).
info(A) -> log(info, A).
info(A, B) -> log(info, A, B).
info(A, B, C) -> log(info, A, B, C).
debug(A) -> log(debug, A).
debug(A, B) -> log(debug, A, B).
debug(A, B, C) -> log(debug, A, B, C).

log(Level, StringOrReport) -> do_log(Level, StringOrReport, #{}).

log(Level, StringOrReport, Meta) when is_map(Meta), not is_function(StringOrReport) ->
    do_log(Level, StringOrReport, Meta);
log(Level, FunOrFormat, Args) ->
    do_log(Level, {FunOrFormat, Args}, #{}).

log(Level, FunOrFormat, Args, Meta) -> do_log(Level, {FunOrFormat, Args}, Meta).

%% What the ?LOG_* macros call once allow/2 passed: the location joins the metadata.
allow(Level, Module) when is_atom(Module) -> clause_logger:allow(Level, Module).

macro_log(Location, Level, StringOrReport) -> log_allowed(Location, Level, StringOrReport, #{}).

macro_log(Location, Level, StringOrReport, Meta) when is_map(Meta), not is_function(StringOrReport) ->
    log_allowed(Location, Level, StringOrReport, Meta);
macro_log(Location, Level, FunOrFormat, Args) ->
    log_allowed(Location, Level, {FunOrFormat, Args}, #{}).

macro_log(Location, Level, FunOrFormat, Args, Meta) -> log_allowed(Location, Level, {FunOrFormat, Args}, Meta).

do_log(Level, Msg, #{mfa := {Module, _, _}} = Meta) ->
    case clause_logger:allow(Level, Module) of
        true -> log_allowed(#{}, Level, Msg, Meta);
        false -> ok
    end;
do_log(Level, Msg, Meta) ->
    %% Without an mfa, only the primary level applies.
    Primary = persistent_term:get({clause_logger, primary}, clause_logger:level_to_int(notice)),
    case clause_logger:level_to_int(Level) =< Primary of
        true -> log_allowed(#{}, Level, Msg, Meta);
        false -> ok
    end.

%% A message fun runs only now, when its level passed.
log_allowed(Location, Level, {Fun, FunArgs} = FunCall, Meta) when is_function(Fun, 1) ->
    try Fun(FunArgs) of
        {Result, #{} = FunMeta} -> fun_result(Location, Level, Result, maps:merge(Meta, FunMeta), FunCall);
        Result -> fun_result(Location, Level, Result, Meta, FunCall)
    catch
        C:R -> log_allowed(Location, Level, {"LAZY_FUN CRASH: ~tp; Reason: ~tp", [{Fun, FunArgs}, {C, R}]}, Meta)
    end;
log_allowed(Location, Level, Msg, CallMeta) when is_map(CallMeta) ->
    {ok, Primary} = clause_logger:config(primary),
    Meta = add_default_metadata(
        maps:merge(
            Location,
            maps:merge(
                maps:get(metadata, Primary),
                maps:merge(
                    process_metadata(), CallMeta
                )
            )
        )
    ),
    case event(Level, Msg, Meta) of
        {ok, Event} -> dispatch(Event, Primary);
        error -> erlang:error(badarg, [Location, Level, Msg, CallMeta])
    end.

fun_result(Location, Level, Result, Meta, FunCall) ->
    case Result of
        ignore ->
            ok;
        {Format, Args} when ?IS_FORMAT(Format), is_list(Args) ->
            log_allowed(Location, Level, Result, Meta);
        Report when ?IS_REPORT(Report) ->
            log_allowed(Location, Level, Report, Meta);
        String when ?IS_STRING(String) ->
            log_allowed(Location, Level, String, Meta);
        Other ->
            log_allowed(Location, Level, {"LAZY_FUN ERROR: ~tp; Returned: ~tp", [FunCall, Other]}, Meta)
    end.

event(Level, {Format, Args}, Meta) when ?IS_LEVEL(Level), ?IS_FORMAT(Format), is_list(Args) ->
    {ok, #{level => Level, msg => {deatomize(Format), Args}, meta => Meta}};
event(Level, Report, Meta) when ?IS_LEVEL(Level), ?IS_REPORT(Report) ->
    {ok, #{level => Level, msg => {report, Report}, meta => Meta}};
event(Level, String, Meta) when ?IS_LEVEL(Level), ?IS_STRING(String) ->
    {ok, #{level => Level, msg => {string, String}, meta => Meta}};
event(_, _, _) ->
    error.

deatomize(Format) when is_atom(Format) -> atom_to_list(Format);
deatomize(Format) -> Format.

%% The primary filters, then each handler whose level and filters let the event through.
dispatch(Event, Primary) ->
    case apply_filters(primary, Event, maps:get(filters, Primary, []), Primary) of
        stop -> ok;
        Filtered -> lists:foreach(fun(Id) -> to_handler(Id, Filtered) end, maps:get(handlers, Primary, []))
    end,
    ok.

to_handler(Id, #{level := Level} = Event) ->
    case clause_logger:handler_level_allows(Id, Level) andalso clause_logger:config(Id) of
        {ok, #{module := Module} = Config} ->
            case apply_filters(Id, Event, maps:get(filters, Config, []), Config) of
                stop -> ok;
                Filtered -> handler_log(Id, Module, Filtered, maps:without(?OWN_KEYS, Config))
            end;
        _ ->
            ok
    end.

%% A handler that fails is removed.
handler_log(Id, Module, Event, Config) ->
    try
        Module:log(Event, Config)
    catch
        _:_ ->
            case remove_handler(Id) of
                ok -> internal_log(error, {removed_failing_handler, Id});
                _ -> ok
            end
    end.

apply_filters(Owner, Event, Filters, Config) ->
    case run_filters(Owner, Event, Filters, ignore) of
        stop ->
            stop;
        ignore ->
            case maps:get(filter_default, Config) of
                log -> Event;
                stop -> stop
            end;
        Filtered ->
            Filtered
    end.

%% A filter returns stop, ignore, or the (possibly changed) event; a failing filter is removed and ignored.
run_filters(Owner, Event, [{Id, {Fun, Arg}} | Filters], Result) ->
    try Fun(Event, Arg) of
        stop ->
            stop;
        ignore ->
            run_filters(Owner, Event, Filters, Result);
        #{level := Level, msg := _, meta := Meta} = Changed when is_atom(Level), is_map(Meta) ->
            run_filters(Owner, Changed, Filters, log);
        _ ->
            failed_filter(Owner, Id),
            run_filters(Owner, Event, Filters, Result)
    catch
        _:_ ->
            failed_filter(Owner, Id),
            run_filters(Owner, Event, Filters, Result)
    end;
run_filters(_, _, [], ignore) ->
    ignore;
run_filters(_, Event, [], log) ->
    Event.

failed_filter(Owner, Id) ->
    case clause_logger:remove_filter(Owner, Id) of
        ok -> internal_log(error, {removed_failing_filter, Id});
        _ -> ok
    end.

add_default_metadata(Meta) ->
    lists:foldl(
        fun(Key, M) ->
            case is_map_key(Key, M) of
                true -> M;
                false -> M#{Key => default_metadata(Key)}
            end
        end,
        Meta,
        [pid, gl, time]
    ).

default_metadata(pid) -> self();
default_metadata(gl) -> group_leader();
default_metadata(time) -> timestamp().

process_metadata() ->
    case get(?META_KEY) of
        Meta when is_map(Meta) -> Meta;
        _ -> #{}
    end.

timestamp() -> os:system_time(microsecond).

%% Reports as text.

format_otp_report(#{label := _, report := Report}) -> format_report(Report);
format_otp_report(Report) -> format_report(Report).

format_report(Report) when is_map(Report) ->
    format_report(maps:to_list(Report));
format_report(Report) ->
    try lists:flatten(Report) of
        [] ->
            {"~tp", [[]]};
        Flat ->
            case is_text(Flat) of
                true -> {"~ts", [Flat]};
                false -> term_lines(Report)
            end
    catch
        _:_ -> {"~tp", [Report]}
    end.

term_lines(Report) ->
    Lines = [term_line(Item) || Item <- Report],
    {lists:flatten(lists:join($\n, [F || {F, _} <- Lines])), lists:append([A || {_, A} <- Lines])}.

term_line({Tag, Data}) ->
    case text_data(Data) of
        true -> {"    ~tp: ~ts", [Tag, Data]};
        false -> {"    ~tp: ~tp", [Tag, Data]}
    end;
term_line(Data) ->
    {"    ~tp", [Data]}.

text_data(Data) ->
    try lists:flatten(Data) of
        Flat -> is_text(Flat)
    catch
        _:_ -> false
    end.

is_text([]) -> false;
is_text(Flat) -> io_lib:printable_unicode_list(Flat).

%% Prints a term on standard output, bypassing the handlers, as logger's internal errors are shown.
internal_log(Level, Term) when is_atom(Level) ->
    io:put_chars("Logger - " ++ atom_to_list(Level) ++ ": "),
    erlang:display(Term).

%% The stack up to the first frame of Module.
filter_stacktrace(Module, [{Module, _, _, _} | _]) -> [];
filter_stacktrace(Module, [Frame | Rest]) -> [Frame | filter_stacktrace(Module, Rest)];
filter_stacktrace(_, []) -> [].

%% Configuration.

add_handler(default = Id, Module, Config) when is_map(Config), not is_map_key(filters, Config) ->
    add_handler(
        Id,
        Module,
        maps:merge(
            #{filter_default => stop, filters => clause_logger:default_handler_filters()},
            Config
        )
    );
add_handler(Id, Module, Config) ->
    try
        check_id(Id),
        check_module(Module),
        checked(Id, Config)
    of
        ok -> clause_logger:add_handler(Id, Module, Config)
    catch
        throw:Error -> {error, Error}
    end.

remove_handler(Id) -> clause_logger:remove_handler(Id).

add_primary_filter(FilterId, Filter) -> add_filter(primary, {FilterId, Filter}).

add_handler_filter(Id, FilterId, Filter) -> add_filter(Id, {FilterId, Filter}).

add_filter(Owner, Filter) ->
    case sanity(Owner, [{filters, [Filter]}]) of
        ok -> clause_logger:add_filter(Owner, Filter);
        Error -> Error
    end.

remove_primary_filter(FilterId) -> clause_logger:remove_filter(primary, FilterId).

remove_handler_filter(Id, FilterId) -> clause_logger:remove_filter(Id, FilterId).

set_primary_config(Key, Value) -> change(set, primary, Key, Value).
set_primary_config(Config) -> change(set, primary, Config).
update_primary_config(Config) -> change(update, primary, Config).

set_handler_config(Id, Key, Value) -> change(set, Id, Key, Value).
set_handler_config(Id, Config) -> change(set, Id, Config).
update_handler_config(Id, Key, Value) -> change(update, Id, Key, Value).
update_handler_config(Id, Config) -> change(update, Id, Config).

set_proxy_config(Config) -> change(set, proxy, Config).
update_proxy_config(Config) -> change(update, proxy, Config).

change(SetOrUpdate, Owner, Key, Value) ->
    case sanity(Owner, [{Key, Value}]) of
        ok -> clause_logger:set_config(SetOrUpdate, Owner, Key, Value);
        Error -> Error
    end.

change(SetOrUpdate, Owner, Config) when is_map(Config) ->
    case sanity(Owner, maps:to_list(Config)) of
        ok -> clause_logger:set_config(SetOrUpdate, Owner, Config);
        Error -> Error
    end;
change(_, proxy, Config) ->
    {error, {invalid_config, Config}};
change(_, _, Config) ->
    {error, {invalid_config, Config}}.

update_formatter_config(Id, Config) when is_map(Config) -> clause_logger:update_formatter_config(Id, Config);
update_formatter_config(_, Config) -> {error, {invalid_formatter_config, Config}}.

update_formatter_config(Id, Key, Value) -> update_formatter_config(Id, #{Key => Value}).

get_primary_config() ->
    {ok, Config} = clause_logger:config(primary),
    maps:remove(handlers, Config).

get_handler_config(Id) ->
    case clause_logger:config(Id) of
        {ok, Config} -> {ok, filtered(Config)};
        Error -> Error
    end.

get_handler_config() ->
    lists:sort(fun(#{id := A}, #{id := B}) -> A =< B end, [filtered(C) || C <- clause_logger:handler_configs()]).

%% A handler's configuration without the keys its module keeps for itself.
filtered(#{module := Module} = Config) ->
    try
        Module:filter_config(Config)
    catch
        _:_ -> Config
    end.

get_handler_ids() ->
    {ok, #{handlers := Ids}} = clause_logger:config(primary),
    lists:sort(Ids).

get_proxy_config() ->
    {ok, Config} = clause_logger:config(proxy),
    Config.

get_config() ->
    #{
        primary => get_primary_config(),
        handlers => get_handler_config(),
        proxy => get_proxy_config(),
        module_levels => lists:keysort(1, get_module_level())
    }.

set_module_level(Module, Level) when is_atom(Module) -> set_module_level([Module], Level);
set_module_level(Modules, Level) when is_list(Modules) ->
    case lists:all(fun erlang:is_atom/1, Modules) of
        true ->
            case sanity(primary, [{level, Level}]) of
                ok -> clause_logger:set_module_level(Modules, Level);
                Error -> Error
            end;
        false ->
            {error, {not_a_list_of_modules, Modules}}
    end;
set_module_level(Modules, _) ->
    {error, {not_a_list_of_modules, Modules}}.

unset_module_level(Module) when is_atom(Module) -> unset_module_level([Module]);
unset_module_level(Modules) when is_list(Modules) ->
    case lists:all(fun erlang:is_atom/1, Modules) of
        true -> clause_logger:unset_module_level(Modules);
        false -> {error, {not_a_list_of_modules, Modules}}
    end;
unset_module_level(Modules) ->
    {error, {not_a_list_of_modules, Modules}}.

unset_module_level() -> clause_logger:unset_module_level(all).

set_application_level(App, Level) ->
    case application_modules(App) of
        {ok, Modules} -> set_module_level(Modules, Level);
        undefined -> {error, {not_loaded, App}}
    end.

unset_application_level(App) ->
    case application_modules(App) of
        {ok, Modules} -> unset_module_level(Modules);
        undefined -> {error, {not_loaded, App}}
    end.

application_modules(App) ->
    try (application_module()):get_key(App, modules) of
        {ok, Modules} -> {ok, Modules};
        _ -> undefined
    catch
        _:_ -> undefined
    end.

%% Created for LIB23: application, reached by a runtime name so that only programs using it link it (LIB24).
application_module() -> application.

get_module_level(Module) when is_atom(Module) -> get_module_level([Module]);
get_module_level(Modules) when is_list(Modules) -> [{M, L} || {M, L} <- get_module_level(), lists:member(M, Modules)].

get_module_level() -> clause_logger:module_levels().

compare_levels(Level, Level) when is_atom(Level) ->
    case lists:member(Level, ?LEVELS) of
        true -> eq;
        false -> erlang:error(badarg, [Level, Level])
    end;
compare_levels(Level1, Level2) ->
    case lists:member(Level1, ?LEVELS) andalso lists:member(Level2, ?LEVELS) of
        true ->
            case clause_logger:level_to_int(Level1) < clause_logger:level_to_int(Level2) of
                true -> gt;
                false -> lt
            end;
        false ->
            erlang:error(badarg, [Level1, Level2])
    end.

set_process_metadata(Meta) when is_map(Meta) ->
    _ = put(?META_KEY, Meta),
    ok;
set_process_metadata(Meta) ->
    erlang:error(badarg, [Meta]).

update_process_metadata(Meta) when is_map(Meta) ->
    case get_process_metadata() of
        undefined -> set_process_metadata(Meta);
        Old when is_map(Old) -> set_process_metadata(maps:merge(Old, Meta))
    end;
update_process_metadata(Meta) ->
    erlang:error(badarg, [Meta]).

get_process_metadata() -> get(?META_KEY).

unset_process_metadata() ->
    _ = erase(?META_KEY),
    ok.

%% Configuration checks, as OTP's logger server makes them before a change: the first bad item is the error.
sanity(Owner, Items) ->
    try
        Type =
            case Owner of
                primary ->
                    primary;
                proxy ->
                    proxy;
                Id ->
                    check_id(Id),
                    handler
            end,
        Type =:= proxy orelse check(Type, Items),
        ok
    catch
        throw:Error -> {error, Error}
    end.

checked(Id, Config) when is_map(Config) ->
    case sanity(Id, maps:to_list(Config)) of
        ok -> ok;
        {error, Error} -> throw(Error)
    end;
checked(_, Config) ->
    throw({invalid_config, Config}).

check(Type, [{level, Level} | Rest]) ->
    lists:member(Level, ?LEVELS) orelse throw({invalid_level, Level}),
    check(Type, Rest);
check(Type, [{filters, Filters} | Rest]) ->
    check_filters(Filters),
    check(Type, Rest);
check(Type, [{filter_default, Default} | Rest]) ->
    Default =:= log orelse Default =:= stop orelse throw({invalid_filter_default, Default}),
    check(Type, Rest);
check(handler, [{formatter, Formatter} | Rest]) ->
    check_formatter(Formatter),
    check(handler, Rest);
check(primary, [{metadata, Meta} | Rest]) when is_map(Meta) ->
    check(primary, Rest);
check(primary, [Item | _]) ->
    throw({invalid_primary_config, Item});
check(handler, [{_, _} | Rest]) ->
    check(handler, Rest);
check(_, []) ->
    true.

check_filters([{Id, {Fun, _}} | Rest]) when is_atom(Id), is_function(Fun, 2) -> check_filters(Rest);
check_filters([Filter | _]) -> throw({invalid_filter, Filter});
check_filters([]) -> ok;
check_filters(Filters) -> throw({invalid_filters, Filters}).

check_formatter({Module, Config}) ->
    check_module(Module),
    try formatter_check(Module, Config) of
        ok -> ok;
        {error, Error} -> throw(Error)
    catch
        throw:Thrown ->
            throw(Thrown);
        C:R:S ->
            case {C, R, S} of
                {error, undef, [{Module, check_config, [Config], _} | _]} -> ok;
                _ -> throw({callback_crashed, {C, R, filter_stacktrace(?MODULE, S)}})
            end
    end;
check_formatter(Formatter) ->
    throw({invalid_formatter, Formatter}).

%% Names the default formatter literally, so programs that configure logger link it.
formatter_check(logger_formatter, Config) -> logger_formatter:check_config(Config);
formatter_check(Module, Config) -> Module:check_config(Config).

check_id(Id) when is_atom(Id) -> ok;
check_id(Id) -> throw({invalid_id, Id}).

check_module(Module) when is_atom(Module) -> ok;
check_module(Module) -> throw({invalid_module, Module}).

%% Kernel setup: the logger environment of applications (empty until application lands).

reconfigure() ->
    Ids = [Id || Id <- get_handler_ids(), Id =/= default],
    [remove_handler(Id) || Id <- Ids],
    unset_module_level(),
    case internal_init_logger() of
        ok -> add_handlers(kernel);
        Error -> Error
    end.

internal_init_logger() ->
    ok = set_primary_config(level, env_level()),
    ok = set_primary_config(metadata, env_metadata()),
    ok = set_primary_config(filter_default, log),
    ok = set_primary_config(filters, []),
    ok.

env_level() ->
    case clause_logger:app_env(kernel, logger_level) of
        {ok, Level} -> Level;
        _ -> notice
    end.

env_metadata() ->
    case clause_logger:app_env(kernel, logger_metadata) of
        {ok, Meta} when is_map(Meta) -> Meta;
        _ -> #{}
    end.

add_handlers(kernel) ->
    add_handlers(kernel, logger_env(kernel));
add_handlers(App) when is_atom(App) ->
    add_handlers(App, logger_env(App));
add_handlers(Config) ->
    add_handlers(undefined, Config).

add_handlers(App, Config) ->
    try
        [check_entry(App, Entry) || Entry <- Config],
        [setup_handler(Id, Module, HConfig) || {handler, Id, Module, HConfig} <- Config],
        ok
    catch
        throw:Reason0 ->
            Reason =
                case App of
                    undefined -> Reason0;
                    _ -> {App, Reason0}
                end,
            ?MODULE:error("Invalid logger handler config: ~p", [Reason]),
            {error, {bad_config, {handler, Reason}}}
    end.

check_entry(_, {handler, _, _, _}) -> ok;
check_entry(kernel, {handler, default, undefined}) -> ok;
check_entry(kernel, {filters, _, _}) -> ok;
check_entry(kernel, {module_level, _, _}) -> ok;
check_entry(kernel, {proxy, _}) -> ok;
check_entry(_, Bad) -> throw(Bad).

setup_handler(Id, Module, Config) ->
    case add_handler(Id, Module, Config) of
        ok -> ok;
        {error, {already_exist, Id}} -> ok;
        {error, Reason} -> throw(Reason)
    end.

logger_env(App) ->
    case clause_logger:app_env(App, logger) of
        {ok, Config} when is_list(Config) -> Config;
        _ -> []
    end.

%% Printing the configuration.

i() ->
    #{primary := Primary, handlers := Handlers, proxy := Proxy, module_levels := Modules} = get_config(),
    M = modifier(),
    i_primary(Primary, M),
    i_handlers(Handlers, M),
    i_proxy(Proxy, M),
    i_modules(Modules, M).

i(primary) ->
    i_primary(get_primary_config(), modifier());
i(handlers) ->
    i_handlers(get_handler_config(), modifier());
i(proxy) ->
    i_proxy(get_proxy_config(), modifier());
i(modules) ->
    i_modules(get_module_level(), modifier());
i(Id) when is_atom(Id) ->
    case get_handler_config(Id) of
        {ok, Config} -> i_handlers([Config], modifier());
        Error -> Error
    end;
i(What) ->
    erlang:error(badarg, [What]).

i_primary(#{level := Level, filters := Filters, filter_default := Default}, M) ->
    io:format("Primary configuration: ~n", []),
    io:format("    Level: ~p~n", [Level]),
    io:format("    Filter Default: ~p~n", [Default]),
    io:format("    Filters: ~n", []),
    print_filters("        ", Filters, M).

i_handlers(Configs, M) ->
    io:format("Handler configuration: ~n", []),
    print_handlers(Configs, M).

i_proxy(Proxy, M) ->
    io:format("Proxy configuration: ~n", []),
    print_custom("    ", Proxy, M).

i_modules(Modules, M) ->
    io:format("Level set per module: ~n", []),
    print_module_levels(Modules, M).

modifier() ->
    case lists:keyfind(encoding, 1, io:getopts()) of
        {encoding, latin1} -> "";
        false -> "";
        _ -> "t"
    end.

print_filters(Indent, {Id, {Fun, Arg}}, M) ->
    io:format("~sId: ~" ++ M ++ "p~n~s    Fun: ~" ++ M ++ "p~n~s    Arg: ~" ++ M ++ "p~n", [
        Indent, Id, Indent, Fun, Indent, Arg
    ]);
print_filters(Indent, [], _) ->
    io:format("~s(none)~n", [Indent]);
print_filters(Indent, Filters, M) ->
    [print_filters(Indent, Filter, M) || Filter <- Filters],
    ok.

print_handlers(
    #{
        id := Id,
        module := Module,
        level := Level,
        filters := Filters,
        filter_default := Default,
        formatter := {FModule, FConfig}
    } = Config,
    M
) ->
    io:format(
        "    Id: ~" ++ M ++
            "p~n        Module: ~p~n        Level:  ~p~n        Formatter:~n"
            "            Module: ~p~n            Config:~n",
        [Id, Module, Level, FModule]
    ),
    print_custom("                ", FConfig, M),
    io:format("        Filter Default: ~p~n        Filters:~n", [Default]),
    print_filters("            ", Filters, M),
    case maps:find(config, Config) of
        {ok, HConfig} ->
            io:format("        Handler Config:~n"),
            print_custom("            ", HConfig, M);
        error ->
            ok
    end,
    case maps:without([filter_default, filters, formatter, level, module, id, config], Config) of
        Empty when Empty =:= #{} ->
            ok;
        Custom ->
            io:format("        Custom Config:~n"),
            print_custom("            ", Custom, M)
    end;
print_handlers([], _) ->
    io:format("    (none)~n");
print_handlers(Configs, M) ->
    [print_handlers(Config, M) || Config <- Configs],
    ok.

print_custom(Indent, {Key, Value}, M) ->
    io:format("~s~" ++ M ++ "p: ~" ++ M ++ "p~n", [Indent, Key, Value]);
print_custom(Indent, Map, M) when is_map(Map) ->
    print_custom(Indent, lists:keysort(1, maps:to_list(Map)), M);
print_custom(Indent, List, M) when is_list(List), is_tuple(hd(List)) ->
    [print_custom(Indent, X, M) || X <- List],
    ok;
print_custom(Indent, Value, M) ->
    io:format("~s~" ++ M ++ "p~n", [Indent, Value]).

print_module_levels({Module, Level}, M) ->
    io:format("    Module: ~" ++ M ++ "p~n        Level: ~p~n", [Module, Level]);
print_module_levels([], _) ->
    io:format("    (none)~n");
print_module_levels(Modules, M) ->
    [print_module_levels(Module, M) || Module <- Modules],
    ok.
