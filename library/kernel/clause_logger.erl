%% Clause's logger server and configuration store (plan 21 step LIB23, docs/library.md), what OTP's logger_server,
%% logger_config and the kernel's logger setup do. The server is the gen_server registered as `logger`, started on
%% first use; it owns the `logger` ETS table of the primary, proxy and handler configurations, keeps the primary,
%% handler and module levels in persistent terms for logger:allow/2, and runs the handlers' adding, changing and
%% removing callbacks. A handler whose process ends is removed.
-module(clause_logger).
-behaviour(gen_server).
-export([
    add_filter/2,
    add_handler/3,
    allow/2,
    app_env/2,
    config/1,
    default_handler_filters/0,
    handler_configs/0,
    handler_level_allows/2,
    level_to_int/1,
    module_levels/0,
    proxy_defaults/0,
    remove_filter/2,
    remove_handler/1,
    set_config/3,
    set_config/4,
    set_module_level/2,
    start/0,
    unset_module_level/1,
    update_formatter_config/2
]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2, terminate/2]).

-define(TABLE, logger).
-define(SERVER, logger).
-define(LEVELS, [none, emergency, alert, critical, error, warning, notice, info, debug, all]).

%% Created for LIB23: the server's state, the configuration table.
-record(state, {table}).

%% Starting: the first process that needs the configuration starts the server; a racing start waits for it.

start() ->
    case ets:whereis(?TABLE) of
        undefined ->
            case gen_server:start({local, ?SERVER}, ?MODULE, [], []) of
                {ok, _} -> ok;
                {error, {already_started, Pid}} -> wait_ready(Pid)
            end;
        _ ->
            ok
    end.

%% A call waits until the server's init/1 has made the table.
wait_ready(Pid) ->
    try gen_server:call(Pid, ready, infinity) of
        ok -> ok
    catch
        exit:_ -> start()
    end.

%% A configuration: primary, proxy or a handler id; {ok, Config} or {error, {not_found, What}}.
config(What) ->
    ok = start(),
    case ets:lookup(?TABLE, key(What)) of
        [{_, Config}] -> {ok, Config};
        [] -> {error, {not_found, What}}
    end.

handler_configs() ->
    ok = start(),
    [Config || {{handler, _}, Config} <- ets:tab2list(?TABLE)].

key(primary) -> primary;
key(proxy) -> proxy;
key(Id) -> {handler, Id}.

%% Levels.

level_to_int(none) -> -1;
level_to_int(emergency) -> 0;
level_to_int(alert) -> 1;
level_to_int(critical) -> 2;
level_to_int(error) -> 3;
level_to_int(warning) -> 4;
level_to_int(notice) -> 5;
level_to_int(info) -> 6;
level_to_int(debug) -> 7;
level_to_int(all) -> 10.

int_to_level(Int) -> hd([Level || Level <- ?LEVELS, level_to_int(Level) =:= Int]).

%% Whether a level passes the calling module's level, else the primary level (notice before the server runs).
allow(Level, Module) ->
    Limit =
        case persistent_term:get({?MODULE, module, Module}, undefined) of
            undefined -> persistent_term:get({?MODULE, primary}, level_to_int(notice));
            Int -> Int
        end,
    level_to_int(Level) =< Limit.

%% Whether a handler's level lets a level through; false for a handler that does not exist.
handler_level_allows(Id, Level) ->
    case persistent_term:get({?MODULE, handler, Id}, undefined) of
        undefined -> false;
        Limit -> level_to_int(Level) =< Limit
    end.

module_levels() ->
    lists:sort([{M, int_to_level(Int)} || {{?MODULE, module, M}, Int} <- persistent_term:get()]).

store_level(What, #{level := Level}) -> persistent_term:put(level_key(What), level_to_int(Level));
store_level(_, _) -> ok.

level_key(primary) -> {?MODULE, primary};
level_key(Id) -> {?MODULE, handler, Id}.

%% The kernel's (or another application's) environment, read through application when it runs; undefined before.
app_env(App, Key) ->
    try (application_module()):get_env(App, Key) of
        Result -> Result
    catch
        _:_ -> undefined
    end.

%% Created for LIB23: application until LIB24, reached by a runtime name so that programs without it still build.
application_module() -> application.

