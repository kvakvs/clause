-module(gen_statems).
-behaviour(gen_statem).
-export([main/1]).
-export([init/1, callback_mode/0, terminate/3, locked/3, open/3]).

%% gen_statem (plan 21 step LIB21): a door with state functions and state enter calls (this module) and a
%% handle_event_function machine (statem_events): calls, casts and infos, postponing, inserted events, event, state
%% and generic time-outs with cancel, update and zero times, replies as actions and stop_and_reply, callback module
%% changes, errors for bad results and actions, starts and stops, request ids and sys. Callbacks report to the test
%% process registered as sm_main.
main(_) ->
    process_flag(trap_exit, true),
    register(sm_main, self()),
    door(),
    postponing(),
    timeouts(),
    events(),
    modules(),
    errors(),
    starts(),
    io:format("gen_statems: ok~n").

%% The door: locked until the code is entered, open until a time-out locks it again.

init({door, Code}) -> {ok, locked, #{code => Code, entered => [], opened => 0}};
init({actions, Actions}) -> {ok, locked, #{code => [], entered => [], opened => 0}, Actions};
init(Other) -> statem_events:init(Other).

callback_mode() -> [state_functions, state_enter].

locked(enter, Old, Data) ->
    report({enter, locked, Old}),
    {keep_state, Data#{entered := []}};
locked(cast, {button, Digit}, #{code := Code, entered := Entered} = Data) ->
    case Entered ++ [Digit] of
        Code ->
            {next_state, open, Data};
        Partial when length(Partial) < length(Code) ->
            {keep_state, Data#{entered := Partial}, [{state_timeout, 300, reset}]};
        _ ->
            {keep_state, Data#{entered := []}}
    end;
locked(state_timeout, reset, Data) ->
    report(reset),
    {keep_state, Data#{entered := []}};
locked({call, From}, status, Data) ->
    {keep_state_and_data, [{reply, From, {locked, maps:get(entered, Data)}}]};
locked({call, From}, open_now, Data) ->
    {next_state, open, Data, [{reply, From, opening}]};
locked(info, Msg, _) ->
    report({info, locked, Msg}),
    keep_state_and_data;
locked(EventType, Event, _) ->
    report({unexpected, locked, EventType, Event}),
    keep_state_and_data.

open(enter, Old, #{opened := N} = Data) ->
    report({enter, open, Old}),
    {keep_state, Data#{opened := N + 1}, [{state_timeout, 250, lock}]};
open(state_timeout, lock, Data) ->
    report(locking),
    {next_state, locked, Data};
open({call, From}, status, _) ->
    {keep_state_and_data, [{reply, From, open}]};
open(cast, {button, _}, _) ->
    {keep_state_and_data, [postpone]};
open(EventType, Event, _) ->
    report({unexpected, open, EventType, Event}),
    keep_state_and_data.

terminate(Reason, State, _) ->
    report({terminate, strip(Reason), State}).

report(Event) -> sm_main ! {report, Event}.

%% Tests.

door() ->
    {ok, Door} = gen_statem:start_link({local, sm_door}, ?MODULE, {door, [1, 2, 3]}, []),
    show(start, reports()),
    show(status, gen_statem:call(sm_door, status)),
    gen_statem:cast(sm_door, {button, 1}),
    gen_statem:cast(sm_door, {button, 2}),
    show(partial, gen_statem:call(Door, status)),
    timer:sleep(400),
    show(reset, [reports(), gen_statem:call(Door, status)]),
    [gen_statem:cast(Door, {button, D}) || D <- [1, 2, 3]],
    show(opened, [gen_statem:call(Door, status), reports()]),
    timer:sleep(400),
    show(locked_again, [reports(), gen_statem:call(Door, status)]),
    Door ! hello,
    show(info, [gen_statem:call(Door, status), reports()]),
    show(call_open, [gen_statem:call(Door, open_now), gen_statem:call(Door, status, 1000)]),
    timer:sleep(400),
    show(after_open, reports()),
    show(sys, [
        element(1, sys:get_state(Door)), lists:sort(maps:to_list(element(2, sys:get_state(Door))))
    ]),
    {status, _, {module, gen_statem}, [_, running, _, _, Misc]} = sys:get_status(Door),
    show(status_header, lists:keyfind(header, 1, Misc)),
    ok = gen_statem:stop(Door),
    show(stopped, [reports(), receive_exit(Door)]).

%% Buttons pressed while the door is open are postponed until it locks again.
postponing() ->
    {ok, Door} = gen_statem:start({local, sm_door}, ?MODULE, {door, [7]}, []),
    reports(),
    gen_statem:cast(Door, {button, 7}),
    gen_statem:cast(Door, {button, 7}),
    gen_statem:cast(Door, {button, 5}),
    show(open, gen_statem:call(Door, status)),
    timer:sleep(400),
    show(retried, [reports(), gen_statem:call(Door, status)]),
    timer:sleep(400),
    show(reports, reports()),
    ok = gen_statem:stop(Door, normal, 1000),
    reports().

timeouts() ->
    {ok, M} = gen_statem:start(statem_events, {events, timeouts}, []),
    show(event_timeout, [gen_statem:call(M, {actions, [{timeout, 30, ev}]}), receive_report()]),
    gen_statem:call(M, {actions, [{timeout, 50, never}]}),
    gen_statem:cast(M, cancels),
    show(event_timeout_cancelled, [receive_report(), quiet(100)]),
    show(generic, [
        gen_statem:call(M, {actions, [{{timeout, a}, 30, ga}, {{timeout, b}, 10, gb}]}),
        receive_report(),
        receive_report()
    ]),
    gen_statem:call(M, {actions, [{{timeout, c}, 50, gc}, {{timeout, c}, cancel}]}),
    show(generic_cancel, quiet(100)),
    gen_statem:call(M, {actions, [{{timeout, d}, 50, old}, {{timeout, d}, update, new}]}),
    show(generic_update, receive_report()),
    gen_statem:call(M, {actions, [{state_timeout, update, inserted}]}),
    show(update_inserts, receive_report()),
    show(zero, [
        gen_statem:call(
            M,
            {actions, [
                {timeout, 0, z1},
                {{timeout, z}, 0, z2},
                {state_timeout, 0, z3},
                {next_event, internal, before_zero}
            ]}
        ),
        receive_report(),
        receive_report(),
        receive_report(),
        receive_report()
    ]),
    show(state_change, [
        gen_statem:call(M, {actions, [{state_timeout, 50, st}]}),
        gen_statem:call(M, {goto, other}),
        quiet(100)
    ]),
    Abs = erlang:monotonic_time(millisecond) + 30,
    show(absolute, [
        gen_statem:call(M, {actions, [{{timeout, abs}, Abs, absolute, {abs, true}}]}),
        receive_report()
    ]),
    show(relative_time, [gen_statem:call(M, {actions, 20}), receive_report()]),
    show(infinity, [gen_statem:call(M, {actions, [{timeout, infinity, x}]}), quiet(50)]),
    show(sys_timeouts, element(2, lists:keyfind("Time-outs", 1, status_data(M)))),
    gen_statem:call(M, {actions, [{{timeout, late}, 1000, late}]}),
    show(sys_timeouts, element(2, lists:keyfind("Time-outs", 1, status_data(M)))),
    ok = gen_statem:stop(M).

events() ->
    {ok, M} = gen_statem:start(statem_events, {events, events}, []),
    show(next_events, [
        gen_statem:call(
            M,
            {actions, [
                {next_event, internal, i1},
                {next_event, cast, c1},
                {next_event, info, n1}
            ]}
        ),
        receive_report(),
        receive_report(),
        receive_report()
    ]),
    show(hibernate, [
        gen_statem:call(M, {actions, [hibernate]}),
        gen_statem:call(M, {actions, [{hibernate, false}]})
    ]),
    gen_statem:cast(M, {postpone_in, here}),
    show(postponed, [quiet(50), gen_statem:call(M, {goto, there}), receive_report()]),
    Caller = self(),
    gen_statem:cast(M, {reply_later, Caller}),
    show(repeat, [gen_statem:call(M, repeat), receive_report()]),
    show(stop_and_reply, [
        gen_statem:call(M, stop_and_reply), receive_report(), receive_down_or_exit(M)
    ]),
    {ok, M2} = gen_statem:start(statem_events, {events, events}, []),
    R1 = gen_statem:send_request(M2, {echo, 1}),
    show(request, [
        gen_statem:receive_response(R1),
        gen_statem:wait_response(gen_statem:send_request(M2, {echo, 2}))
    ]),
    C1 = gen_statem:send_request(M2, {echo, 3}, three, gen_statem:reqids_new()),
    show(collection, [
        gen_statem:receive_response(C1, infinity, true) =/= x, gen_statem:reqids_size(C1)
    ]),
    gen_statem:cast(M2, {stop, normal}),
    show(cast_stop, receive_report()),
    timer:sleep(50),
    show(alive, is_process_alive(M2)).

modules() ->
    {ok, M} = gen_statem:start(statem_events, {events, modules}, []),
    show(push, [gen_statem:call(M, {actions, [{push_callback_module, ?MODULE}]}), reports()]),
    show(pushed_state, catch_call(M, status)),
    {ok, M2} = gen_statem:start(statem_events, {events, modules}, []),
    show(change, gen_statem:call(M2, {actions, [{change_callback_module, statem_events}]})),
    show(pop, [
        gen_statem:call(
            M2, {actions, [{push_callback_module, statem_events}, pop_callback_module]}
        ),
        gen_statem:call(M2, {echo, popped})
    ]),
    show(pop_last, catch_call(M2, {actions, [pop_callback_module]})),
    show(reports, reports()).

errors() ->
    Results = [
        {bad_return, strange},
        {bad_action, [{reply, not_a_from, x}]},
        {bad_action, [{timeout, -1, x}]},
        {bad_action, [{next_event, bogus, x}]},
        {bad_action, [{state_timeout, 10, x, [{abs, maybe_not}]}]},
        {bad_action, [{push_callback_module, "m"}]},
        {bad_action, [pop_callback_module]},
        {bad_action, [{postpone, 1}]},
        {crash, crashed},
        {throw, {keep_state_and_data, [{reply, caller, thrown}]}}
    ],
    show(errors, [error_case(R) || R <- Results]),
    show(reports, reports()),
    {ok, M} = gen_statem:start(statem_events, {events, errors}, []),
    show(enter_postpone, catch_call(M, {goto, postpone_on_enter})),
    {ok, M2} = gen_statem:start(statem_events, {events, errors}, []),
    show(enter_next_state, catch_call(M2, {goto, change_on_enter})),
    show(reports, reports()).

error_case(Request) ->
    {ok, M} = gen_statem:start(statem_events, {events, errors}, []),
    Result = catch_call(M, Request),
    timer:sleep(20),
    Result.

starts() ->
    show(init_results, [
        gen_statem:start(statem_events, {stop, no_reason}, []),
        gen_statem:start(statem_events, {error, failed}, []),
        gen_statem:start(statem_events, ignore, []),
        gen_statem:start(statem_events, {bad, odd}, []),
        strip(gen_statem:start(statem_events, {crash, boom}, [])),
        gen_statem:start(statem_events, {mode, [bad_mode]}, []),
        gen_statem:start(statem_events, {mode, []}, []),
        element(
            1, gen_statem:start(statem_events, {mode, [handle_event_function, state_enter]}, [])
        )
    ]),
    reports(),
    {ok, {M, Ref}} = gen_statem:start_monitor({local, sm_mon}, statem_events, {events, starts}, []),
    show(monitor, [
        is_reference(Ref), element(1, gen_statem:start({local, sm_mon}, statem_events, x, []))
    ]),
    ok = gen_statem:stop(sm_mon, shutdown, infinity),
    show(down, [
        receive_report(),
        receive
            {'DOWN', Ref, process, M, Reason} -> Reason
        after 1000 -> timeout
        end
    ]),
    {ok, Actions} = gen_statem:start(?MODULE, {actions, [{state_timeout, 10, reset}]}, []),
    timer:sleep(50),
    show(init_actions, reports()),
    gen_statem:stop(Actions),
    show(call_errors, [
        catch_call(sm_nobody, status),
        catch_exit(fun() -> gen_statem:call(sm_nobody, status, {dirty_timeout, 10}) end),
        catch_error(fun() -> gen_statem:call(sm_nobody, status, {other, 10}) end),
        catch_error(fun() -> gen_statem:start("m", x, []) end),
        gen_statem:cast(sm_nobody, x),
        gen_statem:reply({self(), make_ref()}, direct) =:= ok,
        gen_statem:reply([]),
        catch_error(fun() -> gen_statem:enter_loop("m", [], s, d) end)
    ]),
    flush().

%% Helpers.

status_data(M) ->
    {status, _, _, [_, _, _, _, Misc]} = sys:get_status(M),
    {data, Data} = lists:keyfind(data, 1, Misc),
    Data.

catch_call(M, Request) ->
    try gen_statem:call(M, Request) of
        Reply -> {reply, Reply}
    catch
        exit:{Reason, {gen_statem, call, _}} -> {exit, strip(Reason)}
    end.

catch_exit(F) ->
    try F() of
        Value -> {value, Value}
    catch
        exit:{Reason, {gen_statem, Function, _}} -> {exit, Reason, Function}
    end.

catch_error(F) ->
    try F() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, strip(Reason)}
    end.

reports() ->
    receive
        {report, Event} -> [Event | reports()]
    after 50 -> []
    end.

receive_report() ->
    receive
        {report, Event} -> Event
    after 1000 -> timeout
    end.

receive_exit(Pid) ->
    receive
        {'EXIT', Pid, Reason} -> Reason
    after 1000 -> timeout
    end.

receive_down_or_exit(Pid) ->
    timer:sleep(20),
    is_process_alive(Pid).

quiet(Time) ->
    receive
        {report, Event} -> {unexpected, Event}
    after Time -> true
    end.

flush() ->
    receive
        _ -> flush()
    after 50 -> ok
    end.

%% Drops stacks from exit reasons so that OTP's and Clause's stacks compare equal.
strip({Reason, [{M, F, A, L} | _]}) when
    is_atom(M), is_atom(F), is_list(L), is_list(A) orelse is_integer(A)
->
    {strip(Reason), stack};
strip({'EXIT', Reason}) ->
    {'EXIT', strip(Reason)};
strip({error, Reason}) ->
    {error, strip(Reason)};
strip({Tag, Reason}) when is_atom(Tag) -> {Tag, strip(Reason)};
strip(Reason) ->
    Reason.

show(Label, Value) -> io:format("~p: ~p~n", [Label, no_pids(Value)]).

no_pids(Pid) when is_pid(Pid) -> pid;
no_pids(Ref) when is_reference(Ref) -> ref;
no_pids([H | T]) -> [no_pids(H) | no_pids(T)];
no_pids(Tuple) when is_tuple(Tuple) -> list_to_tuple(no_pids(tuple_to_list(Tuple)));
no_pids(Map) when is_map(Map) -> lists:sort([{no_pids(K), no_pids(V)} || K := V <- Map]);
no_pids(Other) -> Other.
