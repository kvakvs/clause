-module(gen_fsms).
-behaviour(gen_fsm).
-compile([nowarn_deprecated_function, nowarn_deprecated_callback]).
-export([main/1]).
-export([
    init/1,
    idle/2, idle/3,
    busy/2, busy/3,
    handle_event/3,
    handle_sync_event/4,
    handle_info/3,
    terminate/3
]).

%% gen_fsm (plan 21 step LIB21, deprecated): events and sync events per state, all-state events, infos, state
%% time-outs and timers, replies, stops and crashes, and sys state. The machine reports to fsm_main.
main(_) ->
    process_flag(trap_exit, true),
    register(fsm_main, self()),
    {ok, Fsm} = gen_fsm:start_link({local, fsm}, ?MODULE, 0, []),
    show(sync, [gen_fsm:sync_send_event(fsm, count), gen_fsm:sync_send_event(Fsm, count, 1000)]),
    gen_fsm:send_event(fsm, {work, 30}),
    show(busy, gen_fsm:sync_send_event(fsm, count)),
    show(timeout, receive_one()),
    show(all_state, [
        gen_fsm:send_all_state_event(fsm, {add, 5}),
        gen_fsm:sync_send_all_state_event(fsm, total),
        gen_fsm:sync_send_all_state_event(fsm, total, 1000)
    ]),
    Fsm ! hello,
    show(info, receive_one()),
    gen_fsm:send_event(fsm, {timer, 10}),
    show(timer, receive_one()),
    gen_fsm:send_event(fsm, {event_after, 10}),
    show(event_after, receive_one()),
    gen_fsm:send_event(fsm, cancel_timer),
    show(cancelled, receive_one()),
    show(sys, [sys:get_state(fsm), sys:replace_state(fsm, fun({S, D}) -> {S, D + 1} end)]),
    {status, _, {module, gen_fsm}, [_, running, _, _, Misc]} = sys:get_status(fsm),
    show(status, [lists:keyfind(header, 1, Misc), lists:last(Misc)]),
    show(errors, [
        catch_exit(fun() -> gen_fsm:sync_send_event(fsm_nobody, count) end),
        catch_exit(fun() -> gen_fsm:sync_send_event(fsm, {sleep, 100}, 10) end),
        catch_exit(fun() -> gen_fsm:sync_send_all_state_event(fsm_nobody, x, 100) end)
    ]),
    timer:sleep(150),
    flush(),
    show(stop_reply, gen_fsm:sync_send_event(fsm, stop)),
    show(stopped, [receive_one(), receive_exit(Fsm)]),
    {ok, F2} = gen_fsm:start(?MODULE, 0, []),
    show(crash, catch_exit(fun() -> gen_fsm:sync_send_event(F2, crash) end)),
    show(crash_terminate, receive_one()),
    {ok, F3} = gen_fsm:start(?MODULE, 0, []),
    show(bad_return, catch_exit(fun() -> gen_fsm:sync_send_all_state_event(F3, bad) end)),
    show(bad_terminate, receive_one()),
    {ok, F4} = gen_fsm:start(?MODULE, 0, []),
    show(stop, [gen_fsm:stop(F4), receive_one()]),
    show(init_results, [
        gen_fsm:start(?MODULE, {stop, why}, []),
        gen_fsm:start(?MODULE, ignore, []),
        strip(gen_fsm:start(?MODULE, crash, [])),
        gen_fsm:start(?MODULE, bad, [])
    ]),
    show(cancel_timer, [gen_fsm:cancel_timer(make_ref())]),
    io:format("gen_fsms: ok~n").

init({stop, Reason}) -> {stop, Reason};
init(ignore) -> ignore;
init(crash) -> erlang:error(init_crash);
init(bad) -> bad_init;
init(Count) -> {ok, idle, Count}.

idle({work, Time}, Count) ->
    {next_state, busy, Count + 1, Time};
idle({timer, Time}, Count) ->
    gen_fsm:start_timer(Time, tick),
    {next_state, idle, Count};
idle({timeout, _, tick}, Count) ->
    report({timer, tick}),
    {next_state, idle, Count};
idle({event_after, Time}, Count) ->
    gen_fsm:send_event_after(Time, delayed),
    {next_state, idle, Count};
idle(delayed, Count) ->
    report(delayed),
    {next_state, idle, Count};
idle(cancel_timer, Count) ->
    Ref = gen_fsm:start_timer(1000, never),
    report({cancel, is_integer(gen_fsm:cancel_timer(Ref))}),
    {next_state, idle, Count}.

idle(count, _, Count) ->
    {reply, {idle, Count}, idle, Count};
idle({sleep, Time}, _, Count) ->
    timer:sleep(Time),
    {reply, slept, idle, Count};
idle(stop, _, Count) ->
    {stop, normal, stopping, Count};
idle(crash, _, _) ->
    erlang:error(crashed).

busy(timeout, Count) ->
    report({busy_timeout, Count}),
    {next_state, idle, Count}.

busy(count, From, Count) ->
    gen_fsm:reply(From, {busy, Count}),
    {next_state, busy, Count, 30}.

handle_event({add, N}, State, Count) -> {next_state, State, Count + N}.

handle_sync_event(total, _, State, Count) -> {reply, {total, State, Count}, State, Count};
handle_sync_event(bad, _, _, _) -> nonsense.

handle_info(Info, State, Count) ->
    report({info, Info, State}),
    {next_state, State, Count}.

terminate(Reason, State, Count) -> report({terminate, strip(Reason), State, Count}).

report(Event) -> fsm_main ! {report, Event}.

catch_exit(F) ->
    try F() of
        Value -> {value, Value}
    catch
        exit:{Reason, {gen_fsm, Function, _}} -> {exit, strip(Reason), Function}
    end.

strip({Reason, [{M, F, A, L} | _]}) when
    is_atom(M), is_atom(F), is_list(L), is_list(A) orelse is_integer(A)
->
    {strip(Reason), stack};
strip({error, Reason}) ->
    {error, strip(Reason)};
strip({Tag, Reason}) when is_atom(Tag) -> {Tag, strip(Reason)};
strip(Reason) ->
    Reason.

receive_one() ->
    receive
        {report, Event} -> Event
    after 1000 -> timeout
    end.

receive_exit(Pid) ->
    receive
        {'EXIT', Pid, Reason} -> Reason
    after 1000 -> timeout
    end.

flush() ->
    receive
        _ -> flush()
    after 50 -> ok
    end.

show(Label, Value) -> io:format("~p: ~p~n", [Label, Value]).
