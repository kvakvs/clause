-module(proc_libs).
-compile([nowarn_deprecated_catch, nowarn_deprecated_function]).
-export([main/1]).
-export([child/1, starter/2, special/1, special_loop/3, hibernating/1, woken/1]).
-export([
    system_continue/3,
    system_terminate/4,
    system_code_change/4,
    system_get_state/1,
    system_replace_state/2
]).

%% proc_lib and sys (plan 21 step LIB20): spawning with ancestors and initial calls, synchronous starts with
%% init_ack and init_fail, labels, hibernation, crash exit reasons, stop/1,3, crash report formatting, and the
%% system message protocol of a hand-written special process with sys's debug options. Pids are shown as the atom
%% pid; crash reports go to stderr.
main(_) ->
    process_flag(trap_exit, true),
    register(pl_main, self()),
    spawning(),
    starting(),
    labels(),
    crashes(),
    formatting(),
    special_process(),
    debugging(),
    stopping(),
    io:format("proc_libs: ok~n").

%% Spawned processes report their dictionary entries to pl_main and wait for stop.
child(Tag) ->
    pl_main ! {child, Tag, get('$ancestors'), get('$initial_call')},
    receive
        stop -> ok
    end.

spawning() ->
    Self = self(),
    P1 = proc_lib:spawn(?MODULE, child, [mfa]),
    P2 = proc_lib:spawn(fun() -> child(fun_spawn) end),
    P3 = proc_lib:spawn_link(?MODULE, child, [linked]),
    P4 = proc_lib:spawn_link(fun() -> child(fun_linked) end),
    {P5, Ref} = proc_lib:spawn_opt(?MODULE, child, [opt], [monitor]),
    P6 = proc_lib:spawn_opt(fun() -> child(fun_opt) end, [link]),
    P7 = proc_lib:spawn(node(), ?MODULE, child, [node_mfa]),
    P8 = proc_lib:spawn_link(node(), fun() -> child(node_fun) end),
    P9 = proc_lib:spawn_opt(node(), ?MODULE, child, [node_opt], []),
    Children = [P1, P2, P3, P4, P5, P6, P7, P8, P9],
    show(children, lists:sort([receive_child() || _ <- Children])),
    show(initial_call, [proc_lib:initial_call(P1), proc_lib:translate_initial_call(P1)]),
    show(fun_call, [
        is_tuple(proc_lib:initial_call(P2)), element(1, proc_lib:translate_initial_call(P2))
    ]),
    Plain = spawn(fun() ->
        receive
            stop -> ok
        end
    end),
    show(plain, [proc_lib:initial_call(Plain), proc_lib:translate_initial_call(Plain)]),
    show(info_list, [
        proc_lib:initial_call([{dictionary, [{'$initial_call', {m, f, 2}}]}]),
        proc_lib:translate_initial_call([{dictionary, []}]),
        proc_lib:initial_call([{{dictionary, '$initial_call'}, {m, g, 0}}]),
        proc_lib:initial_call([])
    ]),
    %% A grandchild's ancestors are its parent's registered name, or pid, and the parent's ancestors.
    Grand = proc_lib:spawn(fun() ->
        register(pl_parent, self()),
        proc_lib:spawn(?MODULE, child, [grandchild]),
        receive
            stop -> ok
        end
    end),
    show(grandchild, receive_child()),
    [P ! stop || P <- [Plain, Grand | Children]],
    show(monitored, receive_down(Ref)),
    flush(),
    ok = Self ! ok,
    flush().

receive_child() ->
    receive
        {child, Tag, Ancestors, Call} -> {Tag, no_pids(Ancestors), shown_call(Call)}
    after 1000 -> timeout
    end.

%% An initial call of a fun names a generated function: shown as {M, fun, A}.
shown_call({M, F, A}) ->
    case atom_to_list(F) of
        "-" ++ _ -> {M, 'fun', A};
        _ -> {M, F, A}
    end.

%% Starting: starter(How, Arg) acknowledges or fails in different ways.
starter(ack, Value) ->
    proc_lib:init_ack(Value),
    receive
        stop -> ok
    end;
starter(ack2, Value) ->
    [Parent | _] = get('$ancestors'),
    proc_lib:init_ack(Parent, Value),
    receive
        stop -> ok
    end;
starter(fail, {Return, Exception}) ->
    proc_lib:init_fail(Return, Exception);
starter(fail3, {Return, Exception}) ->
    [Parent | _] = get('$ancestors'),
    proc_lib:init_fail(Parent, Return, Exception);
starter(die, Reason) ->
    exit(Reason);
starter(crash, Reason) ->
    erlang:error(Reason);
