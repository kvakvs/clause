%% Clause's application (plan 21 step LIB24, docs/library.md): OTP's application API over Clause's
%% application_controller. Specifications come from the program's .app and .app.src files, compiled into it, or from
%% {application, Name, Keys} terms; kernel and stdlib are loaded and running from the start. Distribution (dist_ac)
%% is not running, as on a node without distributed applications.
-module(application).
-export([
    ensure_all_started/1,
    ensure_all_started/2,
    ensure_all_started/3,
    ensure_started/1,
    ensure_started/2,
    get_all_env/0,
    get_all_env/1,
    get_all_key/0,
    get_all_key/1,
    get_application/0,
    get_application/1,
    get_env/1,
    get_env/2,
    get_env/3,
    get_key/1,
    get_key/2,
    get_supervisor/1,
    info/0,
    load/1,
    load/2,
    loaded_applications/0,
    permit/2,
    set_env/1,
    set_env/2,
    set_env/3,
    set_env/4,
    start/1,
    start/2,
    start_boot/1,
    start_boot/2,
    start_type/0,
    stop/1,
    takeover/2,
    unload/1,
    unset_env/2,
    unset_env/3,
    which_applications/0,
    which_applications/1
]).

-include("application.types.hrl").

-callback start(StartType :: start_type(), StartArgs :: term()) ->
    {ok, pid()} | {ok, pid(), State :: term()} | {error, Reason :: term()}.
-callback stop(State :: term()) -> term().
-callback prep_stop(State :: term()) -> NewState :: term().
-callback config_change(Changed :: [{atom(), term()}], New :: [{atom(), term()}], Removed :: [atom()]) -> ok.
-callback start_phase(Phase :: atom(), StartType :: start_type(), PhaseArgs :: term()) -> ok | {error, term()}.
-optional_callbacks([prep_stop/1, config_change/3, start_phase/3]).

%% Loading.

load(Application) -> load(Application, []).

%% Distributed nodes go to dist_ac, which is not running.
load(Application, []) ->
    application_controller:load_application(Application);
load(Application, DistNodes) ->
    case application_controller:load_application(Application) of
        ok ->
            Name = name(Application),
            case gen_server:call(dist_ac, {load_application, Name, DistNodes}, infinity) of
                ok ->
                    ok;
                {error, Reason} ->
                    _ = application_controller:unload_application(Name),
                    {error, Reason}
            end;
        Error ->
            Error
    end.

unload(Application) -> application_controller:unload_application(Application).

name({application, Name, _}) -> Name;
name(Name) -> Name.

%% Starting and stopping.

start(Application) -> start(Application, temporary).

start(Application, RestartType) ->
    case ensure_loaded(Application) of
        {ok, Name} -> application_controller:start_application(Name, RestartType);
        Error -> Error
    end.

ensure_loaded(Application) ->
    case load(Application) of
        ok -> {ok, name(Application)};
        {error, {already_loaded, Name}} -> {ok, Name};
        Error -> Error
    end.

ensure_started(Application) -> ensure_started(Application, temporary).

ensure_started(Application, RestartType) ->
    case start(Application, RestartType) of
        ok -> ok;
        {error, {already_started, Application}} -> ok;
        Error -> Error
    end.

ensure_all_started(Applications) -> ensure_all_started(Applications, temporary, serial).

ensure_all_started(Applications, Type) -> ensure_all_started(Applications, Type, serial).

%% Starts the applications and their dependencies, dependencies first; on a failure stops those it started. The
%% concurrent mode starts them one at a time too.
ensure_all_started(Application, Type, Mode) when is_atom(Application) ->
    ensure_all_started([Application], Type, Mode);
ensure_all_started(Applications, Type, Mode) when is_list(Applications), (Mode =:= serial orelse Mode =:= concurrent) ->
    case start_all(Applications, [], Type, []) of
        {ok, Started} ->
            {ok, lists:reverse(Started)};
        {error, Reason, Started} ->
            _ = [stop(Name) || Name <- Started],
            {error, Reason}
    end.

start_all([Application | Rest], Optional, Type, Started) when is_atom(Application) ->
    case application_controller:is_running(Application) of
        true ->
            start_all(Rest, Optional, Type, Started);
        false ->
            case start_one(Application, Optional, Type, Started) of
                {ok, Now} -> start_all(Rest, Optional, Type, Now);
                Error -> Error
            end
    end;
start_all([], _, _, Started) ->
    {ok, Started}.

