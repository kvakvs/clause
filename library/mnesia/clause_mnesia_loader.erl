%% Table transfers between nodes as OTP's mnesia_loader makes them (plan 21 step MIM1H9,
%% docs/libraries/mnesia-protocol.md#replicas-and-loading-mim1h9): a node that gets a replica asks a node holding one
%% by casting send_table to its controller, whose sender process streams the records; the receiver fills its replica,
%% owned by mnesia_monitor, and gets the table events of commits made meanwhile. The sender then makes the new replica
%% active on both nodes. Senders and receivers may be OTP's or Clause's. After a start or a merge, each replica held
%% here loads from a node that has it loaded, or from this node's disc when no other node may hold it newer, or waits
%% for another node to load it (MIM1H10a).
-module(clause_mnesia_loader).
-export([
    orphans/1,
    adopted/2,
    intents/1,
    send/4,
    receive_table/4,
    copy/4,
    load_pending/0,
    pending/1,
    source/1,
    announced/2,
    load/2
]).

-include("clause_mnesia.hrl").

%% Sending.

% Tab's records to Receiver, from a process the controller starts: the receiver hears the table's events while the
% records go, the table is written on its node from then on, and its replica is active on both nodes at the end. The
% records go to the process that asks for more, which OTP's receiver of a disc replica starts (MIM1H10a).
send(Tab, Receiver, Storage, _Reason) ->
    Node = node(Receiver),
    Ref = erlang:monitor(process, Receiver),
    Receiver ! {self(), {first, clause_mnesia_db:info(Tab, size)}},
    _ = clause_mnesia_subscr:subscribe(Receiver, {table, Tab, simple}),
    try
        {ok, Filler} = acked(Ref, more),
        Write = {update_where_to_write, [add, Tab, Node], self()},
        _ = rpc:call(Node, mnesia_controller, call, [Write]),
        _ = clause_mnesia_controller:call(Write),
        [Filler ! {self(), Chunk} || Chunk <- chunks(clause_mnesia_db:match_object(Tab, '_'))],
        Filler ! {self(), no_more},
        activate(Tab, Node, Storage),
        Receiver ! {self(), {no_more, nobin}},
        {ok, _} = acked(Ref, no_more)
    after
        erlang:demonitor(Ref, [flush]),
        _ = clause_mnesia_subscr:unsubscribe(Receiver, {table, Tab, simple})
    end.

% The receiver's answer Kind and the process that gave it; its other answers are dropped, and any process of its node
% may answer.
acked(Ref, Kind) ->
    receive
        {Pid, Kind} when is_pid(Pid) -> {ok, Pid};
        {'DOWN', Ref, process, _, Reason} -> {receiver_down, Reason}
    end.

% The new replica active on its node and here, its table blocked there first, as OTP's sender does; the table's
% checkpoints then join the node (MIM1H11a).
activate(Tab, Node, Storage) ->
    Replica = [Tab, Node, Storage, clause_mnesia_monitor:val({Tab, access_mode})],
    _ = gen_server:call({mnesia_controller, Node}, {block_table, [Tab], self()}, infinity),
    _ = gen_server:call({mnesia_controller, Node}, {add_active_replica, Replica, self()}, infinity),
    _ = clause_mnesia_controller:call({add_active_replica, Replica, self()}),
    clause_mnesia_checkpoint:add_copy(Tab, Node).

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
            filled(Tab, Storage, Sender, kind(Storage));
        {Sender, {first, _, {_Version, _Format}}} when is_pid(Sender) ->
            % A disc_only_copies replica comes as dets chunks from another one (MIM1H10a).
            erlang:demonitor(Ref, [flush]),
            filled(Tab, Storage, Sender, bchunk);
        {'DOWN', Ref, process, _, _} ->
            {error, {node_not_running, Node}}
    end.

% A new copy's transaction is the one the sender's node loads for, as OTP's receiver tells it.
announce(Node, {add_table_copy, Tid}) ->
    _ = rpc:call(Node, mnesia_lib, set, [{mnesia_loader, active_trans}, Tid]),
    ok;
announce(_, _) ->
    ok.

filled(Tab, Storage, Sender, Kind) ->
    Ref = erlang:monitor(process, Sender),
    ok = gen_server:call(mnesia_monitor, {replica_table, Tab, Storage}, infinity),
    Sender ! {self(), more},
    Result = fill(Kind, Tab, Sender, Ref, []),
    erlang:demonitor(Ref, [flush]),
    Result =:= ok andalso on_disc(Tab, Storage),
    Result.

kind(disc_only_copies) -> dets;
kind(_) -> ets.

% A disc replica's files hold the records it received (MIM1H10a): a new DCD file, or its synced dets file.
on_disc(Tab, disc_copies) -> clause_mnesia_disc:ets2dcd(Tab);
on_disc(Tab, disc_only_copies) -> dets:sync(Tab);
on_disc(_, _) -> ok.