starter(slow, _) ->
    timer:sleep(1000).

starting() ->
    show(start, [
        start(proc_lib:start(?MODULE, starter, [ack, started])),
        start(proc_lib:start(?MODULE, starter, [ack2, {ok, two}], 1000)),
        start(proc_lib:start(?MODULE, starter, [ack, opts], 1000, [{priority, normal}])),
        start(proc_lib:start_link(?MODULE, starter, [ack, linked])),
        start(proc_lib:start_link(?MODULE, starter, [ack, linked4], 1000)),
        start(proc_lib:start_link(?MODULE, starter, [ack, linked5], 1000, [])),
        start(proc_lib:start(?MODULE, starter, [die, early])),
        strip(proc_lib:start(?MODULE, starter, [crash, broken])),
        proc_lib:start(?MODULE, starter, [slow, x], 50),
        proc_lib:start_link(?MODULE, starter, [slow, x], 50)
    ]),
    show(fail, [
        proc_lib:start(?MODULE, starter, [fail, {{error, failed}, {exit, normal}}]),
        proc_lib:start(?MODULE, starter, [fail, {{error, thrown}, {throw, value}}]),
        proc_lib:start(?MODULE, starter, [fail3, {failed3, {error, reason}}]),
        proc_lib:start_link(?MODULE, starter, [fail, {{error, linked}, {exit, gone}}])
    ]),
    {Result, Ref} = proc_lib:start_monitor(?MODULE, starter, [ack, monitored]),
    show(start_monitor, [Result, is_reference(Ref)]),
    {Failed, FailedRef} = proc_lib:start_monitor(
        ?MODULE, starter, [fail, {failed, {exit, why}}], 1000
    ),
    show(start_monitor_fail, [Failed, receive_down(FailedRef)]),
    {Died, DiedRef} = proc_lib:start_monitor(?MODULE, starter, [die, dead], 1000, []),
    show(start_monitor_died, [Died, receive_down(DiedRef)]),
    show(monitor_option, [
        catch_error(fun() -> proc_lib:start(?MODULE, starter, [ack, x], 1000, [monitor]) end),
        catch_error(fun() ->
            proc_lib:start_link(?MODULE, starter, [ack, x], 1000, [{monitor, []}])
        end),
        catch_error(fun() -> proc_lib:start_monitor(?MODULE, starter, [ack, x], 1000, [monitor]) end)
    ]),
    timer:sleep(100),
    flush().

start({ok, Pid}) when is_pid(Pid) -> {ok, pid};
start(Other) -> Other.

labels() ->
    show(own_label, [
        proc_lib:get_label(self()), proc_lib:set_label(main_label), proc_lib:get_label(self())
    ]),
    Self = self(),
    Pid = proc_lib:spawn(fun() ->
        proc_lib:set_label({worker, 1}),
        Self ! labelled,
        receive
            stop -> ok
        end
    end),
    receive
        labelled -> ok
    end,
    show(other_label, proc_lib:get_label(Pid)),
    Pid ! stop,
    timer:sleep(20),
    show(dead_label, proc_lib:get_label(Pid)),
    erase('$process_label').

hibernating(Main) ->
    proc_lib:hibernate(?MODULE, woken, [Main]).

woken(Main) ->
    Main ! {woken, get('$initial_call')},
    receive
        Msg -> Main ! {got, Msg}
    end.

crashes() ->
    Hib = proc_lib:spawn_link(?MODULE, hibernating, [self()]),
    timer:sleep(20),
    Hib ! wake,
    show(hibernate, [receive_one(), receive_one(), receive_exit(Hib)]),
    E = proc_lib:spawn_link(fun() -> erlang:error(oops) end),
    X = proc_lib:spawn_link(fun() -> exit(quit) end),
    T = proc_lib:spawn_link(fun() -> throw(ball) end),
    N = proc_lib:spawn_link(fun() -> exit(normal) end),
    S = proc_lib:spawn_link(fun() -> exit({shutdown, done}) end),
    show(exits, [receive_exit(P) || P <- [E, X, T, N, S]]),
    flush().

%% A crash report as proc_lib keeps it, formatted without the logger.
formatting() ->
    Own = [
        {initial_call, {m, f, ['Argument__1']}},
        {pid, pid},
        {registered_name, []},
        {error_info, {exit, gone, []}},
        {ancestors, [p1, p2]},
        {message_queue_len, 0},
        {messages, []},
        {links, []},
        {dictionary, [{k, v}]},
        {trap_exit, false},
        {status, running}
    ],
    Neighbour = [{neighbour, [{pid, pid}, {registered_name, n1}, {message_queue_len, 0}]}],
    show(format, lists:flatten(proc_lib:format([Own, Neighbour]))),
    show(format_unicode, lists:flatten(proc_lib:format([[{x, "text"}], []], unicode))),
    show(format_depth, lists:flatten(proc_lib:format([[{x, lists:seq(1, 20)}], []], latin1, 5))),
    show(
        report_cb,
        lists:flatten(
            proc_lib:report_cb(
                #{label => {proc_lib, crash}, report => [[{x, 1}], []]},
                #{single_line => true}
            )
        )
    ).

