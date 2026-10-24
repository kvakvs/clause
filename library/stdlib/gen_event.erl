%% Clause's gen_event (plan 21 step LIB21, docs/library.md): an event manager process holding a list of handlers,
%% each a callback module with its own state. Requests arrive as gen calls labelled with the caller's pid; handler
%% results, crashes ({'EXIT', ...} terms as catch gives them), supervised handlers' gen_event_EXIT messages and
%% error reports follow OTP.
-module(gen_event).
-export([
    add_handler/3,
    add_sup_handler/3,
    call/3,
    call/4,
    check_response/2,
    check_response/3,
    delete_handler/3,
    format_log/1,
    format_log/2,
    format_status/2,
    init_it/6,
    notify/2,
    receive_response/2,
    receive_response/3,
    reqids_add/3,
    reqids_new/0,
    reqids_size/1,
    reqids_to_list/1,
    send_request/3,
    send_request/5,
    start/0,
    start/1,
    start/2,
    start_link/0,
    start_link/1,
    start_link/2,
    start_monitor/0,
    start_monitor/1,
    start_monitor/2,
    stop/1,
    stop/3,
    swap_handler/3,
    swap_sup_handler/3,
    sync_notify/2,
    system_code_change/4,
    system_continue/3,
    system_get_state/1,
    system_replace_state/2,
    system_terminate/4,
    wait_response/2,
    wait_response/3,
    wake_hib/5,
    which_handlers/1
]).

-include_lib("kernel/include/logger.hrl").
-include("gen_event.types.hrl").

-behaviour(sys).

-callback init(InitArgs :: term()) ->
    {ok, State :: term()} | {ok, State :: term(), hibernate} | {error, Reason :: term()}.
-callback handle_event(Event :: term(), State :: term()) ->
    {ok, NewState :: term()}
    | {ok, NewState :: term(), hibernate}
    | {swap_handler, Args1 :: term(), NewState :: term(), Handler2 :: atom() | {atom(), Id :: term()}, Args2 :: term()}
    | remove_handler.
-callback handle_call(Request :: term(), State :: term()) ->
    {ok, Reply :: term(), NewState :: term()}
    | {ok, Reply :: term(), NewState :: term(), hibernate}
    | {swap_handler, Reply :: term(), Args1 :: term(), NewState :: term(), Handler2 :: atom() | {atom(), Id :: term()},
        Args2 :: term()}
    | {remove_handler, Reply :: term()}.
-callback handle_info(Info :: term(), State :: term()) ->
    {ok, NewState :: term()}
    | {ok, NewState :: term(), hibernate}
    | {swap_handler, Args1 :: term(), NewState :: term(), Handler2 :: atom() | {atom(), Id :: term()}, Args2 :: term()}
    | remove_handler.
-callback terminate(
    Args ::
        term()
        | {stop, Reason :: term()}
        | stop
        | remove_handler
        | {error, {'EXIT', Reason :: term()}}
        | {error, term()},
    State :: term()
) -> term().
-callback code_change(OldVsn :: term() | {down, term()}, State :: term(), Extra :: term()) ->
    {ok, NewState :: term()}.
-callback format_status(Opt, StatusData) -> Status when
    Opt :: normal | terminate,
    StatusData :: [PDict | State],
    PDict :: [{Key :: term(), Value :: term()}],
    State :: term(),
    Status :: term().
-callback format_status(Status) -> NewStatus when
    Status :: format_status(), NewStatus :: format_status().
-optional_callbacks([handle_info/2, terminate/2, code_change/3, format_status/1, format_status/2]).

-compile(nowarn_deprecated_catch).

%% An installed handler: its module, its id (false when added by module alone), its state, and the process that
%% supervises it (false for none).
-record(handler, {module, id = false, state, supervised = false}).

-define(NO_CALLBACK, 'no callback module').

%% Starting and stopping.

start() -> gen:start(?MODULE, nolink, ?NO_CALLBACK, [], []).

