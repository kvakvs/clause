-module(introspection).
-export([main/1]).

%% Process and system introspection (plan step 41G1). Sizes, counts and clock values differ between runtimes, so
%% only their shapes, memberships and relations are printed.
main(_) ->
    own_process(),
    other_process(),
    system(),
    io:format("introspection: ok~n").

own_process() ->
    Self = self(),
    show(process_info(Self, registered_name)),
    register(introspected, Self),
    show(process_info(Self, registered_name)),
    show(process_info(Self, [registered_name, status, trap_exit, priority, error_handler])),
    process_flag(trap_exit, true),
    show(process_info(Self, trap_exit)),
    Self ! a,
    Self ! {b, [1]},
    show(process_info(Self, [message_queue_len, messages])),
    flush(),
    put(key, value),
    show(process_info(Self, dictionary)),
    {heap_size, Heap} = process_info(Self, heap_size),
    {stack_size, Stack} = process_info(Self, stack_size),
    {memory, Memory} = process_info(Self, memory),
    {reductions, Reductions} = process_info(Self, reductions),
    show([is_integer(X) andalso X >= 0 || X <- [Heap, Stack, Memory, Reductions]]),
    Keys = [Key || {Key, _} <- process_info(Self)],
    Expected = [
        status,
        message_queue_len,
        links,
        dictionary,
        trap_exit,
        error_handler,
        priority,
        group_leader
    ],
    show([lists:member(Key, Keys) || Key <- Expected]),
    show(run(fun() -> process_info(Self, bogus) end)),
    show(run(fun() -> process_info(not_a_pid, status) end)),
    show({garbage_collect(), erlang:yield(), is_pid(group_leader())}),
    show(garbage_collect(Self, [{async, request}])),
    receive
        {garbage_collect, request, Done} -> show(Done)
    end,
    show(run(fun() -> garbage_collect(Self, [bogus]) end)).

other_process() ->
    Self = self(),
    Leader = group_leader(),
    Child = spawn_link(fun() -> child(Self) end),
    receive
        {leader, Inherited} -> show(Inherited =:= Leader)
    end,
    timer_wait(),
    show(process_info(Child, status)),
    Child ! hello,
    timer_wait(),
    show(process_info(Child, messages)),
    {links, Links} = process_info(Self, links),
    show(lists:member(Child, Links)),
    Monitor = monitor(process, Child),
    show(process_info(Self, monitors) =:= {monitors, [{process, Child}]}),
    show(process_info(Child, monitored_by) =:= {monitored_by, [Self]}),
    show(lists:member(Child, processes()) andalso lists:member(Self, processes())),
    show(group_leader(Self, Child)),
    Child ! report_leader,
    receive
        {leader, Changed} -> show(Changed =:= Self)
    end,
    show(garbage_collect(Child)),
    Child ! stop,
    receive
        {'DOWN', Monitor, process, Child, normal} -> ok
    end,
    receive
        {'EXIT', Child, normal} -> ok
    end,
    show({process_info(Child, status), process_info(Child), garbage_collect(Child)}),
    show(run(fun() -> group_leader(Self, Child) end)).

% The spawned process: reports its group leader, then answers until told to stop.
child(Parent) ->
    Parent ! {leader, group_leader()},
    receive
        hello -> ok
    end,
    child_loop(Parent).

child_loop(Parent) ->
    receive
        report_leader ->
            Parent ! {leader, group_leader()},
            child_loop(Parent);
        stop ->
            ok
    end.

system() ->
    {Wall, _} = statistics(wall_clock),
    {Runtime, _} = statistics(runtime),
    show([is_integer(X) || X <- [Wall, Runtime, statistics(run_queue)]]),
    show(
        {
            tuple_size(statistics(context_switches)),
            tuple_size(statistics(reductions)),
            tuple_size(statistics(garbage_collection))
        }
    ),
    {{input, _}, {output, _}} = statistics(io),
    show(run(fun() -> statistics(bogus) end)),
    show(erlang:system_info(otp_release)),
    show(erlang:system_info(wordsize) =:= erlang:system_info({wordsize, internal})),
    Counts = [process_count, process_limit, atom_count, atom_limit, schedulers, schedulers_online],
    show([is_integer(erlang:system_info(Item)) || Item <- Counts]),
    show(erlang:system_info(process_count) >= 1),
    show({erlang:system_info(smp_support), is_list(erlang:system_info(version))}),
    show(run(fun() -> erlang:system_info(bogus) end)),
    Memory = erlang:memory(),
    Types = [total, processes, processes_used, system, atom, atom_used, binary, code, ets],
    show([lists:member(Type, [Name || {Name, _} <- Memory]) || Type <- Types]),
    show(is_integer(erlang:memory(total))),
    show([Name || {Name, _} <- erlang:memory([processes, atom])]),
    show(run(fun() -> erlang:memory(bogus) end)).

% Let other processes run.
timer_wait() ->
    receive
    after 20 -> ok
    end.

% Remove every message.
flush() ->
    receive
        _ -> flush()
    after 0 -> ok
    end.

show(Value) -> io:format("~p~n", [Value]).

% The value of Fun, or {error, Reason}.
run(Fun) ->
    try
        Fun()
    catch
        error:Reason -> {error, Reason}
    end.