%% The special process: a counter honouring the system message protocol.
special(Count) ->
    proc_lib:init_ack({ok, self()}),
    [Parent | _] = get('$ancestors'),
    Debug = sys:debug_options([statistics]),
    special_loop(Parent, Debug, Count).

special_loop(Parent, Debug, Count) ->
    receive
        {system, From, Request} ->
            sys:handle_system_msg(Request, From, Parent, ?MODULE, Debug, Count);
        {add, N} = Msg ->
            Debug1 = sys:handle_debug(Debug, fun print_event/3, special, {in, Msg}),
            special_loop(Parent, Debug1, Count + N);
        {get, From} ->
            From ! {count, Count},
            Debug1 = sys:handle_debug(Debug, fun print_event/3, special, {out, Count, sl_main}),
            special_loop(Parent, Debug1, Count)
    end.

print_event(Device, Event, Name) -> io:format(Device, "*DBG* ~p event = ~p~n", [Name, Event]).

system_continue(Parent, Debug, Count) -> special_loop(Parent, Debug, Count).

system_terminate(Reason, _, _, Count) ->
    pl_main ! {special_terminated, Reason, Count},
    exit(Reason).

system_code_change(Count, _, old, extra) -> {ok, Count + 1000}.

system_get_state(Count) -> {ok, {count, Count}}.

system_replace_state(Fun, Count) ->
    {count, New} = Fun({count, Count}),
    {ok, {count, New}, New}.

special_process() ->
    {ok, Pid} = proc_lib:start_link(?MODULE, special, [0]),
    Pid ! {add, 5},
    show(get_state, sys:get_state(Pid)),
    show(replace_state, sys:replace_state(Pid, fun({count, N}) -> {count, N * 10} end)),
    show(count, count(Pid)),
    {status, _, {module, ?MODULE}, [_, running, _, [{statistics, _}], Misc]} = sys:get_status(
        Pid, 1000
    ),
    show(status_misc, Misc),
    show(statistics, [element(1, Item) || Item <- element(2, sys:statistics(Pid, get))]),
    ok = sys:suspend(Pid, 1000),
    Pid ! {add, 1},
    show(suspended, [lists:nth(2, element(4, sys:get_status(Pid))), sys:get_state(Pid)]),
    show(change_code, sys:change_code(Pid, ?MODULE, old, extra)),
    show(changed, sys:get_state(Pid)),
    ok = sys:resume(Pid),
    show(resumed_state, sys:get_state(Pid)),
    show(change_running, sys:change_code(Pid, ?MODULE, old, extra, 1000)),
    show(unknown, catch gen:call(Pid, system, nonsense)),
    ok = sys:terminate(Pid, done),
    show(terminated, [receive_one(), receive_exit(Pid)]),
    show(sys_errors, [
        catch_exit(fun() -> sys:get_state(Pid) end),
        catch_exit(fun() -> sys:suspend(Pid, 100) end),
        catch_exit(fun() -> sys:get_status(sl_nobody) end),
        catch_exit(fun() -> sys:change_code(sl_nobody, m, v, e) end),
        catch_exit(fun() -> sys:log(sl_nobody, true) end),
        catch_exit(fun() -> sys:terminate(sl_nobody, r, 10) end)
    ]),
    flush().

count(Pid) ->
    Pid ! {get, self()},
    receive
        {count, N} -> N
    end.