start(Name) when is_tuple(Name) -> gen:start(?MODULE, nolink, Name, ?NO_CALLBACK, [], []);
start(Options) when is_list(Options) -> gen:start(?MODULE, nolink, ?NO_CALLBACK, [], Options);
start(Arg) -> error(badarg, [Arg]).

start(Name, Options) when is_tuple(Name), is_list(Options) ->
    gen:start(?MODULE, nolink, Name, ?NO_CALLBACK, [], Options);
start(Name, Options) ->
    error(badarg, [Name, Options]).

start_link() -> gen:start(?MODULE, link, ?NO_CALLBACK, [], []).

start_link(Name) when is_tuple(Name) -> gen:start(?MODULE, link, Name, ?NO_CALLBACK, [], []);
start_link(Options) when is_list(Options) -> gen:start(?MODULE, link, ?NO_CALLBACK, [], Options);
start_link(Arg) -> error(badarg, [Arg]).

start_link(Name, Options) when is_tuple(Name), is_list(Options) ->
    gen:start(?MODULE, link, Name, ?NO_CALLBACK, [], Options);
start_link(Name, Options) ->
    error(badarg, [Name, Options]).

start_monitor() -> gen:start(?MODULE, monitor, ?NO_CALLBACK, [], []).

start_monitor(Name) when is_tuple(Name) -> gen:start(?MODULE, monitor, Name, ?NO_CALLBACK, [], []);
start_monitor(Options) when is_list(Options) ->
    gen:start(?MODULE, monitor, ?NO_CALLBACK, [], Options);
start_monitor(Arg) ->
    error(badarg, [Arg]).

start_monitor(Name, Options) when is_tuple(Name), is_list(Options) ->
    gen:start(?MODULE, monitor, Name, ?NO_CALLBACK, [], Options);
start_monitor(Name, Options) ->
    error(badarg, [Name, Options]).

stop(Manager) -> gen:stop(Manager).

stop(Manager, Reason, Timeout) -> gen:stop(Manager, Reason, Timeout).

init_it(Starter, self, Name, Module, Args, Options) ->
    init_it(Starter, self(), Name, Module, Args, Options);
init_it(Starter, Parent, RegName, _, _, Options) ->
    process_flag(trap_exit, true),
    Name = gen:name(RegName),
    Debug = gen:debug_options(Name, Options),
    Idle = gen:hibernate_after(Options),
    proc_lib:init_ack(Starter, {ok, self()}),
    loop(Parent, Name, [], Idle, Debug, false).

%% Requests.

add_handler(Manager, Handler, Args) -> request(Manager, {add_handler, Handler, Args}).

add_sup_handler(Manager, Handler, Args) ->
    request(Manager, {add_sup_handler, Handler, Args, self()}).

notify(Manager, Event) -> send(Manager, {notify, Event}).

sync_notify(Manager, Event) -> request(Manager, {sync_notify, Event}).

delete_handler(Manager, Handler, Args) -> request(Manager, {delete_handler, Handler, Args}).

swap_handler(Manager, {Handler1, Args1}, {Handler2, Args2}) ->
    request(Manager, {swap_handler, Handler1, Args1, Handler2, Args2}).

swap_sup_handler(Manager, {Handler1, Args1}, {Handler2, Args2}) ->
    request(Manager, {swap_sup_handler, Handler1, Args1, Handler2, Args2, self()}).

which_handlers(Manager) -> request(Manager, which_handlers).

call(Manager, Handler, Query) ->
    try gen:call(Manager, self(), {call, Handler, Query}) of
        {ok, Reply} -> Reply
    catch
        exit:Reason -> exit({Reason, {?MODULE, call, [Manager, Handler, Query]}})
    end.

call(Manager, Handler, Query, Timeout) ->
    try gen:call(Manager, self(), {call, Handler, Query}, Timeout) of
        {ok, Reply} -> Reply
    catch
        exit:Reason -> exit({Reason, {?MODULE, call, [Manager, Handler, Query, Timeout]}})
    end.

