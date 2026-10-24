-module(gen_events).
-behaviour(gen_event).
-export([main/1]).
-export([init/1, handle_event/2, handle_call/2, handle_info/2, terminate/2]).

%% gen_event (plan 21 step LIB21): handlers added by module and by {Module, Id}, notify and sync_notify, calls to
%% one handler, handler results (swap, remove, crash, bad returns), supervised handlers and their gen_event_EXIT
%% messages, which_handlers, request ids, sys state, and stopping the manager. Handlers report to the test process
%% registered as ev_main; their states hold no pids.
main(_) ->
    process_flag(trap_exit, true),
    register(ev_main, self()),
    handlers(),
    events(),
    calls(),
    results(),
    supervised(),
    request_ids(),
    managers(),
    io:format("gen_events: ok~n").

%% Handler callbacks: the state is {Name, Count}.

init({crash, Reason}) ->
    erlang:error(Reason);
init({error, Reason}) ->
    {error, Reason};
init({swapped, {Args, State}}) ->
    report({init, swapped, Args, State}),
    {ok, {swapped, 0}};
init(hibernate) ->
    {ok, {hibernated, 0}, hibernate};
init(Name) ->
    report({init, Name}),
    {ok, {Name, 0}}.

handle_event(remove, {Name, _}) ->
    report({removing, Name}),
    remove_handler;
handle_event(crash, {Name, _}) ->
    erlang:error({crashed, Name});
handle_event(bad, _) ->
    strange_return;
handle_event({swap, To}, {Name, Count}) ->
    {swap_handler, {from, Name}, {Name, Count}, To, {to, To}};
handle_event(Event, {Name, Count}) ->
    report({event, Name, Event}),
    {ok, {Name, Count + 1}}.

handle_call(count, {Name, Count} = State) ->
    {ok, {Name, Count}, State};
handle_call(remove, {Name, _}) ->
    {remove_handler, {removed, Name}};
handle_call(crash, _) ->
    erlang:error(call_crash);
handle_call({swap, To}, {Name, Count}) ->
    {swap_handler, swapping, {from, Name}, {Name, Count}, To, {to, To}};
handle_call(hibernate, State) ->
    {ok, hibernating, State, hibernate}.

handle_info(Info, {Name, Count}) ->
    report({info, Name, strip(Info)}),
    {ok, {Name, Count}}.

terminate(Args, {Name, Count}) ->
    report({terminate, Name, strip(Args), Count}),
    {terminated, Name}.

report(Event) -> ev_main ! {report, Event}.

%% Tests.

handlers() ->
    {ok, M} = gen_event:start_link({local, ev_mgr}),
    show(added, [
        gen_event:add_handler(ev_mgr, ?MODULE, h1),
        gen_event:add_handler(ev_mgr, {?MODULE, a}, h2),
        gen_event:add_handler(ev_mgr, {?MODULE, b}, h3),
        strip(gen_event:add_handler(ev_mgr, {?MODULE, c}, {crash, broke})),
        gen_event:add_handler(ev_mgr, {?MODULE, d}, {error, refused}),
        gen_event:add_handler(ev_mgr, {?MODULE, e}, hibernate)
    ]),
    show(reports, reports()),
    show(which_handlers, gen_event:which_handlers(ev_mgr)),
    show(deleted, [
        gen_event:delete_handler(ev_mgr, {?MODULE, e}, bye),
        gen_event:delete_handler(ev_mgr, {?MODULE, e}, again),
        gen_event:delete_handler(ev_mgr, nobody, x)
    ]),
    show(reports, reports()),
    show(sys_state, sys:get_state(ev_mgr)),
    show(
        replaced, sys:replace_state(ev_mgr, fun({Mod, Id, {Name, _}}) -> {Mod, Id, {Name, 100}} end)
    ),
    ok = gen_event:stop(M),
    show(stopped, reports()),
    show(exit, receive_exit(M)).

