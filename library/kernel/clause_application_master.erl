%% Clause's application master (plan 21 step LIB24, docs/libraries/library.md): the process application_controller starts for
%% each application with a start module, as OTP's application_master. It leads the group of the application's
%% processes, runs Mod:start/2 and the start phases in a process of its own, answers I/O requests on standard I/O and
%% on stop runs prep_stop/1, shuts the top supervisor down, runs stop/1 and kills what is left of the group.
-module(clause_application_master).
-export([start_link/2, init/3, start_it/4, get_child/1, stop/1]).

%% Created for LIB24: the application (name, mod, phases, max_t), the starting process that holds the top
%% supervisor, and get_child requests waiting for its answer.
-record(state, {data, child, requests = []}).

%% Data: #{name, mod, phases, max_t}.
start_link(Data, Type) -> proc_lib:start_link(?MODULE, init, [self(), Data, Type]).

%% The top supervisor and the start module, or error.
get_child(Master) -> request(Master, get_child).

stop(Master) -> request(Master, stop).

request(Master, What) ->
    Tag = make_ref(),
    Master ! {What, Tag, self()},
    receive
        {Tag, Reply} -> Reply
    end.

init(Parent, #{name := Name} = Data, Type) ->
    proc_lib:init_ack(Parent, {ok, self()}),
    process_flag(trap_exit, true),
    group_leader(self(), self()),
    ets:insert(ac_tab, {{application_master, Name}, self()}),
    State = #state{data = Data},
    case start_child(State, Type) of
        {ok, Pid} ->
            ok = set_timer(maps:get(max_t, Data)),
            gen_server:cast(Parent, {application_started, Name, {ok, self()}}),
            main_loop(Parent, State#state{child = Pid});
        {error, Reason} ->
            gen_server:cast(Parent, {application_started, Name, {error, Reason}}),
            exit(Reason);
        Other ->
            gen_server:cast(Parent, {application_started, Name, {error, Other}}),
            exit(Other)
    end.

start_child(State, Type) ->
    Tag = make_ref(),
    Pid = spawn_link(?MODULE, start_it, [Tag, State, self(), Type]),
    init_loop(Pid, Tag, State, Type).

init_loop(Pid, Tag, State, Type) ->
    receive
        {io_request, _, _, _} = Request ->
            relay(Request),
            init_loop(Pid, Tag, State, Type);
        {Tag, Result} ->
            Result;
        {'EXIT', Pid, Reason} ->
            {error, Reason};
        {start_type, From} ->
            From ! {start_type, Type},
            init_loop(Pid, Tag, State, Type);
        Other ->
            init_loop(Pid, Tag, handle_msg(Other, State), Type)
    end.

main_loop(Parent, #state{child = Child} = State) ->
    receive
        {io_request, _, _, _} = Request ->
            relay(Request),
            main_loop(Parent, State);
        {'EXIT', Parent, Reason} ->
            terminate(Reason, State);
        {'EXIT', Child, Reason} ->
            terminate(Reason, State#state{child = undefined});
        {'EXIT', _, timeout} ->
            terminate(normal, State);
        {'EXIT', _, _} ->
            main_loop(Parent, State);
        {start_type, From} ->
            From ! {start_type, local},
            main_loop(Parent, State);
        Other ->
            main_loop(Parent, handle_msg(Other, State))
    end.

%% The group's I/O goes to standard I/O.
relay({io_request, From, ReplyAs, Request}) -> From ! {io_reply, ReplyAs, io:request(standard_io, Request)}.

handle_msg({get_child, Tag, From}, #state{child = Child, requests = Requests} = State) ->
    case is_pid(Child) andalso is_process_alive(Child) of
        true ->
            Ref = make_ref(),
            Child ! {self(), get_child, Ref},
            State#state{requests = [{Ref, Tag, From} | Requests]};
        false ->
            From ! {Tag, error},
            State
    end;
handle_msg({stop, Tag, From}, State) ->
    catch terminate(normal, State),
    From ! {Tag, ok},
    exit(normal);
handle_msg({child, Ref, Top, Module}, #state{requests = Requests} = State) ->
    {value, {_, Tag, From}, Rest} = lists:keytake(Ref, 1, Requests),
    From ! {Tag, {Top, Module}},
    State#state{requests = Rest};
handle_msg(_, State) ->
    State.

terminate(Reason, #state{child = Child, requests = Requests}) ->
    _ = [From ! {Tag, error} || {_, Tag, From} <- Requests],
    stop_child(Child),
    kill_group(),
    exit(Reason).

stop_child(undefined) ->
    ok;
stop_child(Child) ->
    Child ! {self(), terminate},
    stop_loop(Child).

stop_loop(Child) ->
    receive
        {io_request, _, _, _} = Request ->
            relay(Request),
            stop_loop(Child);
        {'EXIT', Child, _} ->
            ok;
        _ ->
            stop_loop(Child)
    end.

%% Kills every process this master still leads, until none is left.
kill_group() ->
    Self = self(),
    Led = [P || P <- processes(), P =/= Self, process_info(P, group_leader) =:= {group_leader, Self}],
    case Led of
        [] ->
            ok;
        _ ->
            _ = [exit(P, kill) || P <- Led],
            kill_group()
    end.

set_timer(infinity) ->
    ok;
set_timer(Time) ->
    {ok, _} = timer:exit_after(Time, timeout),
    ok.

%% The starting process: Mod:start/2 and the start phases, then it holds the top supervisor until it ends.
start_it(Tag, #state{data = Data}, From, Type) ->
    process_flag(trap_exit, true),
    #{name := Name, mod := {Module, Args}, phases := Phases} = Data,
    case catch start_app(Type, Module, Args, Phases, Name) of
        {ok, Top, AppState} ->
            From ! {Tag, {ok, self()}},
            hold(From, Top, Module, AppState);
        Error ->
            From ! {Tag, Error}
    end.

start_app(Type, Module, Args, Phases, Name) ->
    case start_top(Type, Module, Args) of
        {ok, Top, AppState} when Phases =:= undefined ->
            link(Top),
            {ok, Top, AppState};
        {ok, Top, AppState} ->
            link(Top),
            case run_phases(Phases, Type, [Name]) of
                ok ->
                    {ok, Top, AppState};
                Error ->
                    unlink(Top),
                    Error
            end;
        Error ->
            Error
    end.

start_top(Type, Module, Args) ->
    case catch Module:start(Type, Args) of
        {ok, Pid} -> {ok, Pid, []};
        {ok, Pid, AppState} -> {ok, Pid, AppState};
        {error, Reason} -> {error, {Reason, {Module, start, [Type, Args]}}};
        {'EXIT', normal} -> {error, {{'EXIT', normal}, {Module, start, [Type, Args]}}};
        Other -> {error, {bad_return, {{Module, start, [Type, Args]}, Other}}}
    end.

%% Each phase in order for the application and, under application_starter, its included applications.
run_phases([], _, _) ->
    ok;
run_phases([{Phase, _} | Rest], Type, Apps) ->
    case run_apps(Phase, Type, Apps) of
        {error, _} = Error -> Error;
        _ -> run_phases(Rest, Type, Apps)
    end.

run_apps(_, _, []) ->
    ok;
run_apps(Phase, Type, [App | Apps]) ->
    case catch run_phase(Phase, Type, App) of
        {error, _} = Error -> Error;
        _ -> run_apps(Phase, Type, Apps)
    end.

run_phase(Phase, Type, App) ->
    case application:get_key(App, mod) of
        {ok, {application_starter, [Module, _]}} ->
            run_the_phase(Phase, Type, App, Module),
            {ok, Included} = application:get_key(App, included_applications),
            run_apps(Phase, Type, Included);
        {ok, {Module, _}} ->
            run_the_phase(Phase, Type, App, Module)
    end.

run_the_phase(Phase, Type, App, Module) ->
    Phases =
        case application_controller:get_key(App, start_phases) of
            {ok, undefined} -> throw({error, {start_phases_undefined, App}});
            {ok, Defined} -> Defined
        end,
    case lists:keyfind(Phase, 1, Phases) of
        false ->
            ok;
        {Phase, Args} ->
            case catch Module:start_phase(Phase, Type, Args) of
                ok -> ok;
                {error, Reason} -> throw({error, {Reason, {Module, start_phase, [Phase, Type, Args]}}});
                Other -> throw({error, {bad_return_value, {{Module, start_phase, [Phase, Type, Args]}, Other}}})
            end
    end.

hold(Master, Top, Module, AppState) ->
    receive
        {Master, get_child, Ref} ->
            Master ! {child, Ref, Top, Module},
            hold(Master, Top, Module, AppState);
        {Master, terminate} ->
            NewState = prep_stop(Module, AppState),
            exit(Top, shutdown),
            receive
                {'EXIT', Top, _} -> ok
            end,
            catch Module:stop(NewState),
            exit(normal);
        {'EXIT', Master, Reason} ->
            NewState = prep_stop(Module, AppState),
            exit(Top, Reason),
            receive
                {'EXIT', Top, _} -> ok
            end,
            catch Module:stop(NewState),
            exit(Reason);
        {'EXIT', Top, Reason} ->
            catch Module:stop(prep_stop(Module, AppState)),
            exit(Reason);
        _ ->
            hold(Master, Top, Module, AppState)
    end.

%% prep_stop/1 is optional; a crash in it is reported and the state kept.
prep_stop(Module, AppState) ->
    case erlang:function_exported(Module, prep_stop, 1) of
        false ->
            AppState;
        true ->
            case catch Module:prep_stop(AppState) of
                {'EXIT', Reason} ->
                    error_logger:error_report([
                        {application_master, shutdown_error}, {Module, {prep_stop, [AppState]}}, {error_info, Reason}
                    ]),
                    AppState;
                NewState ->
                    NewState
            end
    end.