request(Manager, Command) ->
    {ok, Reply} = gen:call(Manager, self(), Command, infinity),
    Reply.

send({global, Name}, Command) ->
    catch (global_registry()):send(Name, Command),
    ok;
send({via, Module, Name}, Command) ->
    catch Module:send(Name, Command),
    ok;
send(Manager, Command) ->
    Manager ! Command,
    ok.

%% Created for LIB21: global until LIB25, reached by a runtime name (gen.erl does the same).
global_registry() -> global.

send_request(Manager, Handler, Request) ->
    badarg_as([Manager, Handler, Request], fun() ->
        gen:send_request(Manager, self(), {call, Handler, Request})
    end).

send_request(Manager, Handler, Request, Label, Collection) ->
    badarg_as([Manager, Handler, Request, Label, Collection], fun() ->
        gen:send_request(Manager, self(), {call, Handler, Request}, Label, Collection)
    end).

%% A handler's {error, _} reply is the response itself.
wait_response(Ref, Timeout) ->
    unwrapped(badarg_as([Ref, Timeout], fun() -> gen:wait_response(Ref, Timeout) end)).

wait_response(Collection, Timeout, Delete) ->
    unwrapped(
        badarg_as([Collection, Timeout, Delete], fun() ->
            gen:wait_response(Collection, Timeout, Delete)
        end)
    ).

receive_response(Ref, Timeout) ->
    unwrapped(badarg_as([Ref, Timeout], fun() -> gen:receive_response(Ref, Timeout) end)).

receive_response(Collection, Timeout, Delete) ->
    unwrapped(
        badarg_as([Collection, Timeout, Delete], fun() ->
            gen:receive_response(Collection, Timeout, Delete)
        end)
    ).

check_response(Msg, Ref) ->
    unwrapped(badarg_as([Msg, Ref], fun() -> gen:check_response(Msg, Ref) end)).

check_response(Msg, Collection, Delete) ->
    unwrapped(
        badarg_as([Msg, Collection, Delete], fun() ->
            gen:check_response(Msg, Collection, Delete)
        end)
    ).

unwrapped({reply, {error, _} = Error}) -> Error;
unwrapped({{reply, {error, _} = Error}, Label, Collection}) -> {Error, Label, Collection};
unwrapped(Result) -> Result.

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

%% The manager loop. Handlers is the list of #handler{} in order (the latest added first).

loop(Parent, Name, Handlers, Idle, Debug, true) ->
    proc_lib:hibernate(?MODULE, wake_hib, [Parent, Name, Handlers, Idle, Debug]);
loop(Parent, Name, Handlers, Idle, Debug, false) ->
    fetch(Parent, Name, Handlers, Idle, Debug, false).

wake_hib(Parent, Name, Handlers, Idle, Debug) -> fetch(Parent, Name, Handlers, Idle, Debug, true).

fetch(Parent, Name, Handlers, Idle, Debug, Hibernated) ->
    receive
        {system, From, Request} ->
            sys:handle_system_msg(
                Request,
                From,
                Parent,
                ?MODULE,
                Debug,
                [Name, Handlers, Idle, Hibernated],
                Hibernated
            );
        {'EXIT', Parent, Reason} ->
            terminate_server(Reason, Parent, Handlers, Name);
        Msg when Debug =:= [] ->
            handle(Msg, Parent, Name, Handlers, Idle, []);
        Msg ->
            handle(
                Msg,
                Parent,
                Name,
                Handlers,
                Idle,
                sys:handle_debug(Debug, fun print_event/3, Name, {in, Msg})
            )
    after Idle -> loop(Parent, Name, Handlers, Idle, Debug, true)
    end.

