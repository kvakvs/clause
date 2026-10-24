%% Clause's gen_server (plan 21 step LIB21, docs/library.md): the generic server over gen, proc_lib and sys. Calls
%% arrive as {'$gen_call', From, Request}, casts as {'$gen_cast', Request}; callback results take OTP's actions
%% (timeouts, hibernate, continue), a crash or stop gives OTP's error report, and system messages run through sys.
-module(gen_server).
-export([
    abcast/2,
    abcast/3,
    call/2,
    call/3,
    cast/2,
    check_response/2,
    check_response/3,
    enter_loop/3,
    enter_loop/4,
    enter_loop/5,
    format_log/1,
    format_log/2,
    format_status/2,
    init_it/6,
    multi_call/2,
    multi_call/3,
    multi_call/4,
    receive_response/2,
    receive_response/3,
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
    wait_response/2,
    wait_response/3
]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/library.md#runtime-module-replacements).
-include("gen_server.types.hrl").

-behaviour(sys).

-callback init(Args :: term()) ->
    {ok, State :: term()}
    | {ok, State :: term(), Action :: action()}
    | {stop, Reason :: term()}
    | ignore
    | {error, Reason :: term()}.
-callback handle_call(Request :: term(), From :: from(), State :: term()) ->
    {reply, Reply :: term(), NewState :: term()}
    | {reply, Reply :: term(), NewState :: term(), Action :: action()}
    | {noreply, NewState :: term()}
    | {noreply, NewState :: term(), Action :: action()}
    | {stop, Reason :: term(), Reply :: term(), NewState :: term()}
    | {stop, Reason :: term(), NewState :: term()}.
-callback handle_cast(Request :: term(), State :: term()) ->
    {noreply, NewState :: term()}
    | {noreply, NewState :: term(), Action :: action()}
    | {stop, Reason :: term(), NewState :: term()}.
-callback handle_info(Info :: timeout | term(), State :: term()) ->
    {noreply, NewState :: term()}
    | {noreply, NewState :: term(), Action :: action()}
    | {stop, Reason :: term(), NewState :: term()}.
-callback handle_continue(Info :: term(), State :: term()) ->
    {noreply, NewState :: term()}
    | {noreply, NewState :: term(), Action :: action()}
    | {stop, Reason :: term(), NewState :: term()}.
-callback terminate(Reason :: normal | shutdown | {shutdown, term()} | term(), State :: term()) ->
    term().
-callback code_change(OldVsn :: term() | {down, term()}, State :: term(), Extra :: term()) ->
    {ok, NewState :: term()} | {error, Reason :: term()}.
-callback format_status(Opt, StatusData) -> Status when
    Opt :: normal | terminate,
    StatusData :: [PDict | State],
    PDict :: [{Key :: term(), Value :: term()}],
    State :: term(),
    Status :: term().
-callback format_status(Status) -> NewStatus when
    Status :: format_status(), NewStatus :: format_status().
-optional_callbacks([
    handle_info/2, handle_continue/2, terminate/2, code_change/3, format_status/1, format_status/2
]).

-define(IS_TIMEOUT(T), (T =:= infinity orelse (is_integer(T) andalso T >= 0))).

%% Created for LIB21: what a server loop keeps besides the callback state. `tag` marks the server's own timeout
%% messages; `hibernate_after` is the idle time before it hibernates.
-record(server, {parent, name, module, hibernate_after, tag = make_ref()}).

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

call(Server, Request) ->
    try gen:call(Server, '$gen_call', Request) of
        {ok, Reply} -> Reply
    catch
        exit:Reason -> exit({Reason, {?MODULE, call, [Server, Request]}});
        error:Reason:Stack -> exit({{Reason, Stack}, {?MODULE, call, [Server, Request]}})
    end.

call(Server, Request, Timeout) ->
    try gen:call(Server, '$gen_call', Request, Timeout) of
        {ok, Reply} -> Reply
    catch
        exit:Reason -> exit({Reason, {?MODULE, call, [Server, Request, Timeout]}});
        error:Reason:Stack -> exit({{Reason, Stack}, {?MODULE, call, [Server, Request, Timeout]}})
    end.

send_request(Server, Request) ->
    badarg_as([Server, Request], fun() -> gen:send_request(Server, '$gen_call', Request) end).

send_request(Server, Request, Label, Collection) ->
    badarg_as([Server, Request, Label, Collection], fun() ->
        gen:send_request(Server, '$gen_call', Request, Label, Collection)
    end).

wait_response(Ref, Timeout) ->
    badarg_as([Ref, Timeout], fun() -> gen:wait_response(Ref, Timeout) end).

wait_response(Collection, Timeout, Delete) ->
    badarg_as([Collection, Timeout, Delete], fun() ->
        gen:wait_response(Collection, Timeout, Delete)
    end).

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

%% A badarg of gen is raised again with this function's arguments.
badarg_as(Args, Fun) ->
    try
        Fun()
    catch
        error:badarg -> error(badarg, Args)
    end.

cast({global, Name}, Request) ->
    catch (global_registry()):send(Name, {'$gen_cast', Request}),
    ok;
cast({via, Module, Name}, Request) ->
    catch Module:send(Name, {'$gen_cast', Request}),
    ok;
cast({Name, Node} = Dest, Request) when is_atom(Name), is_atom(Node) ->
    send(Dest, {'$gen_cast', Request});
cast(Dest, Request) when is_atom(Dest); is_pid(Dest) ->
    send(Dest, {'$gen_cast', Request}).

%% Created for LIB21: global until LIB25, reached by a runtime name (gen.erl does the same).
global_registry() -> global.

%% A send that ignores a missing name or node.
send(Dest, Msg) ->
    try
        erlang:send(Dest, Msg)
    catch
        error:_ -> ok
    end,
    ok.

reply(From, Reply) -> gen:reply(From, Reply).

abcast(Name, Request) when is_atom(Name) -> abcast([node() | nodes()], Name, Request).

abcast(Nodes, Name, Request) when is_list(Nodes), is_atom(Name) ->
    lists:foreach(
        fun(Node) when is_atom(Node) -> send({Name, Node}, {'$gen_cast', Request}) end, Nodes
    ),
    abcast.

multi_call(Name, Request) when is_atom(Name) ->
    multi_call([node() | nodes()], Name, Request, infinity).

multi_call(Nodes, Name, Request) when is_list(Nodes), is_atom(Name) ->
    multi_call(Nodes, Name, Request, infinity).

%% Calls Name on every node at once, answers collected through one alias: {Replies, BadNodes}.
multi_call(Nodes, Name, Request, Timeout) when
    is_list(Nodes), is_atom(Name), ?IS_TIMEOUT(Timeout)
->
    Alias = alias(),
    try
        Timer =
            case Timeout of
                infinity -> undefined;
                _ -> erlang:start_timer(Timeout, self(), Alias)
            end,
        Requests = multi_send(Nodes, Name, Alias, Request, Timer, []),
        multi_receive(Requests, Alias, Timer, [], [])
    after
        _ = unalias(Alias)
    end.

multi_send([], _, _, _, _, Requests) ->
    Requests;
multi_send([Node | Nodes], Name, Alias, Request, Timer, Requests) when is_atom(Node) ->
    Dest = {Name, Node},
    Monitor =
        try
            erlang:monitor(process, Dest, [{tag, Alias}])
        catch
            error:badarg ->
                %% No such node: answered as a 'DOWN' of a monitor never made.
                Ref = make_ref(),
                Alias ! {Alias, Ref, process, Dest, noconnection},
                Ref
        end,
    _ = send(Dest, {'$gen_call', {self(), [[alias | Alias] | Monitor]}, Request}),
    multi_send(Nodes, Name, Alias, Request, Timer, [{Node, Monitor} | Requests]);
multi_send(_, _, Alias, _, Timer, Requests) ->
    unalias(Alias),
    cancel_multi_timer(Timer, Alias),
    _ = multi_receive_late(Requests, Alias, [], []),
    error(badarg).

multi_receive([], Alias, Timer, Replies, BadNodes) ->
    cancel_multi_timer(Timer, Alias),
    unalias(Alias),
    {Replies, BadNodes};
multi_receive([{Node, Monitor} | Rest] = Requests, Alias, Timer, Replies, BadNodes) ->
    receive
        {[[alias | Alias] | Monitor], Reply} ->
            erlang:demonitor(Monitor, [flush]),
            multi_receive(Rest, Alias, Timer, [{Node, Reply} | Replies], BadNodes);
        {Alias, Monitor, process, _, _} ->
            multi_receive(Rest, Alias, Timer, Replies, [Node | BadNodes]);
        {timeout, Timer, Alias} ->
            unalias(Alias),
            multi_receive_late(Requests, Alias, Replies, BadNodes)
    end.

%% After the timeout: replies already in the mailbox count, other nodes are bad.
multi_receive_late([], _, Replies, BadNodes) ->
    {Replies, BadNodes};
multi_receive_late([{Node, Monitor} | Rest], Alias, Replies, BadNodes) ->
    erlang:demonitor(Monitor),
    receive
        {[[alias | Alias] | Monitor], Reply} ->
            multi_receive_late(Rest, Alias, [{Node, Reply} | Replies], BadNodes);
        {Alias, Monitor, process, _, _} ->
            multi_receive_late(Rest, Alias, Replies, [Node | BadNodes])
    after 0 -> multi_receive_late(Rest, Alias, Replies, [Node | BadNodes])
    end.

cancel_multi_timer(undefined, _) ->
    ok;
cancel_multi_timer(Timer, Alias) ->
    case erlang:cancel_timer(Timer) of
        false ->
            receive
                {timeout, Timer, Alias} -> ok
            end;
        _ ->
            ok
    end.

%% Turning the caller into a server.

enter_loop(Module, Options, State) when is_atom(Module), is_list(Options) ->
    enter_loop(Module, Options, State, self(), infinity).

enter_loop(Module, Options, State, {Scope, _} = Name) when
    is_atom(Module), is_list(Options), Scope =:= local;
    is_atom(Module), is_list(Options), Scope =:= global
->
    enter_loop(Module, Options, State, Name, infinity);
enter_loop(Module, Options, State, {via, _, _} = Name) when is_atom(Module), is_list(Options) ->
    enter_loop(Module, Options, State, Name, infinity);
enter_loop(Module, Options, State, Action) when is_atom(Module), is_list(Options) ->
    enter_loop(Module, Options, State, self(), Action).

enter_loop(Module, Options, State, Name, Action) when is_atom(Module), is_list(Options) ->
    ProcName = gen:get_proc_name(Name),
    Server = #server{
        parent = gen:get_parent(),
        name = ProcName,
        module = Module,
        hibernate_after = gen:hibernate_after(Options)
    },
    case loop_action(Server, Action) of
        error ->
            gen:unregister_name(ProcName),
            exit({bad_action, Action});
        LoopAction ->
            loop(Server, State, LoopAction, gen:debug_options(ProcName, Options))
    end.

