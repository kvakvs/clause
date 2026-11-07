%% The mnesia application's callback and its supervisors, registered as in OTP (plan 21 step MIM1A,
%% docs/libraries/mnesia.md#processes): mnesia_sup runs the mnesia_event manager, mnesia_ext_sup for backend
%% processes and mnesia_kernel_sup: mnesia_monitor owns the tables, mnesia_subscr keeps the event subscriptions (MIM1E),
%% mnesia_locker keeps the locks, mnesia_tm the transactions (MIM1B) and mnesia_checkpoint_sup the checkpoints
%% (MIM1F); mnesia_recover, mnesia_rpc, mnesia_controller and mnesia_late_loader are where other nodes find them
%% (MIM1H3).
-module(clause_mnesia_app).
-behaviour(application).
-behaviour(supervisor).
-export([start/2, stop/1, init/1, start_supervisor/1, start_event/0, kill/0]).

start(normal, []) ->
    case supervisor:start_link({local, mnesia_sup}, ?MODULE, mnesia_sup) of
        {ok, Pid} ->
            merge_extra_db_nodes(),
            {ok, Pid};
        Error ->
            Error
    end;
start(Type, Args) ->
    {error, {badarg, Type, Args}}.

stop(_State) ->
    ok.

% The schema merged at start with the extra_db_nodes that run mnesia (MIM1H5); a failed merge leaves this node's own.
merge_extra_db_nodes() ->
    case clause_mnesia_monitor:get_env(extra_db_nodes) of
        [] -> ok;
        Nodes -> _ = clause_mnesia_merge:connect_nodes(Nodes)
    end,
    ok.

start_supervisor(Name) ->
    supervisor:start_link({local, Name}, ?MODULE, Name).

% The event manager mnesia reports system events to, with the event_module parameter's handler (MIM1C).
start_event() ->
    {ok, Pid} = gen_event:start_link({local, mnesia_event}),
    case clause_mnesia_event:add_handler() of
        ok -> {ok, Pid};
        Error -> Error
    end.

% Every child stops mnesia as a whole: none restarts. The children's modules are OTP's, as supervisor:which_children/1
% shows them to other nodes (MIM1H3).
init(mnesia_sup) ->
    Event = #{
        id => mnesia_event, start => {?MODULE, start_event, []}, shutdown => 30000, modules => [mnesia_event, gen_event]
    },
    {ok,
        {#{strategy => one_for_all, intensity => 0, period => 3600}, [
            Event, supervisor(mnesia_ext_sup), supervisor(mnesia_kernel_sup)
        ]}};
init(mnesia_ext_sup) ->
    {ok, {#{strategy => one_for_all, intensity => 0, period => 3600}, []}};
init(mnesia_kernel_sup) ->
    % The literal funs link into programs the servers that child specs, and the modules that other nodes, name only as
    % data (MIM1H3).
    _ = [
        fun clause_mnesia_recover:start_link/0,
        fun mnesia_rpc:start/0,
        fun mnesia_lib:is_running/0,
        fun mnesia_controller:get_cstructs/0,
        fun mnesia_monitor:call/1,
        fun mnesia_recover:call/1,
        fun mnesia_index:dirty_read2/3,
        fun mnesia_index:dirty_match_object/3
    ],
    {ok, {#{strategy => one_for_all, intensity => 0, period => 86400000}, kernel_children()}};
% Each checkpoint's process (MIM1F), never restarted.
init(mnesia_checkpoint_sup) ->
    Checkpoint = #{
        id => mnesia_checkpoint_sup,
        start => {clause_mnesia_checkpoint, start_link, []},
        restart => temporary,
        modules => [mnesia_checkpoint_sup, mnesia_checkpoint, supervisor]
    },
    {ok, {#{strategy => simple_one_for_one, intensity => 0, period => 3600}, [Checkpoint]}}.

% mnesia_kernel_sup's servers in OTP's order, with OTP's names and modules (MIM1H3).
kernel_children() ->
    Lib = [mnesia_monitor, proc_lib],
    [
        worker(mnesia_monitor, {clause_mnesia_monitor, start_link, []}, [gen_server]),
        worker(mnesia_subscr, {clause_mnesia_subscr, start_link, []}, [gen_server]),
        worker(mnesia_locker, {clause_mnesia_locker, start_link, []}, Lib),
        worker(mnesia_recover, {clause_mnesia_recover, start_link, []}, [gen_server]),
        worker(mnesia_tm, {clause_mnesia_tm, start_link, []}, Lib),
        worker(mnesia_rpc, {mnesia_rpc, start, []}, [gen_server]),
        supervisor(mnesia_checkpoint_sup),
        worker(mnesia_controller, {clause_mnesia_controller, start_link, []}, [gen_server]),
        worker(mnesia_late_loader, {clause_mnesia_controller, start_late_loader, []}, Lib)
    ].

worker(Name, Start, Modules) ->
    #{id => Name, start => Start, shutdown => 3000, modules => [Name | Modules]}.

supervisor(Name) ->
    #{
        id => Name,
        start => {?MODULE, start_supervisor, [Name]},
        shutdown => infinity,
        type => supervisor,
        modules => [Name, supervisor]
    }.

% mnesia:lkill/0 (MIM1E): every mnesia process killed, the top supervisor's application master told it ended normally,
% as OTP's debug function does; again until the application is gone.
kill() ->
    Names = [mnesia_fallback | mnesia:ms()],
    lists:foreach(fun kill/1, Names),
    lists:foreach(fun ensure_dead/1, Names),
    timer:sleep(10),
    case lists:keymember(mnesia, 1, application:which_applications()) of
        true -> kill();
        false -> ok
    end.

kill(mnesia_sup) ->
    try
        Sup = whereis(mnesia_sup),
        {dictionary, Dictionary} = process_info(Sup, dictionary),
        [Master] = proplists:get_value('$ancestors', Dictionary),
        unlink(Master),
        exit(Sup, kill),
        Master ! {'EXIT', Sup, normal}
    catch
        _:_ -> ok
    end;
kill(Name) ->
    try
        exit(whereis(Name), kill)
    catch
        _:_ -> ok
    end.

ensure_dead(Name) ->
    case whereis(Name) of
        undefined ->
            ok;
        Pid ->
            exit(Pid, kill),
            timer:sleep(10),
            ensure_dead(Name)
    end.