debugging() ->
    {ok, Pid} = proc_lib:start_link(?MODULE, special, [0]),
    show(log, [sys:log(Pid, true), Pid ! {add, 1}, Pid ! {add, 2}, count(Pid), sys:log(Pid, get)]),
    show(log_resize, [sys:log(Pid, {true, 2}), sys:log(Pid, get)]),
    io:format("print:~n"),
    show(log_print, sys:log(Pid, print)),
    show(log_off, [sys:log(Pid, false), sys:log(Pid, get)]),
    show(trace, sys:trace(Pid, true)),
    Pid ! {add, 3},
    count(Pid),
    show(trace_off, sys:trace(Pid, false, 1000)),
    Self = self(),
    Fun = fun(State, Event, _) ->
        Self ! {debug, State, Event},
        case State of
            2 -> done;
            _ -> State + 1
        end
    end,
    show(install, sys:install(Pid, {Fun, 1})),
    Pid ! {add, 4},
    Pid ! {add, 5},
    Pid ! {add, 6},
    count(Pid),
    show(installed_events, [receive_one(), receive_one(), quiet(50)]),
    show(install_id, [
        sys:install(Pid, {my_id, Fun, 1}), sys:remove(Pid, my_id), sys:remove(Pid, missing, 1000)
    ]),
    File = "proc_libs_debug.log",
    show(log_to_file, [
        sys:log_to_file(Pid, File), Pid ! {add, 7}, count(Pid), sys:log_to_file(Pid, false)
    ]),
    {ok, Logged} = file:read_file(File),
    show(logged, Logged),
    ok = file:delete(File),
    show(log_to_bad_file, sys:log_to_file(Pid, "no_such_directory/x.log")),
    show(statistics_off, [
        sys:statistics(Pid, false),
        sys:statistics(Pid, get),
        sys:statistics(Pid, true),
        sys:no_debug(Pid),
        sys:statistics(Pid, get)
    ]),
    show(unknown_debug, catch gen:call(Pid, system, {debug, nonsense})),
    show(debug_options, [
        sys:debug_options([trace, {log, 3}, unknown]),
        length(sys:debug_options([statistics, {install, {Fun, 0}}, {install, {id, Fun, 0}}])),
        sys:debug_options([log])
    ]),
    show(handle_debug, sys:handle_debug([{log, [2]}], fun print_event/3, name, {in, x})),
    show(get_log, [
        sys:get_log([{log, [[{b, s, f}], {a, s, f}]}]),
        sys:get_log([]),
        sys:get_debug(log, [], none)
    ]),
    io:format("print_log:~n"),
    sys:print_log([{log, [1, {{in, x}, name, fun print_event/3}]}]),
    ok = sys:terminate(Pid, normal),
    receive_one(),
    receive_exit(Pid),
    flush().

stopping() ->
    {ok, P1} = proc_lib:start_link(?MODULE, special, [0]),
    show(stop, [proc_lib:stop(P1), receive_one(), receive_exit(P1)]),
    {ok, P2} = proc_lib:start(?MODULE, special, [0]),
    show(stop3, [proc_lib:stop(P2, {shutdown, x}, 1000), receive_one()]),
    show(stop_errors, [
        catch_exit(fun() -> proc_lib:stop(P2) end),
        catch_exit(fun() -> proc_lib:stop(pl_nobody) end)
    ]),
    P3 = proc_lib:spawn(?MODULE, child, [unstoppable]),
    show(stop_timeout, catch_exit(fun() -> proc_lib:stop(P3, normal, 100) end)),
    exit(P3, kill),
    flush().

%% Helpers.

catch_error(F) ->
    try F() of
        Value -> {value, Value}
    catch
        Class:Reason -> {Class, strip(Reason)}
    end.

catch_exit(F) ->
    try F() of
        Value -> {value, Value}
    catch
        exit:{Reason, {sys, Function, _}} -> {exit, Reason, Function};
        exit:Reason -> {exit, Reason}
    end.

receive_one() ->
    receive
        Msg -> no_pids(strip(Msg))
    after 1000 -> timeout
    end.

receive_down(Ref) ->
    receive
        {'DOWN', Ref, process, _, Reason} -> strip(Reason)
    after 1000 -> timeout
    end.

receive_exit(Pid) ->
    receive
        {'EXIT', Pid, Reason} -> strip(Reason)
    after 1000 -> timeout
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

%% Drops stacks from exit reasons so that OTP's and Clause's stacks compare equal.
strip({Reason, [{M, F, A, L} | _]}) when
    is_atom(M), is_atom(F), is_list(L), is_list(A) orelse is_integer(A)
->
    {strip(Reason), stack};
strip({'EXIT', Pid, Reason}) ->
    {'EXIT', Pid, strip(Reason)};
strip({error, Reason}) ->
    {error, strip(Reason)};
strip({Tag, Reason}) when is_atom(Tag) -> {Tag, strip(Reason)};
strip(Reason) ->
    Reason.

show(Label, Value) -> io:format("~p: ~p~n", [Label, no_pids(Value)]).

no_pids(Pid) when is_pid(Pid) -> pid;
no_pids(Ref) when is_reference(Ref) -> ref;
no_pids(Fun) when is_function(Fun) -> 'fun';
no_pids([H | T]) -> [no_pids(H) | no_pids(T)];
no_pids(Tuple) when is_tuple(Tuple) -> list_to_tuple(no_pids(tuple_to_list(Tuple)));
no_pids(Other) -> Other.
