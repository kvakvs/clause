%% mnesia's report on the terminal (plan 21 step MIM1E, docs/libraries/mnesia.md#information): info/0 prints the
%% locks, the transactions and the active tables, then the system's state and its tables, which system_info/0 prints
%% alone, in OTP's layout; without a running mnesia only the system's state.
-module(clause_mnesia_info).
-export([print/1]).

% The report of info/0 (full) or system_info/0 (system); whether mnesia runs.
print(Kind) ->
    Running = clause_mnesia_monitor:is_running(),
    case Running of
        yes -> report(Kind, clause_mnesia_tm:transactions(), mnesia:system_info(held_locks), lock_queue());
        _ -> system_state()
    end,
    Running.

lock_queue() ->
    mnesia:system_info(lock_queue).

report(full, Coordinators, Held, Queued) ->
    io:format("---> Processes holding locks <--- ~n", []),
    [io:format("Lock: ~p~n", [L]) || L <- Held],
    io:format("---> Processes waiting for locks <--- ~n", []),
    [
        io:format("Tid ~p waits for ~p lock on oid ~p owned by ~p ~n", [Tid, Op, Oid, Owner])
     || {Oid, Op, _Pid, Tid, Owner} <- Queued
    ],
    io:format("---> Participant transactions <--- ~n", []),
    io:format("---> Coordinator transactions <---~n", []),
    [io:format("Tid: ~p (owned by ~p) ~n", [Counter, Pid]) || {Counter, Pid, _} <- Coordinators],
    io:format("---> Uncertain transactions <--- ~n", []),
    active_tables(),
    report(system, Coordinators, Held, Queued);
report(system, Coordinators, Held, Queued) ->
    system_state(),
    tables(),
    Counters = [transaction_commits, transaction_failures, transaction_restarts, transaction_log_writes],
    io:format(
        "~w transactions committed, ~w aborted, ~w restarted, ~w logged to disc~n",
        [mnesia:system_info(C) || C <- Counters]
    ),
    io:format(
        "~w held locks, ~w in queue; ~w local transactions, ~w remote~n",
        [length(Held), length(Queued), length(Coordinators), 0]
    ),
    io:format("~w transactions waits for other nodes: ~p~n", [0, []]).

% Each table with a replica to read here: its size and memory, or its file size for a disc_only_copies table.
active_tables() ->
    io:format("---> Active tables <--- ~n", []),
    Node = node(),
    [
        table_line(Tab, clause_mnesia_monitor:val({Tab, storage_type}))
     || Tab <- val({schema, local_tables}), val({Tab, where_to_read}) =:= Node
    ].

table_line(Tab, disc_only_copies) ->
    table_line(Tab, dets:info(Tab, size), dets:info(Tab, file_size), "bytes on disc");
table_line(Tab, _) ->
    table_line(Tab, ets:info(Tab, size), ets:info(Tab, memory), "words of mem").

table_line(Tab, Size, Memory, Unit) ->
    io:format(
        "~ts: with ~s records occupying ~s ~s~n",
        [padded(atom_to_list(Tab), 15), padded(integer_to_list(Size), 8), padded(integer_to_list(Memory), 8), Unit]
    ).

% A name padded with spaces to Width, never cut.
padded(Name, Width) ->
    Name ++ lists:duplicate(max(0, Width - length(Name)), $\s).

system_state() ->
    io:format(
        "===> System info in version ~p, debug level = ~p <===~n",
        [mnesia:system_info(version), mnesia:system_info(debug)]
    ),
    Not =
        case mnesia:system_info(use_dir) of
            true -> "";
            false -> "NOT "
        end,
    io:format(
        "~w. Directory ~p is ~sused.~n",
        [mnesia:system_info(schema_location), mnesia:system_info(directory), Not]
    ),
    io:format("use fallback at restart = ~w~n", [mnesia:system_info(fallback_activated)]),
    Running = mnesia:system_info(running_db_nodes),
    io:format("running db nodes   = ~w~n", [Running]),
    All = lists:usort(mnesia:system_info(db_nodes) ++ mnesia:system_info(extra_db_nodes)),
    io:format("stopped db nodes   = ~w~n", [All -- Running]).

% The tables by storage, then by where they are written.
tables() ->
    io:format("master node tables = ~p~n", [lists:sort(mnesia:system_info(master_node_tables))]),
    Tabs = mnesia:system_info(tables),
    Storage = [{T, mnesia:table_info(T, storage_type)} || T <- Tabs],
    Of = fun(Kind) -> lists:sort([T || {T, S} <- Storage, S =:= Kind]) end,
    io:format("remote             = ~p~n", [Of(unknown)]),
    io:format("ram_copies         = ~p~n", [Of(ram_copies)]),
    io:format("disc_copies        = ~p~n", [Of(disc_copies)]),
    io:format("disc_only_copies   = ~p~n", [Of(disc_only_copies)]),
    Groups = lists:foldl(fun group/2, [], Tabs),
    [io:format("~p = ~p~n", [Writers, Group]) || {Writers, Group} <- lists:sort(Groups)].

% Tables written at the same replicas share a line, the last table first.
group(Tab, Groups) ->
    Writers =
        case val({Tab, access_mode}) of
            read_only -> lists:sort([{N, read_only} || N <- val({Tab, active_replicas})]);
            read_write -> mnesia:table_info(Tab, where_to_commit)
        end,
    case lists:keyfind(Writers, 1, Groups) of
        {_, Group} -> lists:keyreplace(Writers, 1, Groups, {Writers, [Tab | Group]});
        false -> [{Writers, [Tab]} | Groups]
    end.

val(Name) -> clause_mnesia_monitor:val(Name).
