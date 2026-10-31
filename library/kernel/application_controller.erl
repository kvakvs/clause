%% Clause's application_controller (plan 21 step LIB24, docs/libraries/library.md): the registered server behind application,
%% started at its first use. Loaded applications, their environments and masters live in the public ETS table
%% ac_tab as in OTP; specifications come from clause_applications, which clau generates from the program's .app and
%% .app.src files. kernel and stdlib are loaded and running from the start, without masters. A permanent application
%% that ends halts the program as OTP's kernel does.
-module(application_controller).
-export([
    change_application_data/2,
    code_change/3,
    config_change/1,
    control_application/1,
    do_config_diff/2,
    format_log/1,
    format_log/2,
    get_all_env/1,
    get_all_key/1,
    get_application/1,
    get_application_module/1,
    get_env/2,
    get_env/3,
    get_key/2,
    get_loaded/1,
    get_master/1,
    get_pid_all_env/1,
    get_pid_all_key/1,
    get_pid_env/2,
    get_pid_key/2,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    info/0,
    is_running/1,
    load_application/1,
    loaded_applications/0,
    permit_application/2,
    prep_config_change/0,
    set_env/2,
    set_env/3,
    set_env/4,
    start/1,
    start_application/2,
    start_application_request/2,
    start_boot_application/2,
    start_type/1,
    stop_application/1,
    stop_application_request/1,
    terminate/2,
    test_change_apps/2,
    unload_application/1,
    unset_env/2,
    unset_env/3,
    which_applications/0,
    which_applications/1
]).

-include_lib("kernel/include/logger.hrl").

-define(AC, application_controller).
-define(TABLE, ac_tab).

%% Created for LIB24: a loaded application, as its specification gives it.
-record(appl, {
    name,
    descr = "",
    id = "",
    vsn = "",
    mods = [],
    regs = [],
    apps = [],
    inc_apps = [],
    opt_apps = [],
    mod = [],
    phases = undefined,
    max_p = infinity,
    max_t = infinity
}).

%% Created for LIB24: the controller's state. Callers waiting for starts and stops, applications being started
%% ({Name, RestartType, Type, From}), running ones ({Name, Master}), started ones with their restart types, starts
%% held back by permissions and the persistent environment of set_env/2,4.
-record(state, {
    starting = [], start_req = [], running = [], started = [], stop_req = [], start_p_false = [], conf = []
}).

%% The versions and keys of kernel and stdlib in OTP 29.1.1.
-define(OTP_DESCRIPTION, "ERTS  CXC 138 10").
-define(KERNEL_VSN, "11.0.4").
-define(STDLIB_VSN, "8.1").

%% Starting the server.

%% Booting the kernel application: Clause starts the controller at its first use instead.
start(_) -> erlang:error({not_implemented, start, 1}).

ensure_started() ->
    case whereis(?AC) of
        undefined -> start_controller();
        _ -> ok
    end.

%% A concurrent first use may start another controller: the one that creates ac_tab wins.
start_controller() ->
    Caller = self(),
    Pid = proc_lib:spawn(fun() -> init(Caller) end),
    Ref = monitor(process, Pid),
    receive
        {Pid, ok} ->
            demonitor(Ref, [flush]),
            ok;
        {'DOWN', Ref, process, Pid, _} ->
            wait_registered()
    end.

wait_registered() ->
    case whereis(?AC) of
        undefined ->
            receive
            after 1 -> wait_registered()
            end;
        _ ->
            ok
    end.

init(Caller) ->
    ?TABLE = ets:new(?TABLE, [set, public, named_table, {read_concurrency, true}]),
    process_flag(trap_exit, true),
    lists:foreach(fun insert_loaded/1, [kernel_appl(), stdlib_appl()]),
    add_env(kernel, kernel_env()),
    true = register(?AC, self()),
    % Once registered: the log line of a bad --env value starts the logger, which reads kernel's environment.
    add_boot_envs(),
    Caller ! {self(), ok},
    State = #state{
        running = [{stdlib, undefined}, {kernel, undefined}], started = [{stdlib, permanent}, {kernel, permanent}]
    },
    gen_server:enter_loop(?MODULE, [], State, {local, ?AC}).

kernel_appl() ->
    #appl{
        name = kernel,
        descr = ?OTP_DESCRIPTION,
        vsn = ?KERNEL_VSN,
        mods = library_modules(kernel),
        regs = [application_controller, error_logger, logger, timer_server],
        mod = {kernel, []}
    }.

stdlib_appl() ->
    #appl{name = stdlib, descr = ?OTP_DESCRIPTION, vsn = ?STDLIB_VSN, mods = library_modules(stdlib), apps = [kernel]}.

