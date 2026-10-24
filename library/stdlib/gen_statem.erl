%% Clause's gen_statem (plan 21 step LIB21, docs/library.md): the generic state machine over gen, proc_lib and sys.
%% Events are queued and handled by state functions or handle_event/4, with OTP's transition actions: replies,
%% postponing (retried after a state change), inserted events, state enter calls, event, state and generic
%% time-outs (time-out zero events queued after the inserted ones), hibernation and callback module changes. Errors,
%% reports and system messages follow OTP.
-module(gen_statem).
-export([
    call/2,
    call/3,
    cast/2,
    check_response/2,
    check_response/3,
    enter_loop/4,
    enter_loop/5,
    enter_loop/6,
    format_log/1,
    format_log/2,
    format_status/2,
    init_it/6,
    receive_response/1,
    receive_response/2,
    receive_response/3,
    reply/1,
    reply/2,
    reqids_add/3,
    reqids_new/0,
    reqids_size/1,
    reqids_to_list/1,
    send_request/2,
    send_request/4,
    start/3,
    start/4,
    start_link/3,
    start_link/4,
    start_monitor/3,
    start_monitor/4,
    stop/1,
    stop/3,
    system_code_change/4,
    system_continue/3,
    system_get_state/1,
    system_replace_state/2,
    system_terminate/4,
    wait_response/1,
    wait_response/2,
    wait_response/3
]).

-include_lib("kernel/include/logger.hrl").
-include("gen_statem.types.hrl").

-behaviour(sys).

-callback init(Args :: term()) -> init_result(state()).
-callback callback_mode() -> callback_mode_result().
-callback 'StateName'
    (enter, OldStateName :: state_name(), data()) -> state_enter_result(state_name());
    (EventType :: event_type(), EventContent :: event_content(), Data :: data()) ->
        event_handler_result(state_name()).
-callback handle_event
    (enter, OldState, CurrentState, Data) -> state_enter_result(CurrentState) when
        OldState :: state(), CurrentState :: state(), Data :: data();
    (EventType, EventContent, CurrentState, Data) -> event_handler_result(state()) when
        EventType :: event_type(),
        EventContent :: event_content(),
        CurrentState :: state(),
        Data :: data().
-callback terminate(
    Reason :: normal | shutdown | {shutdown, term()} | term(), CurrentState :: state(), data()
) ->
    any().
-callback code_change(
    OldVsn :: term() | {down, term()}, OldState :: state(), OldData :: data(), Extra :: term()
) ->
    {ok, NewState :: state(), NewData :: data()} | (Reason :: term()).
-callback format_status(StatusOption, [[{Key :: term(), Value :: term()}] | state() | data()]) ->
    Status :: term()
when
    StatusOption :: normal | terminate.
-callback format_status(Status) -> NewStatus when
    Status :: format_status(), NewStatus :: format_status().
-optional_callbacks([
    format_status/1, format_status/2, terminate/3, code_change/4, 'StateName'/3, handle_event/4
]).

-define(RELATIVE(T), ((is_integer(T) andalso T >= 0) orelse T =:= infinity)).
-define(ABSOLUTE(T), (is_integer(T) orelse T =:= infinity)).

%% Created for LIB21: what stays fixed between events. `callback_mode` is state_functions or
%% handle_event_function; `modules` is the callback module stack, the active module first.
-record(params, {
    callback_mode = state_functions,
    state_enter = false,
    parent,
    modules = [?MODULE],
    name,
    hibernate_after = infinity
}).

%% Created for LIB21: the machine between events. `timers` maps a running time-out's type to {TimerRef, Msg} and a
%% cancelled timer whose message may still arrive to []; `t0q` lists time-out zero events, {Type, Ref}, to deliver
%% once the queue is empty.
-record(state, {
    state_data = {undefined, undefined},
    postponed = [],
    timers = #{},
    t0q = [],
    hibernate = false
}).

%% Created for LIB21: what the actions of one callback gather before the transition: inserted events and time-out
%% actions (both latest first), the hibernate and postpone options, and whether the callback handled an event
%% (state_call) or was a state enter call.
-record(step, {
    next_events = [], hibernate = false, timeouts = [], postpone = false, state_call = true
}).

%% Starting and stopping.

start(Module, Args, Options) when is_atom(Module), is_list(Options) ->
    gen:start(?MODULE, nolink, Module, Args, Options);
start(Module, Args, Options) ->
    error(badarg, [Module, Args, Options]).

start(Name, Module, Args, Options) when is_tuple(Name), is_atom(Module), is_list(Options) ->
    gen:start(?MODULE, nolink, Name, Module, Args, Options);
start(Name, Module, Args, Options) ->
    error(badarg, [Name, Module, Args, Options]).

start_link(Module, Args, Options) when is_atom(Module), is_list(Options) ->
    gen:start(?MODULE, link, Module, Args, Options);
start_link(Module, Args, Options) ->
    error(badarg, [Module, Args, Options]).

start_link(Name, Module, Args, Options) when is_tuple(Name), is_atom(Module), is_list(Options) ->
    gen:start(?MODULE, link, Name, Module, Args, Options);
start_link(Name, Module, Args, Options) ->
    error(badarg, [Name, Module, Args, Options]).

start_monitor(Module, Args, Options) when is_atom(Module), is_list(Options) ->
    gen:start(?MODULE, monitor, Module, Args, Options);
start_monitor(Module, Args, Options) ->
    error(badarg, [Module, Args, Options]).

start_monitor(Name, Module, Args, Options) when is_tuple(Name), is_atom(Module), is_list(Options) ->
    gen:start(?MODULE, monitor, Name, Module, Args, Options);
start_monitor(Name, Module, Args, Options) ->
    error(badarg, [Name, Module, Args, Options]).

stop(Server) -> gen:stop(Server).

