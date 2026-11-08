%% Table transfers between nodes as OTP's mnesia_loader makes them (plan 21 step MIM1H9,
%% docs/libraries/mnesia-protocol.md#replicas-and-loading-mim1h9): a node that gets a replica asks a node holding one
%% by casting send_table to its controller, whose sender process streams the records; the receiver fills its replica,
%% owned by mnesia_monitor, and gets the table events of commits made meanwhile. The sender then makes the new replica
%% active on both nodes. Senders and receivers may be OTP's or Clause's.
-module(clause_mnesia_loader).
-export([send/4, receive_table/4, copy/4, load_pending/0]).

-include("clause_mnesia.hrl").

%% Sending.

% Tab's records to Receiver, from a process the controller starts: the receiver hears the table's events while the
% records go, the table is written on its node from then on, and its replica is active on both nodes at the end.
send(Tab, Receiver, Storage, _Reason) ->
    Node = node(Receiver),
    Ref = erlang:monitor(process, Receiver),
    Receiver ! {self(), {first, clause_mnesia_db:info(Tab, size)}},
    _ = clause_mnesia_subscr:subscribe(Receiver, {table, Tab, simple}),
    try
        ok = acked(Ref, more),
        Write = {update_where_to_write, [add, Tab, Node], self()},
        _ = rpc:call(Node, mnesia_controller, call, [Write]),
        _ = clause_mnesia_controller:call(Write),
        [Receiver ! {self(), Chunk} || Chunk <- chunks(clause_mnesia_db:match_object(Tab, '_'))],
        Receiver ! {self(), no_more},
        activate(Tab, Node, Storage),
        Receiver ! {self(), {no_more, nobin}},
        ok = acked(Ref, no_more)
    after
        erlang:demonitor(Ref, [flush]),
        _ = clause_mnesia_subscr:unsubscribe(Receiver, {table, Tab, simple})
    end.

% The receiver's answer Kind; its other answers are dropped, and any process of its node may answer.
acked(Ref, Kind) ->
    receive
        {Pid, Kind} when is_pid(Pid) -> ok;
        {'DOWN', Ref, process, _, Reason} -> {receiver_down, Reason}
    end.

% The new replica active on its node and here, its table blocked there first, as OTP's sender does.
activate(Tab, Node, Storage) ->
    Replica = [Tab, Node, Storage, clause_mnesia_monitor:val({Tab, access_mode})],
    _ = gen_server:call({mnesia_controller, Node}, {block_table, [Tab], self()}, infinity),
    _ = gen_server:call({mnesia_controller, Node}, {add_active_replica, Replica, self()}, infinity),
    _ = clause_mnesia_controller:call({add_active_replica, Replica, self()}),
    ok.

% The records in chunks of about max_transfer_size bytes, compressed with send_compressed above 0.
chunks(Records) ->
    Max = clause_mnesia_monitor:get_env(max_transfer_size),
    Level = clause_mnesia_monitor:get_env(send_compressed),
    [chunk(C, Level) || C <- split(Records, Max, 0, [], [])].

split([], _, _, [], Chunks) ->
    lists:reverse(Chunks);
split([], _, _, Chunk, Chunks) ->
    lists:reverse([lists:reverse(Chunk) | Chunks]);
split([Record | Rest], Max, Size, Chunk, Chunks) when Size >= Max ->
    split([Record | Rest], Max, 0, [], [lists:reverse(Chunk) | Chunks]);
split([Record | Rest], Max, Size, Chunk, Chunks) ->
    split(Rest, Max, Size + erlang:external_size(Record), [Record | Chunk], Chunks).

chunk(Records, 0) -> {more, Records};
chunk(Records, _) -> {more_z, zlib:compress(term_to_binary(Records))}.

%% Receiving.

% Tab's records from Node's replica into a new replica here, from the calling process; Reason is send_table's: load,
% or {add_table_copy, Tid}. ok, or {error, Reason} when the sender goes.
receive_table(Tab, Storage, Node, Reason) ->
    announce(Node, Reason),
    Ref = erlang:monitor(process, {mnesia_controller, Node}),
    gen_server:cast({mnesia_controller, Node}, {send_table, Tab, self(), Storage, Reason}),
    receive
        {Sender, {first, _}} when is_pid(Sender) ->
            erlang:demonitor(Ref, [flush]),
            filled(Tab, Sender);
        {'DOWN', Ref, process, _, _} ->
            {error, {node_not_running, Node}}
    end.