% The chunks, then the table events of commits made meanwhile, in their order, into the ets or dets table (Kind). Each
% chunk asks for more, as OTP's receiver does, whose sender may wait for it (MIM1H10a). Dets chunks (bchunk) fill the
% dets table at the end.
fill(bchunk, Tab, Sender, Ref, Events) ->
    fill_dets(Tab, Sender, Ref, [], Events);
fill(Kind, Tab, Sender, Ref, Events) ->
    receive
        {Sender, {more, Records}} ->
            ok = put_records(Kind, Tab, Records),
            Sender ! {self(), more},
            fill(Kind, Tab, Sender, Ref, Events);
        {Sender, {more_z, Compressed}} ->
            ok = put_records(Kind, Tab, binary_to_term(zlib:uncompress(Compressed))),
            Sender ! {self(), more},
            fill(Kind, Tab, Sender, Ref, Events);
        {Sender, no_more} ->
            fill(Kind, Tab, Sender, Ref, Events);
        {Sender, {no_more, _}} ->
            [event(Kind, Tab, E) || E <- lists:reverse(Events)],
            Sender ! {self(), no_more},
            ok;
        {mnesia_table_event, Event} ->
            fill(Kind, Tab, Sender, Ref, [Event | Events]);
        {'DOWN', Ref, process, _, Reason} ->
            {error, {sender_down, Reason}}
    end.

fill_dets(Tab, Sender, Ref, Chunks, Events) ->
    receive
        {Sender, {more, More}} ->
            Sender ! {self(), more},
            fill_dets(Tab, Sender, Ref, [More | Chunks], Events);
        {Sender, no_more} ->
            fill_dets(Tab, Sender, Ref, Chunks, Events);
        {Sender, {no_more, _}} ->
            All = lists:append(lists:reverse(Chunks)),
            ok = dets:init_table(Tab, fun(read) -> {All, fun(_) -> end_of_input end} end, [{format, bchunk}]),
            [event(dets, Tab, E) || E <- lists:reverse(Events)],
            Sender ! {self(), no_more},
            ok;
        {mnesia_table_event, Event} ->
            fill_dets(Tab, Sender, Ref, Chunks, [Event | Events]);
        {'DOWN', Ref, process, _, Reason} ->
            {error, {sender_down, Reason}}
    end.

put_records(ets, Tab, Records) ->
    true = ets:insert(Tab, Records),
    ok;
put_records(dets, Tab, Records) ->
    dets:insert(Tab, Records).

% A simple table event names the table in place of the record name.
event(Kind, Tab, {write, Record, _}) when element(1, Record) =:= Tab ->
    put_records(Kind, Tab, [stored(Tab, Record)]);
event(Kind, Tab, {delete, {Tab, Key}, _}) ->
    Kind:delete(Tab, Key);
event(Kind, Tab, {delete_object, Record, _}) when element(1, Record) =:= Tab ->
    Kind:delete_object(Tab, stored(Tab, Record));
event(_, _, _) ->
    ok.

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

% The replicas held here and not loaded, after a start or a merge (MIM1H9, MIM1H10a): the controller loads each, as
% OTP's controller does, from a running node whose replica is active, or here when none is; the others wait. The
% running nodes' disc load intents are asked first, as OTP's late loader does: their answers come after the tables
% they announced loaded (adopt_orphans).
load_pending() ->
    case [T || T <- val({schema, tables}), pending(T)] of
        [] ->
            ok;
        Pending ->
            _ = intents(lists:delete(node(), val(recover_nodes))),
            gen_server:cast(mnesia_controller, {load_tables, Pending})
    end.

% Whether a table has a replica here that has not loaded (MIM1H10a).
pending(Tab) ->
    ets:member(schema, Tab) andalso clause_mnesia_schema:storage(val({Tab, cstruct})) =/= unknown andalso
        not clause_mnesia_schema:is_loaded(Tab).

% The running nodes' disc load intents, asked under OTP's lock of late disc loads: the tables they hold loaded or will
% load from their own disc, which adopted orphans leave to them (MIM1H10c).
intents([]) ->
    [];
intents(Nodes) ->
    {atomic, Tabs} = mnesia:transaction(fun() ->
        mnesia:lock({global, mnesia_late_disc_load, [node() | Nodes]}, write),
        lists:usort(lists:append([intended(N) || N <- Nodes]))
    end),
    Tabs.

intended(Node) ->
    try gen_server:call({mnesia_controller, Node}, disc_load_intents) of
        {ok, _, Tabs} -> Tabs;
        _ -> []
    catch
        exit:_ -> []
    end.