handle(Msg, Parent, Name, Handlers, Idle, Debug) ->
    Next = fun({Hibernate, New}) -> loop(Parent, Name, New, Idle, Debug, Hibernate) end,
    case Msg of
        {notify, Event} ->
            Next(notify(Event, handle_event, Handlers, Name));
        {_, From, {sync_notify, Event}} ->
            Result = notify(Event, handle_event, Handlers, Name),
            gen:reply(From, ok),
            Next(Result);
        {'EXIT', Pid, Reason} ->
            Next({false, handle_exit(Pid, Reason, Handlers, Name)});
        {_, From, Command} ->
            case is_request(Command) of
                true -> command(Command, From, Parent, Name, Handlers, Next);
                false -> Next(notify(Msg, handle_info, Handlers, Name))
            end;
        _ ->
            Next(notify(Msg, handle_info, Handlers, Name))
    end.

is_request({call, _, _}) ->
    true;
is_request({add_handler, _, _}) ->
    true;
is_request({add_sup_handler, _, _, _}) ->
    true;
is_request({delete_handler, _, _}) ->
    true;
is_request({swap_handler, _, _, _, _}) ->
    true;
is_request({swap_sup_handler, _, _, _, _, _}) ->
    true;
is_request(Command) ->
    Command =:= stop orelse Command =:= which_handlers orelse Command =:= get_modules.

%% A request; Next continues the loop with {Hibernate, Handlers}.
command({call, Handler, Query}, From, _, Name, Handlers, Next) ->
    {Hibernate, Reply, New} = call_handler(Handler, Query, Handlers, Name),
    replied(From, Reply, Next, {Hibernate, New});
command({add_handler, Handler, Args}, From, _, _, Handlers, Next) ->
    {Hibernate, Reply, New} = add(Handler, Args, Handlers, false),
    replied(From, Reply, Next, {Hibernate, New});
command({add_sup_handler, Handler, Args, Supervisor}, From, _, _, Handlers, Next) ->
    link(Supervisor),
    {Hibernate, Reply, New} = add(Handler, Args, Handlers, Supervisor),
    replied(From, Reply, Next, {Hibernate, New});
command({delete_handler, Handler, Args}, From, _, Name, Handlers, Next) ->
    {Reply, New} =
        case split(Handler, Handlers) of
            {Module, Found, Rest} ->
                {
                    terminate_handler(
                        Module, Found, Args, Found#handler.state, delete, Name, normal
                    ),
                    Rest
                };
            error ->
                {{error, module_not_found}, Handlers}
        end,
    replied(From, Reply, Next, {false, New});
command({swap_handler, Handler1, Args1, Handler2, Args2}, From, _, Name, Handlers, Next) ->
    {State2, Supervisor, Rest} = remove_for_swap(Handler1, Args1, Handlers, Name, Handler2, false),
    {Hibernate, Reply, New} = add_swapped(Supervisor, Handler2, {Args2, State2}, Rest),
    replied(From, Reply, Next, {Hibernate, New});
command(
    {swap_sup_handler, Handler1, Args1, Handler2, Args2, Supervisor}, From, _, Name, Handlers, Next
) ->
    {State2, _, Rest} = remove_for_swap(Handler1, Args1, Handlers, Name, Handler2, Supervisor),
    {Hibernate, Reply, New} = add_swapped(Supervisor, Handler2, {Args2, State2}, Rest),
    replied(From, Reply, Next, {Hibernate, New});
command(stop, From, Parent, Name, Handlers, _) ->
    catch terminate_server(normal, Parent, Handlers, Name),
    gen:reply(From, ok);
command(which_handlers, From, _, _, Handlers, Next) ->
    replied(From, [handler_name(H) || H <- Handlers], Next, {false, Handlers});