%% The process side: init_it/6 runs in the new process (from gen:init_it/6,7).
init_it(Starter, self, Name, Module, Args, Options) ->
    init_it(Starter, self(), Name, Module, Args, Options);
init_it(Starter, Parent, RegName, Module, Args, Options) ->
    Name = gen:name(RegName),
    Server = #server{
        parent = Parent,
        name = Name,
        module = Module,
        hibernate_after = gen:hibernate_after(Options)
    },
    Debug = gen:debug_options(Name, Options),
    case run_init(Module, Args) of
        {ok, {ok, State}} ->
            proc_lib:init_ack(Starter, {ok, self()}),
            loop(Server, State, infinity, Debug);
        {ok, {ok, State, Action} = Return} ->
            case loop_action(Server, Action) of
                error ->
                    gen:unregister_name(RegName),
                    exit({bad_return_value, Return});
                LoopAction ->
                    proc_lib:init_ack(Starter, {ok, self()}),
                    loop(Server, State, LoopAction, Debug)
            end;
        {ok, {stop, Reason}} ->
            %% Unregistered before the starter learns of the failure, so it can start the server again at once.
            gen:unregister_name(RegName),
            exit(Reason);
        {ok, {error, _} = Error} ->
            gen:unregister_name(RegName),
            proc_lib:init_fail(Starter, Error, {exit, normal});
        {ok, ignore} ->
            gen:unregister_name(RegName),
            proc_lib:init_fail(Starter, ignore, {exit, normal});
        {ok, Else} ->
            gen:unregister_name(RegName),
            exit({bad_return_value, Else});
        {'EXIT', Class, Reason, Stack} ->
            gen:unregister_name(RegName),
            erlang:raise(Class, Reason, Stack)
    end.