%% Requests to the server; the server's own callbacks may not change handlers or filters (they would wait on
%% themselves).

add_handler(Id, Module, Config) -> call({add_handler, Id, Module, Config}).
remove_handler(Id) -> call({remove_handler, Id}).
add_filter(Owner, Filter) -> call({add_filter, Owner, Filter}).
remove_filter(Owner, FilterId) -> call({remove_filter, Owner, FilterId}).
set_config(SetOrUpdate, Owner, Config) -> call({change_config, SetOrUpdate, Owner, Config}).
set_config(SetOrUpdate, Owner, Key, Value) -> call({change_config, SetOrUpdate, Owner, Key, Value}).
update_formatter_config(Id, Config) -> call({update_formatter_config, Id, Config}).
set_module_level(Modules, Level) -> call({set_module_level, Modules, Level}).
unset_module_level(Modules) -> call({unset_module_level, Modules}).

call(Request) ->
    ok = start(),
    case whereis(?SERVER) =:= self() of
        true -> {error, {attempting_syncronous_call_to_self, Request}};
        false -> gen_server:call(?SERVER, Request, infinity)
    end.

%% The server.

init([]) ->
    process_flag(trap_exit, true),
    Table = ets:new(?TABLE, [set, protected, named_table, {read_concurrency, true}]),
    ets:insert(Table, {proxy, proxy_defaults()}),
    Primary = #{
        level => kernel_level(),
        metadata => kernel_metadata(),
        filters => [],
        filter_default => log,
        handlers => []
    },
    put_config(Table, primary, Primary),
    add_default_handler(Table),
    {ok, #state{table = Table}}.

kernel_level() ->
    case app_env(kernel, logger_level) of
        {ok, Level} -> Level;
        _ -> notice
    end.

kernel_metadata() ->
    case app_env(kernel, logger_metadata) of
        {ok, Meta} when is_map(Meta) -> Meta;
        _ -> #{}
    end.

%% The default handler: logger_std_h with the legacy header, OTP's default filters, writing on standard error.
add_default_handler(Table) ->
    Config = (default_config(default, logger_std_h))#{
        filter_default => stop,
        filters => default_handler_filters(),
        formatter => {logger_formatter, #{legacy_header => true, single_line => false}},
        config => #{type => standard_error}
    },
    {ok, Added} = logger_std_h:adding_handler(Config),
    store_handler(Table, default, Added).

default_handler_filters() ->
    Domain =
        case app_env(kernel, logger_sasl_compatible) of
            {ok, true} -> [otp];
            _ -> [otp, sasl]
        end,
    [
        {remote_gl, {fun logger_filters:remote_gl/2, stop}},
        {domain, {fun logger_filters:domain/2, {log, super, Domain}}},
        {no_domain, {fun logger_filters:domain/2, {log, undefined, []}}}
    ].

proxy_defaults() ->
    #{
        sync_mode_qlen => 500,
        drop_mode_qlen => 1000,
        flush_qlen => 5000,
        burst_limit_enable => false,
        burst_limit_max_count => 500,
        burst_limit_window_time => 1000,
        overload_kill_enable => false,
        overload_kill_qlen => 20000,
        overload_kill_mem_size => 3000000,
        overload_kill_restart_after => 5000
    }.

default_config(primary) ->
    #{level => notice, metadata => #{}, filters => [], filter_default => log};
default_config(Id) ->
    #{id => Id, level => all, filters => [], filter_default => log, formatter => {logger_formatter, #{}}}.

default_config(Id, Module) -> (default_config(Id))#{module => Module}.

put_config(Table, What, Config) ->
    store_level(What, Config),
    ets:insert(Table, {key(What), Config}),
    ok.