command(get_modules, From, _, _, Handlers, Next) ->
    replied(From, lists:usort([H#handler.module || H <- Handlers]), Next, {false, Handlers}).

replied(From, Reply, Next, Result) ->
    gen:reply(From, Reply),
    Next(Result).

terminate_server(Reason, Parent, Handlers, Name) ->
    lists:foreach(
        fun(H) ->
            terminate_handler(H#handler.module, H, stop, H#handler.state, stop, Name, shutdown)
        end,
        Handlers
    ),
    lists:foreach(
        fun
            (#handler{supervised = Pid}) when is_pid(Pid), Pid =/= Parent -> unlink(Pid);
            (_) -> ok
        end,
        Handlers
    ),
    exit(Reason).

%% A linked process ended: the handlers it supervised go, then every handler sees the 'EXIT' as info.
handle_exit(Pid, Reason, Handlers, Name) ->
    Kept = lists:filter(
        fun
            (#handler{supervised = Supervisor} = H) when Supervisor =:= Pid ->
                terminate_handler(
                    H#handler.module,
                    H,
                    {stop, Reason},
                    H#handler.state,
                    {parent_terminated, {Pid, Reason}},
                    Name,
                    shutdown
                ),
                false;
            (_) ->
                true
        end,
        Handlers
    ),
    element(2, notify({'EXIT', Pid, Reason}, handle_info, Kept, Name)).

%% Adding and swapping handlers.

add({Module, Id}, Args, Handlers, Supervisor) ->
    add_new(Module, #handler{module = Module, id = Id, supervised = Supervisor}, Args, Handlers);
add(Module, Args, Handlers, Supervisor) ->
    add_new(Module, #handler{module = Module, supervised = Supervisor}, Args, Handlers).

add_new(Module, Handler, Args, Handlers) ->
    case catch Module:init(Args) of
        {ok, State} -> {false, ok, [Handler#handler{state = State} | Handlers]};
        {ok, State, hibernate} -> {true, ok, [Handler#handler{state = State} | Handlers]};
        Other -> {false, Other, Handlers}
    end.

add_swapped(false, Handler, Args, Handlers) ->
    error_wrapped(add(Handler, Args, Handlers, false));
add_swapped(Supervisor, Handler, Args, Handlers) ->
    link(Supervisor),
    error_wrapped(add(Handler, Args, Handlers, Supervisor)).

error_wrapped({Hibernate, ok, Handlers}) -> {Hibernate, ok, Handlers};
error_wrapped({Hibernate, What, Handlers}) -> {Hibernate, {error, What}, Handlers}.

%% {State for the new handler's init, the old supervisor, the other handlers}; error as state when the old
%% handler is not installed.
remove_for_swap(Handler, Args, Handlers, Name, Handler2, Supervisor) ->
    case split(Handler, Handlers) of
        {Module, Found, Rest} ->
            Old = Found#handler.supervised,
            New =
                case Supervisor of
                    false -> Old;
                    _ -> Supervisor
                end,
            {
                terminate_handler(
                    Module,
                    Found,
                    Args,
                    Found#handler.state,
                    swapped,
                    Name,
                    {swapped, Handler2, New}
                ),
                Old,
                Rest
            };
        error ->
            {error, false, Handlers}
    end.

%% Delivering events: {Hibernate, Handlers} with the handlers that remain.
notify(Event, Callback, [Handler | Rest], Name) ->
    case update(Handler, Callback, Event, Name) of
        {ok, New} ->
            {Hibernate, Others} = notify(Event, Callback, Rest, Name),
            {Hibernate, [New | Others]};
        {hibernate, New} ->
            {_, Others} = notify(Event, Callback, Rest, Name),
            {true, [New | Others]};
        no ->
            notify(Event, Callback, Rest, Name)
    end;
notify(_, _, [], _) ->
    {false, []}.

update(#handler{module = Module, state = State} = Handler, Callback, Event, Name) ->
    case catch Module:Callback(Event, State) of
        {ok, New} ->
            {ok, Handler#handler{state = New}};
        {ok, New, hibernate} ->
            {hibernate, Handler#handler{state = New}};
        {swap_handler, Args1, New, Handler2, Args2} ->
            swap(Module, Handler, Args1, New, Handler2, Args2, Name);
        remove_handler ->
            terminate_handler(Module, Handler, remove_handler, State, remove, Name, normal),
            no;
        {'EXIT', {undef, [{Module, handle_info, [_, _], _} | _]}} ->
            ?LOG(
                warning,
                #{
                    label => {gen_event, no_handle_info},
                    module => Module,
                    message => Event
                },
                #{
                    domain => [otp],
                    report_cb => fun gen_event:format_log/2,
                    error_logger => #{tag => warning_msg, report_cb => fun gen_event:format_log/1}
                }
            ),
            {ok, Handler};
        Other ->
            terminate_handler(Module, Handler, {error, Other}, State, Event, Name, crash),
            no
    end.

swap(Module, Handler, Args1, State1, Handler2, Args2, Name) ->
    State2 = terminate_handler(
        Module,
        Handler,
        Args1,
        State1,
        swapped,
        Name,
        {swapped, Handler2, Handler#handler.supervised}
    ),
    {Module2, New} =
        case Handler2 of
            {M, Id} -> {M, #handler{module = M, id = Id, supervised = Handler#handler.supervised}};
            M -> {M, #handler{module = M, supervised = Handler#handler.supervised}}
        end,
    case catch Module2:init({Args2, State2}) of
        {ok, State} ->
            {ok, New#handler{state = State}};
        Other ->
            %% As OTP reports it: reason crash, the failure as the state, the manager's name as the last event.
            report_terminate(New, crash, {error, Other}, Name, false),
            no
    end.

%% Calls to one handler.

call_handler(Handler, Query, Handlers, Name) ->
    case search(Handler, Handlers) of
        {ok, Found} ->
            case call_update(Found, Query, Name) of
                {no, Reply} -> {false, Reply, delete(Handler, Handlers)};
                {{ok, New}, Reply} -> {false, Reply, replace(Handler, Handlers, New)};
                {{hibernate, New}, Reply} -> {true, Reply, replace(Handler, Handlers, New)}
            end;
        false ->
            {false, {error, bad_module}, Handlers}
    end.

call_update(#handler{module = Module, state = State} = Handler, Query, Name) ->
    case catch Module:handle_call(Query, State) of
        {ok, Reply, New} ->
            {{ok, Handler#handler{state = New}}, Reply};
        {ok, Reply, New, hibernate} ->
            {{hibernate, Handler#handler{state = New}}, Reply};
        {swap_handler, Reply, Args1, New, Handler2, Args2} ->
            {swap(Module, Handler, Args1, New, Handler2, Args2, Name), Reply};
        {remove_handler, Reply} ->
            terminate_handler(Module, Handler, remove_handler, State, remove, Name, normal),
            {no, Reply};
        Other ->
            terminate_handler(Module, Handler, {error, Other}, State, Query, Name, crash),
            {no, {error, Other}}
    end.

%% A handler is named by its module (installed without an id) or {Module, Id}.
matches({Module, Id}, #handler{module = Module, id = Id}) -> true;
matches(Module, #handler{module = Module, id = false}) -> true;
matches(_, _) -> false.

split(Name, Handlers) ->
    case lists:splitwith(fun(H) -> not matches(Name, H) end, Handlers) of
        {Before, [Found | After]} -> {Found#handler.module, Found, Before ++ After};
        {_, []} -> error
    end.

search(Name, Handlers) ->
    case lists:search(fun(H) -> matches(Name, H) end, Handlers) of
        {value, Found} -> {ok, Found};
        false -> false
    end.

delete(Name, [H | Rest]) ->
    case matches(Name, H) of
        true -> Rest;
        false -> [H | delete(Name, Rest)]
    end;
delete(_, []) ->
    [].

replace(Name, [H | Rest], New) ->
    case matches(Name, H) of
        true -> [New | Rest];
        false -> [H | replace(Name, Rest, New)]
    end;
replace(_, [], New) ->
    [New].

handler_name(#handler{module = Module, id = false}) -> Module;
handler_name(#handler{module = Module, id = Id}) -> {Module, Id}.

%% Removing a handler: its terminate/2 (when exported) gets Args, the result is returned; a crash or another
%% How than normal, shutdown or a swap is reported, and a supervising process gets {gen_event_EXIT, Handler, How}.
terminate_handler(Module, Handler, Args, State, LastIn, Name, How) ->
    Result =
        case erlang:function_exported(Module, terminate, 2) of
            true -> catch Module:terminate(Args, State);
            false -> ok
        end,
    Reason =
        case {How, Args} of
            {crash, {error, Why}} -> Why;
            _ -> How
        end,
    report_terminate(Handler, Reason, State, LastIn, Name),
    Result.

report_terminate(Handler, Reason, State, LastIn, Name) ->
    report_error(Handler, Reason, State, LastIn, Name),
    case Handler#handler.supervised of
        false ->
            ok;
        Pid ->
            Pid ! {gen_event_EXIT, handler_name(Handler), Reason},
            ok
    end.

report_error(_, normal, _, _, _) ->
    ok;
report_error(_, shutdown, _, _, _) ->
    ok;
report_error(_, {swapped, _, _}, _, _, _) ->
    ok;
report_error(Handler, Exit, State, LastIn, Name) ->
    %% format_status/1 sees the reason without catch's 'EXIT' and stack, which are put back afterwards.
    {Reason, Rewrap} =
        case Exit of
            {'EXIT', {R, Stack}} -> {R, fun(Shown) -> {'EXIT', {Shown, Stack}} end};
            {'EXIT', R} -> {R, fun(Shown) -> {'EXIT', Shown} end};
            R -> {R, fun(Shown) -> Shown end}
        end,
    Status = gen:format_status(
        Handler#handler.module,
        terminate,
        #{
            state => State,
            message => LastIn,
            reason => Reason
        },
        [get(), State]
    ),
    ?LOG(
        error,
        #{
            label => {gen_event, terminate},
            handler => handler_name(Handler),
            name => Name,
            last_message => maps:get(message, Status),
            state => maps:get('$status', Status, maps:get(state, Status)),
            reason => Rewrap(maps:get(reason, Status)),
            process_label => proc_lib:get_label(self())
        },
        #{
            domain => [otp],
            report_cb => fun gen_event:format_log/2,
            error_logger => #{tag => error, report_cb => fun gen_event:format_log/1}
        }
    ).

%% sys callbacks; Misc is [Name, Handlers, Idle, Hibernated].

system_continue(Parent, Debug, [Name, Handlers, Idle, Hibernated]) ->
    loop(Parent, Name, Handlers, Idle, Debug, Hibernated).

system_terminate(Reason, Parent, _, [Name, Handlers, _, _]) ->
    terminate_server(Reason, Parent, Handlers, Name).

system_code_change([Name, Handlers, Idle, Hibernated], Module, OldVsn, Extra) ->
    Changed = lists:map(
        fun
            (#handler{module = M, state = State} = H) when M =:= Module ->
                {ok, New} = Module:code_change(OldVsn, State, Extra),
                H#handler{state = New};
            (H) ->
                H
        end,
        Handlers
    ),
    {ok, [Name, Changed, Idle, Hibernated]}.

system_get_state([_, Handlers, _, _]) ->
    {ok, [{M, Id, State} || #handler{module = M, id = Id, state = State} <- Handlers]}.

%% Each handler's state through StateFun; a handler whose fun fails keeps its state.
system_replace_state(StateFun, [Name, Handlers, Idle, Hibernated]) ->
    {New, States} = lists:unzip([replaced(StateFun, H) || H <- Handlers]),
    {ok, States, [Name, New, Idle, Hibernated]}.

replaced(StateFun, #handler{module = M, id = Id, state = State} = H) ->
    Current = {M, Id, State},
    try StateFun(Current) of
        {M, Id, New} = Replaced -> {H#handler{state = New}, Replaced};
        _ -> {H, Current}
    catch
        _:_ -> {H, Current}
    end.

print_event(Dev, {in, Msg}, Name) ->
    case Msg of
        {notify, Event} ->
            io:format(Dev, "*DBG* ~tp got event ~tp~n", [Name, Event]);
        {_, _, {call, Handler, Query}} ->
            io:format(Dev, "*DBG* ~tp(~tp) got call ~tp~n", [Name, Handler, Query]);
        _ ->
            io:format(Dev, "*DBG* ~tp got ~tp~n", [Name, Msg])
    end;
print_event(Dev, Event, Name) ->
    io:format(Dev, "*DBG* ~tp : ~tp~n", [Name, Event]).

format_status(Opt, [Dictionary, SysState, Parent, Debug, [Name, Handlers, _, _]]) ->
    Header = gen:format_status_header("Status for event handler", Name),
    {Shown, Logs} = lists:mapfoldl(
        fun(#handler{module = Module, state = State} = H, Log) ->
            Status = gen:format_status(Module, Opt, #{log => Log, state => State}, [
                Dictionary, State
            ]),
            {
                H#handler{state = maps:get('$status', Status, maps:get(state, Status))},
                maps:get(log, Status)
            }
        end,
        sys:get_log(Debug),
        Handlers
    ),
    [
        {header, Header},
        {data, [{"Status", SysState}, {"Logged Events", Logs}, {"Parent", Parent}]},
        {items, {"Installed handlers", Shown}}
    ].

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
        label := {gen_event, terminate},
        handler := Handler,
        name := Name,
        last_message := LastIn,
        state := State,
        reason := Reason,
        process_label := Label
    },
    #{single_line := true, depth := Depth} = Options
) ->
    P = p(Options),
    Format =
        lists:append([
            "Generic event handler ",
            P,
            " crashed. Installed: ",
            P,
            labelled(Label, ". Label: " ++ P),
            ". Last event: ",
            P,
            ". State: ",
            P,
            ". Reason: ",
            P,
            "."
        ]),
    {Format,
        depth_args(
            Depth,
            [Handler, Name] ++ labelled(Label, [Label]) ++ [LastIn, State, fix_reason(Reason)]
        )};
format_log_single(
    #{label := {gen_event, no_handle_info}, module := Module, message := Msg},
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
        label := {gen_event, terminate},
        handler := Handler,
        name := Name,
        last_message := LastIn,
        state := State,
        reason := Reason,
        process_label := Label
    },
    #{depth := Depth} = Options
) ->
    P = p(Options),
    Format =
        lists:append([
            "** gen_event handler ",
            P,
            " crashed.\n",
            "** Was installed in ",
            P,
            "\n",
            labelled(Label, "** Process label == " ++ P ++ "\n"),
            "** Last event was: ",
            P,
            "\n",
            "** When handler state == ",
            P,
            "\n",
            "** Reason == ",
            P,
            "\n"
        ]),
    {Format,
        depth_args(
            Depth,
            [Handler, Name] ++ labelled(Label, [Label]) ++ [LastIn, State, fix_reason(Reason)]
        )};
format_log_multi(
    #{label := {gen_event, no_handle_info}, module := Module, message := Msg},
    #{depth := Depth} = Options
) ->
    Args =
        case Depth of
            unlimited -> [Module, Msg];
            _ -> [Module, Msg, Depth]
        end,
    {"** Undefined handle_info in ~p\n** Unhandled message: " ++ p(Options) ++ "\n", Args}.

labelled(undefined, _) -> [];
labelled(_, Value) -> Value.

fix_reason({'EXIT', {undef, [{M, F, A, _} | _] = MFAs} = Reason}) ->
    case code:is_loaded(M) of
        false ->
            {'module could not be loaded', MFAs};
        _ ->
            case is_list(A) andalso erlang:function_exported(M, F, length(A)) of
                true -> Reason;
                false -> {'function not exported', MFAs}
            end
    end;
fix_reason({'EXIT', Reason}) ->
    Reason;
fix_reason(Reason) ->
    Reason.

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
