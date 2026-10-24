-module(supervisors).
-behaviour(supervisor).
-export([main/1]).
-export([init/1]).
-export([start_worker/2, start_failing/1, start_ignore/0, start_bad/0, worker/2]).

%% supervisor and supervisor_bridge (plan 21 step LIB21): child specifications and their errors, the restart
%% strategies, restart intensity, child management calls, simple_one_for_one children, automatic shutdown on
%% significant children, shutdown rules, and a bridge to a process started by its module. Workers report their
%% starts and ends to the test process by id; pids are shown only as pid checks.
main(_) ->
    process_flag(trap_exit, true),
    register(sup_main, self()),
    specs(),
    one_for_one(),
    management(),
    one_for_all(),
    rest_for_one(),
    intensity(),
    simple(),
    significant(),
    shutdowns(),
    bridge(),
    io:format("supervisors: ok~n").

%% supervisor callbacks: init({Flags, Specs}) returns them; init(ignore) and init({bad, X}) for errors.
init({bad, X}) ->
    X;
init({Flags, Specs}) ->
    {ok, {Flags, Specs}};
init(ignore) ->
    ignore.

%% Workers: a process that reports its start and end, ends on request, and traps exits when asked to.
start_worker(Id, Options) ->
    Pid = proc_lib:spawn_link(?MODULE, worker, [Id, Options]),
    {ok, Pid}.

start_failing(Reason) -> {error, Reason}.

start_ignore() -> ignore.

start_bad() -> not_a_start_result.

worker(Id, Options) ->
    lists:member(trap, Options) andalso process_flag(trap_exit, true),
    register(Id, self()),
    sup_main ! {started, Id},
    worker_loop(Id, Options).

worker_loop(Id, Options) ->
    receive
        {exit, Reason} ->
            sup_main ! {ended, Id, Reason},
            exit(Reason);
        {'EXIT', _, shutdown} when Options =:= [trap, slow] ->
            timer:sleep(500),
            sup_main ! {ended, Id, slow_shutdown},
            exit(shutdown);
        {'EXIT', _, Reason} ->
            sup_main ! {ended, Id, Reason},
            exit(Reason)
    end.

child(Id) -> child(Id, permanent).

child(Id, Restart) ->
    #{id => Id, start => {?MODULE, start_worker, [Id, [trap]]}, restart => Restart}.

%% Tests.

