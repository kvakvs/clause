%% mnesia's checkpoints (plan 21 steps MIM1F and MIM1H11a, docs/libraries/mnesia.md#checkpoints): a checkpoint keeps
%% its tables as they were at activation. It has a process under mnesia_checkpoint_sup on each of its nodes, which
%% holds a retainer for each table; a node writing a table's replica keeps an ets store of {Tab, Key, OldRecords} for
%% each key changed since activation, and updates retain the records they replace before they change a table. Nodes
%% prepare, join and leave checkpoints with OTP's calls (mnesia-protocol.md#checkpoints-over-nodes-mim1h11a), so OTP's
%% and Clause's nodes share them. The bookkeeping in mnesia_gvar is OTP's: checkpoints, {checkpoint, Name},
%% {Tab, checkpoints}, {Tab, {retainer, Name}} and a {checkpoints, Names} entry first in the table's commit_work.
-module(clause_mnesia_checkpoint).
-behaviour(gen_server).
-export([activate/1, deactivate/1, tables_and_cookie/1, retainer/2, retain/3, start_link/1]).
%% Checkpoints over nodes (MIM1H11a): OTP's calls from other nodes, and this node's replicas coming and going.
-export([tm_prepare/1, call/2, cast/2, remote_deactivate/1, add_copy/2, del_copy/2, mnesia_down/1]).
-export([init/1, handle_call/3, handle_cast/2]).

-include("clause_mnesia.hrl").

%% How often a checkpoint being prepared looks whether the commits applying here ended, 10 ms apart.
-define(COMMITTED_POLLS, 500).

%% A checkpoint's arguments and state, OTP's record that nodes exchange: its name, whether a table without a replica
%% here may be retained elsewhere, the tables whose RAM records override their dump, its nodes, the node activating
%% it, its cookie, the tables kept with least and most redundancy, the retainers, and its supervisor and process.
-record(checkpoint_args, {
    name = {erlang:unique_integer([positive]), node()},
    allow_remote = true,
    ram_overrides_dump = false,
    nodes = [],
    node = node(),
    now,
    cookie,
    min = [],
    max = [],
    pending_tab,
    wait_for_old,
    is_activated = false,
    ignore_new = [],
    retainers = [],
    iterators = [],
    supervisor,
    pid
}).

%% A table's retainer, as OTP's gvar row {Tab, {retainer, Name}} holds it: the store of retained records on a node
%% that writes it, the nodes that write it, and whether it retains updates (false for a RAM table read from its dump).
-record(retainer, {cp_name, tab_name, store, writers = [], really_retain = true}).

%% Activation and deactivation, in the caller.