store_handler(Table, Id, Config) ->
    put_config(Table, Id, Config),
    {ok, Primary} = lookup(Table, primary),
    put_config(Table, primary, Primary#{handlers => [Id | maps:get(handlers, Primary) -- [Id]]}).

lookup(Table, What) ->
    case ets:lookup(Table, key(What)) of
        [{_, Config}] -> {ok, Config};
        [] -> {error, {not_found, What}}
    end.

handle_call(ready, _, State) ->
    {reply, ok, State};
handle_call(Request, _, #state{table = Table} = State) ->
    {reply, request(Request, Table), State}.

handle_cast(_, State) -> {noreply, State}.

%% A handler process that ends takes its handler along.
handle_info({'EXIT', Pid, _}, #state{table = Table} = State) ->
    [
        drop_handler(Table, Id)
     || #{id := Id, config := #{olp := Olp}} <- [C || {{handler, _}, C} <- ets:tab2list(Table)], Olp =:= Pid
    ],
    {noreply, State};
handle_info(_, State) ->
    {noreply, State}.

terminate(_, _) -> ok.

request({add_handler, Id, Module, Config0}, Table) ->
    case lookup(Table, Id) of
        {ok, _} ->
            {error, {already_exist, Id}};
        {error, _} ->
            Config = maps:merge(default_config(Id, Module), Config0),
            case callback(Module, adding_handler, [Config], {ok, Config}) of
                {ok, Added} ->
                    case erlang:function_exported(Module, log, 2) of
                        true -> store_handler(Table, Id, Added);
                        false -> {error, {invalid_handler, {function_not_exported, {Module, log, 2}}}}
                    end;
                {error, Reason} ->
                    {error, {handler_not_added, Reason}}
            end
    end;
request({remove_handler, Id}, Table) ->
    case lookup(Table, Id) of
        {ok, #{module := Module} = Config} ->
            _ = callback(Module, removing_handler, [Config], ok),
            drop_handler(Table, Id);
        _ ->
            {error, {not_found, Id}}
    end;
request({add_filter, Owner, {FilterId, _} = Filter}, Table) ->
    case lookup(Table, Owner) of
        {ok, Config} ->
            Filters = maps:get(filters, Config, []),
            case lists:keymember(FilterId, 1, Filters) of
                true -> {error, {already_exist, FilterId}};
                false -> put_config(Table, Owner, Config#{filters => [Filter | Filters]})
            end;
        Error ->
            Error
    end;
request({remove_filter, Owner, FilterId}, Table) ->
    case lookup(Table, Owner) of
        {ok, Config} ->
            case lists:keytake(FilterId, 1, maps:get(filters, Config, [])) of
                {value, _, Filters} -> put_config(Table, Owner, Config#{filters => Filters});
                false -> {error, {not_found, FilterId}}
            end;
        Error ->
            Error
    end;
request({change_config, SetOrUpdate, proxy, Config0}, Table) ->
    Base =
        case SetOrUpdate of
            set -> proxy_defaults();
            update -> element(2, lookup(Table, proxy))
        end,
    Config = maps:merge(Base, Config0),
    case check_olp(Config) of
        ok -> put_config(Table, proxy, Config);
        Error -> Error
    end;
request({change_config, SetOrUpdate, primary, Config0}, Table) ->
    {ok, #{handlers := Handlers} = Old} = lookup(Table, primary),
    Base =
        case SetOrUpdate of
            set -> default_config(primary);
            update -> Old
        end,
    put_config(Table, primary, (maps:merge(Base, Config0))#{handlers => Handlers});
request({change_config, _, primary, Key, Value}, Table) ->
    {ok, Old} = lookup(Table, primary),
    put_config(Table, primary, Old#{Key => Value});
request({change_config, SetOrUpdate, Id, Config0}, Table) ->
    case lookup(Table, Id) of
        {ok, #{module := Module} = Old} ->
            Base =
                case SetOrUpdate of
                    set -> default_config(Id, Module);
                    update -> Old
                end,
            change_handler(Table, Id, SetOrUpdate, Old, maps:merge(Base, Config0));
        _ ->
            {error, {not_found, Id}}
    end;
request({change_config, SetOrUpdate, Id, Key, Value}, Table) ->
    case lookup(Table, Id) of
        {ok, Old} -> change_handler(Table, Id, SetOrUpdate, Old, Old#{Key => Value});
        _ -> {error, {not_found, Id}}
    end;
request({update_formatter_config, Id, New}, Table) ->
    case lookup(Table, Id) of
        {ok, #{formatter := {Module, Old}} = Config} ->
            Merged = maps:merge(Old, New),
            case check_formatter(Module, Merged) of
                ok -> put_config(Table, Id, Config#{formatter => {Module, Merged}});
                {error, Reason} -> {error, Reason}
            end;
        _ ->
            {error, {not_found, Id}}
    end;
request({set_module_level, Modules, Level}, _) ->
    [persistent_term:put({?MODULE, module, M}, level_to_int(Level)) || M <- Modules],
    ok;
request({unset_module_level, all}, _) ->
    [persistent_term:erase(Key) || {{?MODULE, module, _} = Key, _} <- persistent_term:get()],
    ok;
request({unset_module_level, Modules}, _) ->
    [persistent_term:erase({?MODULE, module, M}) || M <- Modules],
    ok.

%% A handler's id, level and module cannot change; the handler's changing_config/3 checks the rest.
change_handler(Table, Id, SetOrUpdate, Old, New) ->
    Fixed = maps:with([id, module], Old),
    case maps:with([id, module], New) of
        Fixed ->
            case changing_config(maps:get(module, New), SetOrUpdate, Old, New) of
                {ok, Changed} -> put_config(Table, Id, Changed);
                Error -> Error
            end;
        Changed ->
            {error,
                {illegal_config_change, maps:filter(fun(K, V) -> maps:get(K, Changed) =/= V end, Fixed),
                    maps:filter(fun(K, V) -> maps:get(K, Fixed) =/= V end, Changed)}}
    end.

%% changing_config/3, or the older changing_config/2 of a handler written before it.
changing_config(Module, SetOrUpdate, Old, New) ->
    case erlang:function_exported(Module, changing_config, 3) of
        true -> callback(Module, changing_config, [SetOrUpdate, Old, New], {ok, New});
        false -> callback(Module, changing_config, [Old, New], {ok, New})
    end.

drop_handler(Table, Id) ->
    {ok, Primary} = lookup(Table, primary),
    put_config(Table, primary, Primary#{handlers => lists:delete(Id, maps:get(handlers, Primary))}),
    persistent_term:erase(level_key(Id)),
    ets:delete(Table, key(Id)),
    ok.

%% A handler callback, or Default when the module does not export it; a failing callback is an error.
callback(Module, Function, Args, Default) ->
    case erlang:function_exported(Module, Function, length(Args)) of
        true ->
            try
                apply(Module, Function, Args)
            catch
                C:R:S -> {error, {callback_crashed, {C, R, logger:filter_stacktrace(?MODULE, S)}}}
            end;
        false ->
            Default
    end.

check_formatter(Module, Config) ->
    try Module:check_config(Config) of
        ok -> ok;
        {error, Error} -> {error, Error}
    catch
        error:undef:Stack ->
            case Stack of
                [{Module, check_config, [Config], _} | _] -> ok;
                _ -> {error, {callback_crashed, {error, undef, Stack}}}
            end;
        C:R:S ->
            {error, {callback_crashed, {C, R, S}}}
    end.

%% Overload protection options as logger_olp checks them: known keys and values, and ordered queue lengths.
check_olp(Options) ->
    case [{K, V} || K := V <- Options, not olp_valid(K, V)] of
        [{K, V} | _] ->
            {error, {invalid_olp_config, #{K => V}}};
        [] ->
            Sync = maps:get(sync_mode_qlen, Options, 10),
            Drop = maps:get(drop_mode_qlen, Options, 200),
            Flush = maps:get(flush_qlen, Options, 1000),
            case Drop > 1 andalso Sync =< Drop andalso Drop =< Flush of
                true -> ok;
                false -> {error, {invalid_olp_levels, maps:with([sync_mode_qlen, drop_mode_qlen, flush_qlen], Options)}}
            end
    end.

olp_valid(burst_limit_enable, V) ->
    is_boolean(V);
olp_valid(overload_kill_enable, V) ->
    is_boolean(V);
olp_valid(overload_kill_restart_after, V) ->
    is_integer(V) orelse V =:= infinity;
olp_valid(K, V) ->
    lists:member(K, [
        sync_mode_qlen,
        drop_mode_qlen,
        flush_qlen,
        burst_limit_max_count,
        burst_limit_window_time,
        overload_kill_qlen,
        overload_kill_mem_size
    ]) andalso is_integer(V).