%% The --env values of kernel and stdlib over their environments, merged as OTP loads them; a value that is no term
%% stops the program as it stops OTP's boot. Added for --env (plan 30 step DIST4a1).
add_boot_envs() ->
    try
        add_env(kernel, merge_app_env(merge_app_env(kernel_env(), []), get_cmd_env(kernel))),
        add_env(stdlib, merge_app_env([], get_cmd_env(stdlib)))
    catch
        throw:{error, Reason} ->
            Text = lists:flatten(io_lib:format("~p", [Reason])),
            erlang:halt(lists:flatten(io_lib:format("Could not start kernel pid (~p) (~p)", [?AC, Text])))
    end.

%% kernel's environment as its kernel.app sets it.
kernel_env() ->
    [
        {logger_level, notice},
        {logger_sasl_compatible, false},
        {net_tickintensity, 4},
        {net_ticktime, 60},
        {prevent_overlapping_partitions, true},
        {shell_docs_ansi, auto},
        {connect_all, true},
        {shell_history_drop, []}
    ].

%% The generated module of the program's specifications, named at run time: clau adds it with this module.
resources_module() -> clause_applications.

library_modules(Application) -> (resources_module()):library_modules(Application).

insert_loaded(#appl{name = Name} = Appl) -> ets:insert(?TABLE, {{loaded, Name}, Appl}).

call(Request) -> call(Request, 5000).

call(Request, Timeout) ->
    ensure_started(),
    case gen_server:call(?AC, Request, Timeout) of
        {error, terminating} -> exit(terminating);
        Reply -> Reply
    end.

%% Requests.

load_application(Application) -> call({load_application, Application}, infinity).

unload_application(Name) -> call({unload_application, Name}, infinity).

start_application(Name, RestartType) -> call({start_application, Name, RestartType}, infinity).

start_application_request(Name, RestartType) ->
    ensure_started(),
    gen_server:send_request(?AC, {start_application, Name, RestartType}).

%% A boot start: loads the application first; a permanent one with a bad environment ends the caller.
start_boot_application(Application, RestartType) ->
    case {application:load(Application), RestartType} of
        {ok, _} ->
            call({start_application, name(Application), RestartType}, infinity);
        {{error, {already_loaded, Name}}, _} ->
            call({start_application, Name, RestartType}, infinity);
        {{error, {bad_environment_value, Env}}, permanent} ->
            Text = io_lib:format("Bad environment variable: ~tp  Application: ~p", [Env, Application]),
            exit({error, list_to_atom(lists:flatten(Text))});
        {Error, _} ->
            Error
    end.

stop_application(Name) -> call({stop_application, Name}, infinity).

stop_application_request(Name) ->
    ensure_started(),
    gen_server:send_request(?AC, {stop_application, Name}).

is_running(Name) -> call({is_running, Name}, infinity).

permit_application(Application, Bool) -> call({permit_application, Application, Bool}, infinity).

which_applications() -> call(which_applications).

which_applications(Timeout) -> call(which_applications, Timeout).

info() -> call(info).

start_type(Master) ->
    case application_of(Master) of
        {ok, Name} -> call({start_type, Name}, infinity);
        undefined -> undefined
    end.

set_env(Config, Options) ->
    case check_conf_data(Config) of
        ok -> call({set_env, Config, Options}, proplists:get_value(timeout, Options, 5000));
        Error -> Error
    end.

set_env(Name, Key, Value) -> call({set_env, Name, Key, Value, []}).

set_env(Name, Key, Value, Options) ->
    call({set_env, Name, Key, Value, Options}, proplists:get_value(timeout, Options, 5000)).

unset_env(Name, Key) -> call({unset_env, Name, Key, []}).

unset_env(Name, Key, Options) -> call({unset_env, Name, Key, Options}, proplists:get_value(timeout, Options, 5000)).

%% Release handling and distributed control are not available.
change_application_data(_, _) -> erlang:error({not_implemented, change_application_data, 2}).
config_change(_) -> erlang:error({not_implemented, config_change, 1}).
control_application(_) -> erlang:error({not_implemented, control_application, 1}).
do_config_diff(_, _) -> erlang:error({not_implemented, do_config_diff, 2}).
prep_config_change() -> erlang:error({not_implemented, prep_config_change, 0}).
test_change_apps(_, _) -> erlang:error({not_implemented, test_change_apps, 2}).

%% Reads straight from ac_tab.

loaded_applications() ->
    ensure_started(),
    [{Name, Descr, Vsn} || {{loaded, Name}, #appl{descr = Descr, vsn = Vsn}} <- ets:tab2list(?TABLE)].

get_loaded(Application) ->
    ensure_started(),
    case ets:lookup(?TABLE, {loaded, name(Application)}) of
        [{_, Appl}] -> {true, Appl};
        [] -> false
    end.

get_env(Name, Key) ->
    ensure_started(),
    case ets:lookup(?TABLE, {env, Name, Key}) of
        [{_, Value}] -> {ok, Value};
        [] -> undefined
    end.

get_env(Name, Key, Default) ->
    ensure_started(),
    ets:lookup_element(?TABLE, {env, Name, Key}, 2, Default).

get_all_env(Name) ->
    ensure_started(),
    [{Key, Value} || [Key, Value] <- ets:match(?TABLE, {{env, Name, '$1'}, '$2'})].

get_key(Name, Key) ->
    case get_loaded(Name) of
        {true, Appl} -> key(Key, Appl);
        false -> undefined
    end.

key(Key, #appl{} = Appl) ->
    case lists:keyfind(Key, 1, keys(Appl)) of
        {Key, Value} -> {ok, Value};
        false -> undefined
    end.

get_all_key(Name) ->
    case get_loaded(Name) of
        {true, Appl} -> {ok, keys(Appl)};
        false -> undefined
    end.

keys(#appl{name = Name} = A) ->
    [
        {description, A#appl.descr},
        {id, A#appl.id},
        {vsn, A#appl.vsn},
        {modules, A#appl.mods},
        {maxP, A#appl.max_p},
        {maxT, A#appl.max_t},
        {registered, A#appl.regs},
        {included_applications, A#appl.inc_apps},
        {optional_applications, A#appl.opt_apps},
        {applications, A#appl.apps},
        {env, get_all_env(Name)},
        {mod, A#appl.mod},
        {start_phases, A#appl.phases}
    ].

%% The application whose master is `Master`.
application_of(Master) ->
    ensure_started(),
    case ets:match(?TABLE, {{application_master, '$1'}, Master}) of
        [[Name]] -> {ok, Name};
        _ -> undefined
    end.

get_application(Master) -> application_of(Master).

get_pid_env(Master, Key) ->
    case application_of(Master) of
        {ok, Name} -> get_env(Name, Key);
        undefined -> undefined
    end.

get_pid_all_env(Master) ->
    case application_of(Master) of
        {ok, Name} -> get_all_env(Name);
        undefined -> []
    end.

get_pid_key(Master, Key) ->
    case application_of(Master) of
        {ok, Name} -> get_key(Name, Key);
        undefined -> undefined
    end.

get_pid_all_key(Master) ->
    case application_of(Master) of
        {ok, Name} -> get_all_key(Name);
        undefined -> []
    end.

get_master(Name) ->
    ensure_started(),
    ets:lookup_element(?TABLE, {application_master, Name}, 2, undefined).

get_application_module(Module) ->
    ensure_started(),
    Owners = [Name || {{loaded, Name}, #appl{mods = Modules}} <- ets:tab2list(?TABLE), lists:member(Module, Modules)],
    case Owners of
        [Name | _] -> {ok, Name};
        [] -> undefined
    end.

%% Configuration checks of set_env/2, with OTP's messages.

check_conf_data([]) ->
    ok;
check_conf_data([{Name, Parameters} | Rest]) when is_atom(Name), is_list(Parameters) ->
    case lists:keymember(Name, 1, Rest) of
        true ->
            {error, "duplicate application config: " ++ atom_to_list(Name)};
        false ->
            case check_para(Parameters, Name) of
                ok -> check_conf_data(Rest);
                Error -> Error
            end
    end;
check_conf_data([{Name, Parameters} | _]) when is_list(Parameters) ->
    {error, "application: " ++ text(Name) ++ "; application name must be an atom"};
check_conf_data([{Name, _} | _]) ->
    {error, "application: " ++ text(Name) ++ "; parameters must be a list"};
check_conf_data([Other | _]) ->
    {error, "invalid application config: " ++ text(Other)};
check_conf_data(_) ->
    {error, "configuration must be a list ended by <dot><whitespace>"}.

check_para([], _) ->
    ok;
check_para([{Key, _} | Rest], Name) when is_atom(Key) ->
    case lists:keymember(Key, 1, Rest) of
        true -> {error, "application: " ++ atom_to_list(Name) ++ "; duplicate parameter: " ++ atom_to_list(Key)};
        false -> check_para(Rest, Name)
    end;
check_para([{Key, _} | _], Name) ->
    {error, "application: " ++ atom_to_list(Name) ++ "; invalid parameter name: " ++ text(Key)};
check_para([Other | _], Name) ->
    {error, "application: " ++ atom_to_list(Name) ++ "; invalid parameter: " ++ text(Other)}.

text(Term) -> lists:flatten(io_lib:format("~tp", [Term])).

%% Specifications.

name({application, Name, _}) when is_atom(Name) -> Name;
name(Name) when is_atom(Name) -> Name;
name(Other) -> throw({error, {bad_application, Other}}).

%% A specification by name comes from the program's resources, with the modules beside the file by default.
make_appl(Name) when is_atom(Name) ->
    Found = [{Spec, Modules} || {{application, N, _} = Spec, Modules} <- (resources_module()):specs(), N =:= Name],
    case Found of
        [{Spec, Modules} | _] -> make_appl_i(Spec, Modules);
        [] -> {error, {"no such file or directory", atom_to_list(Name) ++ ".app"}}
    end;
make_appl(Spec) ->
    make_appl_i(Spec, []).

make_appl_i({application, Name, Keys}, Beside) when is_atom(Name), is_list(Keys) ->
    Env = get_opt(env, Keys, []),
    case check_para(Env, Name) of
        ok -> ok;
        {error, Reason} -> throw({error, {invalid_options, Reason}})
    end,
    Appl = #appl{
        name = Name,
        descr = get_opt(description, Keys, ""),
        id = get_opt(id, Keys, ""),
        vsn = get_opt(vsn, Keys, ""),
        mods = default(get_opt(modules, Keys, []), Beside),
        regs = get_opt(registered, Keys, []),
        apps = get_opt(applications, Keys, []),
        inc_apps = get_opt(included_applications, Keys, []),
        opt_apps = get_opt(optional_applications, Keys, []),
        mod = start_spec(get_opt(mod, Keys, [])),
        phases = get_opt(start_phases, Keys, undefined),
        max_p = get_opt(maxP, Keys, infinity),
        max_t = get_opt(maxT, Keys, infinity)
    },
    {ok, Appl, Env};
make_appl_i({application, Name, Keys}, _) when is_list(Keys) ->
    throw({error, {invalid_name, Name}});
make_appl_i({application, _, Keys}, _) ->
    throw({error, {invalid_options, Keys}});
make_appl_i(Other, _) ->
    throw({error, {bad_application, Other}}).

default([], Beside) -> Beside;
default(Modules, _) -> Modules.

start_spec({Module, _} = Spec) when is_atom(Module) -> Spec;
start_spec([]) -> [];
start_spec(Other) -> throw({error, {badstartspec, Other}}).

get_opt(Key, Keys, Default) ->
    case lists:keyfind(Key, 1, Keys) of
        {Key, Value} -> Value;
        false -> Default
    end.

%% gen_server callbacks.

handle_call({load_application, Application}, _, S) ->
    Reply =
        try
            load(Application, S)
        catch
            throw:Error -> Error
        end,
    {reply, Reply, S};
handle_call({unload_application, Name}, _, S) ->
    {reply, unload_checked(Name, S), S};
handle_call({start_application, Name, RestartType}, From, S) ->
    case lists:keymember(Name, 1, S#state.start_req) of
        true -> {noreply, S#state{start_req = [{Name, From} | S#state.start_req]}};
        false -> start_checked(Name, RestartType, From, S)
    end;
handle_call({is_running, Name}, _, S) ->
    {reply, lists:keymember(Name, 1, S#state.running), S};
handle_call({permit_application, Application, Bool}, From, S) ->
    permit(name(Application), Bool, From, S);
handle_call({stop_application, Name}, From, S) ->
    stop(Name, From, S);
handle_call(which_applications, _, S) ->
    Reply = [
        {Name, Appl#appl.descr, Appl#appl.vsn}
     || {Name, _} <- S#state.running, {true, Appl} <- [get_loaded(Name)]
    ],
    {reply, Reply, S};
handle_call({set_env, Config, Options}, _, S) ->
    _ = [add_env(Name, Env) || {Name, Env} <- Config],
    case proplists:get_value(persistent, Options, false) of
        true -> {reply, ok, S#state{conf = merge_conf(S#state.conf, Config)}};
        false -> {reply, ok, S}
    end;
handle_call({set_env, Name, Key, Value, Options}, _, S) ->
    ets:insert(?TABLE, {{env, Name, Key}, Value}),
    Change = fun(Env) -> lists:keystore(Key, 1, Env, {Key, Value}) end,
    {reply, ok, persistent(Options, Name, Change, S)};
handle_call({unset_env, Name, Key, Options}, _, S) ->
    ets:delete(?TABLE, {env, Name, Key}),
    {reply, ok, persistent(Options, Name, fun(Env) -> lists:keydelete(Key, 1, Env) end, S)};
handle_call({start_type, Name}, _, S) ->
    Type =
        case lists:keyfind(Name, 1, S#state.starting) of
            {_, _, StartType, _} -> StartType;
            false -> local
        end,
    {reply, Type, S};
handle_call(info, _, S) ->
    Reply = [
        {loaded, loaded_applications()},
        {loading, []},
        {started, S#state.started},
        {start_p_false, S#state.start_p_false},
        {running, S#state.running},
        {starting, S#state.starting}
    ],
    {reply, Reply, S}.

handle_cast({application_started, Name, Result}, S) ->
    started(Name, Result, S);
handle_cast({application_stopped, Name}, S) ->
    Stops = reply_all(Name, S#state.stop_req, ok),
    {noreply, S#state{
        running = lists:keydelete(Name, 1, S#state.running),
        started = lists:keydelete(Name, 1, S#state.started),
        stop_req = Stops
    }}.

%% A master that ends while its application runs: the application exited.
handle_info({'EXIT', Pid, Reason}, S) ->
    ets:match_delete(?TABLE, {{application_master, '_'}, Pid}),
    case lists:keyfind(Pid, 2, S#state.running) of
        {Name, _} -> master_exited(Name, Reason, S#state{running = lists:keydelete(Pid, 2, S#state.running)}, S);
        false -> {noreply, S}
    end;
handle_info(_, S) ->
    {noreply, S}.

master_exited(Name, Reason, NewS, S) ->
    case {lists:keymember(Name, 1, S#state.stop_req), lists:keyfind(Name, 1, S#state.started)} of
        {true, _} ->
            {noreply, S};
        {false, {_, temporary}} ->
            info_exited(Name, Reason, temporary),
            {noreply, NewS};
        {false, {_, transient}} when Reason =:= normal ->
            info_exited(Name, Reason, transient),
            {noreply, NewS};
        {false, {_, Type}} ->
            info_exited(Name, Reason, Type),
            halt_node({application_terminated, Name, Reason}, NewS)
    end.

terminate(_, S) ->
    stop_running(S),
    ok.

code_change(_, State, _) -> {ok, State}.

%% Loading and unloading.

load(Application, S) ->
    case get_loaded(name(Application)) of
        {true, _} ->
            {error, {already_loaded, name(Application)}};
        false ->
            case make_appl(Application) of
                {ok, Appl, Env} -> load_appl(Appl, Env, S);
                Error -> Error
            end
    end.

%% Stores the application with its environment, the persistent one winning, then loads its included applications.
load_appl(#appl{name = Name, inc_apps = Included} = Appl, Env, S) ->
    add_env(Name, merge_app_env(merge_app_env(Env, proplists:get_value(Name, S#state.conf, [])), get_cmd_env(Name))),
    insert_loaded(Appl),
    lists:foreach(
        fun(Inner) ->
            case get_loaded(Inner) of
                {true, _} ->
                    ok;
                false ->
                    case load(Inner, S) of
                        ok -> ok;
                        Error -> throw(Error)
                    end
            end
        end,
        Included
    ).

unload_checked(Name, S) ->
    case {lists:keymember(Name, 1, S#state.running), get_loaded(Name)} of
        {true, _} -> {error, {running, Name}};
        {false, {true, _}} -> unload(Name);
        {false, false} -> {error, {not_loaded, Name}}
    end.

unload(Name) ->
    {ok, Included} = get_key(Name, included_applications),
    ets:match_delete(?TABLE, {{env, Name, '_'}, '_'}),
    ets:delete(?TABLE, {loaded, Name}),
    lists:foreach(
        fun(Inner) ->
            case get_loaded(Inner) of
                {true, _} -> unload(Inner);
                false -> ok
            end
        end,
        Included
    ).

add_env(Name, Env) -> lists:foreach(fun({Key, Value}) -> ets:insert(?TABLE, {{env, Name, Key}, Value}) end, Env).

merge_env(Env, Over) -> lists:foldl(fun({Key, _} = Pair, Acc) -> lists:keystore(Key, 1, Acc, Pair) end, Env, Over).

%% The --env values of an application (OTP's -App Key Value), from init's flags as OTP reads them; a value that is no
%% term throws {error, {bad_environment_value, Text}} after OTP's log line. Added for --env (plan 30 step DIST4a1).
get_cmd_env(Name) ->
    case init:get_argument(Name) of
        {ok, Args} -> lists:foldl(fun(List, Res) -> conv(List) ++ Res end, [], Args);
        _ -> []
    end.

conv([Key, Val | T]) -> [{make_term(Key), make_term(Val)} | conv(T)];
conv(_) -> [].

make_term(Str) ->
    case erl_scan:string(Str) of
        {ok, Tokens, _} ->
            case erl_parse:parse_term(Tokens ++ [{dot, erl_anno:new(1)}]) of
                {ok, Term} -> Term;
                {error, {_, M, Reason}} -> make_term_error(M, Reason, Str)
            end;
        {error, {_, M, Reason}, _} ->
            make_term_error(M, Reason, Str)
    end.

make_term_error(Mod, Reason, Str) ->
    ?LOG_ERROR("application_controller: ~ts: ~ts~n", [Mod:format_error(Reason), Str], #{error_logger => #{tag => error}}),
    throw({error, {bad_environment_value, Str}}).

%% OTP's merge of a configured or --env environment over an application's: the first value of a key the application
%% has wins, the last of a new key, and the result is reversed, which decides between --env duplicates as in OTP.
merge_app_env(Env1, Env2) -> merge_app_env(Env1, Env2, []).

merge_app_env([{Key, Val} | T], Env2, Res) ->
    case get_env_key(Key, Env2) of
        {value, NewVal, RestEnv} -> merge_app_env(T, RestEnv, [{Key, NewVal} | Res]);
        _ -> merge_app_env(T, Env2, [{Key, Val} | Res])
    end;
merge_app_env([], Env2, Res) ->
    Env2 ++ Res.

%% The first value of Key and the other entries in OTP's order, which decides between later duplicates.
get_env_key(Key, Env) -> get_env_key(Env, Key, []).

get_env_key([{Key, Val} | T], Key, Res) -> {value, Val, T ++ Res};
get_env_key([H | T], Key, Res) -> get_env_key(T, Key, [H | Res]);
get_env_key([], _Key, Res) -> Res.

merge_conf(Conf, Config) ->
    lists:foldl(
        fun({Name, Env}, Acc) ->
            lists:keystore(Name, 1, Acc, {Name, merge_env(proplists:get_value(Name, Acc, []), Env)})
        end,
        Conf,
        Config
    ).

persistent(Options, Name, Change, S) ->
    case proplists:get_value(persistent, Options, false) of
        true ->
            Conf = S#state.conf,
            S#state{conf = lists:keystore(Name, 1, Conf, {Name, Change(proplists:get_value(Name, Conf, []))})};
        false ->
            S
    end.

%% Starting.

start_checked(Name, RestartType, From, S) ->
    try check_start(Name, RestartType, S) of
        {ok, Appl} ->
            Waiting = S#state{
                starting = [{Name, RestartType, normal, From} | S#state.starting],
                start_req = [{Name, From} | S#state.start_req]
            },
            case permitted(Name) of
                true ->
                    spawn_starter(Appl, S),
                    {noreply, Waiting};
                false ->
                    {reply, ok, S#state{start_p_false = [{Name, RestartType, normal, From} | S#state.start_p_false]}}
            end;
        Error ->
            {reply, Error, S}
    catch
        throw:Error -> {reply, Error, S}
    end.

check_start(Name, RestartType, S) ->
    valid_restart_type(RestartType),
    case get_loaded(Name) of
        {true, Appl} ->
            case lists:keymember(Name, 1, S#state.running) of
                true -> {error, {already_started, Name}};
                false -> missing_dependency(Appl, S#state.started, not_started)
            end;
        false ->
            {error, {not_loaded, Name}}
    end.

missing_dependency(#appl{apps = Needed, opt_apps = Optional} = Appl, Known, Tag) ->
    case lists:search(fun(Other) -> not lists:keymember(Other, 1, Known) end, Needed -- Optional) of
        {value, Missing} -> throw({error, {Tag, Missing}});
        false -> {ok, Appl}
    end.

valid_restart_type(Type) when Type =:= permanent; Type =:= temporary; Type =:= transient -> ok;
valid_restart_type(Type) -> throw({error, {invalid_restart_type, Type}}).

%% kernel's permissions environment: [{Name, false}] holds a start back.
permitted(Name) ->
    case get_env(kernel, permissions) of
        {ok, Permissions} -> not lists:member({Name, false}, Permissions);
        undefined -> true
    end.

%% A library application starts at once; another gets a master, which reports back.
spawn_starter(#appl{name = Name, mod = []}, _) ->
    gen_server:cast(?AC, {application_started, Name, {ok, undefined}});
spawn_starter(#appl{name = Name} = Appl, S) ->
    try missing_dependency(Appl, S#state.running, not_running) of
        {ok, _} ->
            clause_application_master:start_link(
                #{name => Name, mod => Appl#appl.mod, phases => Appl#appl.phases, max_t => Appl#appl.max_t}, normal
            )
    catch
        throw:{error, Missing} -> gen_server:cast(?AC, {application_started, Name, {info, Missing}})
    end.

started(Name, Result, S) ->
    Requests = reply_all(Name, S#state.start_req, result(Result)),
    {Name, RestartType, _, _} = lists:keyfind(Name, 1, S#state.starting),
    Rest = S#state{starting = lists:keydelete(Name, 1, S#state.starting), start_req = Requests},
    case {Result, RestartType} of
        {{ok, Master}, _} ->
            info_started(Name, node()),
            {noreply, Rest#state{
                running = keyreplaceadd(Name, Rest#state.running, {Name, Master}),
                started = keyreplaceadd(Name, Rest#state.started, {Name, RestartType})
            }};
        {{error, Reason}, temporary} ->
            info_exited(Name, Reason, temporary),
            {noreply, Rest};
        {{info, _}, temporary} ->
            {noreply, Rest};
        {{Kind, Reason}, transient} ->
            report_failed(Kind, Name, Reason, transient),
            transient_failed(Name, Reason, Rest);
        {{Kind, Reason}, permanent} ->
            report_failed(Kind, Name, Reason, permanent),
            halt_node({application_start_failure, Name, Reason}, Rest)
    end.

report_failed(error, Name, Reason, Type) -> info_exited(Name, Reason, Type);
report_failed(info, _, _, _) -> ok.

transient_failed(_, {{'EXIT', normal}, _}, S) -> {noreply, S};
transient_failed(Name, Reason, S) -> halt_node({application_start_failure, Name, Reason}, S).

result({ok, _}) -> ok;
result({info, Reason}) -> {error, Reason};
result(Error) -> Error.

%% Answers every caller waiting on the application; keeps the other requests.
reply_all(Name, Requests, Reply) ->
    lists:filter(
        fun
            ({N, From}) when N =:= Name ->
                gen_server:reply(From, Reply),
                false;
            (_) ->
                true
        end,
        Requests
    ).

keyreplaceadd(Name, List, New) ->
    case lists:keymember(Name, 1, List) of
        true -> lists:keyreplace(Name, 1, List, New);
        false -> [New | List]
    end.

%% Permissions of local applications: false stops a running one, true starts one held back.
permit(Name, Bool, From, S) ->
    case {get_loaded(Name), Bool, lists:keyfind(Name, 1, S#state.running)} of
        {false, _, _} ->
            {reply, {error, {not_loaded, Name}}, S};
        {_, true, {_, _}} ->
            {reply, ok, S};
        {_, false, {_, Master}} ->
            {_, Type} = lists:keyfind(Name, 1, S#state.started),
            update_permissions(Name, false),
            stop_appl(Name, Master, Type),
            {reply, ok, S#state{running = lists:keydelete(Name, 1, S#state.running)}};
        {{true, Appl}, true, false} ->
            update_permissions(Name, true),
            permit_held(Name, Appl, From, S);
        {{true, _}, false, false} ->
            update_permissions(Name, false),
            {reply, ok, S#state{start_p_false = lists:keydelete(Name, 1, S#state.start_p_false)}}
    end.

%% Starts an application held back or started before but not running now.
permit_held(Name, Appl, From, S) ->
    Held = lists:keyfind(Name, 1, S#state.start_p_false),
    Started = lists:keyfind(Name, 1, S#state.started),
    case {Held, Started} of
        {{_, RestartType, _, _}, _} ->
            spawn_starter(Appl, S),
            {noreply, S#state{
                starting = [{Name, RestartType, normal, From} | S#state.starting],
                start_p_false = lists:keydelete(Name, 1, S#state.start_p_false),
                start_req = [{Name, From} | S#state.start_req]
            }};
        {false, {_, RestartType}} ->
            spawn_starter(Appl, S),
            {noreply, S#state{
                starting = [{Name, RestartType, normal, From} | S#state.starting],
                started = lists:keydelete(Name, 1, S#state.started),
                start_req = [{Name, From} | S#state.start_req]
            }};
        {false, false} ->
            {reply, ok, S}
    end.

update_permissions(Name, Bool) ->
    Old =
        case get_env(kernel, permissions) of
            {ok, Permissions} -> lists:keydelete(Name, 1, Permissions);
            undefined -> []
        end,
    ets:insert(?TABLE, {{env, kernel, permissions}, [{Name, Bool} | Old]}).

%% Stopping.

stop(Name, From, S) ->
    case {lists:keymember(Name, 1, S#state.stop_req), lists:keyfind(Name, 1, S#state.running)} of
        {true, _} ->
            {noreply, S#state{stop_req = [{Name, From} | S#state.stop_req]}};
        {false, {_, Master}} ->
            {_, Type} = lists:keyfind(Name, 1, S#state.started),
            spawn(fun() ->
                stop_appl(Name, Master, Type),
                gen_server:cast(?AC, {application_stopped, Name})
            end),
            {noreply, S#state{stop_req = [{Name, From} | S#state.stop_req]}};
        {false, false} ->
            case lists:keymember(Name, 1, S#state.started) of
                true -> {reply, ok, S#state{started = lists:keydelete(Name, 1, S#state.started)}};
                false -> {reply, {error, {not_started, Name}}, S}
            end
    end.

stop_appl(Name, Master, Type) when is_pid(Master) ->
    unlink(Master),
    clause_application_master:stop(Master),
    info_exited(Name, stopped, Type),
    ets:delete(?TABLE, {application_master, Name});
stop_appl(Name, undefined, Type) ->
    info_exited(Name, stopped, Type).

%% Shuts every running application down before the program halts.
stop_running(S) ->
    lists:foreach(
        fun
            ({_, Master}) when is_pid(Master) ->
                Ref = monitor(process, Master),
                unlink(Master),
                exit(Master, shutdown),
                receive
                    {'DOWN', Ref, process, Master, _} -> ok
                end;
            (_) ->
                ok
        end,
        S#state.running
    ).

%% A permanent application ended: OTP's kernel terminates and the node halts.
halt_node(Reason, S) ->
    stop_running(S),
    Text = to_string(Reason),
    erlang:halt(lists:flatten(io_lib:format("Kernel pid terminated (~p) (~p)", [?AC, Text]))).

to_string(Term) ->
    case io_lib:printable_list(Term) of
        true -> Term;
        false -> lists:flatten(io_lib:format("~0p", [Term]))
    end.

%% Reports.

info_started(Name, Node) ->
    ?LOG_INFO(
        #{label => {application_controller, progress}, report => [{application, Name}, {started_at, Node}]},
        #{
            domain => [otp, sasl],
            report_cb => fun application_controller:format_log/2,
            logger_formatter => #{title => "PROGRESS REPORT"},
            error_logger => #{
                tag => info_report, type => progress, report_cb => fun application_controller:format_log/1
            }
        }
    ).

info_exited(Name, Reason, Type) ->
    ?LOG_NOTICE(
        #{label => {application_controller, exit}, report => [{application, Name}, {exited, Reason}, {type, Type}]},
        #{
            domain => [otp],
            report_cb => fun application_controller:format_log/2,
            error_logger => #{
                tag => info_report, type => std_info, report_cb => fun application_controller:format_log/1
            }
        }
    ).

format_log(Report) ->
    Depth = error_logger:get_format_depth(),
    Options = #{chars_limit => unlimited, depth => Depth, single_line => false, encoding => utf8},
    format_multi(limit_report(Report, Depth), Options).

limit_report(Report, unlimited) ->
    Report;
limit_report(#{report := Items} = Report, Depth) ->
    Report#{
        report := [{application, Name} || {application, Name} <- Items] ++
            [{Key, io_lib:limit_term(Value, Depth)} || {Key, Value} <- Items, Key =/= application]
    }.

format_log(Report, Options0) ->
    Options = maps:merge(
        #{chars_limit => unlimited, depth => unlimited, single_line => false, encoding => utf8}, Options0
    ),
    IoOptions =
        case Options of
            #{chars_limit := unlimited} -> [];
            #{chars_limit := Limit} -> [{chars_limit, Limit}]
        end,
    {Format, Args} =
        case Options of
            #{single_line := true} -> format_single(Report, Options);
            _ -> format_multi(Report, Options)
        end,
    io_lib:format(Format, Args, IoOptions).

format_single(#{label := {application_controller, progress}, report := [{application, Name}, {started_at, Node}]}, O) ->
    P = p(O),
    {"Application: " ++ P ++ ". Started at: " ++ P ++ ".", args([Name, Node], O)};
format_single(#{label := {application_controller, exit}, report := [{application, Name}, {exited, R}, {type, T}]}, O) ->
    P = p(O),
    {"Application: " ++ P ++ ". Exited: " ++ P ++ ". Type: " ++ P ++ ".", args([Name, R, T], O)}.

format_multi(#{label := {application_controller, progress}, report := [{application, Name}, {started_at, Node}]}, O) ->
    P = p(O),
    {"    application: " ++ P ++ "~n    started_at: " ++ P ++ "~n", args([Name, Node], O)};
format_multi(#{label := {application_controller, exit}, report := [{application, Name}, {exited, R}, {type, T}]}, O) ->
    P = p(O),
    {"    application: " ++ P ++ "~n    exited: " ++ P ++ "~n    type: " ++ P ++ "~n", args([Name, R, T], O)}.

args(Terms, #{depth := unlimited}) -> Terms;
args(Terms, #{depth := Depth}) -> lists:append([[Term, Depth] || Term <- Terms]).

p(#{single_line := Single, depth := Depth, encoding := Encoding}) ->
    "~" ++ single(Single) ++ modifier(Encoding) ++ control(Depth).

single(true) -> "0";
single(false) -> "".

modifier(latin1) -> "";
modifier(_) -> "t".

control(unlimited) -> "p";
control(_) -> "P".