stop(Server, Reason, Timeout) -> gen:stop(Server, Reason, Timeout).

%% Requests.

cast(Server, Msg) when is_pid(Server); is_atom(Server) ->
    send(Server, {'$gen_cast', Msg});
cast({global, Name}, Msg) ->
    try (global_registry()):send(Name, {'$gen_cast', Msg}) of
        _ -> ok
    catch
        _:_ -> ok
    end;
cast({via, Module, Name}, Msg) ->
    try Module:send(Name, {'$gen_cast', Msg}) of
        _ -> ok
    catch
        _:_ -> ok
    end;
cast({Name, Node} = Server, Msg) when is_atom(Name), is_atom(Node) ->
    send(Server, {'$gen_cast', Msg}).

%% Created for LIB21: global until LIB25, reached by a runtime name (gen.erl does the same).
global_registry() -> global.

send(Dest, Msg) ->
    try
        erlang:send(Dest, Msg)
    catch
        error:_ -> ok
    end,
    ok.

call(Server, Request) -> call(Server, Request, infinity).

%% {dirty_timeout, T} and {clean_timeout, T} both wait T (aliases make every timeout clean).
call(Server, Request, infinity) -> call(Server, Request, infinity, infinity);
call(Server, Request, {dirty_timeout, T} = Timeout) -> call(Server, Request, Timeout, T);
call(Server, Request, {clean_timeout, T} = Timeout) -> call(Server, Request, Timeout, T);
call(Server, Request, {_, _} = Timeout) -> error(badarg, [Server, Request, Timeout]);
call(Server, Request, Timeout) -> call(Server, Request, Timeout, Timeout).

call(Server, Request, Timeout, T) ->
    try gen:call(Server, '$gen_call', Request, T) of
        {ok, Reply} -> Reply
    catch
        exit:Reason:Stack ->
            erlang:raise(exit, {Reason, {?MODULE, call, [Server, Request, Timeout]}}, Stack)
    end.

send_request(Server, Request) ->
    badarg_as([Server, Request], fun() -> gen:send_request(Server, '$gen_call', Request) end).

send_request(Server, Request, Label, Collection) ->
    badarg_as([Server, Request, Label, Collection], fun() ->
        gen:send_request(Server, '$gen_call', Request, Label, Collection)
    end).

wait_response(Ref) -> wait_response(Ref, infinity).

wait_response(Ref, Timeout) ->
    badarg_as([Ref, Timeout], fun() -> gen:wait_response(Ref, Timeout) end).

wait_response(Collection, Timeout, Delete) ->
    badarg_as([Collection, Timeout, Delete], fun() ->
        gen:wait_response(Collection, Timeout, Delete)
    end).

receive_response(Ref) -> receive_response(Ref, infinity).

receive_response(Ref, Timeout) ->
    badarg_as([Ref, Timeout], fun() -> gen:receive_response(Ref, Timeout) end).

receive_response(Collection, Timeout, Delete) ->
    badarg_as([Collection, Timeout, Delete], fun() ->
        gen:receive_response(Collection, Timeout, Delete)
    end).

check_response(Msg, Ref) -> badarg_as([Msg, Ref], fun() -> gen:check_response(Msg, Ref) end).

check_response(Msg, Collection, Delete) ->
    badarg_as([Msg, Collection, Delete], fun() -> gen:check_response(Msg, Collection, Delete) end).

reqids_new() -> gen:reqids_new().

reqids_size(Collection) -> badarg_as([Collection], fun() -> gen:reqids_size(Collection) end).

reqids_add(Ref, Label, Collection) ->
    badarg_as([Ref, Label, Collection], fun() -> gen:reqids_add(Ref, Label, Collection) end).

reqids_to_list(Collection) -> badarg_as([Collection], fun() -> gen:reqids_to_list(Collection) end).

badarg_as(Args, Fun) ->
    try
        Fun()
    catch
        error:badarg -> error(badarg, Args)
    end.

reply({reply, From, Reply}) ->
    reply(From, Reply);
reply(Replies) when is_list(Replies) ->
    lists:foreach(fun({reply, From, Reply}) -> reply(From, Reply) end, Replies).

reply(From, Reply) -> gen:reply(From, Reply).

%% Turning the caller into a state machine.

enter_loop(Module, Options, State, Data) -> enter_loop(Module, Options, State, Data, self()).

enter_loop(Module, Options, State, Data, ServerOrActions) when is_list(ServerOrActions) ->
    enter_loop(Module, Options, State, Data, self(), ServerOrActions);
enter_loop(Module, Options, State, Data, Server) ->
    enter_loop(Module, Options, State, Data, Server, []).

enter_loop(Module, Options, State, Data, Server, Actions) ->
    is_atom(Module) orelse error({atom, Module}),
    Parent = gen:get_parent(),
    Name = gen:get_proc_name(Server),
    Debug = gen:debug_options(Name, Options),
    enter(Parent, Debug, Module, Name, gen:hibernate_after(Options), {State, Data}, Actions).

init_it(Starter, self, Server, Module, Args, Options) ->
    init_it(Starter, self(), Server, Module, Args, Options);