specs() ->
    show(check_childspecs, [
        supervisor:check_childspecs([child(a), child(b)]),
        supervisor:check_childspecs([{a, {m, f, []}, permanent, 5000, worker, [m]}]),
        supervisor:check_childspecs([child(a), child(a)]),
        supervisor:check_childspecs([#{start => {m, f, []}}]),
        supervisor:check_childspecs([#{id => x}]),
        supervisor:check_childspecs([#{id => x, start => {m, f, a}}]),
        supervisor:check_childspecs([#{id => x, start => {m, f, []}, restart => sometimes}]),
        supervisor:check_childspecs([#{id => x, start => {m, f, []}, type => manager}]),
        supervisor:check_childspecs([#{id => x, start => {m, f, []}, shutdown => -1}]),
        supervisor:check_childspecs([#{id => x, start => {m, f, []}, modules => [1]}]),
        supervisor:check_childspecs([#{id => x, start => {m, f, []}, modules => one}]),
        supervisor:check_childspecs([#{id => x, start => {m, f, []}, significant => true}]),
        supervisor:check_childspecs(
            [#{id => x, start => {m, f, []}, significant => true}], any_significant
        ),
        supervisor:check_childspecs(
            [#{id => x, start => {m, f, []}, significant => true, restart => transient}],
            any_significant
        ),
        supervisor:check_childspecs(
            [#{id => x, start => {m, f, []}, significant => maybe_not}], never
        ),
        supervisor:check_childspecs([{x, y}]),
        supervisor:check_childspecs(not_a_list),
        supervisor:check_childspecs([], sometimes)
    ]),
    show(bad_flags, [
        start_error(#{strategy => one_for_some}),
        start_error(#{intensity => -1}),
        start_error(#{period => 0}),
        start_error(#{auto_shutdown => sometimes}),
        start_error(#{strategy => simple_one_for_one, auto_shutdown => any_significant}),
        start_error(#{hibernate_after => -5}),
        start_error(flags_not_a_map),
        start_error({one_for_one, x, 5})
    ]),
    show(bad_init, [
        supervisor:start_link(?MODULE, ignore),
        supervisor:start_link(?MODULE, {bad, odd}),
        supervisor:start_link(?MODULE, {#{}, [child(a), child(a)]}),
        supervisor:start_link(?MODULE, {#{strategy => simple_one_for_one}, [child(a), child(b)]}),
        supervisor:start_link(
            ?MODULE, {#{}, [#{id => f, start => {?MODULE, start_failing, [nope]}}]}
        ),
        supervisor:start_link(?MODULE, {#{}, [#{id => b, start => {?MODULE, start_bad, []}}]})
    ]),
    flush().

start_error(Flags) -> supervisor:start_link(?MODULE, {Flags, []}).

one_for_one() ->
    {ok, Sup} = supervisor:start_link(
        {local, sup_one}, ?MODULE, {#{intensity => 10}, [child(w1), child(w2)]}
    ),
    show(started, collect()),
    show(children, children(sup_one)),
    show(counts, supervisor:count_children(sup_one)),
    w1 ! {exit, crashed},
    show(restart, collect()),
    w2 ! {exit, normal},
    show(permanent_normal, collect()),
    show(children_after, children(Sup)),
    ok = supervisor:stop(Sup),
    show(stopped, collect()),
    show(sup_exit, receive_exit(Sup)).

management() ->
    {ok, Sup} = supervisor:start_link(?MODULE, {#{}, [child(m1)]}),
    show(started, collect()),
    show(start_child, [
        ok_pid(supervisor:start_child(Sup, child(m2, transient))),
        supervisor:start_child(
            Sup, {m3, {?MODULE, start_worker, [m3, [trap]]}, temporary, 1000, worker, []}
        ) =/= x,
        already(supervisor:start_child(Sup, child(m2))),
        supervisor:start_child(Sup, #{id => f, start => {?MODULE, start_failing, [no]}}),
        supervisor:start_child(Sup, #{
            id => i, start => {?MODULE, start_ignore, []}, restart => temporary
        }),
        supervisor:start_child(Sup, #{id => j, start => {?MODULE, start_ignore, []}}),
        supervisor:start_child(Sup, #{id => bad}),
        supervisor:start_child(Sup, #{id => b, start => {?MODULE, start_bad, []}})
    ]),
    show(started_children, collect()),
    show(which_children, children(Sup)),
    show(which_child, [
        child_info(supervisor:which_child(Sup, m1)),
        supervisor:which_child(Sup, j),
        supervisor:which_child(Sup, nobody),
        supervisor:which_child(Sup, self())
    ]),
    {ok, Spec} = supervisor:get_childspec(Sup, m2),
    show(get_childspec, [lists:sort(maps:to_list(Spec)), supervisor:get_childspec(Sup, nobody)]),
    show(counts, supervisor:count_children(Sup)),
    show(terminate_child, [
        supervisor:terminate_child(Sup, m2), collect(), supervisor:terminate_child(Sup, nobody)
    ]),
    show(after_terminate, children(Sup)),
    show(restart_child, [
        ok_pid(supervisor:restart_child(Sup, m2)),
        collect(),
        supervisor:restart_child(Sup, m2),
        supervisor:restart_child(Sup, nobody),
        supervisor:restart_child(Sup, j)
    ]),
    show(delete_child, [
        supervisor:delete_child(Sup, m2),
        supervisor:delete_child(Sup, j),
        supervisor:delete_child(Sup, nobody)
    ]),
    ok = supervisor:terminate_child(Sup, m3),
    show(temporary_gone, [collect(), supervisor:which_child(Sup, m3)]),
    show(callback_module, supervisor:get_callback_module(Sup)),
    show(sys_state_module, element(1, sys:get_state(Sup))),
    ok = supervisor:stop(Sup, shutdown, 1000),
    show(stopped, collect()),
    show(sup_exit, receive_exit(Sup)).

one_for_all() ->
    {ok, Sup} = supervisor:start_link(
        ?MODULE, {#{strategy => one_for_all, intensity => 5}, [child(a1), child(a2), child(a3)]}
    ),
    show(started, collect()),
    a2 ! {exit, boom},
    show(restart_all, collect()),
    ok = supervisor:stop(Sup),
    show(stopped, collect()),
    receive_exit(Sup).

rest_for_one() ->
    {ok, Sup} = supervisor:start_link(
        ?MODULE, {#{strategy => rest_for_one, intensity => 5}, [child(r1), child(r2), child(r3)]}
    ),
    show(started, collect()),
    r2 ! {exit, boom},
    show(restart_rest, collect()),
    show(order, [Id || {Id, _, _, _} <- supervisor:which_children(Sup)]),
    ok = supervisor:stop(Sup),
    show(stopped, collect()),
    receive_exit(Sup).

intensity() ->
    {ok, Sup} = supervisor:start_link(?MODULE, {#{intensity => 1, period => 10}, [child(i1)]}),
    show(started, collect()),
    i1 ! {exit, once},
    show(first, collect()),
    i1 ! {exit, twice},
    show(second, collect()),
    show(gave_up, receive_exit(Sup)),
    {ok, Zero} = supervisor:start_link(?MODULE, {#{intensity => 0}, [child(z1)]}),
    show(zero_started, collect()),
    z1 ! {exit, at_once},
    show(zero, [collect(), receive_exit(Zero)]),
    flush().

simple() ->
    Spec = #{id => ignored_id, start => {?MODULE, start_worker, []}, restart => transient},
    {ok, Sup} = supervisor:start_link(?MODULE, {#{strategy => simple_one_for_one}, [Spec]}),
    show(no_children, supervisor:which_children(Sup)),
    {ok, P1} = supervisor:start_child(Sup, [s1, [trap]]),
    {ok, P2} = supervisor:start_child(Sup, [s2, [trap]]),
    show(started, collect()),
    show(children, [{Id, is_pid(P), T, M} || {Id, P, T, M} <- supervisor:which_children(Sup)]),
    show(counts, supervisor:count_children(Sup)),
    show(simple_errors, [
        supervisor:terminate_child(Sup, s1),
        supervisor:restart_child(Sup, P1),
        supervisor:delete_child(Sup, P1),
        supervisor:which_child(Sup, s1),
        child_info(supervisor:which_child(Sup, P1)),
        supervisor:start_child(Sup, [s9]) =/= x
    ]),
    s1 ! {exit, abnormal},
    show(transient_restart, collect()),
    s2 ! {exit, normal},
    show(transient_normal, [collect(), length(supervisor:which_children(Sup))]),
    show(terminate_by_pid, [
        supervisor:terminate_child(Sup, P2), supervisor:terminate_child(Sup, whereis(s1))
    ]),
    show(terminated, collect()),
    show(empty, supervisor:which_children(Sup)),
    ok = supervisor:stop(Sup),
    receive_exit(Sup),
    flush().

significant() ->
    Child = fun(Id) ->
        #{
            id => Id,
            start => {?MODULE, start_worker, [Id, [trap]]},
            restart => transient,
            significant => true
        }
    end,
    {ok, Any} = supervisor:start_link(
        ?MODULE, {#{auto_shutdown => any_significant}, [Child(g1), child(g2)]}
    ),
    show(any_started, collect()),
    g1 ! {exit, normal},
    show(any_shutdown, [collect(), receive_exit(Any)]),
    {ok, All} = supervisor:start_link(
        ?MODULE, {#{auto_shutdown => all_significant}, [Child(h1), Child(h2)]}
    ),
    show(all_started, collect()),
    h1 ! {exit, normal},
    show(all_one, [collect(), is_process_alive(All)]),
    h2 ! {exit, shutdown},
    show(all_shutdown, [collect(), receive_exit(All)]).

shutdowns() ->
    Specs = [
        #{id => k1, start => {?MODULE, start_worker, [k1, [trap]]}, shutdown => brutal_kill},
        #{id => k2, start => {?MODULE, start_worker, [k2, [trap, slow]]}, shutdown => 50},
        #{
            id => k3,
            start => {?MODULE, start_worker, [k3, [trap]]},
            shutdown => infinity,
            type => supervisor
        }
    ],
    {ok, Sup} = supervisor:start_link(?MODULE, {#{}, Specs}),
    show(started, collect()),
    show(types, [{Id, Type} || {Id, _, Type, _} <- supervisor:which_children(Sup)]),
    ok = supervisor:stop(Sup),
    show(stopped, collect()),
    show(sup_exit, receive_exit(Sup)),
    flush().

%% supervisor_bridge: the module's init/1 starts the bridged process, terminate/2 stops it.
bridge() ->
    {ok, Bridge} = supervisor_bridge:start_link({local, sup_bridge}, sup_bridged, started),
    show(bridge_started, collect()),
    show(bridge_children, supervisor:which_children(sup_bridge)),
    show(bridge_other_call, gen_server:call(sup_bridge, other)),
    ok = gen_server:stop(Bridge, shutdown, 1000),
    show(bridge_stopped, collect()),
    show(bridge_exit, receive_exit(Bridge)),
    {ok, Bridge2} = supervisor_bridge:start_link(sup_bridged, started),
    show(bridge2_started, collect()),
    bridged ! {exit, gone},
    show(bridged_ended, [collect(), receive_exit(Bridge2)]),
    show(bridge_results, [
        supervisor_bridge:start_link(sup_bridged, ignore),
        supervisor_bridge:start_link(sup_bridged, {error, why})
    ]),
    flush().

%% Helpers.

children(Sup) ->
    [
        {Id, is_pid(Pid) orelse Pid, Type, Modules}
     || {Id, Pid, Type, Modules} <- supervisor:which_children(Sup)
    ].

child_info({ok, {Id, Pid, Type, Modules}}) -> {ok, {Id, is_pid(Pid), Type, Modules}};
child_info(Other) -> Other.

ok_pid({ok, Pid}) -> {ok, is_pid(Pid)};
ok_pid(Other) -> Other.

already({error, {already_started, Pid}}) -> {error, {already_started, is_pid(Pid)}};
already(Other) -> Other.

%% The worker reports that arrive until things settle, sorted since several workers act at once.
collect() -> lists:sort(reports()).

reports() ->
    receive
        {started, Id} -> [{started, Id} | reports()];
        {ended, Id, Reason} -> [{ended, Id, Reason} | reports()];
        {bridge_terminate, Reason} -> [{bridge_terminate, Reason} | reports()]
    after 200 -> []
    end.

receive_exit(Pid) ->
    receive
        {'EXIT', Pid, Reason} -> Reason
    after 3000 -> timeout
    end.

flush() ->
    receive
        _ -> flush()
    after 50 -> ok
    end.

show(Label, Value) -> io:format("~p: ~p~n", [Label, Value]).