% {ok, Name, Nodes} or {error, Reason}, with OTP's checks.
activate(Args) when is_list(Args) ->
    try lists:foldl(fun check_arg/2, #checkpoint_args{}, Args) of
        Cp ->
            case check_tables(Cp) of
                {ok, Overriders, Tabs} -> arrange(Cp#checkpoint_args{ram_overrides_dump = Overriders}, Tabs);
                Error -> Error
            end
    catch
        exit:Reason -> {error, Reason};
        error:Reason -> {error, Reason}
    end;
activate(Args) ->
    {error, {badarg, Args}}.

check_arg({name, Name}, Cp) ->
    lists:member(Name, checkpoints()) andalso exit({already_exists, Name}),
    Cp#checkpoint_args{name = Name};
check_arg({allow_remote, Bool}, Cp) when is_boolean(Bool) ->
    Cp#checkpoint_args{allow_remote = Bool};
check_arg({ram_overrides_dump, Overrides}, Cp) when is_boolean(Overrides); is_list(Overrides) ->
    Cp#checkpoint_args{ram_overrides_dump = Overrides};
check_arg({min, Tabs}, Cp) when is_list(Tabs) ->
    Cp#checkpoint_args{min = Tabs};
check_arg({max, Tabs}, Cp) when is_list(Tabs) ->
    Cp#checkpoint_args{max = Tabs};
check_arg({ignore_new, Tids}, Cp) when is_list(Tids) ->
    Cp;
check_arg(Arg, _) ->
    exit({badarg, Arg}).

% The tables, none of them both min and max; ram_overrides_dump tables must be in both lists, which no table can be.
check_tables(#checkpoint_args{name = Name, min = Min, max = Max, ram_overrides_dump = Overriders}) ->
    case {[T || T <- Min, lists:member(T, Max)], Overriders} of
        {[_ | _] = Both, _} -> {error, {combine_error, Name, [{min, Both}, {max, Both}]}};
        _ when Min =:= [], Max =:= [] -> {error, {combine_error, Name, [{min, Min}, {max, Max}]}};
        {[], false} -> {ok, [], Min ++ Max};
        {[], true} -> {ok, Min ++ Max, Min ++ Max};
        {[], _} -> overriders(Name, Overriders, Min, Max)
    end.

overriders(Name, Overriders, Min, Max) ->
    case {[T || T <- Overriders, not lists:member(T, Min)], [T || T <- Overriders, not lists:member(T, Max)]} of
        {[], []} -> {ok, Overriders, Min ++ Max};
        {[], Outside} -> {error, {combine_error, Name, [{ram_overrides_dump, Outside}, {max, Outside}]}};
        {Outside, _} -> {error, {combine_error, Name, [{ram_overrides_dump, Outside}, {min, Outside}]}}
    end.

% A retainer for each table, written by its active replicas (max) or by one of them (min); the checkpoint is then
% prepared and activated on the nodes writing them.
arrange(#checkpoint_args{name = Name} = Cp, Tabs) ->
    try [#retainer{cp_name = Name, tab_name = T, writers = writers(Cp, T)} || T <- Tabs] of
        Retainers ->
            prepare_on_nodes(Cp#checkpoint_args{
                retainers = Retainers, nodes = writer_nodes(Retainers), cookie = cookie()
            })
    catch
        throw:Reason -> {error, Reason}
    end.

% A max table's active replicas; a min table's replica here, or another one when the checkpoint allows it.
writers(#checkpoint_args{name = Name, allow_remote = Remote, max = Max}, Tab) ->
    Active = clause_mnesia_monitor:val({Tab, active_replicas}),
    case {lists:member(Tab, Max), Remote, lists:member(node(), Active)} of
        _ when Active =:= [] -> throw({"Cannot prepare checkpoint (replica not available)", [Tab, Name]});
        {true, _, _} -> Active;
        {false, _, true} -> [node()];
        {false, true, false} -> [hd(Active)];
        _ -> throw({"Cannot prepare checkpoint (replica not available)", [Tab, Name]})
    end.

% The nodes writing any retainer, in OTP's order (observed with two nodes: the greater first).
writer_nodes(Retainers) ->
    lists:reverse(lists:usort(lists:append([W || #retainer{writers = W} <- Retainers]))).

cookie() ->
    {{erlang:monotonic_time() + erlang:time_offset(), erlang:unique_integer(), 1}, node()}.

% Each node prepares the checkpoint, then collects its pending transactions and activates it, as OTP's activation;
% the first failure deactivates it everywhere.
prepare_on_nodes(#checkpoint_args{name = Name, nodes = Nodes} = Cp) ->
    case [R || R <- [on(N, mnesia_tm, prepare_checkpoint, [Cp]) || N <- Nodes], element(1, R) =/= ok] of
        [] ->
            Pending = lists:append([
                P
             || {ok, P} <- [on(N, mnesia_checkpoint, call, [Name, collect_pending]) || N <- Nodes]
            ]),
            [on(N, mnesia_checkpoint, call, [Name, {activate, Pending}]) || N <- Nodes],
            {ok, Name, Nodes};
        [Error | _] ->
            [on(N, mnesia_checkpoint, remote_deactivate, [Name]) || N <- Nodes],
            Error
    end.

% M:F(A) on Node: OTP's names here, or a call there; {badrpc, Reason} when Node fails.
on(Node, M, F, A) when Node =:= node() ->
    local(M, F, A);
on(Node, M, F, A) ->
    % The literal funs link this node's OTP-named entries into programs, which other nodes call by name.
    _ = [fun mnesia_tm:prepare_checkpoint/1, fun mnesia_checkpoint:call/2, fun mnesia_checkpoint:cast/2],
    _ = [fun mnesia_checkpoint:remote_deactivate/1, fun mnesia_checkpoint:tm_prepare/1],
    rpc:call(Node, M, F, A).

% OTP's entries on this node, as they answer other nodes.
local(mnesia_tm, prepare_checkpoint, [Cp]) -> tm_prepare(Cp);
local(mnesia_checkpoint, tm_prepare, [Cp]) -> tm_prepare(Cp);
local(mnesia_checkpoint, call, [Name, Request]) -> call(Name, Request);
local(mnesia_checkpoint, cast, [Name, Message]) -> cast(Name, Message);
local(mnesia_checkpoint, remote_deactivate, [Name]) -> remote_deactivate(Name).

% The checkpoint's processes on its nodes end: ok, or {error, {no_exists, Name}} without one here.
deactivate(Name) ->
    case call(Name, get_checkpoint) of
        #checkpoint_args{nodes = Nodes} ->
            [on(N, mnesia_checkpoint, remote_deactivate, [Name]) || N <- Nodes],
            ok;
        Error ->
            Error
    end.

remote_deactivate(Name) ->
    call(Name, deactivate).

% The checkpoint's tables and cookie, for its backup.
tables_and_cookie(Name) ->
    case call(Name, get_checkpoint) of
        #checkpoint_args{min = Min, max = Max, cookie = Cookie} -> {ok, Min ++ Max, Cookie};
        Error -> Error
    end.

% A table's retainer in a checkpoint: {ok, Store, ReallyRetain} where this node writes it, {remote, Node} for a node
% writing it elsewhere, or an error when the checkpoint does not hold it here.
retainer(Name, Tab) ->
    case ets:lookup(?GVAR, {Tab, {retainer, Name}}) of
        [{_, #retainer{store = {ets, Store}, writers = Writers, really_retain = Really}}] ->
            case lists:member(node(), Writers) of
                true -> {ok, Store, Really};
                false -> {remote, hd(Writers)}
            end;
        [{_, #retainer{writers = [Node | _]}}] ->
            {remote, Node};
        _ ->
            {error, {"No retainer attached to table", [Tab, Name]}}
    end.

% A request to the checkpoint's process here, OTP's mnesia_checkpoint:call/2.
call(Name, Request) ->
    case ets:lookup(?GVAR, {checkpoint, Name}) of
        [{_, Pid}] ->
            try
                gen_server:call(Pid, Request, infinity)
            catch
                exit:{Reason, _} -> {error, {"Got exit", [Name, Reason]}}
            end;
        [] ->
            {error, {no_exists, Name}}
    end.

% A message to the checkpoint's process here, OTP's mnesia_checkpoint:cast/2.
cast(Name, Message) ->
    case ets:lookup(?GVAR, {checkpoint, Name}) of
        [{_, Pid}] ->
            gen_server:cast(Pid, Message),
            {ok, Pid};
        [] ->
            {error, {no_exists, Name}}
    end.

% Message to the checkpoint's process on each of Nodes, as OTP's abcast through erpc.
abcast(Nodes, Name, Message) ->
    [erpc:cast(N, mnesia_checkpoint, cast, [Name, Message]) || N <- Nodes],
    ok.

checkpoints() ->
    try
        ets:lookup_element(?GVAR, checkpoints, 2)
    catch
        error:_ -> []
    end.

% The checkpoints whose retainer of Tab this node writes.
checkpoints(Tab) ->
    case clause_mnesia_monitor:val({Tab, commit_work}) of
        [{checkpoints, Names} | _] -> Names;
        _ -> []
    end.

%% This node's replicas coming and going (MIM1H11a).

% A sender here gave Node a replica of Tab: each checkpoint keeping Tab with most redundancy joins Node, as OTP's.
add_copy(Tab, Node) ->
    [call(Name, {add_copy, Tab, Node}) || Name <- checkpoints(Tab)],
    ok.

% This node's replica of Tab goes: its retainers lose this node, and a checkpoint without writers of a table ends.
del_copy(Tab, Node) ->
    [call(Name, {del_copy, Tab, Node}) || Name <- checkpoints(Tab)],
    ok.

% Node's mnesia went: each checkpoint here loses it as a writer.
mnesia_down(Node) ->
    [call(Name, {mnesia_down, Node}) || Name <- checkpoints()],
    ok.

%% Retaining, where an update happens.

% The records of Key that an update replaces, kept by each checkpoint of the table that has not kept the key yet; a
% cleared table keeps each of its keys.
retain(Tab, Key, Update) ->
    case ets:lookup(?GVAR, {Tab, commit_work}) of
        [{_, [{checkpoints, Names} | _]}] -> retain_keys(Tab, Key, Update, Names);
        _ -> ok
    end.

retain_keys(Tab, _, {clear_table, _}, Names) ->
    Records = stored(Tab, '_'),
    [
        retain_key(Tab, K, [R || R <- Records, element(2, R) =:= K], Names)
     || K <- lists:usort([element(2, R) || R <- Records])
    ],
    ok;
retain_keys(Tab, Key, _, Names) ->
    retain_key(Tab, Key, stored(Tab, Key), Names).

retain_key(Tab, Key, Old, Names) ->
    [ets:insert_new(Store, {Tab, Key, Old}) || Name <- Names, {ok, Store, true} <- [retainer(Name, Tab)]],
    ok.

stored(schema, '_') -> ets:tab2list(schema);
stored(schema, Key) -> ets:lookup(schema, Key);
stored(Tab, '_') -> clause_mnesia_db:match_object(Tab, '_');
stored(Tab, Key) -> clause_mnesia_db:lookup(Tab, Key).

%% The checkpoint's process.

% This node's part of a checkpoint, OTP's mnesia_checkpoint:tm_prepare/1: {ok, Name, IgnoreNew, node()}. The commits
% of other nodes' transactions still applying here end first, so that their writes come before the checkpoint, where
% OTP's retainers ignore those transactions instead.
tm_prepare(#checkpoint_args{name = Name} = Cp) ->
    try
        committed(clause_mnesia_tm:committing(), ?COMMITTED_POLLS),
        supervisor:start_child(mnesia_checkpoint_sup, [Cp])
    of
        {ok, _} -> {ok, Name, [], node()};
        {error, Reason} -> {error, {"Cannot create checkpoint retainer", Name, node(), Reason}}
    catch
        exit:_ -> {error, {node_not_running, node()}}
    end.

committed(_, 0) ->
    ok;
committed(Tids, Polls) ->
    case [T || T <- clause_mnesia_tm:committing(), lists:member(T, Tids)] of
        [] ->
            ok;
        Left ->
            timer:sleep(10),
            committed(Left, Polls - 1)
    end.

start_link(Cp) ->
    gen_server:start_link(?MODULE, Cp, []).

init(#checkpoint_args{name = Name, retainers = Retainers} = Cp) ->
    Cp2 = Cp#checkpoint_args{supervisor = whereis(mnesia_checkpoint_sup), pid = self()},
    Prepared = [prepare(Cp2, R) || R <- Retainers],
    ets:insert(?GVAR, {{checkpoint, Name}, self()}),
    add(checkpoints, Name),
    {ok, Cp2#checkpoint_args{retainers = Prepared}}.

% A retainer's gvar row; this node keeps a store where it writes the table.
prepare(Cp, #retainer{writers = Writers} = R) ->
    case lists:member(node(), Writers) of
        true -> prepare_store(Cp, R);
        false -> row(R)
    end.

% A retainer's store, its gvar rows and the checkpoints entry of its table's commit work. A RAM table whose RAM records
% do not override its dump keeps its dumped records and retains nothing.
prepare_store(#checkpoint_args{ram_overrides_dump = Overriders}, #retainer{cp_name = Name, tab_name = Tab} = R) ->
    Store = ets:new(mnesia_retainer, [set, public, {keypos, 2}]),
    Really =
        case clause_mnesia_monitor:val({Tab, storage_type}) =:= ram_copies andalso not overrides(Tab, Overriders) of
            true -> dumped(Tab, Store);
            false -> true
        end,
    add({Tab, checkpoints}, Name),
    add_to_work(Tab, Name),
    row(R#retainer{store = {ets, Store}, really_retain = Really}).

overrides(Tab, Overriders) -> is_list(Overriders) andalso lists:member(Tab, Overriders).

row(#retainer{cp_name = Name, tab_name = Tab} = R) ->
    ets:insert(?GVAR, {{Tab, {retainer, Name}}, R}),
    R.

dumped(Tab, Store) ->
    [
        ets:insert(Store, {Tab, element(2, Rec), [Rec | old(Store, element(2, Rec))]})
     || Rec <- clause_mnesia_disc:dumped_records(Tab)
    ],
    false.

old(Store, Key) ->
    case ets:lookup(Store, Key) of
        [] -> [];
        [{_, _, Old}] -> Old
    end.

add_to_work(Tab, Name) ->
    case clause_mnesia_monitor:val({Tab, commit_work}) of
        [{checkpoints, Names} | Rest] ->
            set({Tab, commit_work}, [{checkpoints, [Name | lists:delete(Name, Names)]} | Rest]);
        Work ->
            set({Tab, commit_work}, [{checkpoints, [Name]} | Work])
    end.

handle_call(get_checkpoint, _From, Cp) ->
    {reply, Cp, Cp};
handle_call(collect_pending, _From, Cp) ->
    {reply, {ok, []}, Cp};
handle_call({activate, _Pending}, _From, Cp) ->
    {reply, activated, Cp#checkpoint_args{is_activated = true}};
handle_call(deactivate, _From, Cp) ->
    stop(Cp),
    {stop, normal, deactivated, Cp};
handle_call({add_copy, Tab, Node}, _From, Cp) ->
    {reply, ok, add_copy_to(Cp, Tab, Node)};
handle_call({del_copy, Tab, Node}, _From, Cp) ->
    case retainer_of(Tab, Cp) of
        false ->
            {reply, ok, Cp};
        R ->
            abcast(Cp#checkpoint_args.nodes -- [node()], Cp#checkpoint_args.name, {del_retainer, R, Node}),
            reply_or_stop(ok, del_writer(Cp, Tab, Node))
    end;
handle_call({mnesia_down, Node}, _From, Cp) ->
    reply_or_stop(ok, down(Cp, Node)).

handle_cast({add_retainer, #retainer{tab_name = Tab, writers = Writers}, Node}, Cp) ->
    {noreply, set_writers(Cp, Tab, Writers, Node)};
handle_cast({del_retainer, #retainer{tab_name = Tab}, Node}, Cp) ->
    case del_writer(Cp, Tab, Node) of
        {stop, Cp2} -> {stop, normal, Cp2};
        Cp2 -> {noreply, Cp2}
    end;
handle_cast({retain, _Tid, Tab, Key, Old}, Cp) ->
    case retainer_of(Tab, Cp) of
        #retainer{store = {ets, Store}, really_retain = true} -> ets:insert_new(Store, {Tab, Key, Old});
        _ -> ok
    end,
    {noreply, Cp};
handle_cast(_, Cp) ->
    {noreply, Cp}.

reply_or_stop(Reply, {stop, Cp}) -> {stop, normal, Reply, Cp};
reply_or_stop(Reply, Cp) -> {reply, Reply, Cp}.

retainer_of(Tab, #checkpoint_args{retainers = Retainers}) ->
    lists:keyfind(Tab, #retainer.tab_name, Retainers).

% Node gets a replica of Tab: a max retainer's writers take it, the checkpoint is prepared there, the other nodes hear
% of the new writer, and the records retained here are sent to it, as OTP's sender does.
add_copy_to(#checkpoint_args{name = Name, max = Max, nodes = Nodes} = Cp, Tab, Node) ->
    case {lists:member(Tab, Max), retainer_of(Tab, Cp)} of
        {true, #retainer{writers = Writers, store = Store} = R} ->
            R2 = R#retainer{writers = Writers ++ [Node]},
            _ = on(Node, mnesia_checkpoint, tm_prepare, [Cp]),
            abcast(Nodes -- [node(), Node], Name, {add_retainer, R2, Node}),
            _ = on(Node, mnesia_checkpoint, cast, [Name, {add_retainer, R2, Node}]),
            send_retained(Node, Name, Tab, Store),
            set_writers(Cp, Tab, R2#retainer.writers, Node);
        _ ->
            Cp
    end.

send_retained(Node, Name, Tab, {ets, Store}) ->
    [abcast([Node], Name, {retain, {dirty, send_retainer}, Tab, Key, Old}) || {_, Key, Old} <- ets:tab2list(Store)];
send_retained(_, _, _, _) ->
    ok.

% A retainer's new writers; Node, when it is this node, makes its store.
set_writers(Cp, Tab, Writers, Node) ->
    case retainer_of(Tab, Cp) of
        false ->
            Cp;
        R when Node =:= node() ->
            replace(Cp, prepare_store(Cp, R#retainer{writers = Writers}));
        R ->
            replace(Cp, row(R#retainer{writers = Writers}))
    end.

% A retainer loses writer Node, its store with it on Node; a retainer without writers ends the checkpoint with OTP's
% report: {stop, Cp}.
del_writer(Cp, Tab, Node) ->
    case retainer_of(Tab, Cp) of
        #retainer{writers = Writers} = R when Writers =/= [] ->
            Node =:= node() andalso drop_store(R),
            Cp2 = replace(Cp, row(R#retainer{writers = lists:delete(Node, Writers)})),
            case lists:delete(Node, Writers) of
                [] -> ended(Cp2);
                _ -> Cp2
            end;
        _ ->
            Cp
    end.

% Node's mnesia went: it writes no retainer, and the checkpoint ends when a retainer has no writer left, as OTP's.
down(#checkpoint_args{retainers = Retainers} = Cp, Node) ->
    Left = [R#retainer{writers = lists:delete(Node, W)} || #retainer{writers = W} = R <- Retainers],
    Cp2 = lists:foldl(fun(R, Acc) -> replace(Acc, row(R)) end, Cp, Left),
    case [R || #retainer{writers = []} = R <- Left] of
        [] -> Cp2;
        _ -> ended(Cp2)
    end.

ended(#checkpoint_args{name = Name} = Cp) ->
    clause_mnesia_subscr:report_system_event({mnesia_checkpoint_deactivated, Name}),
    stop(Cp),
    {stop, Cp}.

replace(#checkpoint_args{retainers = Retainers} = Cp, #retainer{tab_name = Tab} = R) ->
    Retainers2 = lists:keyreplace(Tab, #retainer.tab_name, Retainers, R),
    Cp#checkpoint_args{retainers = Retainers2, nodes = writer_nodes(Retainers2)}.

% The checkpoint ends here: its bookkeeping and each retainer go.
stop(#checkpoint_args{name = Name, retainers = Retainers}) ->
    ets:delete(?GVAR, {checkpoint, Name}),
    del(checkpoints, Name),
    [remove(R) || R <- Retainers],
    ok.

remove(#retainer{cp_name = Name, tab_name = Tab, writers = Writers} = R) ->
    lists:member(node(), Writers) andalso drop_store(R),
    ets:delete(?GVAR, {Tab, {retainer, Name}}).

% This node's store of a retainer goes, with its table's checkpoint entries; a table that is gone has none left.
drop_store(#retainer{cp_name = Name, tab_name = Tab, store = {ets, Store}}) ->
    try
        ets:delete(Store),
        del({Tab, checkpoints}, Name),
        case clause_mnesia_monitor:val({Tab, commit_work}) of
            [{checkpoints, [Name]} | Rest] -> set({Tab, commit_work}, Rest);
            [{checkpoints, Names} | Rest] -> set({Tab, commit_work}, [{checkpoints, lists:delete(Name, Names)} | Rest]);
            _ -> ok
        end
    catch
        _:_ -> ok
    end;
drop_store(_) ->
    ok.

add(Var, Value) ->
    set(Var, [Value | lists:delete(Value, clause_mnesia_monitor:val(Var))]).

del(Var, Value) ->
    set(Var, lists:delete(Value, clause_mnesia_monitor:val(Var))).

set(Name, Value) -> ets:insert(?GVAR, {Name, Value}).