events() ->
    {ok, M} = gen_event:start({local, ev_mgr}),
    ok = gen_event:add_handler(ev_mgr, ?MODULE, h1),
    ok = gen_event:add_handler(ev_mgr, {?MODULE, x}, h2),
    reports(),
    show(notify, gen_event:notify(ev_mgr, first)),
    show(sync_notify, gen_event:sync_notify(M, second)),
    show(notify_remote, gen_event:notify({ev_mgr, node()}, third)),
    M ! an_info,
    gen_event:sync_notify(M, sync),
    show(reports, reports()),
    show(counts, [
        gen_event:call(ev_mgr, ?MODULE, count), gen_event:call(ev_mgr, {?MODULE, x}, count)
    ]),
    ok = gen_event:stop(ev_mgr, normal, 1000),
    show(stopped, reports()).

calls() ->
    {ok, M} = gen_event:start(),
    ok = gen_event:add_handler(M, ?MODULE, h1),
    ok = gen_event:add_handler(M, {?MODULE, x}, h2),
    reports(),
    show(calls, [
        gen_event:call(M, ?MODULE, count),
        gen_event:call(M, ?MODULE, hibernate, 1000),
        gen_event:call(M, nobody, count),
        gen_event:call(M, {?MODULE, x}, remove),
        gen_event:which_handlers(M),
        strip(gen_event:call(M, ?MODULE, crash)),
        gen_event:which_handlers(M)
    ]),
    show(reports, reports()),
    show(call_errors, [
        catch_exit(fun() -> gen_event:call(ev_nobody, ?MODULE, count) end),
        catch_exit(fun() -> gen_event:call(ev_nobody, ?MODULE, count, 100) end)
    ]),
    ok = gen_event:stop(M).

results() ->
    {ok, M} = gen_event:start(),
    ok = gen_event:add_handler(M, {?MODULE, r}, h_remove),
    ok = gen_event:add_handler(M, {?MODULE, c}, h_crash),
    ok = gen_event:add_handler(M, {?MODULE, b}, h_bad),
    ok = gen_event:add_handler(M, {?MODULE, s}, h_swap),
    reports(),
    gen_event:sync_notify(M, remove),
    show(after_remove, gen_event:which_handlers(M)),
    gen_event:sync_notify(M, crash),
    show(after_crash, gen_event:which_handlers(M)),
    gen_event:sync_notify(M, bad),
    show(after_bad, gen_event:which_handlers(M)),
    show(reports, reports()),
    ok = gen_event:add_handler(M, {?MODULE, t}, h_swap2),
    reports(),
    gen_event:sync_notify(M, {swap, {?MODULE, swapped_id}}),
    show(after_swap, gen_event:which_handlers(M)),
    show(reports, reports()),
    show(
        swap_handler, gen_event:swap_handler(M, {{?MODULE, swapped_id}, again}, {{?MODULE, new}, x})
    ),
    show(swap_missing, gen_event:swap_handler(M, {nobody, x}, {{?MODULE, n2}, y})),
    show(after_swaps, gen_event:which_handlers(M)),
    show(reports, reports()),
    show(call_swap, gen_event:call(M, {?MODULE, new}, {swap, {?MODULE, from_call}})),
    show(after_call_swap, gen_event:which_handlers(M)),
    show(reports, reports()),
    ok = gen_event:stop(M),
    reports().

