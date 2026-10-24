-module(gen_servers).
-behaviour(gen_server).
-compile(nowarn_deprecated_catch).
-export([main/1]).
-export([
    init/1,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    handle_continue/2,
    terminate/2,
    format_status/1
]).
-export([loop_entry/1, plain_reply/0]).

%% gen_server (plan 21 step LIB21): starts with names and results of init/1, calls, casts and info, deferred
%% replies, timeouts and continue actions, stops and crashes with their exit reasons, request ids and their
%% collections, multi_call and abcast on the local node, enter_loop, sys:get_status/1 through format_status/1, and
%% bad arguments. Crashes write error reports on stderr; stdout shows reasons without stacks.
main(_) ->
    process_flag(trap_exit, true),
    starts(),
    requests(),
    actions(),
    deferred(),
    request_ids(),
    multi(),
    entered(),
    stops(),
    crashes(),
    status(),
    bad_arguments(),
    io:format("gen_servers: ok~n").

%% Callbacks: the state is a map of what the server logged; the test process to notify is in the dictionary.

init({ok, Main}) ->
    put(main, Main),
    {ok, #{log => []}};
init({action, Main, Action}) ->
    put(main, Main),
    {ok, #{log => []}, Action};
init({trap, Main}) ->
    process_flag(trap_exit, true),
    init({ok, Main});
init({stop, Reason}) ->
    {stop, Reason};
init({error, Reason}) ->
    {error, Reason};
init(ignore) ->
    ignore;
init({throw, Value}) ->
    throw(Value);
init({crash, Reason}) ->
    erlang:error(Reason);
init({exit, Reason}) ->
    exit(Reason);
init({bad, Value}) ->
    Value.

handle_call(state, _, State) ->
    {reply, maps:get(log, State), State};
handle_call({echo, X}, _, State) ->
    {reply, {echo, X}, State};
handle_call({reply_action, X, Action}, _, State) ->
    {reply, X, State, Action};
handle_call({defer, Tag}, From, State) ->
    get(main) ! {deferred, Tag, From},
    {noreply, State};
handle_call({throw, Value}, _, State) ->
    throw({reply, Value, State});
handle_call({stop, Reason, Reply}, _, State) ->
    {stop, Reason, Reply, State};
handle_call({stop, Reason}, _, State) ->
    {stop, Reason, State};
handle_call({crash, Reason}, _, _) ->
    erlang:error(Reason);
handle_call({bad, Value}, _, _) ->
    Value;
handle_call({sleep, Time}, _, State) ->
    timer:sleep(Time),
    {reply, slept, State};
handle_call(self_call, _, State) ->
    {reply, catch_call(self(), state), State}.

handle_cast({log, X}, #{log := Log} = State) ->
    {noreply, State#{log := [X | Log]}};
handle_cast({action, Action}, State) ->
    {noreply, State, Action};
handle_cast({stop, Reason}, State) ->
    {stop, Reason, State}.

handle_info(Msg, #{log := Log} = State) ->
    get(main) ! {info, Msg},
    {noreply, State#{log := [{info, Msg} | Log]}}.

handle_continue(Continue, #{log := Log} = State) ->
    get(main) ! {continued, Continue},
    case Continue of
        {again, X} -> {noreply, State#{log := [X | Log]}, {continue, X}};
        _ -> {noreply, State#{log := [Continue | Log]}}
    end.

terminate(Reason, _) ->
    case get(main) of
        undefined -> ok;
        Main -> Main ! {terminated, strip(Reason)}
    end.

format_status(Status) ->
    maps:map(
        fun
            (state, #{log := Log}) -> {log_length, length(Log)};
            (_, Value) -> Value
        end,
        Status
    ).

%% Tests.

starts() ->
    Self = self(),
    {ok, Plain} = gen_server:start(?MODULE, {ok, Self}, []),
    {ok, Linked} = gen_server:start_link({local, gs_named}, ?MODULE, {ok, Self}, []),
    {ok, {Monitored, Ref}} = gen_server:start_monitor(?MODULE, {ok, Self}, []),
    show(started, [
        is_pid(Plain), whereis(gs_named) =:= Linked, is_reference(Ref), is_pid(Monitored)
    ]),
    show(again, [
        same(
            gen_server:start({local, gs_named}, ?MODULE, {ok, Self}, []),
            {error, {already_started, Linked}}
        ),
        same(
            gen_server:start_link({local, gs_named}, ?MODULE, {ok, Self}, []),
            {error, {already_started, Linked}}
        )
    ]),
    show(init_results, [
        gen_server:start(?MODULE, {stop, no_reason}, []),
        gen_server:start(?MODULE, {error, failed}, []),
        gen_server:start(?MODULE, ignore, []),
        element(1, gen_server:start(?MODULE, {throw, {ok, #{log => [thrown]}}}, [])),
        strip(gen_server:start(?MODULE, {crash, boom}, [])),
        gen_server:start(?MODULE, {exit, gone}, []),
        gen_server:start(?MODULE, {bad, odd}, []),
        bad_return(gen_server:start(?MODULE, {action, Self, {bad_action}}, [])),
        gen_server:start({local, gs_failing}, ?MODULE, {stop, again}, []),
        whereis(gs_failing),
        gen_server:start_monitor(?MODULE, {error, monitored}, []),
        element(
            1,
            gen_server:start(?MODULE, {ok, Self}, [
                {timeout, 1000}, {spawn_opt, [{priority, normal}]}
            ])
        )
    ]),
    flush(),
    ok = gen_server:stop(Plain),
    ok = gen_server:stop(gs_named),
    ok = gen_server:stop(Monitored),
    show(stopped_down, receive_down(Ref)),
    flush().

requests() ->
    {ok, Pid} = gen_server:start({local, gs_requests}, ?MODULE, {ok, self()}, []),
    show(calls, [
        gen_server:call(Pid, {echo, 1}),
        gen_server:call(gs_requests, {echo, 2}),
        gen_server:call({gs_requests, node()}, {echo, 3}),
        gen_server:call(gs_requests, {echo, 4}, 1000),
        gen_server:call(gs_requests, {echo, 5}, infinity),
        gen_server:call(gs_requests, {throw, thrown_reply})
    ]),
    ok = gen_server:cast(gs_requests, {log, a}),
    ok = gen_server:cast(Pid, {log, b}),
    ok = gen_server:cast({gs_requests, node()}, {log, c}),
    ok = gen_server:cast(gs_nobody, {log, lost}),
    ok = gen_server:cast({gs_nobody, node()}, {log, lost}),
    Pid ! plain_info,
    show(info, receive_one()),
    show(log, gen_server:call(Pid, state)),
    show(call_errors, [
        catch gen_server:call(gs_nobody, x),
        catch gen_server:call({gs_nobody, node()}, x),
        catch gen_server:call({gs_nobody, other@nohost}, x),
        catch gen_server:call(gs_requests, {sleep, 200}, 50),
        gen_server:call(gs_requests, self_call)
    ]),
    timer:sleep(250),
    flush(),
    show(reply_to_self, catch_call(self(), x)),
    ok = gen_server:stop(Pid).

%% Actions of callbacks: timeouts, hibernation and continue.
actions() ->
    Self = self(),
    {ok, Pid} = gen_server:start(?MODULE, {action, Self, {continue, first}}, []),
    show(continue, receive_one()),
    show(reply_continue, gen_server:call(Pid, {reply_action, r1, {continue, {again, second}}})),
    show(continued, [receive_one(), receive_one()]),
    show(timeout_info, gen_server:call(Pid, {reply_action, r2, 20})),
    show(timed_out, receive_one()),
    gen_server:cast(Pid, {action, {timeout, 20, my_timeout}}),
    show(timeout_message, receive_one()),
    gen_server:cast(Pid, {action, {timeout, 300, never_sent}}),
    Pid ! cancels_timeout,
    show(cancelled_timeout, [receive_one(), quiet(400)]),
    gen_server:cast(Pid, {action, {timeout, 0, zero}}),
    show(zero_timeout, receive_one()),
    Deadline = erlang:monotonic_time(millisecond) + 20,
    gen_server:cast(Pid, {action, {timeout, Deadline, absolute, {abs, true}}}),
    show(absolute_timeout, receive_one()),
    gen_server:cast(Pid, {action, {hibernate, 20, after_hibernate, [{abs, false}]}}),
    show(hibernate_timeout, receive_one()),
    show(hibernate, gen_server:call(Pid, {reply_action, r3, hibernate})),
    show(after_hibernate, gen_server:call(Pid, {echo, awake})),
    show(infinity_action, gen_server:call(Pid, {reply_action, r4, infinity})),
    show(bad_reply_action, catch_call(Pid, {reply_action, r5, {timeout, -1, bad}})),
    show(terminated, receive_one()),
    flush().

%% A reply sent later with gen_server:reply/2, from the server or another process.
deferred() ->
    {ok, Pid} = gen_server:start(?MODULE, {ok, self()}, []),
    Caller = spawn_link(fun() -> exit({result, gen_server:call(Pid, {defer, one})}) end),
    receive
        {deferred, one, From} ->
            show(from, [element(1, From) =:= Caller, is_tuple(From)]),
            show(reply, gen_server:reply(From, later))
    end,
    show(caller, receive_exit(Caller)),
    ok = gen_server:stop(Pid).

request_ids() ->
    flush(),
    {ok, Pid} = gen_server:start({local, gs_ids}, ?MODULE, {ok, self()}, []),
    R1 = gen_server:send_request(gs_ids, {echo, 1}),
    show(receive_response, gen_server:receive_response(R1, 1000)),
    R2 = gen_server:send_request(Pid, {echo, 2}),
    show(wait_response, gen_server:wait_response(R2, infinity)),
    R3 = gen_server:send_request(Pid, {echo, 3}),
    show(
        check_response,
        receive
            Msg -> gen_server:check_response(Msg, R3)
        end
    ),
    R4 = gen_server:send_request(Pid, {sleep, 100}),
    show(response_timeout, [gen_server:wait_response(R4, 0), gen_server:receive_response(R4, 300)]),
    R5 = gen_server:send_request(gs_ids_missing, x),
    show(missing, strip(gen_server:receive_response(R5, 1000))),
    show(not_a_reply, gen_server:check_response(other, make_ref())),
    C0 = gen_server:reqids_new(),
    C1 = gen_server:send_request(Pid, {echo, a}, label_a, C0),
    C2 = gen_server:send_request(Pid, {echo, b}, label_b, C1),
    show(collection, [
        gen_server:reqids_size(C2), lists:sort([L || {_, L} <- gen_server:reqids_to_list(C2)])
    ]),
    {Got1, Label1, C3} = gen_server:receive_response(C2, 1000, true),
    {Got2, Label2, C4} = gen_server:wait_response(C3, 1000, true),
    show(collected, lists:sort([{Label1, Got1}, {Label2, Got2}])),
    show(empty_collection, [
        gen_server:reqids_size(C4),
        gen_server:receive_response(C4, 0, true),
        gen_server:wait_response(C4, 0, false),
        gen_server:check_response(x, C4, true)
    ]),
    C5 = gen_server:reqids_add(gen_server:send_request(Pid, {echo, c}), label_c, C4),
    {Got3, label_c, C6} = gen_server:receive_response(C5, 1000, false),
    show(kept, [Got3, gen_server:reqids_size(C6)]),
    show(reqid_errors, [
        catch_error(fun() -> gen_server:reqids_add(not_ref, x, C0) end),
        catch_error(fun() -> gen_server:reqids_size(not_map) end),
        catch_error(fun() -> gen_server:reqids_to_list(not_map) end),
        catch_error(fun() -> gen_server:wait_response(make_ref(), -1) end),
        catch_error(fun() -> gen_server:check_response(x, not_ref) end),
        catch_error(fun() -> gen_server:receive_response(C0, 0, perhaps) end)
    ]),
    ok = gen_server:stop(Pid).

multi() ->
    {ok, Pid} = gen_server:start({local, gs_multi}, ?MODULE, {ok, self()}, []),
    show(multi_call, [
        gen_server:multi_call(gs_multi, {echo, m}),
        gen_server:multi_call([node()], gs_multi, {echo, n}),
        gen_server:multi_call([node(), other@nohost], gs_multi, {echo, o}, 1000),
        gen_server:multi_call([node()], gs_multi_missing, x)
    ]),
    show(abcast, [
        gen_server:abcast(gs_multi, {log, x}),
        gen_server:abcast([node(), other@nohost], gs_multi, {log, y})
    ]),
    show(abcast_log, gen_server:call(Pid, state)),
    ok = gen_server:stop(Pid).

%% A process that becomes a server itself.
entered() ->
    Self = self(),
    {ok, Pid} = proc_lib:start(?MODULE, loop_entry, [Self]),
    show(entered, gen_server:call(Pid, {echo, entered})),
    show(entered_name, whereis(gs_entered) =:= Pid),
    show(entered_continue, receive_one()),
    ok = gen_server:stop(Pid),
    show(not_registered, catch proc_lib:start(?MODULE, plain_reply, [])),
    flush().

loop_entry(Main) ->
    put(main, Main),
    register(gs_entered, self()),
    proc_lib:init_ack({ok, self()}),
    gen_server:enter_loop(?MODULE, [], #{log => []}, {local, gs_entered}, {continue, entered}).

plain_reply() ->
    proc_lib:init_ack(ok),
    try
        gen_server:enter_loop(?MODULE, [], #{}, {local, gs_unregistered})
    catch
        exit:Reason -> exit(Reason)
    end.

stops() ->
    Self = self(),
    {ok, P1} = gen_server:start(?MODULE, {ok, Self}, []),
    show(stop_reply, gen_server:call(P1, {stop, normal, bye})),
    show(stop_terminate, receive_one()),
    {ok, P2} = gen_server:start(?MODULE, {ok, Self}, []),
    show(stop_call, catch_call(P2, {stop, shutdown})),
    show(stop_terminate, receive_one()),
    {ok, P3} = gen_server:start(?MODULE, {ok, Self}, []),
    Ref3 = monitor(process, P3),
    gen_server:cast(P3, {stop, {shutdown, done}}),
    show(cast_stop, [receive_one(), receive_down(Ref3)]),
    {ok, P4} = gen_server:start(?MODULE, {ok, Self}, []),
    show(stop3, [gen_server:stop(P4, {shutdown, x}, 1000), receive_one()]),
    {Dead, DeadRef} = spawn_monitor(fun() -> ok end),
    receive_down(DeadRef),
    show(stop_missing, [catch gen_server:stop(gs_nobody), catch gen_server:stop(Dead)]),
    {ok, P5} = gen_server:start_link(?MODULE, {trap, Self}, []),
    exit(P5, parent_gone),
    show(parent_exit, [receive_one(), receive_exit(P5)]),
    flush().

crashes() ->
    Self = self(),
    {ok, P1} = gen_server:start(?MODULE, {ok, Self}, []),
    Ref1 = monitor(process, P1),
    show(crash_call, catch_call(P1, {crash, broken})),
    show(crash_terminate, receive_one()),
    show(crash_down, receive_down(Ref1)),
    {ok, P2} = gen_server:start(?MODULE, {ok, Self}, []),
    Ref2 = monitor(process, P2),
    show(bad_return, catch_call(P2, {bad, strange})),
    show(bad_terminate, receive_one()),
    show(bad_down, receive_down(Ref2)),
    flush().

status() ->
    {ok, Pid} = gen_server:start({local, gs_status}, ?MODULE, {ok, self()}, []),
    gen_server:cast(Pid, {log, one}),
    {status, Pid, {module, gen_server}, [_Dictionary, running, _Parent, [], Misc]} = sys:get_status(
        gs_status
    ),
    show(status, [Item || Item <- Misc, element(1, Item) =/= data orelse not has_parent(Item)]),
    show(get_state, maps:get(log, sys:get_state(gs_status))),
    show(
        replace_state,
        maps:get(log, sys:replace_state(gs_status, fun(S) -> S#{log := [replaced]} end))
    ),
    show(after_replace, gen_server:call(Pid, state)),
    ok = sys:suspend(gs_status),
    gen_server:cast(Pid, {log, while_suspended}),
    show(suspended, lists:nth(2, element(4, sys:get_status(gs_status)))),
    ok = sys:resume(gs_status),
    show(resumed, gen_server:call(Pid, state)),
    ok = gen_server:stop(Pid).

has_parent({data, Items}) -> lists:keymember("Parent", 1, Items).

bad_arguments() ->
    show(bad_arguments, [
        catch_error(fun() -> gen_server:start(id("m"), x, []) end),
        catch_error(fun() -> gen_server:start(?MODULE, x, id(nolist)) end),
        catch_error(fun() -> gen_server:start(id(name), ?MODULE, x, []) end),
        catch_error(fun() -> gen_server:start_link(?MODULE, x, id(nolist)) end),
        catch_error(fun() -> gen_server:start_monitor(id(1), x, []) end),
        catch_error(fun() -> gen_server:start({local, gs_x}, id("m"), x, []) end),
        catch_error(fun() -> gen_server:send_request(id(1), x) end),
        catch_error(fun() -> gen_server:cast(id(1), x) end)
    ]).

%% Helpers.

catch_call(Pid, Request) ->
    try gen_server:call(Pid, Request) of
        Reply -> {reply, Reply}
    catch
        exit:{Reason, {gen_server, call, _}} -> {exit, strip(Reason)}
    end.

catch_error(F) ->
    try F() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, strip(Reason)}
    end.

%% Drops stacks from exit reasons so that OTP's and Clause's stacks compare equal.
strip({Reason, [{M, F, _, L} | _]}) when is_atom(M), is_atom(F), is_list(L) ->
    {strip(Reason), stack};
strip({'EXIT', Reason}) ->
    {'EXIT', strip(Reason)};
strip({error, Reason}) ->
    {error, strip(Reason)};
strip({Tag, Reason}) when is_atom(Tag) -> {Tag, strip(Reason)};
strip(Reason) ->
    Reason.

%% A bad init/1 action: {error, {bad_return_value, {ok, State, Action}}}, shown without the state.
bad_return({error, {bad_return_value, {ok, _, Action}}}) -> {bad_return_value, Action};
bad_return(Other) -> Other.

same(A, A) -> true;
same(_, _) -> false.

receive_one() ->
    receive
        Msg -> strip(Msg)
    after 2000 -> timeout
    end.

receive_down(Ref) ->
    receive
        {'DOWN', Ref, process, _, Reason} -> strip(Reason)
    after 2000 -> timeout
    end.

receive_exit(Pid) ->
    receive
        {'EXIT', Pid, Reason} -> strip(Reason)
    after 2000 -> timeout
    end.

quiet(Time) ->
    receive
        Msg -> {unexpected, Msg}
    after Time -> true
    end.

flush() ->
    receive
        _ -> flush()
    after 50 -> ok
    end.

show(Label, Value) -> io:format("~p: ~p~n", [Label, Value]).

id(X) -> X.