init_it(Starter, Parent, Server, Module, Args, Options) ->
    Name = gen:get_proc_name(Server),
    Debug = gen:debug_options(Name, Options),
    Idle = gen:hibernate_after(Options),
    P = #params{parent = Parent, name = Name, modules = [Module]},
    try Module:init(Args) of
        Result -> init_result(Starter, Server, Module, Result, {Debug, Idle, P})
    catch
        throw:Result ->
            init_result(Starter, Server, Module, Result, {Debug, Idle, P});
        Class:Reason:Stack ->
            gen:unregister_name(Server),
            error_info(Class, Reason, Stack, Debug, P, #state{}, []),
            proc_lib:init_fail(Starter, {error, Reason}, {Class, Reason, Stack})
    end.

init_result(
    Starter, Server, Module, Result, {Debug, Idle, #params{parent = Parent, name = Name} = P}
) ->
    case Result of
        {ok, State, Data} ->
            proc_lib:init_ack(Starter, {ok, self()}),
            enter(Parent, Debug, Module, Name, Idle, {State, Data}, []);
        {ok, State, Data, Actions} ->
            proc_lib:init_ack(Starter, {ok, self()}),
            enter(Parent, Debug, Module, Name, Idle, {State, Data}, Actions);
        {stop, Reason} ->
            gen:unregister_name(Server),
            exit(Reason);
        {error, _} = Error ->
            gen:unregister_name(Server),
            proc_lib:init_fail(Starter, Error, {exit, normal});
        ignore ->
            gen:unregister_name(Server),
            proc_lib:init_fail(Starter, ignore, {exit, normal});
        _ ->
            gen:unregister_name(Server),
            Reason = {bad_return_from_init, Result},
            error_info(error, Reason, [], Debug, P, #state{}, []),
            exit(Reason)
    end.

%% Starts the machine on a fake {internal, init_state} event that the forced {postpone, false} discards, so the
%% initial actions (and a state enter call) run as after a transition into the first state.
enter(Parent, Debug, Module, Name, Idle, {State, _} = StateData, Actions) ->
    P = #params{parent = Parent, name = Name, hibernate_after = Idle},
    S = #state{state_data = StateData},
    Q = [{internal, init_state}],
    case callback_mode(P, [Module]) of
        #params{} = P1 ->
            Debug1 = debug(Debug, Name, {enter, Module, State}),
            actions(
                P1, Debug1, S, Q, StateData, #step{}, true, listify(Actions) ++ [{postpone, false}]
            );
        {Class, Reason, Stack} ->
            terminate(Class, Reason, Stack, P#params{modules = [Module]}, Debug, S, Q)
    end.

%% Receiving.

loop(P, Debug, #state{hibernate = true} = S) ->
    receive
        Msg ->
            %% A message is already here: hibernating would end at once.
            garbage_collect(),
            message(P, Debug, S, Msg)
    after 0 -> hibernate(P, Debug, S)
    end;
loop(#params{hibernate_after = Idle} = P, Debug, S) ->
    receive
        Msg -> message(P, Debug, S, Msg)
    after Idle -> hibernate(P, Debug, S)
    end.

hibernate(P, Debug, S) ->
    erlang:hibernate(),
    receive
        Msg -> message(P, Debug, S, Msg)
    end.

message(P, Debug, #state{timers = Timers} = S, Msg) ->
    case Msg of
        {'$gen_call', From, Request} ->
            external_event(P, Debug, S, {{call, From}, Request});
        {'$gen_cast', Cast} ->
            external_event(P, Debug, S, {cast, Cast});
        {timeout, TRef, Type} when is_reference(TRef) ->
            case Timers of
                #{Type := {TRef, Content}} ->
                    external_event(
                        P,
                        Debug,
                        S#state{timers = maps:remove(Type, Timers)},
                        {Type, Content}
                    );
                #{TRef := _} ->
                    loop(P, Debug, S#state{timers = maps:remove(TRef, Timers)});
                #{} ->
                    external_event(P, Debug, S, {info, Msg})
            end;
        {system, From, Request} ->
            sys:handle_system_msg(
                Request, From, P#params.parent, ?MODULE, Debug, {P, S}, S#state.hibernate
            );
        {'EXIT', Parent, Reason} when Parent =:= P#params.parent ->
            terminate(exit, Reason, [], P, Debug, S, []);
        _ ->
            external_event(P, Debug, S, {info, Msg})
    end.

external_event(P, Debug, S, Event) -> queued_event(P, Debug, S, Event, false).

queued_event(
    #params{name = Name} = P, Debug, #state{state_data = {State, _}} = S, Event, Hibernate
) ->
    event(P, debug(Debug, Name, {in, Event, State}), S, [Event], Hibernate).

%% Handles the first event of the queue Q. A hibernate option left by the previous transition is honoured as a
%% garbage collection, since there are events to handle.
event(P, Debug, #state{state_data = StateData} = S, [Event | _] = Q, Hibernate) ->
    Hibernate andalso garbage_collect(),
    state_callback(P, Debug, S, Q, StateData, #step{hibernate = Hibernate}, Event).

%% Calls the state function or handle_event/4 for an event, or for a state enter call ({enter, OldState}).
state_callback(
    #params{callback_mode = Mode, modules = [Module | _]} = P,
    Debug,
    S,
    Q,
    {State, Data} = StateData,
    Step,
    {Type, Content}
) ->
    try
        case Mode of
            state_functions -> Module:State(Type, Content, Data);
            handle_event_function -> Module:handle_event(Type, Content, State, Data)
        end
    of
        Result -> callback_result(P, Debug, S, Q, StateData, Step, Result)
    catch
        throw:Result -> callback_result(P, Debug, S, Q, StateData, Step, Result);
        Class:Reason:Stack -> terminate(Class, Reason, Stack, P, Debug, S, Q)
    end.

callback_result(
    P, Debug, S, Q, {State, _} = StateData, #step{state_call = StateCall} = Step, Result
) ->
    case Result of
        {next_state, State, NewData} ->
            actions(P, Debug, S, Q, {State, NewData}, Step, false, []);
        {next_state, Next, NewData} when StateCall ->
            actions(P, Debug, S, Q, {Next, NewData}, Step, true, []);
        {next_state, State, NewData, Actions} ->
            actions(P, Debug, S, Q, {State, NewData}, Step, false, listify(Actions));
        {next_state, Next, NewData, Actions} when StateCall ->
            actions(P, Debug, S, Q, {Next, NewData}, Step, true, listify(Actions));
        {next_state, _, _} ->
            bad(
                P,
                Debug,
                S,
                Q,
                StateData,
                Step,
                {bad_state_enter_return_from_state_function, Result}
            );
        {next_state, _, _, _} ->
            bad(
                P,
                Debug,
                S,
                Q,
                StateData,
                Step,
                {bad_state_enter_return_from_state_function, Result}
            );
        {keep_state, NewData} ->
            actions(P, Debug, S, Q, {State, NewData}, Step, false, []);
        {keep_state, NewData, Actions} ->
            actions(P, Debug, S, Q, {State, NewData}, Step, false, listify(Actions));
        keep_state_and_data ->
            actions(P, Debug, S, Q, StateData, Step, false, []);
        {keep_state_and_data, Actions} ->
            actions(P, Debug, S, Q, StateData, Step, false, listify(Actions));
        {repeat_state, NewData} ->
            actions(P, Debug, S, Q, {State, NewData}, Step, true, []);
        {repeat_state, NewData, Actions} ->
            actions(P, Debug, S, Q, {State, NewData}, Step, true, listify(Actions));
        repeat_state_and_data ->
            actions(P, Debug, S, Q, StateData, Step, true, []);
        {repeat_state_and_data, Actions} ->
            actions(P, Debug, S, Q, StateData, Step, true, listify(Actions));
        stop ->
            stopped(P, Debug, S, Q, StateData, Step, normal);
        {stop, Reason} ->
            stopped(P, Debug, S, Q, StateData, Step, Reason);
        {stop, Reason, NewData} ->
            stopped(P, Debug, S, Q, {State, NewData}, Step, Reason);
        {stop_and_reply, Reason, Replies} ->
            reply_then_terminate(Reason, P, Debug, with(S, StateData, Step), Q, listify(Replies));
        {stop_and_reply, Reason, Replies, NewData} ->
            reply_then_terminate(
                Reason, P, Debug, with(S, {State, NewData}, Step), Q, listify(Replies)
            );
        _ ->
            bad(P, Debug, S, Q, StateData, Step, {bad_return_from_state_function, Result})
    end.

with(S, StateData, #step{hibernate = Hibernate}) ->
    S#state{state_data = StateData, hibernate = Hibernate}.

stopped(P, Debug, S, Q, StateData, Step, Reason) ->
    terminate(exit, Reason, [], P, Debug, with(S, StateData, Step), Q).

bad(P, Debug, S, Q, StateData, Step, Reason) ->
    terminate(error, Reason, [], P, Debug, with(S, StateData, Step), Q).

%% Runs the actions of a callback in order; CallEnter says whether the transition calls state enter.
actions(P, Debug, S, Q, StateData, Step, CallEnter, []) ->
    case CallEnter andalso P#params.state_enter of
        true ->
            {PrevState, _} = S#state.state_data,
            state_callback(
                P, Debug, S, Q, StateData, Step#step{state_call = false}, {enter, PrevState}
            );
        false ->
            transition(P, Debug, S, Q, StateData, Step)
    end;
actions(P, Debug, S, Q, StateData, #step{state_call = StateCall} = Step, CallEnter, [
    Action | Actions
]) ->
    Next = fun(P1, Debug1, Step1) ->
        actions(P1, Debug1, S, Q, StateData, Step1, CallEnter, Actions)
    end,
    case Action of
        {reply, From, Reply} ->
            case is_from(From) of
                true ->
                    reply(From, Reply),
                    Next(P, debug(Debug, P#params.name, {out, Reply, From}), Step);
                false ->
                    bad(P, Debug, S, Q, StateData, Step, {bad_action_from_state_function, Action})
            end;
        {hibernate, Hibernate} when is_boolean(Hibernate) ->
            Next(P, Debug, Step#step{hibernate = Hibernate});
        hibernate ->
            Next(P, Debug, Step#step{hibernate = true});
        {postpone, Postpone} when not Postpone orelse StateCall ->
            Next(P, Debug, Step#step{postpone = Postpone});
        postpone when StateCall ->
            Next(P, Debug, Step#step{postpone = true});
        postpone ->
            bad(
                P,
                Debug,
                S,
                Q,
                StateData,
                Step,
                {bad_state_enter_action_from_state_function, Action}
            );
        {next_event, Type, Content} ->
            case is_event_type(Type) andalso StateCall of
                true ->
                    {NextState, _} = StateData,
                    Event = {Type, Content},
                    Debug1 = debug(Debug, P#params.name, {in, Event, NextState}),
                    Next(P, Debug1, Step#step{next_events = [Event | Step#step.next_events]});
                false ->
                    Tag =
                        case StateCall of
                            true -> bad_action_from_state_function;
                            false -> bad_state_enter_action_from_state_function
                        end,
                    bad(P, Debug, S, Q, StateData, Step, {Tag, Action})
            end;
        {Tag, Module} when
            (Tag =:= change_callback_module orelse Tag =:= push_callback_module), is_atom(Module)
        ->
            Modules =
                case Tag of
                    change_callback_module -> [Module | tl(P#params.modules)];
                    push_callback_module -> [Module | P#params.modules]
                end,
            module_action(P, Debug, {S, Q, StateData, Step}, Action, Modules, Next);
        pop_callback_module when tl(P#params.modules) =/= [] ->
            module_action(P, Debug, {S, Q, StateData, Step}, Action, tl(P#params.modules), Next);
        _ ->
            case timeout_action(Action) of
                {ok, Timeout} ->
                    Next(P, Debug, Step#step{timeouts = [Timeout | Step#step.timeouts]});
                error ->
                    bad(P, Debug, S, Q, StateData, Step, {bad_action_from_state_function, Action})
            end
    end.

%% Changing the callback module stack: allowed while handling an event, not in a state enter call.
module_action(P, Debug, {S, Q, StateData, #step{state_call = true} = Step}, _, Modules, Next) ->
    case callback_mode(P, Modules) of
        #params{} = P1 ->
            {NextState, _} = StateData,
            Next(P1, debug(Debug, P#params.name, {module, hd(Modules), NextState}), Step);
        {Class, Reason, Stack} ->
            terminate(Class, Reason, Stack, P, Debug, with(S, StateData, Step), Q)
    end;
module_action(P, Debug, {S, Q, StateData, Step}, Action, _, _) ->
    bad(P, Debug, S, Q, StateData, Step, {bad_state_enter_action_from_state_function, Action}).

%% A time-out action as {Type, Time, Msg} (relative, update or cancel as infinity) or {Type, Time, Msg, Options}
%% (absolute); error for anything else.
timeout_action({Type, Time, Msg, Options} = Action) ->
    case is_timeout_type(Type) andalso absolute(Options) of
        true when ?ABSOLUTE(Time) -> {ok, Action};
        false when ?RELATIVE(Time) -> {ok, {Type, Time, Msg}};
        _ -> error
    end;
timeout_action({Type, Time, _} = Action) ->
    case is_timeout_type(Type) andalso (?RELATIVE(Time) orelse Time =:= update) of
        true -> {ok, Action};
        false -> error
    end;
timeout_action({Type, cancel}) ->
    case is_timeout_type(Type) of
        true -> {ok, {Type, infinity, undefined}};
        false -> error
    end;
timeout_action(Time) when ?RELATIVE(Time) ->
    {ok, {timeout, Time, Time}};
timeout_action(_) ->
    error.

%% Whether time-out options ask for an absolute time; error for invalid options. The last {abs, _} wins.
absolute(Options) -> absolute(listify(Options), false).

absolute([], Abs) -> Abs;
absolute([{abs, Abs} | Options], _) when is_boolean(Abs) -> absolute(Options, Abs);
absolute(_, _) -> error.

is_timeout_type(timeout) -> true;
is_timeout_type(state_timeout) -> true;
is_timeout_type({timeout, _}) -> true;
is_timeout_type(_) -> false.

is_event_type({call, From}) ->
    is_from(From);
is_event_type(Type) ->
    Type =:= cast orelse Type =:= info orelse Type =:= internal orelse is_timeout_type(Type).

is_from({Pid, _}) -> is_pid(Pid);
is_from(_) -> false.

%% The transition: the current event is consumed or postponed; a new state retries the postponed events first and
%% cancels the state time-out; any transition cancels the event time-out.
transition(
    #params{name = Name} = P,
    Debug,
    #state{state_data = {State, _}, postponed = Postponed} = S,
    [Event | Events],
    {NextState, _} = StateData,
    #step{postpone = Postpone} = Step
) ->
    Postponed1 =
        case Postpone of
            true -> [Event | Postponed];
            false -> Postponed
        end,
    Debug1 =
        case Postpone of
            true -> debug(Debug, Name, {postpone, Event, State, NextState});
            false -> debug(Debug, Name, {consume, Event, State, NextState})
        end,
    case NextState =:= State of
        true ->
            Timers = cancel_timeout(timeout, S#state.timers),
            next_events(P, Debug1, S, Events, StateData, Step, Postponed1, Timers);
        false ->
            Timers = cancel_timeout(timeout, cancel_timeout(state_timeout, S#state.timers)),
            next_events(
                P, Debug1, S, lists:reverse(Postponed1, Events), StateData, Step, [], Timers
            )
    end.

%% Starts, restarts, updates and cancels the time-outs the actions asked for (the last action of each type wins),
%% then continues with the queue. Walking the actions latest first, time-out zero events gather in action order.
next_events(P, Debug, S, Events, StateData, #step{timeouts = Timeouts} = Step, Postponed, Timers) ->
    {Debug1, Timers1, ZeroEvents} = timeouts(P, Debug, StateData, Timeouts, {Timers, #{}, []}),
    S1 = S#state{
        state_data = StateData,
        postponed = Postponed,
        timers = Timers1,
        t0q = S#state.t0q ++ ZeroEvents,
        hibernate = Step#step.hibernate
    },
    done(P, Debug1, S1, lists:reverse(Step#step.next_events, Events)).

timeouts(_, Debug, _, [], {Timers, _, ZeroEvents}) ->
    {Debug, Timers, ZeroEvents};
timeouts(P, Debug, StateData, [Timeout | Rest], {Timers, Seen, ZeroEvents}) ->
    Type = element(1, Timeout),
    case is_map_key(Type, Seen) of
        true ->
            timeouts(P, Debug, StateData, Rest, {Timers, Seen, ZeroEvents});
        false ->
            {Debug1, Timers1, ZeroEvents1} = timeout(
                P, Debug, StateData, Timeout, {Timers, ZeroEvents}
            ),
            timeouts(P, Debug1, StateData, Rest, {Timers1, Seen#{Type => true}, ZeroEvents1})
    end.

timeout(P, Debug, StateData, Timeout, {Timers, ZeroEvents}) ->
    {Type, Time, Msg, Options} =
        case Timeout of
            {T, Tm, M} -> {T, Tm, M, []};
            {T, Tm, M, O} -> {T, Tm, M, listify(O)}
        end,
    case Time of
        infinity ->
            {Debug, cancel_timeout(Type, Timers), ZeroEvents};
        update ->
            case Timers of
                #{Type := {TRef, _}} ->
                    {Debug, Timers#{Type := {TRef, Msg}}, ZeroEvents};
                #{} ->
                    TRef = make_ref(),
                    {Debug, Timers#{Type => {TRef, Msg}}, [{Type, TRef} | ZeroEvents]}
            end;
        0 ->
            %% Time-out zero: a fake timer reference whose event is queued.
            TRef = make_ref(),
            start_timeout(
                P,
                Debug,
                StateData,
                {Type, Time, Msg, Options},
                TRef,
                {Timers, [{Type, TRef} | ZeroEvents]}
            );
        _ ->
            TRef = erlang:start_timer(Time, self(), Type, Options),
            start_timeout(
                P, Debug, StateData, {Type, Time, Msg, Options}, TRef, {Timers, ZeroEvents}
            )
    end.

start_timeout(P, Debug, {State, _}, {Type, _, Msg, _} = Action, TRef, {Timers, ZeroEvents}) ->
    Debug1 = debug(Debug, P#params.name, {start_timer, Action, State}),
    Timers1 =
        case Timers of
            #{Type := {OldRef, _}} -> forget_timer(OldRef, Timers#{Type := {TRef, Msg}});
            #{} -> Timers#{Type => {TRef, Msg}}
        end,
    {Debug1, Timers1, ZeroEvents}.

%% Cancels a running time-out of Type; a timer that already fired is remembered so its message is dropped.
cancel_timeout(Type, Timers) ->
    case Timers of
        #{Type := {TRef, _}} -> forget_timer(TRef, maps:remove(Type, Timers));
        #{} -> Timers
    end.

forget_timer(TRef, Timers) ->
    case erlang:cancel_timer(TRef) of
        false -> Timers#{TRef => []};
        _ -> Timers
    end.

%% Continues with the queued events, then the time-out zero events, then the mailbox.
done(P, Debug, S, []) ->
    case S#state.t0q of
        [] -> loop(P, Debug, S);
        T0Q -> zero_timeouts(P, Debug, S, S#state.timers, T0Q)
    end;
done(P, Debug, S, Q) ->
    event(P, Debug, S, Q, S#state.hibernate).

zero_timeouts(P, Debug, S, Timers, []) ->
    loop(P, Debug, S#state{timers = Timers, t0q = []});
zero_timeouts(P, Debug, S, Timers, [{Type, TRef} | T0Q]) ->
    case Timers of
        #{Type := {TRef, Msg}} ->
            queued_event(
                P,
                Debug,
                S#state{timers = maps:remove(Type, Timers), t0q = T0Q},
                {Type, Msg},
                S#state.hibernate
            );
        #{TRef := _} ->
            zero_timeouts(P, Debug, S, maps:remove(TRef, Timers), T0Q)
    end.

%% The callback mode: #params{} with the modules, or {Class, Reason, Stack} when callback_mode/0 fails or gives an
%% invalid result.
callback_mode(P, [Module | _] = Modules) ->
    try Module:callback_mode() of
        Result -> mode_result(P, Modules, Result)
    catch
        throw:Result -> mode_result(P, Modules, Result);
        Class:Reason:Stack -> {Class, Reason, Stack}
    end.

mode_result(P, Modules, Result) ->
    Items = listify(Result),
    Modes = [Item || Item <- Items, Item =:= state_functions orelse Item =:= handle_event_function],
    Valid = lists:all(
        fun(Item) -> lists:member(Item, [state_functions, handle_event_function, state_enter]) end,
        Items
    ),
    case Valid andalso Modes =/= [] of
        true ->
            P#params{
                modules = Modules,
                callback_mode = lists:last(Modes),
                state_enter = lists:member(
                    state_enter,
                    Items
                )
            };
        false ->
            {error, {bad_return_from_callback_mode, Result}, []}
    end.

reply_then_terminate(Reason, P, Debug, S, Q, []) ->
    terminate(exit, Reason, [], P, Debug, S, Q);
reply_then_terminate(Reason, P, Debug, S, Q, [{reply, From, Reply} = R | Rs]) ->
    case is_from(From) of
        true ->
            reply(From, Reply),
            reply_then_terminate(
                Reason, P, debug(Debug, P#params.name, {out, Reply, From}), S, Q, Rs
            );
        false ->
            terminate(error, {bad_reply_action_from_state_function, R}, [], P, Debug, S, Q)
    end;
reply_then_terminate(_, P, Debug, S, Q, [R | _]) ->
    terminate(error, {bad_reply_action_from_state_function, R}, [], P, Debug, S, Q).

%% Termination: terminate/3 of the module, then an error report unless the reason is normal, shutdown or
%% {shutdown, _}.
terminate(
    Class,
    Reason,
    Stack,
    #params{modules = [Module | _]} = P,
    Debug,
    #state{state_data = {State, Data}} = S,
    Q
) ->
    case erlang:function_exported(Module, terminate, 3) of
        true ->
            try Module:terminate(Reason, State, Data) of
                _ -> ok
            catch
                throw:_ ->
                    ok;
                C:R:ST ->
                    error_info(C, R, ST, Debug, P, S, Q),
                    erlang:raise(C, R, ST)
            end;
        false ->
            ok
    end,
    _ =
        case Reason of
            normal -> debug(Debug, P#params.name, {terminate, Reason, State});
            shutdown -> debug(Debug, P#params.name, {terminate, Reason, State});
            {shutdown, _} -> debug(Debug, P#params.name, {terminate, Reason, State});
            _ -> error_info(Class, Reason, Stack, Debug, P, S, Q)
        end,
    case Stack of
        [] -> erlang:Class(Reason);
        _ -> erlang:raise(Class, Reason, Stack)
    end.

error_info(
    Class,
    Reason,
    Stack,
    Debug,
    #params{name = Name, modules = [Module | _] = Modules} = P,
    #state{postponed = Postponed, timers = Timers, state_data = {State, Data}},
    Q
) ->
    Timeouts = list_timeouts(Timers),
    Status = gen:format_status(
        Module,
        terminate,
        #{
            reason => Reason,
            state => State,
            data => Data,
            queue => Q,
            postponed => Postponed,
            timeouts => Timeouts,
            log => sys:get_log(Debug)
        },
        [get(), State, Data]
    ),
    Shown =
        case maps:find('$status', Status) of
            error -> {maps:get(state, Status), maps:get(data, Status)};
            {ok, Custom} -> Custom
        end,
    ?LOG(
        error,
        #{
            label => {gen_statem, terminate},
            name => Name,
            queue => maps:get(queue, Status),
            postponed => maps:get(postponed, Status),
            modules => Modules,
            callback_mode => P#params.callback_mode,
            state_enter => P#params.state_enter,
            state => Shown,
            timeouts => {length(Timeouts), maps:get(timeouts, Status)},
            log => maps:get(log, Status),
            reason => {Class, maps:get(reason, Status), Stack},
            client_info => client_info(Q),
            process_label => proc_lib:get_label(self())
        },
        #{
            domain => [otp],
            report_cb => fun gen_statem:format_log/2,
            error_logger => #{tag => error, report_cb => fun gen_statem:format_log/1}
        }
    ).

%% The caller of the event being handled, when it was a call: its name or pid and its stack (Clause keeps none).
client_info([{{call, {Pid, _}}, _} | _]) when is_pid(Pid), node(Pid) =:= node() ->
    case process_info(Pid, registered_name) of
        undefined -> {Pid, dead};
        {registered_name, Name} -> {Pid, {Name, []}};
        [] -> {Pid, {Pid, []}}
    end;
client_info([{{call, {Pid, _}}, _} | _]) when is_pid(Pid) ->
    {Pid, remote};
client_info(_) ->
    undefined.

list_timeouts(Timers) -> [{Type, Msg} || Type := {_, Msg} <- Timers].

listify(Items) when is_list(Items) -> Items;
listify(Item) -> [Item].

debug([], _, _) -> [];
debug(Debug, Name, Event) -> sys:handle_debug(Debug, fun print_event/3, Name, Event).

%% sys callbacks; Misc is {P, S}.

system_continue(Parent, Debug, {P, S}) -> loop(P#params{parent = Parent}, Debug, S).

system_terminate(Reason, Parent, Debug, {P, S}) ->
    terminate(exit, Reason, [], P#params{parent = Parent}, Debug, S, []).

system_code_change(
    {#params{modules = [Module | _] = Modules} = P, #state{state_data = {State, Data}} = S},
    _,
    OldVsn,
    Extra
) ->
    Result =
        try
            Module:code_change(OldVsn, State, Data, Extra)
        catch
            throw:Thrown -> Thrown
        end,
    case Result of
        {ok, NewState, NewData} ->
            case callback_mode(P, Modules) of
                #params{} = P1 -> {ok, {P1, S#state{state_data = {NewState, NewData}}}};
                {Class, Reason, Stack} -> erlang:raise(Class, Reason, Stack)
            end;
        {ok, _} = Error ->
            error({case_clause, Error});
        Error ->
            Error
    end.

system_get_state({_, #state{state_data = StateData}}) -> {ok, StateData}.

system_replace_state(StateFun, {P, #state{state_data = StateData} = S}) ->
    New = StateFun(StateData),
    {ok, New, {P, S#state{state_data = New}}}.

format_status(Opt, [
    Dictionary,
    SysState,
    Parent,
    Debug,
    {#params{name = Name, modules = [Module | _] = Modules}, #state{
        postponed = Postponed, timers = Timers, state_data = {State, Data}
    }}
]) ->
    Header = gen:format_status_header("Status for state machine", Name),
    Timeouts = list_timeouts(Timers),
    Base = #{
        state => State,
        data => Data,
        postponed => Postponed,
        log => sys:get_log(Debug),
        timeouts => Timeouts
    },
    Status =
        case gen:format_status(Module, Opt, Base, [Dictionary, State, Data]) of
            #{'EXIT' := R} -> Base#{'$status' => [{data, [{"State", {State, R}}]}]};
            #{'$status' := L} = M when is_list(L) -> M;
            #{'$status' := T} = M -> M#{'$status' := [T]};
            #{state := St, data := D} = M -> M#{'$status' => [{data, [{"State", {St, D}}]}]}
        end,
    [
        {header, Header},
        {data, [
            {"Status", SysState},
            {"Parent", Parent},
            {"Modules", Modules},
            {"Time-outs", {length(Timeouts), maps:get(timeouts, Status)}},
            {"Logged Events", maps:get(log, Status)},
            {"Postponed", maps:get(postponed, Status)}
        ]}
        | maps:get('$status', Status)
    ].

print_event(Dev, Event, Name) ->
    case Event of
        {in, In, State} ->
            io:format(Dev, "*DBG* ~tp receive ~ts in state ~tp~n", [Name, event_string(In), State]);
        {out, Reply, {To, _}} ->
            io:format(Dev, "*DBG* ~tp send ~tp to ~tw~n", [Name, Reply, To]);
        {enter, Module, State} ->
            io:format(Dev, "*DBG* ~tp enter ~tp in state ~tp~n", [Name, Module, State]);
        {module, Module, State} ->
            io:format(Dev, "*DBG* ~tp module ~tp in state ~tp~n", [Name, Module, State]);
        {start_timer, Action, State} ->
            io:format(Dev, "*DBG* ~tp start_timer ~tp in state ~tp~n", [Name, Action, State]);
        {insert_timeout, In, State} ->
            io:format(Dev, "*DBG* ~tp insert_timeout ~tp in state ~tp~n", [Name, In, State]);
        {terminate, Reason, State} ->
            io:format(Dev, "*DBG* ~tp terminate ~tp in state ~tp~n", [Name, Reason, State]);
        {Tag, In, State, NextState} when Tag =:= postpone; Tag =:= consume ->
            States =
                case NextState of
                    State -> io_lib:format("~tp", [State]);
                    _ -> io_lib:format("~tp => ~tp", [State, NextState])
                end,
            io:format(Dev, "*DBG* ~tp ~tw ~ts in state ~ts~n", [Name, Tag, event_string(In), States])
    end.

event_string({{call, {Pid, _}}, Request}) -> io_lib:format("call ~tp from ~tw", [Request, Pid]);
event_string({Type, Content}) -> io_lib:format("~tw ~tp", [Type, Content]).

%% Report text.

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
    #{
        label := {gen_statem, terminate},
        name := Name,
        queue := Q,
        state := Shown,
        log := Log,
        reason := {Class, Reason, Stack},
        client_info := Client,
        process_label := Label
    },
    #{single_line := true, depth := Depth} = Options
) ->
    P = p(Options),
    {FixedReason, FixedStack} = fix_reason(Class, Reason, Stack),
    {ClientFormat, ClientArgs} = client_log_single(Client, P, Depth),
    Format =
        lists:append([
            "State machine ",
            P,
            " terminating",
            labelled(Label, ". Label: " ++ P),
            ". Reason: ",
            P,
            unless_empty(FixedStack, ". Stack: " ++ P),
            unless_empty(Q, ". Last event: " ++ P),
            ". State: ",
            P,
            unless_empty(Log, ". Log: " ++ P),
            "."
        ]),
    Args =
        [Name] ++ labelled(Label, [Label]) ++ [FixedReason] ++
            unless_empty(FixedStack, [FixedStack]) ++
            unless_empty(Q, [hd_or(Q)]) ++ [Shown] ++ unless_empty(Log, [Log]),
    {Format ++ ClientFormat, depth_args(Depth, Args) ++ ClientArgs};
format_log_single(Report, Options) ->
    format_log_multi(Report, Options).

format_log_multi(
    #{
        label := {gen_statem, terminate},
        name := Name,
        queue := Q,
        postponed := Postponed,
        modules := Modules,
        callback_mode := Mode,
        state_enter := StateEnter,
        state := Shown,
        timeouts := Timeouts,
        log := Log,
        reason := {Class, Reason, Stack},
        client_info := Client,
        process_label := Label
    },
    #{depth := Depth} = Options
) ->
    P = p(Options),
    {FixedReason, FixedStack} = fix_reason(Class, Reason, Stack),
    {ClientFormat, ClientArgs} = client_log(Client, P, Depth),
    ModeShown =
        case StateEnter of
            true -> [Mode, state_enter];
            false -> Mode
        end,
    Queued =
        case Q of
            [_, _ | _] -> true;
            _ -> false
        end,
    Format =
        lists:append([
            "** State machine ",
            P,
            " terminating~n",
            labelled(Label, "** Process label = " ++ P ++ "~n"),
            unless_empty(Q, "** Last event = " ++ P ++ "~n"),
            "** When server state  = ",
            P,
            "~n",
            "** Reason for termination = ",
            P,
            ":",
            P,
            "~n",
            "** Callback modules = ",
            P,
            "~n",
            "** Callback mode = ",
            P,
            "~n",
            when_true(Queued, "** Queued = " ++ P ++ "~n"),
            unless_empty(Postponed, "** Postponed = " ++ P ++ "~n"),
            unless_empty(FixedStack, "** Stacktrace =~n**  " ++ P ++ "~n"),
            when_true(element(1, Timeouts) =/= 0, "** Time-outs: " ++ P ++ "~n"),
            unless_empty(Log, "** Log =~n**  " ++ P ++ "~n")
        ]),
    Args =
        [Name] ++ labelled(Label, [Label]) ++ unless_empty(Q, [hd_or(Q)]) ++
            [Shown, Class, FixedReason, Modules, ModeShown] ++ when_true(Queued, [tl_or(Q)]) ++
            unless_empty(Postponed, [Postponed]) ++ unless_empty(FixedStack, [FixedStack]) ++
            when_true(element(1, Timeouts) =/= 0, [Timeouts]) ++ unless_empty(Log, [Log]),
    {Format ++ ClientFormat, depth_args(Depth, Args) ++ ClientArgs}.

hd_or([H | _]) -> H.

tl_or([_ | T]) -> T.

labelled(undefined, _) -> [];
labelled(_, Value) -> Value.

unless_empty([], _) -> [];
unless_empty(_, Value) -> Value.

when_true(true, Value) -> Value;
when_true(false, _) -> [].

fix_reason(error, undef, [{M, F, Args, _} | Rest] = Stack) ->
    case code:is_loaded(M) of
        false ->
            {{'module could not be loaded', M}, Rest};
        _ ->
            Arity =
                case Args of
                    _ when is_list(Args) -> length(Args);
                    _ -> Args
                end,
            case erlang:function_exported(M, F, Arity) of
                true -> {undef, Stack};
                false -> {{'function not exported', {M, F, Arity}}, Rest}
            end
    end;
fix_reason(_, Reason, Stack) ->
    {Reason, Stack}.

client_log_single(undefined, _, _) ->
    {"", []};
client_log_single({Pid, dead}, _, _) ->
    {" Client ~0p is dead.", [Pid]};
client_log_single({Pid, remote}, _, _) ->
    {" Client ~0p is remote on node ~0p.", [Pid, node(Pid)]};
client_log_single({_, {Name, Stack}}, P, Depth) ->
    {
        " Client " ++ P ++ " stacktrace: " ++ P ++ ".",
        depth_args(Depth, [Name, lists:sublist(Stack, 4)])
    }.

client_log(undefined, _, _) ->
    {"", []};
client_log({Pid, dead}, _, _) ->
    {"** Client ~p is dead~n", [Pid]};
client_log({Pid, remote}, _, _) ->
    {"** Client ~p is remote on node ~p~n", [Pid, node(Pid)]};
client_log({_, {Name, Stack}}, P, Depth) ->
    {"** Client " ++ P ++ " stacktrace~n** " ++ P ++ "~n", depth_args(Depth, [Name, Stack])}.

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