run_init(Module, Args) ->
    try
        {ok, Module:init(Args)}
    catch
        throw:Result -> {ok, Result};
        Class:Reason:Stack -> {'EXIT', Class, Reason, Stack}
    end.

%% The server loop. A loop action is infinity, a time, hibernate, {continue, C}, {timeout_zero, Msg} (deliver Msg at
%% once) or {timeout, Timer, infinity | hibernate} (a timer for a timeout message runs); Timer is [] or [TRef | Msg].

loop(Server, State, {continue, Continue} = Msg, Debug) ->
    Result = callback(fun() -> (Server#server.module):handle_continue(Continue, State) end),
    Debug1 = debug(Server, Debug, Msg),
    handle_result(Server, State, Msg, Debug1, undefined, Result);
loop(Server, State, {timeout_zero, Msg}, Debug) ->
    dispatch(Server, State, infinity, Debug, [], Msg);
loop(Server, State, {timeout, Timer, Wait}, Debug) ->
    wait(Server, State, Wait, Debug, Timer);
loop(Server, State, Wait, Debug) ->
    wait(Server, State, Wait, Debug, []).

wait(Server, State, hibernate, Debug, Timer) ->
    receive
        Msg ->
            erlang:garbage_collect(),
            dispatch(Server, State, hibernate, Debug, Timer, Msg)
    after 0 -> hibernate(Server, State, Timer, Debug)
    end;
wait(#server{hibernate_after = Idle} = Server, State, infinity, Debug, Timer) ->
    receive
        Msg -> dispatch(Server, State, infinity, Debug, Timer, Msg)
    after Idle -> hibernate(Server, State, Timer, Debug)
    end;
wait(Server, State, Time, Debug, Timer) ->
    receive
        Msg -> dispatch(Server, State, Time, Debug, Timer, Msg)
    after Time -> dispatch(Server, State, infinity, Debug, Timer, timeout)
    end.

hibernate(Server, State, Timer, Debug) ->
    erlang:hibernate(),
    receive
        Msg -> dispatch(Server, State, hibernate, Debug, Timer, Msg)
    end.

%% Sorts one message: system requests, the parent's exit, the server's own timeouts, and everything else.
dispatch(#server{parent = Parent, tag = Tag} = Server, State, Wait, Debug, Timer, Msg) ->
    case Msg of
        {system, From, Request} ->
            sys:handle_system_msg(
                Request,
                From,
                Parent,
                ?MODULE,
                Debug,
                [Server, State, Wait, Timer],
                Wait =:= hibernate
            );
        {'EXIT', Parent, Reason} ->
            terminate(Server, State, Msg, undefined, {exit, Reason, []}, false, Debug);
        {timeout, TRef, Tag} when Timer =/= [], TRef =:= hd(Timer) ->
            handle(Server, State, debug(Server, Debug, {in, tl(Timer)}), tl(Timer));
        {timeout, _, Tag} ->
            wait(Server, State, Wait, Debug, Timer);
        _ ->
            cancel_timer(Timer),
            handle(Server, State, debug(Server, Debug, {in, Msg}), Msg)
    end.

cancel_timer([]) -> ok;
cancel_timer([TRef | _]) -> ok = erlang:cancel_timer(TRef, [{async, true}, {info, false}]).

handle(#server{module = Module} = Server, State, Debug, {'$gen_call', From, Request}) ->
    case callback(fun() -> Module:handle_call(Request, From, State) end) of
        {ok, {reply, Reply, NewState}} ->
            loop(Server, NewState, infinity, reply(Server, From, Reply, NewState, Debug));
        {ok, {reply, Reply, NewState, Action} = Return} ->
            case loop_action(Server, Action) of
                error ->
                    terminate(
                        Server,
                        State,
                        Request,
                        From,
                        {exit, {bad_return_value, Return}, []},
                        false,
                        Debug
                    );
                LoopAction ->
                    loop(Server, NewState, LoopAction, reply(Server, From, Reply, NewState, Debug))
            end;
        {ok, {stop, Reason, Reply, NewState}} ->
            try
                terminate(Server, NewState, Request, From, {exit, Reason, []}, false, Debug)
            after
                _ = reply(Server, From, Reply, NewState, Debug)
            end;
        Result ->
            handle_result(Server, State, Request, Debug, From, Result)
    end;
handle(#server{module = Module} = Server, State, Debug, {'$gen_cast', Request} = Msg) ->
    handle_result(
        Server,
        State,
        Msg,
        Debug,
        undefined,
        callback(fun() -> Module:handle_cast(Request, State) end)
    );
handle(Server, State, Debug, Msg) ->
    handle_result(Server, State, Msg, Debug, undefined, handle_info(Server, State, Msg)).

handle_info(#server{module = Module}, State, Msg) ->
    try
        {ok, Module:handle_info(Msg, State)}
    catch
        throw:Result ->
            {ok, Result};
        error:undef = Reason:Stack ->
            case erlang:function_exported(Module, handle_info, 2) of
                false ->
                    log(
                        warning,
                        #{label => {gen_server, no_handle_info}, module => Module, message => Msg},
                        warning_msg
                    ),
                    {ok, {noreply, State}};
                true ->
                    {'EXIT', error, Reason, Stack}
            end;
        Class:Reason:Stack ->
            {'EXIT', Class, Reason, Stack}
    end.

%% A callback's result: {ok, Value} (a thrown value counts as returned) or {'EXIT', Class, Reason, Stack}.
callback(Fun) ->
    try
        {ok, Fun()}
    catch
        throw:Result -> {ok, Result};
        Class:Reason:Stack -> {'EXIT', Class, Reason, Stack}
    end.

handle_result(Server, State, Msg, Debug, From, Result) ->
    case Result of
        {ok, {noreply, NewState}} ->
            loop(Server, NewState, infinity, debug(Server, Debug, {noreply, NewState}));
        {ok, {noreply, NewState, Action} = Return} ->
            case loop_action(Server, Action) of
                error ->
                    terminate(
                        Server,
                        State,
                        Msg,
                        From,
                        {exit, {bad_return_value, Return}, []},
                        false,
                        Debug
                    );
                LoopAction ->
                    loop(Server, NewState, LoopAction, debug(Server, Debug, {noreply, NewState}))
            end;
        {ok, {stop, Reason, NewState}} ->
            terminate(Server, NewState, Msg, From, {exit, Reason, []}, false, Debug);
        {'EXIT', Class, Reason, Stack} ->
            terminate(Server, State, Msg, From, {Class, Reason, Stack}, true, Debug);
        {ok, BadReturn} ->
            terminate(
                Server, State, Msg, From, {exit, {bad_return_value, BadReturn}, []}, false, Debug
            )
    end.

reply(Server, From, Reply, State, Debug) ->
    reply(From, Reply),
    debug(Server, Debug, {out, Reply, From, State}).

debug(_, [], _) ->
    [];
debug(#server{name = Name}, Debug, Event) ->
    sys:handle_debug(Debug, fun print_event/3, Name, Event).

%% The loop action of a callback's action, or error for an invalid one.
loop_action(Server, Action) ->
    case Action of
        {continue, _} ->
            Action;
        hibernate ->
            hibernate;
        Time when ?IS_TIMEOUT(Time) -> Time;
        {timeout, Time, Msg} ->
            relative_timer(Server, Time, Msg, infinity);
        {hibernate, Time, Msg} ->
            relative_timer(Server, Time, Msg, hibernate);
        {timeout, Time, Msg, Options} ->
            timer(Server, Time, Msg, infinity, options(Options), false);
        {hibernate, Time, Msg, Options} ->
            timer(Server, Time, Msg, hibernate, options(Options), false);
        _ ->
            error
    end.

options(Options) when is_list(Options) -> Options;
options(Option) -> [Option].

relative_timer(Server, Time, Msg, Wait) when ?IS_TIMEOUT(Time) ->
    start_timer(Server, Time, Msg, Wait, false);
relative_timer(_, _, _, _) ->
    error.

timer(Server, Time, Msg, Wait, [], false) when ?IS_TIMEOUT(Time) ->
    start_timer(Server, Time, Msg, Wait, false);
timer(Server, Time, Msg, Wait, [], true) when Time =:= infinity; is_integer(Time) ->
    start_timer(Server, Time, Msg, Wait, true);
timer(Server, Time, Msg, Wait, [{abs, Abs} | Options], _) when is_boolean(Abs) ->
    timer(Server, Time, Msg, Wait, Options, Abs);
timer(_, _, _, _, _, _) ->
    error.

start_timer(_, 0, Msg, Wait, false) ->
    Wait =:= hibernate andalso erlang:garbage_collect(),
    {timeout_zero, Msg};
start_timer(_, infinity, _, Wait, _) ->
    Wait;
start_timer(#server{tag = Tag}, Time, Msg, Wait, Abs) ->
    TRef = erlang:start_timer(Time, self(), Tag, [{abs, Abs}]),
    {timeout, [TRef | Msg], Wait}.

%% sys callbacks; Misc is [Server, State, Wait, Timer].

system_continue(Parent, Debug, [#server{parent = Parent} = Server, State, Wait, Timer]) ->
    wait(Server, State, Wait, Debug, Timer).

system_terminate(Reason, _, Debug, [Server, State, _, _]) ->
    terminate(Server, State, [], undefined, {exit, Reason, []}, false, Debug).

system_code_change([#server{module = Module} = Server, State, Wait, Timer], _, OldVsn, Extra) ->
    case catch Module:code_change(OldVsn, State, Extra) of
        {ok, NewState} -> {ok, [Server, NewState, Wait, Timer]};
        Else -> Else
    end.

system_get_state([_, State, _, _]) -> {ok, State}.

system_replace_state(StateFun, [Server, State, Wait, Timer]) ->
    NewState = StateFun(State),
    {ok, NewState, [Server, NewState, Wait, Timer]}.

print_event(Dev, {in, Msg}, Name) ->
    case Msg of
        {'$gen_call', {From, _}, Call} ->
            io:format(Dev, "*DBG* ~tp got call ~tp from ~tw~n", [Name, Call, From]);
        {'$gen_cast', Cast} ->
            io:format(Dev, "*DBG* ~tp got cast ~tp~n", [Name, Cast]);
        _ ->
            io:format(Dev, "*DBG* ~tp got ~tp~n", [Name, Msg])
    end;
print_event(Dev, {out, Msg, {To, _}, State}, Name) ->
    io:format(Dev, "*DBG* ~tp sent ~tp to ~tw, new state ~tp~n", [Name, Msg, To, State]);
print_event(Dev, {noreply, State}, Name) ->
    io:format(Dev, "*DBG* ~tp new state ~tp~n", [Name, State]);
print_event(Dev, Event, Name) ->
    io:format(Dev, "*DBG* ~tp dbg  ~tp~n", [Name, Event]).

%% Termination: terminate/2 of the module, then OTP's error report unless the exit reason is normal, shutdown or
%% {shutdown, _}; the stack joins the reported reason when ReportStack is set (a crash, not a stop).
terminate(
    #server{module = Module} = Server, State, Msg, From, {Class, Reason, Stack}, ReportStack, Debug
) ->
    Result =
        case erlang:function_exported(Module, terminate, 2) of
            true -> callback(fun() -> Module:terminate(caught(Class, Reason, Stack), State) end);
            false -> {ok, ok}
        end,
    case Result of
        {'EXIT', C, R, S} ->
            error_report(Server, State, Msg, From, R, S, Debug),
            erlang:raise(C, R, S);
        _ ->
            case quiet(Class, Reason) of
                true ->
                    ok;
                false when ReportStack ->
                    error_report(Server, State, Msg, From, Reason, Stack, Debug);
                false ->
                    error_report(Server, State, Msg, From, Reason, undefined, Debug)
            end,
            case Stack of
                [] -> erlang:Class(Reason);
                _ -> erlang:raise(Class, Reason, Stack)
            end
    end.

%% The reason terminate/2 gets: an error's reason with its stack.
caught(error, Reason, Stack) -> {Reason, Stack};
caught(exit, Reason, _) -> Reason.

quiet(exit, normal) -> true;
quiet(exit, shutdown) -> true;
quiet(exit, {shutdown, _}) -> true;
quiet(_, _) -> false.

error_report(#server{name = application_controller}, _, _, _, _, _, _) ->
    %% The application controller's end is reported by init.
    ok;
error_report(#server{name = Name, module = Module}, State, Msg, From, Reason, Stack, Debug) ->
    Status = gen:format_status(
        Module,
        terminate,
        #{reason => Reason, state => State, message => Msg, log => sys:get_log(Debug)},
        [get(), State]
    ),
    Reported =
        case Stack of
            undefined -> maps:get(reason, Status);
            _ -> {maps:get(reason, Status), Stack}
        end,
    log(
        error,
        #{
            label => {gen_server, terminate},
            name => Name,
            last_message => maps:get(message, Status),
            state => maps:get('EXIT', Status, maps:get('$status', Status, maps:get(state, Status))),
            log => format_log_state(Module, maps:get(log, Status)),
            reason => Reported,
            client_info => client_info(From),
            process_label => proc_lib:get_label(self())
        },
        error
    ).

log(Level, Report, Tag) ->
    clause_logger:log(Level, {report, Report}, #{
        domain => [otp],
        report_cb => fun gen_server:format_log/2,
        error_logger => #{tag => Tag, report_cb => fun gen_server:format_log/1}
    }).

%% The caller of a call that crashed the server: its name or pid and its stack (Clause keeps none: []).
client_info(undefined) ->
    undefined;
client_info({From, _}) ->
    client_info(From);
client_info(From) when is_pid(From), node(From) =:= node() ->
    case process_info(From, registered_name) of
        undefined -> {From, dead};
        {registered_name, Name} -> {From, {Name, []}};
        [] -> {From, {From, []}}
    end;
client_info(From) when is_pid(From) ->
    {From, remote}.

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
        label := {gen_server, terminate},
        name := Name,
        last_message := Msg,
        state := State,
        log := Log,
        reason := Reason,
        client_info := Client,
        process_label := Label
    },
    #{single_line := true, depth := Depth} = Options
) ->
    P = p(Options),
    Format =
        lists:append([
            "Generic server ",
            P,
            " terminating",
            labelled(Label, ". Label: " ++ P),
            ". Reason: ",
            P,
            ". Last message: ",
            P,
            ". State: ",
            P,
            "."
        ]),
    {LogFormat, LogArgs} = server_log_single(Log, Options),
    {ClientFormat, ClientArgs} = client_log_single(Client, Options),
    Args = depth_args(
        Depth, [Name] ++ labelled(Label, [Label]) ++ [fix_reason(Reason), Msg, State]
    ),
    {Format ++ LogFormat ++ ClientFormat, Args ++ LogArgs ++ ClientArgs};
format_log_single(
    #{label := {gen_server, no_handle_info}, module := Module, message := Msg},
    #{single_line := true, depth := Depth} = Options
) ->
    P = p(Options),
    {
        "Undefined handle_info in " ++ P ++ ". Unhandled message: " ++ P ++ ".",
        depth_args(Depth, [Module, Msg])
    };
format_log_single(Report, Options) ->
    format_log_multi(Report, Options).

format_log_multi(
    #{
        label := {gen_server, terminate},
        name := Name,
        last_message := Msg,
        state := State,
        log := Log,
        reason := Reason,
        client_info := Client,
        process_label := Label
    },
    #{depth := Depth} = Options
) ->
    P = p(Options),
    {ClientFormat, ClientArgs} = client_log(Client, Options),
    LogFormat =
        case Log of
            [] ->
                [];
            _ ->
                ["** Log ==~n** [" | lists:join(",~n    ", lists:duplicate(length(Log), P))] ++
                    ["]~n"]
        end,
    Format =
        lists:append(
            ["** Generic server ", P, " terminating \n"] ++
                labelled(Label, [
                    "** Process label == ",
                    P,
                    "~n"
                ]) ++
                [
                    "** Last message in was ",
                    P,
                    "~n** When Server state == ",
                    P,
                    "~n** Reason for termination ==~n** ",
                    P,
                    "~n"
                ] ++ LogFormat
        ) ++ ClientFormat,
    Args = depth_args(
        Depth, [Name] ++ labelled(Label, [Label]) ++ [Msg, State, fix_reason(Reason)] ++ Log
    ),
    {Format, Args ++ ClientArgs};
format_log_multi(
    #{label := {gen_server, no_handle_info}, module := Module, message := Msg},
    #{depth := Depth} = Options
) ->
    P = p(Options),
    Args =
        case Depth of
            unlimited -> [Module, Msg];
            _ -> [Module, Msg, Depth]
        end,
    {"** Undefined handle_info in ~p~n** Unhandled message: " ++ P ++ "~n", Args}.

labelled(undefined, _) -> [];
labelled(_, Value) -> Value.

%% Every argument followed by the depth when the depth is limited (~P takes two).
depth_args(unlimited, Args) -> Args;
depth_args(Depth, Args) -> lists:append([[Arg, Depth] || Arg <- Args]).

fix_reason({undef, [{M, F, A, L} | MFAs]} = Reason) ->
    case code:is_loaded(M) of
        false ->
            {'module could not be loaded', [{M, F, A, L} | MFAs]};
        _ ->
            case is_list(A) andalso erlang:function_exported(M, F, length(A)) of
                true -> Reason;
                false -> {'function not exported', [{M, F, A, L} | MFAs]}
            end
    end;
fix_reason(Reason) ->
    Reason.

server_log_single([], _) ->
    {"", []};
server_log_single(Log, #{depth := Depth} = Options) ->
    {" Log: " ++ p(Options), depth_args(Depth, [Log])}.

client_log_single(undefined, _) ->
    {"", []};
client_log_single({From, dead}, _) ->
    {" Client ~0p is dead.", [From]};
client_log_single({From, remote}, _) ->
    {" Client ~0p is remote on node ~0p.", [From, node(From)]};
client_log_single({_, {Name, Stack}}, #{depth := Depth} = Options) ->
    P = p(Options),
    {
        " Client " ++ P ++ " stacktrace: " ++ P ++ ".",
        depth_args(Depth, [Name, lists:sublist(Stack, 4)])
    }.

client_log(undefined, _) ->
    {"", []};
client_log({From, dead}, _) ->
    {"** Client ~p is dead~n", [From]};
client_log({From, remote}, _) ->
    {"** Client ~p is remote on node ~p~n", [From, node(From)]};
client_log({_, {Name, Stack}}, #{depth := Depth} = Options) ->
    P = p(Options),
    {"** Client " ++ P ++ " stacktrace~n** " ++ P ++ "~n", depth_args(Depth, [Name, Stack])}.

p(#{single_line := Single, depth := Depth, encoding := Encoding}) ->
    "~" ++ single(Single) ++ modifier(Encoding) ++ letter(Depth).

single(true) -> "0";
single(false) -> "".

modifier(latin1) -> "";
modifier(_) -> "t".

letter(unlimited) -> "p";
letter(_) -> "P".

%% sys:get_status/1,2's data of a server.
format_status(Opt, [
    Dictionary,
    SysState,
    Parent,
    Debug,
    [
        #server{parent = Parent, name = Name, module = Module},
        State,
        _,
        _
    ]
]) ->
    Header = gen:format_status_header("Status for generic server", Name),
    Status =
        case
            gen:format_status(Module, Opt, #{state => State, log => sys:get_log(Debug)}, [
                Dictionary, State
            ])
        of
            #{'EXIT' := R} = M -> M#{'$status' => [{data, [{"State", R}]}]};
            #{'$status' := S} = M when is_list(S) -> M;
            #{'$status' := S} = M -> M#{'$status' := [S]};
            #{state := S} = M -> M#{'$status' => [{data, [{"State", S}]}]}
        end,
    [
        {header, Header},
        {data, [
            {"Status", SysState},
            {"Parent", Parent},
            {"Logged events",
                format_log_state(
                    Module,
                    maps:get(log, Status)
                )}
        ]}
        | maps:get('$status', Status)
    ].

%% Logged events with their states passed through format_status/2 (format_status/1 already saw the whole log).
format_log_state(Module, Log) ->
    case erlang:function_exported(Module, format_status, 1) of
        false -> [logged_event(Module, Event) || Event <- Log];
        true -> Log
    end.

logged_event(Module, {out, Msg, From, State}) -> {out, Msg, From, shown_state(Module, State)};
logged_event(Module, {noreply, State}) -> {noreply, shown_state(Module, State)};
logged_event(_, Event) -> Event.

shown_state(Module, State) ->
    maps:get(state, gen:format_status(Module, terminate, #{state => State}, [get(), State])).