% A new copy's transaction is the one the sender's node loads for, as OTP's receiver tells it.
announce(Node, {add_table_copy, Tid}) ->
    _ = rpc:call(Node, mnesia_lib, set, [{mnesia_loader, active_trans}, Tid]),
    ok;
announce(_, _) ->
    ok.

filled(Tab, Sender) ->
    Ref = erlang:monitor(process, Sender),
    ok = gen_server:call(mnesia_monitor, {replica_table, Tab}, infinity),
    Sender ! {self(), more},
    Result = fill(Tab, Sender, Ref, []),
    erlang:demonitor(Ref, [flush]),
    Result.

% The chunks, then the table events of commits made meanwhile, in their order.
fill(Tab, Sender, Ref, Events) ->
    receive
        {Sender, {more, Records}} ->
            ets:insert(Tab, Records),
            fill(Tab, Sender, Ref, Events);
        {Sender, {more_z, Compressed}} ->
            ets:insert(Tab, binary_to_term(zlib:uncompress(Compressed))),
            fill(Tab, Sender, Ref, Events);
        {Sender, no_more} ->
            fill(Tab, Sender, Ref, Events);
        {Sender, {no_more, _}} ->
            [event(Tab, E) || E <- lists:reverse(Events)],
            Sender ! {self(), no_more},
            ok;
        {mnesia_table_event, Event} ->
            fill(Tab, Sender, Ref, [Event | Events]);
        {'DOWN', Ref, process, _, Reason} ->
            {error, {sender_down, Reason}}
    end.

% A simple table event names the table in place of the record name.
event(Tab, {write, Record, _}) when element(1, Record) =:= Tab -> ets:insert(Tab, stored(Tab, Record));
event(Tab, {delete, {Tab, Key}, _}) -> ets:delete(Tab, Key);
event(Tab, {delete_object, Record, _}) when element(1, Record) =:= Tab -> ets:delete_object(Tab, stored(Tab, Record));
event(_, _) -> ok.

stored(Tab, Record) -> setelement(1, Record, clause_mnesia_monitor:val({Tab, record_name})).

%% Loading replicas.

% A new copy of Tab on Node in transaction Tid: when Node is this node and the table is read on another node, its
% records are loaded from there first; {LoadNode, Reason} of the copy, or none.
copy(Tab, Node, Storage, Tid) when Node =:= node() ->
    case clause_mnesia_monitor:val({Tab, where_to_read}) of
        Source when Source =/= nowhere, Source =/= Node ->
            Reason = {add_table_copy, Tid},
            case receive_table(Tab, Storage, Source, Reason) of
                ok -> {Source, Reason};
                {error, Why} -> mnesia:abort(Why)
            end;
        _ ->
            none
    end;
copy(_, _, _, _) ->
    none.

% Each table with a replica here that a merge left unloaded is loaded from a running node holding one, by a process of
% its own, as OTP's controller loads them at start.
load_pending() ->
    Running = clause_mnesia_monitor:val(recover_nodes),
    [
        spawn(fun() -> load(Tab, Source) end)
     || Tab <- clause_mnesia_monitor:val({schema, tables}),
        clause_mnesia_monitor:val({Tab, storage_type}) =:= unknown,
        Source <- [source(clause_mnesia_monitor:val({Tab, cstruct}), Running)],
        Source =/= none
    ],
    ok.

% The first other running node holding a table that has a replica here.
source(#cstruct{ram_copies = Ram, disc_copies = Disc, disc_only_copies = DiscOnly} = Cs, Running) ->
    Holders = Ram ++ Disc ++ DiscOnly,
    case clause_mnesia_schema:storage(Cs) =/= unknown andalso [N || N <- Holders, lists:member(N, Running)] of
        [Source | _] -> Source;
        _ -> none
    end.

load(Tab, Source) ->
    Storage = clause_mnesia_schema:storage(clause_mnesia_monitor:val({Tab, cstruct})),
    case receive_table(Tab, Storage, Source, load) of
        ok -> gen_server:call(mnesia_monitor, {replica_loaded, Tab, Source, {active_remote, Source}}, infinity);
        {error, _} -> ok
    end.