%% One application after the applications it needs; an optional one that has no specification is left out.
start_one(Application, Optional, Type, Started) ->
    case ensure_loaded(Application) of
        {ok, Name} ->
            {ok, Needed} = get_key(Name, applications),
            {ok, MayUse} = get_key(Name, optional_applications),
            case start_all(Needed, MayUse, Type, Started) of
                {ok, Now} -> start_loaded(Name, Type, Now);
                Error -> Error
            end;
        {error, {"no such file or directory", _} = Reason} ->
            case lists:member(Application, Optional) of
                true -> {ok, Started};
                false -> {error, {Application, Reason}, Started}
            end;
        {error, Reason} ->
            {error, {Application, Reason}, Started}
    end.

start_loaded(Name, Type, Started) ->
    case application_controller:start_application(Name, Type) of
        ok -> {ok, [Name | Started]};
        {error, {already_started, Name}} -> {ok, Started};
        {error, Reason} -> {error, {Name, Reason}, Started}
    end.

start_boot(Application) -> start_boot(Application, temporary).

start_boot(Application, RestartType) -> application_controller:start_boot_application(Application, RestartType).

%% Taking over from another node is distribution: dist_ac is not running.
takeover(Application, RestartType) -> gen_server:call(dist_ac, {takeover_application, Application, RestartType}).

permit(Application, Bool) when is_boolean(Bool) -> application_controller:permit_application(Application, Bool);
permit(Application, Bad) -> exit({badarg, {?MODULE, permit, [Application, Bad]}}).

stop(Application) -> application_controller:stop_application(Application).

%% Queries.

which_applications() -> application_controller:which_applications().

which_applications(infinity) ->
    application_controller:which_applications(infinity);
which_applications(Timeout) when is_integer(Timeout), Timeout >= 0 ->
    application_controller:which_applications(Timeout).

loaded_applications() -> application_controller:loaded_applications().

info() -> application_controller:info().

%% Environment.

set_env(Config) when is_list(Config) -> set_env(Config, []).

set_env(Config, Options) when is_list(Config), is_list(Options) ->
    case application_controller:set_env(Config, Options) of
        ok -> ok;
        {error, Message} -> erlang:error({badarg, Message}, [Config, Options])
    end.

set_env(Application, Key, Value) -> application_controller:set_env(Application, Key, Value).

set_env(Application, Key, Value, infinity) ->
    set_env(Application, Key, Value, [{timeout, infinity}]);
set_env(Application, Key, Value, Timeout) when is_integer(Timeout), Timeout >= 0 ->
    set_env(Application, Key, Value, [{timeout, Timeout}]);
set_env(Application, Key, Value, Options) when is_list(Options) ->
    application_controller:set_env(Application, Key, Value, Options).

unset_env(Application, Key) -> application_controller:unset_env(Application, Key).

unset_env(Application, Key, infinity) ->
    unset_env(Application, Key, [{timeout, infinity}]);
unset_env(Application, Key, Timeout) when is_integer(Timeout), Timeout >= 0 ->
    unset_env(Application, Key, [{timeout, Timeout}]);
unset_env(Application, Key, Options) when is_list(Options) ->
    application_controller:unset_env(Application, Key, Options).

get_env(Key) -> application_controller:get_pid_env(group_leader(), Key).

get_env(Application, Key) -> application_controller:get_env(Application, Key).

get_env(Application, Key, Default) -> application_controller:get_env(Application, Key, Default).

get_all_env() -> application_controller:get_pid_all_env(group_leader()).

get_all_env(Application) -> application_controller:get_all_env(Application).

get_key(Key) -> application_controller:get_pid_key(group_leader(), Key).

get_key(Application, Key) -> application_controller:get_key(Application, Key).

get_all_key() -> application_controller:get_pid_all_key(group_leader()).

get_all_key(Application) -> application_controller:get_all_key(Application).

%% The application of a process is the one whose master leads its group.
get_application() -> application_controller:get_application(group_leader()).

get_application(Pid) when is_pid(Pid) ->
    case process_info(Pid, group_leader) of
        {group_leader, Leader} -> application_controller:get_application(Leader);
        undefined -> undefined
    end;
get_application(Module) when is_atom(Module) ->
    application_controller:get_application_module(Module).

get_supervisor(Application) when is_atom(Application) ->
    case application_controller:get_master(Application) of
        undefined ->
            undefined;
        Master ->
            case clause_application_master:get_child(Master) of
                {Root, _} -> {ok, Root};
                error -> undefined
            end
    end.

start_type() -> application_controller:start_type(group_leader()).