% Where a table loads from, in the controller: {remote, Node} with the latest running node whose replica is active,
% {local, Reason}, or wait. A table with master nodes loads from them alone (MIM1H10b).
source(Tab) ->
    Running = val(recover_nodes),
    Active = [N || N <- val({Tab, active_replicas}), N =/= node(), lists:member(N, Running)],
    case {masters(Tab), Active} of
        {[], [Node | _]} -> {remote, Node};
        {[], []} -> local_load(val({Tab, cstruct}));
        {Masters, _} -> master_load(val({Tab, cstruct}), Masters, Active)
    end.

% A table with master nodes loads here when this node is one of them (local_master), unless no other node holds it
% (local_only); else from an active master; else it waits, even for a replica on disc here (MIM1H10b).
master_load(Cs, Masters, Active) ->
    case {local_load(Cs), lists:member(node(), Masters), [N || N <- Active, lists:member(N, Masters)]} of
        {{local, local_only} = Alone, _, _} -> Alone;
        {_, true, _} -> {local, local_master};
        {_, false, [Node | _]} -> {remote, Node};
        _ -> wait
    end.

% How a waiting replica loads once Node announced it loaded the table: from Node, unless the table's master nodes
% leave it out (MIM1H10b).
announced(Tab, Node) ->
    Masters = masters(Tab),
    case Masters =:= [] orelse lists:member(Node, Masters) of
        true -> {remote, Node};
        false -> wait
    end.

% The waiting replicas this node adopts when Node comes up (MIM1H10c): those Node holds on disc that no node loaded
% and this node's start leaves waiting while every disc holder runs, when this node is the smallest disc holder.
% {Local, Remote}: those loaded here from disc, and per master node those its master nodes load instead.
orphans(Node) ->
    Running = val({current, db_nodes}),
    Orphans = [T || T <- val({schema, local_tables}), T =/= schema, pending(T), orphan(T, Node, Running)],
    {Local, Mastered} = lists:partition(fun(T) -> masters(T) =:= [] end, Orphans),
    Masters = lists:usort([M || T <- Mastered, M <- masters(T), lists:member(M, Running)]),
    {Local, [{M, [T || T <- Mastered, lists:member(M, masters(T))]} || M <- Masters]}.

orphan(Tab, Node, Running) ->
    #cstruct{disc_copies = Disc, disc_only_copies = DiscOnly} = val({Tab, cstruct}),
    OnDisc = Disc ++ DiscOnly,
    lists:member(Node, OnDisc) andalso val({Tab, active_replicas}) =:= [] andalso OnDisc -- Running =:= [] andalso
        lists:min(OnDisc) =:= node() andalso source(Tab) =:= wait.

% How a replica that another node's adoption asks this node to load does (MIM1H10c): from an active replica, else from
% this node's disc.
adopted(Tab, Reason) ->
    case [N || N <- val({Tab, active_replicas}), N =/= node(), lists:member(N, val(recover_nodes))] of
        [Node | _] -> {remote, Node};
        [] -> {local, Reason}
    end.

masters(Tab) ->
    case ets:lookup(mnesia_decision, {tab, Tab}) of
        [{master_nodes, _, Nodes}] -> Nodes;
        [] -> []
    end.

% A replica no running node has loaded: alone, it loads here (local_only); a RAM replica starts empty unless another
% node holds the table on disc (ram_only); a disc replica loads from its files unless another node holding it on disc
% may have seen it after this one did, that is unless this node logged that node going down before it started
% (initial); otherwise it waits.
local_load(#cstruct{ram_copies = Ram, disc_copies = Disc, disc_only_copies = DiscOnly} = Cs) ->
    Others = (Ram ++ Disc ++ DiscOnly) -- [node()],
    OnDisc = (Disc ++ DiscOnly) -- [node()],
    case {clause_mnesia_schema:storage(Cs), Others, OnDisc -- clause_mnesia_disc:down_at_start()} of
        {_, [], _} -> {local, local_only};
        {ram_copies, _, _} when OnDisc =:= [] -> {local, ram_only};
        {ram_copies, _, _} -> wait;
        {_, _, []} -> {local, initial};
        _ -> wait
    end.

% A replica loaded from Node or here, from the controller's process for the load; a replica loaded here is announced
% to the running nodes, as OTP's i_have_tab.
load(Tab, {remote, Source}) ->
    Storage = clause_mnesia_schema:storage(val({Tab, cstruct})),
    case receive_table(Tab, Storage, Source, load) of
        ok -> gen_server:call(mnesia_monitor, {replica_loaded, Tab, Source, {active_remote, Source}}, infinity);
        {error, _} -> ok
    end;
load(Tab, {local, Reason}) ->
    ok = gen_server:call(mnesia_monitor, {load_local, Tab, Reason}, infinity),
    gen_server:abcast(lists:delete(node(), val(recover_nodes)), mnesia_controller, {i_have_tab, Tab, node()}),
    ok.

val(Name) -> clause_mnesia_monitor:val(Name).