supervised() ->
    {ok, M} = gen_event:start(),
    show(add_sup, gen_event:add_sup_handler(M, {?MODULE, sup}, h_sup)),
    reports(),
    show(delete_sup, gen_event:delete_handler(M, {?MODULE, sup}, bye)),
    show(sup_exit, receive_gen_event_exit()),
    ok = gen_event:add_sup_handler(M, {?MODULE, sup2}, h_sup2),
    gen_event:sync_notify(M, crash),
    show(crashed_sup, receive_gen_event_exit()),
    ok = gen_event:add_sup_handler(M, {?MODULE, sup3}, h_sup3),
    show(swap_sup, gen_event:swap_sup_handler(M, {{?MODULE, sup3}, x}, {{?MODULE, sup4}, y})),
    show(swapped_sup, receive_gen_event_exit()),
    reports(),
    %% A process supervising a handler ends: the handler is removed.
    Self = self(),
    Owner = spawn(fun() ->
        ok = gen_event:add_sup_handler(M, {?MODULE, owned}, h_owned),
        Self ! added,
        receive
            stop -> ok
        end
    end),
    receive
        added -> ok
    end,
    show(owned, lists:member({?MODULE, owned}, gen_event:which_handlers(M))),
    Owner ! stop,
    timer:sleep(100),
    show(owner_gone, lists:member({?MODULE, owned}, gen_event:which_handlers(M))),
    show(reports, reports()),
    ok = gen_event:stop(M),
    show(stop_exit, receive_gen_event_exit()),
    reports().

request_ids() ->
    {ok, M} = gen_event:start(),
    ok = gen_event:add_handler(M, ?MODULE, h1),
    reports(),
    R1 = gen_event:send_request(M, ?MODULE, count),
    show(receive_response, gen_event:receive_response(R1, 1000)),
    R2 = gen_event:send_request(M, nobody, count),
    show(error_response, gen_event:wait_response(R2, 1000)),
    R3 = gen_event:send_request(M, ?MODULE, count),
    show(
        check_response,
        receive
            Msg -> gen_event:check_response(Msg, R3)
        end
    ),
    C0 = gen_event:reqids_new(),
    C1 = gen_event:send_request(M, ?MODULE, count, one, C0),
    C2 = gen_event:send_request(M, nobody, count, two, C1),
    {A, LA, C3} = gen_event:receive_response(C2, 1000, true),
    {B, LB, C4} = gen_event:wait_response(C3, 1000, true),
    show(collection, [
        lists:sort([{LA, A}, {LB, B}]), gen_event:reqids_size(C4), gen_event:reqids_to_list(C4)
    ]),
    show(reqid_errors, [
        catch_error(fun() -> gen_event:reqids_add(x, y, C0) end),
        catch_error(fun() -> gen_event:reqids_size(x) end),
        catch_error(fun() -> gen_event:send_request(1, ?MODULE, x) end)
    ]),
    ok = gen_event:stop(M).

managers() ->
    {ok, {M, Ref}} = gen_event:start_monitor(),
    show(monitored, is_reference(Ref)),
    ok = gen_event:stop(M),
    show(
        down,
        receive
            {'DOWN', Ref, process, M, Reason} -> Reason
        after 1000 -> timeout
        end
    ),
    show(start_errors, [
        catch_error(fun() -> gen_event:start(1) end),
        catch_error(fun() -> gen_event:start_link(1) end),
        catch_error(fun() -> gen_event:start_monitor(1) end),
        catch_error(fun() -> gen_event:start({local, x}, 1) end)
    ]),
    {ok, Named} = gen_event:start({local, ev_named}, []),
    show(already, element(1, element(2, gen_event:start({local, ev_named})))),
    ok = gen_event:stop(Named).

%% Helpers.

reports() ->
    receive
        {report, Event} -> [Event | reports()]
    after 100 -> []
    end.

receive_gen_event_exit() ->
    receive
        {gen_event_EXIT, Handler, Reason} -> {Handler, strip(Reason)}
    after 1000 -> timeout
    end.

receive_exit(Pid) ->
    receive
        {'EXIT', Pid, Reason} -> Reason
    after 1000 -> timeout
    end.

catch_exit(F) ->
    try F() of
        Value -> {value, Value}
    catch
        exit:{Reason, {gen_event, Function, _}} -> {exit, Reason, Function}
    end.

catch_error(F) ->
    try F() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, Reason}
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

%% Pids differ between runs: shown as the atom pid.
no_pids(Pid) when is_pid(Pid) -> pid;
no_pids([H | T]) -> [no_pids(H) | no_pids(T)];
no_pids(Tuple) when is_tuple(Tuple) -> list_to_tuple(no_pids(tuple_to_list(Tuple)));
no_pids(Other) -> Other.
