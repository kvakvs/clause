%% Clause's supervisor (plan 21 step LIB21, docs/library.md): a gen_server that starts, watches and restarts
%% children by OTP's strategies, restart intensity, shutdown rules and automatic shutdown, with OTP's error terms
%% and supervisor reports. Children are kept as {Ids, #{Id => #child{}}} with the ids in reverse start order; a
%% simple_one_for_one supervisor keeps its pids in a map. The #child{} record keeps OTP's shape, which
%% start_child/2 errors show.
-module(supervisor).
-behaviour(gen_server).
-export([
    check_childspecs/1,
    check_childspecs/2,
    code_change/3,
    count_children/1,
    delete_child/2,
    format_log/1,
    format_log/2,
    get_callback_module/1,
    get_childspec/2,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    init/1,
    restart_child/2,
    start_child/2,
    start_link/2,
    start_link/3,
    stop/1,
    stop/3,
    terminate/2,
    terminate_child/2,
    which_child/2,
    which_children/1
]).
-include("supervisor.types.hrl").

-callback init(Args :: term()) ->
    {ok, {SupFlags :: sup_flags(), [ChildSpec :: child_spec()]}} | ignore.

-define(DEFAULT_FLAGS, #{
    strategy => one_for_one, intensity => 1, period => 5, auto_shutdown => never
}).
-define(DEFAULT_CHILD, #{restart => permanent, type => worker}).
-define(DIRTY_RESTART_LIMIT, 1000).

%% A child: `pid` is undefined when it does not run, {restarting, Pid} while a restart is retried, and a pid list
%% in reports of dynamic children.
-record(child, {
    pid = undefined,
    id,
    mfargs,
    restart_type,
    significant,
    shutdown,
    child_type,
    modules = []
}).

%% Created for LIB21: a supervisor's state. `children` is {Ids, Db}; `dynamics` holds a simple_one_for_one
%% supervisor's pids ({maps, Pid => Args} or {mapsets, Pid => []} for temporary children); `restarts` are the
%% recent restart times in seconds, `nrestarts` their count; `tag` marks the supervisor's own messages.
-record(state, {
    name,
    strategy = one_for_one,
    children = {[], #{}},
    dynamics,
    intensity = 1,
    period = 5,
    restarts = [],
    nrestarts = 0,
    dynamic_restarts = 0,
    auto_shutdown = never,
    hibernate_after = infinity,
    hibernating = false,
    tag = make_ref(),
    module,
    args
}).

-define(IS_SIMPLE(State), State#state.strategy =:= simple_one_for_one).

%% API.

start_link(Module, Args) -> gen_server:start_link(supervisor, {self, Module, Args}, []).

start_link(Name, Module, Args) -> gen_server:start_link(Name, supervisor, {Name, Module, Args}, []).

start_child(Supervisor, SpecOrArgs) -> call(Supervisor, {start_child, SpecOrArgs}).

restart_child(Supervisor, Id) -> call(Supervisor, {restart_child, Id}).

delete_child(Supervisor, Id) -> call(Supervisor, {delete_child, Id}).

terminate_child(Supervisor, Id) -> call(Supervisor, {terminate_child, Id}).

get_childspec(Supervisor, Id) -> call(Supervisor, {get_childspec, Id}).

which_children(Supervisor) -> call(Supervisor, which_children).

which_child(Supervisor, Id) -> call(Supervisor, {which_child, Id}).

count_children(Supervisor) -> call(Supervisor, count_children).

stop(Supervisor) -> gen_server:stop(Supervisor).

stop(Supervisor, Reason, Timeout) -> gen_server:stop(Supervisor, Reason, Timeout).

call(Supervisor, Request) -> gen_server:call(Supervisor, Request, infinity).

check_childspecs(Specs) -> check_childspecs(Specs, undefined).

check_childspecs(Specs, AutoShutdown) when is_list(Specs) ->
    case lists:member(AutoShutdown, [undefined, never, any_significant, all_significant]) of
        true ->
            case check_startspec(Specs, AutoShutdown) of
                {ok, _} -> ok;
                Error -> {error, Error}
            end;
        false ->
            {error, {badarg, AutoShutdown}}
    end;
check_childspecs(Specs, _) ->
    {error, {badarg, Specs}}.

get_callback_module(Pid) ->
    {status, _, {module, _}, [_, _, _, _, Misc]} = sys:get_status(Pid),
    case lists:keyfind(supervisor, 1, Misc) of
        {supervisor, [{"Callback", Module}]} ->
            Module;
        _ ->
            [_, _, {data, [{"State", State}]} | _] = Misc,
            State#state.module
    end.

%% gen_server callbacks.

init({Name, Module, Args}) ->
    process_flag(trap_exit, true),
    case Module:init(Args) of
        {ok, {Flags, Specs}} ->
            case
                set_flags(Flags, #state{name = sup_name(Name, Module), module = Module, args = Args})
            of
                {ok, State} when ?IS_SIMPLE(State) -> init_dynamic(State, Specs);
                {ok, State} -> init_children(State, Specs);
                Error -> {stop, {supervisor_data, Error}}
            end;
        ignore ->
            ignore;
        Error ->
            {stop, {bad_return, {Module, init, Error}}}
    end.

sup_name(self, Module) -> {self(), Module};
sup_name(Name, _) -> Name.

init_children(State, Specs) ->
    case check_startspec(Specs, State#state.auto_shutdown) of
        {ok, Children} ->
            case start_children(Children, State#state.name) of
                {ok, Started} ->
                    {ok, State#state{children = Started}, hibernate_after(State)};
                {error, Started, Reason} ->
                    _ = terminate_children(Started, State#state.name),
                    {stop, {shutdown, Reason}}
            end;
        Error ->
            {stop, {start_spec, Error}}
    end.

init_dynamic(State, [Spec]) ->
    case check_startspec([Spec], State#state.auto_shutdown) of
        {ok, Children} ->
            {ok, dynamics_init(State#state{children = Children}), hibernate_after(State)};
        Error ->
            {stop, {start_spec, Error}}
    end;
init_dynamic(_, Specs) ->
    {stop, {bad_start_spec, Specs}}.

handle_call(Request, From, #state{hibernating = true} = State) ->
    handle_call(Request, From, wakeup(State));
handle_call(Request, _, State) ->
    {Reply, NewState} = request(Request, State),
    {reply, Reply, NewState, hibernate_after(State)}.

handle_cast(Msg, #state{hibernating = true} = State) ->
    handle_cast(Msg, wakeup(State));
handle_cast({try_again_restart, Tag, Id}, #state{tag = Tag} = State) ->
    case find_child_and_args(Id, State) of
        {ok, #child{pid = {restarting, _}} = Child} -> after_restart(restart(Child, State), State);
        _ -> {noreply, State, hibernate_after(State)}
    end.

handle_info({hibernate, Tag}, #state{tag = Tag} = State) ->
    {noreply, (purge_restarts(State))#state{hibernating = true}, hibernate};
handle_info(Msg, #state{hibernating = true} = State) ->
    handle_info(Msg, wakeup(State));
handle_info({'EXIT', Pid, Reason}, State) ->
    case find_child_and_args(Pid, State) of
        {ok, Child} -> after_restart(child_exited(Reason, Child, State), State);
        error -> {noreply, State, hibernate_after(State)}
    end;
handle_info(Msg, State) ->
    clause_logger:log(error, {"Supervisor received unexpected message: ~tp~n", [Msg]}, #{
        domain => [otp], error_logger => #{tag => error}
    }),
    {noreply, State, hibernate_after(State)}.

after_restart({ok, NewState}, State) -> {noreply, NewState, hibernate_after(State)};
after_restart({shutdown, NewState}, _) -> {stop, shutdown, NewState}.

terminate(_, State) when ?IS_SIMPLE(State) -> terminate_dynamic_children(State);
terminate(_, State) -> terminate_children(State#state.children, State#state.name).

%% A code change re-reads init/1: new flags and child specifications for the children kept.
code_change(_, State, _) ->
    case (State#state.module):init(State#state.args) of
        {ok, {Flags, Specs}} ->
            case set_flags(Flags, State) of
                {ok, NewState} -> update_specs(NewState, Specs);
                {invalid_type, Flags} -> {error, {bad_flags, Flags}};
                Error -> {error, Error}
            end;
        ignore ->
            {ok, State};
        Error ->
            Error
    end.

update_specs(State, Specs) when ?IS_SIMPLE(State) ->
    case check_startspec(Specs, State#state.auto_shutdown) of
        {ok, {[_], _} = Children} -> {ok, State#state{children = Children}};
        Error -> {error, Error}
    end;
update_specs(#state{children = {OldIds, OldDb}} = State, Specs) ->
    case check_startspec(Specs, State#state.auto_shutdown) of
        {ok, {Ids, Db}} ->
            %% Specifications that name a running child take its pid; old children without one are kept after them.
            {NewDb, Kept} = lists:foldl(
                fun(Id, {Acc, KeptIds}) ->
                    case maps:find(Id, Acc) of
                        {ok, Child} ->
                            {
                                Acc#{Id => Child#child{pid = (maps:get(Id, OldDb))#child.pid}},
                                KeptIds
                            };
                        error ->
                            {Acc, [Id | KeptIds]}
                    end
                end,
                {Db, []},
                OldIds
            ),
            {ok, State#state{
                children = {lists:reverse(Ids ++ Kept), maps:merge(maps:with(Kept, OldDb), NewDb)}
            }};
        Error ->
            {error, Error}
    end.

wakeup(State) -> (purge_restarts(State))#state{hibernating = false}.

hibernate_after(#state{tag = Tag, hibernate_after = Time}) -> {timeout, Time, {hibernate, Tag}}.

%% Requests: {Reply, State}.

request({start_child, ExtraArgs}, State) when ?IS_SIMPLE(State) ->
    #child{mfargs = {M, F, A}} = dynamic_child(State),
    Args = A ++ ExtraArgs,
    case start_mfa(M, F, Args) of
        {ok, undefined} -> {{ok, undefined}, State};
        {ok, Pid} -> {{ok, Pid}, dynamics_store(Pid, Args, State)};
        {ok, Pid, Extra} -> {{ok, Pid, Extra}, dynamics_store(Pid, Args, State)};
        Other -> {Other, State}
    end;
request({start_child, Spec}, State) ->
    case check_childspec(Spec, State#state.auto_shutdown) of
        {ok, Child} -> start_new_child(Child, State);
        Error -> {{error, Error}, State}
    end;
request({terminate_child, Id}, State) when not is_pid(Id), ?IS_SIMPLE(State) ->
    {{error, simple_one_for_one}, State};
request({terminate_child, Id}, State) ->
    case find_child(Id, State) of
        {ok, Child} ->
            stop_child(Child, State#state.name),
            {ok, delete_from(Child, State)};
        error ->
            {{error, not_found}, State}
    end;
request({restart_child, _}, State) when ?IS_SIMPLE(State) ->
    {{error, simple_one_for_one}, State};
request({restart_child, Id}, State) ->
    case find_child(Id, State) of
        {ok, #child{pid = undefined} = Child} ->
            case start_child_of(State#state.name, Child, debug_report) of
                {ok, Pid} -> {{ok, Pid}, set_pid(Pid, Id, State)};
                {ok, Pid, Extra} -> {{ok, Pid, Extra}, set_pid(Pid, Id, State)};
                Error -> {Error, State}
            end;
        {ok, #child{pid = {restarting, _}}} ->
            {{error, restarting}, State};
        {ok, _} ->
            {{error, running}, State};
        error ->
            {{error, not_found}, State}
    end;
request({delete_child, _}, State) when ?IS_SIMPLE(State) ->
    {{error, simple_one_for_one}, State};
request({delete_child, Id}, #state{children = {Ids, Db}} = State) ->
    case find_child(Id, State) of
        {ok, #child{pid = undefined}} ->
            {ok, State#state{children = {lists:delete(Id, Ids), maps:remove(Id, Db)}}};
        {ok, #child{pid = {restarting, _}}} ->
            {{error, restarting}, State};
        {ok, _} ->
            {{error, running}, State};
        error ->
            {{error, not_found}, State}
    end;
request({get_childspec, Id}, State) ->
    case find_child(Id, State) of
        {ok, Child} -> {{ok, child_to_spec(Child)}, State};
        error -> {{error, not_found}, State}
    end;
request(which_children, State) when ?IS_SIMPLE(State) ->
    #child{child_type = Type, modules = Modules} = dynamic_child(State),
    {[{undefined, running_pid(Pid), Type, Modules} || Pid <- dynamics_pids(State)], State};
request(which_children, #state{children = {Ids, Db}} = State) ->
    {[child_info(Id, maps:get(Id, Db)) || Id <- Ids], State};
request({which_child, Id}, State) when not is_pid(Id), ?IS_SIMPLE(State) ->
    {{error, simple_one_for_one}, State};
request({which_child, Pid}, State) when ?IS_SIMPLE(State) ->
    case find_dynamic_child(Pid, State) of
        {ok, Child} -> {{ok, child_info(undefined, Child)}, State};
        error -> {{error, not_found}, State}
    end;
request({which_child, Id}, State) ->
    case find_child(Id, State) of
        {ok, Child} -> {{ok, child_info(Id, Child)}, State};
        error -> {{error, not_found}, State}
    end;
request(count_children, #state{dynamic_restarts = Restarts} = State) when ?IS_SIMPLE(State) ->
    #child{child_type = Type} = dynamic_child(State),
    Size = map_size(element(2, State#state.dynamics)),
    {Supervisors, Workers} =
        case Type of
            supervisor -> {Size, 0};
            worker -> {0, Size}
        end,
    {
        [{specs, 1}, {active, Size - Restarts}, {supervisors, Supervisors}, {workers, Workers}],
        State
    };
request(count_children, #state{children = {_, Db}} = State) ->
    {Specs, Active, Supervisors, Workers} = maps:fold(
        fun(_, Child, Counts) -> count(Child, Counts) end,
        {0, 0, 0, 0},
        Db
    ),
    {[{specs, Specs}, {active, Active}, {supervisors, Supervisors}, {workers, Workers}], State}.

running_pid({restarting, _}) -> restarting;
running_pid(Pid) -> Pid.

child_info(Id, #child{pid = Pid, child_type = Type, modules = Modules}) ->
    {Id, running_pid(Pid), Type, Modules}.

count(#child{pid = Pid, child_type = Type}, {Specs, Active, Supervisors, Workers}) ->
    Alive =
        case is_pid(Pid) andalso is_process_alive(Pid) of
            true -> 1;
            false -> 0
        end,
    case Type of
        worker -> {Specs + 1, Active + Alive, Supervisors, Workers + 1};
        supervisor -> {Specs + 1, Active + Alive, Supervisors + 1, Workers}
    end.

start_new_child(#child{id = Id} = Child, State) ->
    case find_child(Id, State) of
        error ->
            case start_child_of(State#state.name, Child, debug_report) of
                {ok, undefined} when Child#child.restart_type =:= temporary ->
                    {{ok, undefined}, State};
                {ok, Pid} ->
                    {{ok, Pid}, save_child(Child#child{pid = Pid}, State)};
                {ok, Pid, Extra} ->
                    {{ok, Pid, Extra}, save_child(Child#child{pid = Pid}, State)};
                {error, {already_started, _} = Reason} ->
                    {{error, Reason}, State};
                {error, Reason} ->
                    {{error, {Reason, Child}}, State}
            end;
        {ok, #child{pid = Pid}} when is_pid(Pid) ->
            {{error, {already_started, Pid}}, State};
        {ok, _} ->
            {{error, already_present}, State}
    end.

%% A temporary child is saved without its start arguments: it is never restarted.
save_child(#child{restart_type = temporary, mfargs = {M, F, _}} = Child, State) ->
    add_child(Child#child{mfargs = {M, F, undefined}}, State);
save_child(Child, State) ->
    add_child(Child, State).

add_child(#child{id = Id} = Child, #state{children = {Ids, Db}} = State) ->
    State#state{children = {[Id | Ids], Db#{Id => Child}}}.

%% Starting children.

%% Starts children in order; {ok, Children} with the ids reversed, or {error, Children, Reason} after the first
%% failure (the children not started keep no pid).
start_children(Children, Name) ->
    map_children(
        fun(Id, Child) ->
            case start_child_of(Name, Child, info_report) of
                {ok, undefined} when Child#child.restart_type =:= temporary ->
                    remove;
                {ok, Pid} ->
                    {update, Child#child{pid = Pid}};
                {ok, Pid, _} ->
                    {update, Child#child{pid = Pid}};
                {error, Reason} ->
                    report(start_error, Reason, Child, Name),
                    {abort, {failed_to_start_child, Id, Reason}}
            end
        end,
        Children
    ).

start_child_of(Name, #child{mfargs = {M, F, Args}} = Child, Report) ->
    case start_mfa(M, F, Args) of
        {ok, Pid} = Started when is_pid(Pid) ->
            progress(Child#child{pid = Pid}, Name, Report),
            Started;
        {ok, Pid, _} = Started when is_pid(Pid) ->
            progress(Child#child{pid = Pid}, Name, Report),
            Started;
        Other ->
            Other
    end.

%% What a child's start function gave: {ok, Pid}, {ok, Pid, Extra}, {ok, undefined} for ignore, or {error, Reason}.
start_mfa(M, F, A) ->
    try apply(M, F, A) of
        Result -> start_result(Result)
    catch
        throw:Result -> start_result(Result);
        exit:Reason -> {error, {'EXIT', Reason}};
        error:Reason:Stack -> {error, {'EXIT', {Reason, Stack}}}
    end.

start_result({ok, Pid} = Result) when is_pid(Pid) -> Result;
start_result({ok, Pid, _} = Result) when is_pid(Pid) -> Result;
start_result(ignore) -> {ok, undefined};
start_result({error, _} = Error) -> Error;
start_result(Other) -> {error, Other}.

%% Restarts.

child_exited(Reason, #child{restart_type = permanent} = Child, State) ->
    report(child_terminated, Reason, Child, State#state.name),
    restart(Child, State);
child_exited(Reason, Child, State) when Reason =:= normal; Reason =:= shutdown ->
    auto_shutdown(Child, delete_from(Child, State));
child_exited({shutdown, _}, Child, State) ->
    auto_shutdown(Child, delete_from(Child, State));
child_exited(Reason, #child{restart_type = transient} = Child, State) ->
    report(child_terminated, Reason, Child, State#state.name),
    restart(Child, State);
child_exited(Reason, #child{restart_type = temporary} = Child, State) ->
    report(child_terminated, Reason, Child, State#state.name),
    auto_shutdown(Child, delete_from(Child, State)).

%% {ok, State}, or {shutdown, State} when a significant child's end shuts the supervisor down.
auto_shutdown(_, #state{auto_shutdown = never} = State) ->
    {ok, State};
auto_shutdown(#child{significant = false}, State) ->
    {ok, State};
auto_shutdown(_, #state{auto_shutdown = any_significant} = State) ->
    {shutdown, State};
auto_shutdown(_, #state{auto_shutdown = all_significant, children = {_, Db}} = State) ->
    Running = [Id || Id := #child{pid = Pid, significant = true} <- Db, Pid =/= undefined],
    case Running of
        [] -> {shutdown, State};
        _ -> {ok, State}
    end.

restart(Child, State) ->
    case add_restart(State) of
        {ok, Counted} ->
            case restart(Counted#state.strategy, Child, Counted) of
                {{try_again, Id}, Retrying} ->
                    %% The retry comes back as a cast, so requests (a shutdown, say) are handled in between.
                    gen_server:cast(self(), {try_again_restart, Retrying#state.tag, Id}),
                    {ok, Retrying};
                Other ->
                    Other
            end;
        {terminate, Counted} ->
            report(shutdown, reached_max_restart_intensity, Child, State#state.name),
            {shutdown, delete_from(Child, Counted)}
    end.

restart(simple_one_for_one, #child{pid = OldPid, mfargs = {M, F, A}} = Child, State0) ->
    State1 =
        case OldPid of
            {restarting, _} -> State0#state{dynamic_restarts = State0#state.dynamic_restarts - 1};
            _ -> State0
        end,
    State2 = dynamics_erase(OldPid, State1),
    case start_mfa(M, F, A) of
        {ok, undefined} ->
            {ok, State2};
        {ok, Pid} ->
            {ok, dynamics_store(Pid, A, State2)};
        {ok, Pid, _} ->
            {ok, dynamics_store(Pid, A, State2)};
        {error, Error} ->
            Retrying = restarting(OldPid),
            State3 = dynamics_store(Retrying, A, State2#state{
                dynamic_restarts = State2#state.dynamic_restarts + 1
            }),
            report(start_error, Error, Child, State3#state.name),
            {{try_again, Retrying}, State3}
    end;
restart(one_for_one, #child{id = Id, pid = OldPid} = Child, State) ->
    case start_child_of(State#state.name, Child, info_report) of
        {ok, Pid} ->
            {ok, set_pid(Pid, Id, State)};
        {ok, Pid, _} ->
            {ok, set_pid(Pid, Id, State)};
        {error, Reason} ->
            report(start_error, Reason, Child, State#state.name),
            {{try_again, Id}, set_pid(restarting(OldPid), Id, State)}
    end;
restart(rest_for_one, #child{id = Id} = Child, #state{name = Name} = State) ->
    {After, Before} = split_children(Id, State#state.children),
    {Result, Restarted} = restart_children(Child, After, Name),
    {Result, State#state{children = append_children(Restarted, Before)}};
restart(one_for_all, #child{id = Id} = Child, #state{name = Name, children = Children} = State) ->
    {Result, Restarted} = restart_children(Child, without_pid(Id, Children), Name),
    {Result, State#state{children = Restarted}}.

restart_children(Child, Children, Name) ->
    case start_children(terminate_children(Children, Name), Name) of
        {ok, Started} ->
            {ok, Started};
        {error, Started, {failed_to_start_child, FailedId, _}} ->
            Pid =
                case FailedId =:= Child#child.id of
                    true -> restarting(Child#child.pid);
                    false -> {restarting, undefined}
                end,
            {{try_again, FailedId}, set_pid(Pid, FailedId, Started)}
    end.

restarting(Pid) when is_pid(Pid) -> {restarting, Pid};
restarting(Restarting) -> Restarting.

%% Restart intensity: at most `intensity` restarts within `period` seconds.
add_restart(#state{intensity = 0} = State) ->
    {terminate, State};
add_restart(#state{intensity = I, restarts = R, nrestarts = N} = State) when
    N < min(I, ?DIRTY_RESTART_LIMIT)
->
    {ok, State#state{restarts = [erlang:monotonic_time(second) | R], nrestarts = N + 1}};
add_restart(#state{intensity = I, period = P, restarts = R} = State) ->
    Now = erlang:monotonic_time(second),
    case recent(I - 1, Now - P, R, [], 0) of
        {true, N, Kept} -> {ok, State#state{restarts = [Now | Kept], nrestarts = N + 1}};
        {false, N, Kept} -> {terminate, State#state{restarts = Kept, nrestarts = N}}
    end.

recent(_, _, [], Acc, N) ->
    {true, N, lists:reverse(Acc)};
recent(_, Threshold, [Time | _], Acc, N) when Time < Threshold -> {true, N, lists:reverse(Acc)};
recent(0, _, [_ | _], Acc, N) ->
    {false, N, lists:reverse(Acc)};
recent(Left, Threshold, [Time | Rest], Acc, N) ->
    recent(Left - 1, Threshold, Rest, [Time | Acc], N + 1).

purge_restarts(#state{period = P, restarts = [Latest | _]} = State) ->
    case erlang:monotonic_time(second) - P of
        Threshold when Latest < Threshold -> State#state{restarts = [], nrestarts = 0};
        _ -> State
    end;
purge_restarts(State) ->
    State.

%% Stopping children.

%% Stops children in reverse start order; temporary ones are removed, the others kept without a pid.
terminate_children(Children, Name) ->
    {ok, Kept} = map_children(
        fun
            (_, #child{restart_type = temporary} = Child) ->
                stop_child(Child, Name),
                remove;
            (_, Child) ->
                stop_child(Child, Name),
                {update, Child#child{pid = undefined}}
        end,
        Children
    ),
    Kept.

stop_child(#child{pid = Pid} = Child, Name) when is_pid(Pid) ->
    case shutdown(Child) of
        ok -> ok;
        {error, Reason} -> report(shutdown_error, Reason, Child, Name)
    end,
    ok;
stop_child(_, _) ->
    ok.

%% Ends a child by its shutdown rule: ok when it ended as the rule expects.
shutdown(#child{pid = Pid, shutdown = brutal_kill} = Child) ->
    Monitor = monitor(process, Pid),
    exit(Pid, kill),
    receive
        {'DOWN', Monitor, process, Pid, Reason} ->
            shutdown_result(Child, killed, unlink_flush(Pid, Reason))
    end;
shutdown(#child{pid = Pid, shutdown = Time} = Child) ->
    Monitor = monitor(process, Pid),
    exit(Pid, shutdown),
    receive
        {'DOWN', Monitor, process, Pid, Reason} ->
            shutdown_result(Child, shutdown, unlink_flush(Pid, Reason))
    after Time ->
        exit(Pid, kill),
        receive
            {'DOWN', Monitor, process, Pid, Reason} ->
                shutdown_result(Child, shutdown, unlink_flush(Pid, Reason))
        end
    end.

%% A child ended well with Expected, and a non-permanent one also with normal, shutdown or {shutdown, _}.
shutdown_result(_, Expected, Expected) -> ok;
shutdown_result(#child{restart_type = permanent}, _, Reason) -> {error, Reason};
shutdown_result(_, _, Reason) when Reason =:= normal; Reason =:= shutdown -> ok;
shutdown_result(_, _, {shutdown, _}) -> ok;
shutdown_result(_, _, Reason) -> {error, Reason}.

%% The real exit reason of a child whose 'DOWN' said noproc comes from its link; the 'EXIT' is dropped.
unlink_flush(Pid, noproc) ->
    {links, Links} = process_info(self(), links),
    Wait =
        case lists:member(Pid, Links) of
            true -> infinity;
            false -> 0
        end,
    receive
        {'EXIT', Pid, Reason} -> Reason
    after Wait -> child_process_unlinked
    end;
unlink_flush(Pid, Reason) ->
    unlink(Pid),
    receive
        {'EXIT', Pid, _} -> ok
    after 0 -> ok
    end,
    Reason.

%% Stops every dynamic child at once and waits for them, killing those still running when the shutdown time ends.
terminate_dynamic_children(State) ->
    Child = dynamic_child(State),
    Exit =
        case Child#child.shutdown of
            brutal_kill -> kill;
            _ -> shutdown
        end,
    Pids = maps:from_list([
        {{Pid, stop_one(Pid, Exit)}, true}
     || Pid <- dynamics_pids(State), is_pid(Pid)
    ]),
    Timer =
        case Child#child.shutdown of
            brutal_kill -> undefined;
            infinity -> undefined;
            Time -> erlang:start_timer(Time, self(), kill)
        end,
    Errors = wait_children(Child, Pids, Timer, #{}),
    maps:foreach(
        fun(Reason, Failed) ->
            report(shutdown_error, Reason, Child#child{pid = Failed}, State#state.name)
        end,
        Errors
    ).

stop_one(Pid, Exit) ->
    Monitor = monitor(process, Pid),
    exit(Pid, Exit),
    Monitor.

wait_children(_, Pids, undefined, Errors) when map_size(Pids) =:= 0 ->
    Errors;
wait_children(_, Pids, Timer, Errors) when map_size(Pids) =:= 0 ->
    _ = erlang:cancel_timer(Timer),
    receive
        {timeout, Timer, kill} -> Errors
    after 0 -> Errors
    end;
wait_children(Child, Pids, Timer, Errors) ->
    Expected =
        case Child#child.shutdown of
            brutal_kill -> killed;
            _ -> shutdown
        end,
    receive
        {'DOWN', Monitor, process, Pid, Reason} when is_map_key({Pid, Monitor}, Pids) ->
            Rest = maps:remove({Pid, Monitor}, Pids),
            case shutdown_result(Child, Expected, unlink_flush(Pid, Reason)) of
                ok -> wait_children(Child, Rest, Timer, Errors);
                {error, Failure} -> wait_children(Child, Rest, Timer, prepend(Failure, Pid, Errors))
            end;
        {timeout, Timer, kill} when Timer =/= undefined ->
            maps:foreach(fun({Pid, _}, _) -> exit(Pid, kill) end, Pids),
            wait_children(Child, Pids, undefined, Errors)
    end.

prepend(Key, Value, Map) -> Map#{Key => [Value | maps:get(Key, Map, [])]}.

%% Children storage.

find_child(Pid, State) when is_pid(Pid), ?IS_SIMPLE(State) ->
    case find_dynamic_child(Pid, State) of
        error ->
            case find_dynamic_child(restarting(Pid), State) of
                error ->
                    case is_process_alive(Pid) of
                        true -> error;
                        false -> {ok, dynamic_child(State)}
                    end;
                Found ->
                    Found
            end;
        Found ->
            Found
    end;
find_child(Id, #state{children = {_, Db}}) ->
    maps:find(Id, Db).

find_child_and_args(Pid, State) when ?IS_SIMPLE(State) ->
    case find_dynamic_child(Pid, State) of
        {ok, #child{mfargs = {M, F, _}} = Child} ->
            {ok, Child#child{mfargs = {M, F, dynamics_args(Pid, State)}}};
        error ->
            error
    end;
find_child_and_args(Pid, #state{children = {_, Db}}) when is_pid(Pid) ->
    case [Child || _ := #child{pid = P} = Child <- Db, P =:= Pid] of
        [Child | _] -> {ok, Child};
        [] -> error
    end;
find_child_and_args(Id, #state{children = {_, Db}}) ->
    maps:find(Id, Db).

find_dynamic_child(Pid, #state{dynamics = {_, Db}} = State) ->
    case is_map_key(Pid, Db) of
        true -> {ok, (dynamic_child(State))#child{pid = Pid}};
        false -> error
    end.

dynamic_child(#state{children = {[Id], Db}}) -> maps:get(Id, Db).

%% Forgets a child's pid; a temporary child is forgotten altogether.
delete_from(#child{pid = Pid}, State) when ?IS_SIMPLE(State) ->
    dynamics_erase(Pid, State);
delete_from(#child{id = Id}, #state{children = Children} = State) ->
    State#state{children = without_pid(Id, Children)}.

without_pid(Id, {Ids, Db}) ->
    case maps:get(Id, Db) of
        #child{restart_type = temporary} -> {lists:delete(Id, Ids), maps:remove(Id, Db)};
        Child -> {Ids, Db#{Id => Child#child{pid = undefined}}}
    end.

set_pid(Pid, Id, #state{children = Children} = State) ->
    State#state{children = set_pid(Pid, Id, Children)};
set_pid(Pid, Id, {Ids, Db}) ->
    {Ids, maps:update_with(Id, fun(Child) -> Child#child{pid = Pid} end, Db)}.

%% {After, Before}: the child Id and those started after it (without its pid), and the ones started before.
split_children(Id, {Ids, Db}) ->
    {AfterIds, BeforeIds} = split_ids(Id, Ids, []),
    #{Id := Child} = After = maps:with(AfterIds, Db),
    {{AfterIds, After#{Id => Child#child{pid = undefined}}}, {BeforeIds, maps:with(BeforeIds, Db)}}.

split_ids(Id, [Id | Ids], After) -> {lists:reverse([Id | After]), Ids};
split_ids(Id, [Other | Ids], After) -> split_ids(Id, Ids, [Other | After]).

append_children({Ids1, Db1}, {Ids2, Db2}) -> {Ids1 ++ Ids2, maps:merge(Db1, Db2)}.

%% Applies Fun to each child in id order: {update, Child}, remove, or {abort, Reason} which stops the walk and
%% gives {error, Children, Reason}. {ok, Children} reverses the ids.
map_children(Fun, {Ids, Db}) -> map_children(Fun, Ids, Db, []).

map_children(Fun, [Id | Ids], Db, Acc) ->
    case Fun(Id, maps:get(Id, Db)) of
        {update, Child} -> map_children(Fun, Ids, Db#{Id => Child}, [Id | Acc]);
        remove -> map_children(Fun, Ids, maps:remove(Id, Db), Acc);
        {abort, Reason} -> {error, {lists:reverse(Ids) ++ [Id | Acc], Db}, Reason}
    end;
map_children(_, [], Db, Acc) ->
    {ok, {Acc, Db}}.

dynamics_init(#state{} = State) ->
    case dynamic_child(State) of
        #child{restart_type = temporary} -> State#state{dynamics = {mapsets, #{}}};
        _ -> State#state{dynamics = {maps, #{}}}
    end.

%% Temporary dynamic children are never restarted, so their start arguments are not kept.
dynamics_store(Pid, _, #state{dynamics = {mapsets, Db}} = State) ->
    State#state{dynamics = {mapsets, Db#{Pid => []}}};
dynamics_store(Pid, Args, #state{dynamics = {maps, Db}} = State) ->
    State#state{dynamics = {maps, Db#{Pid => Args}}}.

dynamics_erase(Pid, #state{dynamics = {Kind, Db}} = State) ->
    State#state{dynamics = {Kind, maps:remove(Pid, Db)}}.

dynamics_pids(#state{dynamics = {_, Db}}) -> maps:keys(Db).

dynamics_args(_, #state{dynamics = {mapsets, _}}) -> undefined;
dynamics_args(Pid, #state{dynamics = {maps, Db}}) -> maps:get(Pid, Db).

%% Flags and child specifications, checked as OTP checks them: the first invalid field is thrown.

set_flags(Flags, State) ->
    try check_flags(Flags) of
        #{strategy := S, intensity := I, period := P, auto_shutdown := A, hibernate_after := H} ->
            {ok, State#state{
                strategy = S, intensity = I, period = P, auto_shutdown = A, hibernate_after = H
            }}
    catch
        throw:Error -> Error
    end.

check_flags(Flags) when is_map(Flags) ->
    Merged = maps:merge(?DEFAULT_FLAGS, Flags),
    Complete =
        case Merged of
            #{hibernate_after := _} -> Merged;
            #{strategy := simple_one_for_one} -> Merged#{hibernate_after => infinity};
            _ -> Merged#{hibernate_after => 1000}
        end,
    check_flag_values(Complete);
check_flags({Strategy, Intensity, Period}) ->
    check_flags(#{strategy => Strategy, intensity => Intensity, period => Period});
check_flags(Other) ->
    throw({invalid_type, Other}).

check_flag_values(
    #{strategy := S, intensity := I, period := P, auto_shutdown := A, hibernate_after := H} = Flags
) ->
    lists:member(S, [simple_one_for_one, one_for_one, one_for_all, rest_for_one]) orelse
        throw({invalid_strategy, S}),
    is_integer(I) andalso I >= 0 orelse throw({invalid_intensity, I}),
    is_integer(P) andalso P > 0 orelse throw({invalid_period, P}),
    lists:member(A, [never, any_significant, all_significant]) orelse
        throw({invalid_auto_shutdown, A}),
    A =:= never orelse S =/= simple_one_for_one orelse
        throw({bad_combination, [{auto_shutdown, A}, {strategy, simple_one_for_one}]}),
    H =:= infinity orelse (is_integer(H) andalso H >= 0) orelse throw({invalid_hibernate_after, H}),
    Flags.

check_startspec(Specs, AutoShutdown) -> check_startspec(Specs, [], #{}, AutoShutdown).

check_startspec([Spec | Specs], Ids, Db, AutoShutdown) ->
    case check_childspec(Spec, AutoShutdown) of
        {ok, #child{id = Id} = Child} ->
            case is_map_key(Id, Db) of
                true -> {duplicate_child_name, Id};
                false -> check_startspec(Specs, [Id | Ids], Db#{Id => Child}, AutoShutdown)
            end;
        Error ->
            Error
    end;
check_startspec([], Ids, Db, _) ->
    {ok, {lists:reverse(Ids), Db}}.

check_childspec(Spec, AutoShutdown) when is_map(Spec) ->
    try
        child_of(maps:merge(?DEFAULT_CHILD, Spec), AutoShutdown)
    catch
        throw:Error -> Error
    end;
check_childspec({Id, Start, Restart, Shutdown, Type, Modules}, AutoShutdown) ->
    check_childspec(
        #{
            id => Id,
            start => Start,
            restart => Restart,
            significant => false,
            shutdown => Shutdown,
            type => Type,
            modules => Modules
        },
        AutoShutdown
    );
check_childspec(Other, _) ->
    {invalid_child_spec, Other}.

child_of(#{restart := Restart, type := Type} = Spec, AutoShutdown) ->
    is_map_key(id, Spec) orelse throw(missing_id),
    is_map_key(start, Spec) orelse throw(missing_start),
    #{id := Id, start := Start} = Spec,
    valid_start(Start) orelse throw({invalid_mfa, Start}),
    lists:member(Restart, [permanent, temporary, transient]) orelse
        throw({invalid_restart_type, Restart}),
    Significant = maps:get(significant, Spec, false),
    check_significant(Significant, Restart, AutoShutdown),
    lists:member(Type, [supervisor, worker]) orelse throw({invalid_child_type, Type}),
    Shutdown = maps:get(shutdown, Spec, default_shutdown(Type)),
    valid_shutdown(Shutdown) orelse throw({invalid_shutdown, Shutdown}),
    Modules = maps:get(modules, Spec, [element(1, Start)]),
    check_modules(Modules),
    {ok, #child{
        id = Id,
        mfargs = Start,
        restart_type = Restart,
        significant = Significant,
        shutdown = Shutdown,
        child_type = Type,
        modules = Modules
    }}.

valid_start({M, F, A}) -> is_atom(M) andalso is_atom(F) andalso is_list(A);
valid_start(_) -> false.

check_significant(true, _, never) ->
    throw({bad_combination, [{auto_shutdown, never}, {significant, true}]});
check_significant(true, permanent, _) ->
    throw({bad_combination, [{restart, permanent}, {significant, true}]});
check_significant(Significant, _, _) when is_boolean(Significant) -> true;
check_significant(Significant, _, _) ->
    throw({invalid_significant, Significant}).

default_shutdown(worker) -> 5000;
default_shutdown(supervisor) -> infinity.

valid_shutdown(Shutdown) when is_integer(Shutdown), Shutdown >= 0 -> true;
valid_shutdown(Shutdown) -> Shutdown =:= infinity orelse Shutdown =:= brutal_kill.

check_modules(dynamic) ->
    true;
check_modules(Modules) when is_list(Modules) ->
    lists:foreach(fun(M) -> is_atom(M) orelse throw({invalid_module, M}) end, Modules);
check_modules(Modules) ->
    throw({invalid_modules, Modules}).

child_to_spec(#child{
    id = Id,
    mfargs = Start,
    restart_type = Restart,
    significant = Significant,
    shutdown = Shutdown,
    child_type = Type,
    modules = Modules
}) ->
    #{
        id => Id,
        start => Start,
        restart => Restart,
        significant => Significant,
        shutdown => Shutdown,
        type => Type,
        modules => Modules
    }.

%% Reports.

report(Context, Reason, Child, Name) ->
    clause_logger:log(
        error,
        {report, #{
            label => {supervisor, Context},
            report => [
                {supervisor, Name},
                {errorContext, Context},
                {reason, Reason},
                {offender, offender(Child)}
            ]
        }},
        #{
            domain => [otp, sasl],
            report_cb => fun supervisor:format_log/2,
            logger_formatter => #{title => "SUPERVISOR REPORT"},
            error_logger => #{
                tag => error_report,
                type => supervisor_report,
                report_cb => fun supervisor:format_log/1
            }
        }
    ).

%% Progress reports are info (started at init and by restarts) or debug (start_child, restart_child): below the
%% default level either way.
progress(Child, Name, Report) ->
    Level =
        case Report of
            info_report -> info;
            debug_report -> debug
        end,
    clause_logger:log(
        Level,
        {report, #{
            label => {supervisor, progress},
            report => [{supervisor, Name}, {started, offender(Child)}]
        }},
        #{
            domain => [otp, sasl],
            report_cb => fun supervisor:format_log/2,
            logger_formatter => #{title => "PROGRESS REPORT"},
            error_logger => #{
                tag => info_report, type => progress, report_cb => fun supervisor:format_log/1
            }
        }
    ).

offender(#child{pid = Pids} = Child) when is_list(Pids) ->
    [{nb_children, length(Pids)} | tl(offender(Child#child{pid = undefined}))];
offender(Child) ->
    [
        {pid, Child#child.pid},
        {id, Child#child.id},
        {mfargs, Child#child.mfargs},
        {restart_type, Child#child.restart_type},
        {significant, Child#child.significant},
        {shutdown, Child#child.shutdown},
        {child_type, Child#child.child_type}
    ].

format_log(Report) ->
    format_log_multi(Report, #{
        chars_limit => unlimited, depth => unlimited, single_line => false, encoding => utf8
    }).

format_log(Report, Options0) ->
    Options = maps:merge(
        #{chars_limit => unlimited, depth => unlimited, single_line => false, encoding => utf8},
        Options0
    ),
    IoOptions =
        case Options of
            #{chars_limit := unlimited} -> [];
            #{chars_limit := Limit} -> [{chars_limit, Limit}]
        end,
    {Format, Args} = format_log_single(Report, Options),
    io_lib:format(Format, Args, IoOptions).

format_log_single(
    #{label := {supervisor, progress}, report := [{supervisor, Name}, {started, Child}]},
    #{single_line := true, depth := Depth} = Options
) ->
    {ChildFormat, ChildArgs} = child_log_single(Child, "Started:"),
    {"Supervisor: " ++ p(Options) ++ "." ++ ChildFormat, depth_args(Depth, [Name]) ++ ChildArgs};
format_log_single(
    #{
        label := {supervisor, _},
        report := [
            {supervisor, Name},
            {errorContext, Context},
            {reason, Reason},
            {offender, Child}
        ]
    },
    #{single_line := true, depth := Depth} = Options
) ->
    P = p(Options),
    {ChildFormat, ChildArgs} = child_log_single(Child, "Offender:"),
    {
        "Supervisor: " ++ P ++ ". Context: " ++ P ++ ". Reason: " ++ P ++ "." ++ ChildFormat,
        depth_args(Depth, [Name, Context, Reason]) ++ ChildArgs
    };
format_log_single(Report, Options) ->
    format_log_multi(Report, Options).

format_log_multi(
    #{label := {supervisor, progress}, report := [{supervisor, Name}, {started, Child}]},
    #{depth := Depth} = Options
) ->
    P = p(Options),
    {"    supervisor: " ++ P ++ "~n    started: " ++ P ++ "~n", depth_args(Depth, [Name, Child])};
format_log_multi(
    #{
        label := {supervisor, _},
        report := [
            {supervisor, Name},
            {errorContext, Context},
            {reason, Reason},
            {offender, Child}
        ]
    },
    #{depth := Depth} = Options
) ->
    P = p(Options),
    {
        "    supervisor: " ++ P ++ "~n    errorContext: " ++ P ++ "~n    reason: " ++ P ++
            "~n    offender: " ++ P ++
            "~n",
        depth_args(Depth, [Name, Context, Reason, Child])
    }.

child_log_single(Child, Tag) ->
    {id, Id} = lists:keyfind(id, 1, Child),
    case lists:keyfind(pid, 1, Child) of
        false ->
            {nb_children, Count} = lists:keyfind(nb_children, 1, Child),
            {" ~s id=~w,nb_children=~w.", [Tag, Id, Count]};
        {pid, Pid} ->
            {" ~s id=~w,pid=~w.", [Tag, Id, Pid]}
    end.

depth_args(unlimited, Args) -> Args;
depth_args(Depth, Args) -> lists:append([[Arg, Depth] || Arg <- Args]).

p(#{single_line := Single, depth := Depth, encoding := Encoding}) ->
    "~" ++
        case Single of
            true -> "0";
            false -> ""
        end ++
        case Encoding of
            latin1 -> "";
            _ -> "t"
        end ++
        case Depth of
            unlimited -> "p";
            _ -> "P"
        end.
