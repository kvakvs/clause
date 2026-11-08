%% mnesia's recovery server, registered as mnesia_recover as in OTP (plan 21 steps MIM1H3, MIM1H4,
%% docs/libraries/mnesia.md#connecting-nodes): it connects this node's mnesia to other nodes', with which it then
%% exchanges transaction serials and decisions, and it records another node's mnesia going down. The nodes it is
%% connected to are the recover_nodes variable. It keeps the outcomes of the transactions committed over several nodes
%% that this node took part in, which other nodes ask about (MIM1H8). On a disc schema it logs a commit's committed
%% decision until each disc node taking part is certain of it, as their im_certain messages say (MIM1H10a).
-module(clause_mnesia_recover).
-behaviour(gen_server).
-export([start_link/0, call/1, connect_nodes/1, has_mnesia_down/1, mnesia_down/1, log_mnesia_up/1, sync_serial/1]).
-export([note_decision/2]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2]).

-include("clause_mnesia.hrl").

%% The outcomes kept in the newest generation before the oldest one is dropped (MIM1H8).
-define(GENERATION_SIZE, 1000).

%% The server's state: transaction outcomes as {Tid, Outcome} in two ets generations, the newest first (MIM1H8); per
%% commit over disc nodes, the nodes not yet certain once committed here, or those certain before (MIM1H10a), tagged
%% announced once this node told the others it is certain (MIM1H10b).
-record(state, {latest, previous, certain = #{}}).

start_link() ->
    gen_server:start_link({local, mnesia_recover}, ?MODULE, [], [{timeout, infinity}]).

% A request to the server; {error, {node_not_running, node()}} when it is not running or ends meanwhile.
call(Request) ->
    case whereis(mnesia_recover) of
        undefined ->
            {error, {node_not_running, node()}};
        Pid ->
            try
                gen_server:call(Pid, Request, infinity)
            catch
                exit:_ -> {error, {node_not_running, node()}}
            end
    end.

% {NewlyConnected, AlreadyConnected}: the nodes whose mnesia agreed on a protocol and runs.
connect_nodes(Nodes) ->
    call({connect_nodes, Nodes}).

% Whether this node logged Node's mnesia going down since it last came up.
has_mnesia_down(Node) ->
    ets:member(mnesia_decision, {node, Node}).

% Node's mnesia came up with this node's after a schema merge (MIM1H5): its mnesia_down record goes.
log_mnesia_up(Node) ->
    call({log_mnesia_up, Node}).

% A transaction committed over several nodes was decided here: committed, aborted or still unclear (MIM1H8).
note_decision(Tid, Outcome) ->
    gen_server:cast(mnesia_recover, {note_decision, Tid, Outcome}).

% Node's mnesia went down, as its monitor's link says: it is no longer a node to recover with.
mnesia_down(Node) ->
    del(recover_nodes, Node),
    gen_server:cast(mnesia_recover, {mnesia_down, Node}).

init([]) ->
    set(recover_nodes, []),
    {ok, #state{latest = decisions(), previous = decisions()}}.

decisions() -> ets:new(mnesia_transient_decision, [set, private]).

% A commit's coordinator is certain once it sent committed, a participant once it heard it (MIM1H10b): the other disc
% nodes hear it at once, as from OTP's, before the log follows.
handle_call({certain, #decision{tid = Tid, disc_nodes = Disc} = Decision}, _From, #state{certain = Certain} = State) ->
    Known = known(maps:get(Tid, Certain, [])),
    Others = lists:delete(node(), Disc),
    Told = Decision#decision{outcome = committed, disc_nodes = Others -- Known, ram_nodes = []},
    gen_server:abcast(Others, mnesia_recover, {im_certain, node(), Told}),
    {reply, ok, State#state{certain = Certain#{Tid => {announced, Known}}}};
handle_call({connect_nodes, Nodes}, _From, State) ->
    Connected = val(recover_nodes),
    case clause_mnesia_monitor:negotiate_protocol(lists:delete(node(), Nodes) -- Connected) of
        [] -> {reply, {[], Connected}, State};
        Agreed -> {reply, {connected(Agreed), Connected}, State}
    end;
handle_call({log_mnesia_up, Node}, _From, State) ->
    ets:delete(mnesia_decision, {node, Node}),
    {Date, Time} = erlang:localtime(),
    Node =:= node() orelse clause_mnesia_disc:log_node({mnesia_up, Node, Date, Time}),
    {reply, ok, State};
% Another node asks what became of a transaction (MIM1H7): the outcome kept here (MIM1H8), or the asker's default, as
% OTP's for a transaction it never decided.
handle_call({what_happened, Default, Tid}, _From, State) ->
    {reply, {ok, outcome(Tid, Default, State)}, State};
handle_call(_Request, _From, State) ->
    {reply, badop, State}.

% The agreed nodes whose mnesia still runs join recover_nodes and learn this node's serial; a node that logged this
% one down while this one logged it down means a partition at start, unless the schema has master nodes.
connected(Agreed) ->
    Good = [N || N <- Agreed, lists:member(remote_status(N), [yes, starting])],
    set(recover_nodes, union(val(recover_nodes), Good)),
    gen_server:cast(self(), {announce_all, Good}),
    ets:member(mnesia_decision, {tab, schema}) orelse
        clause_mnesia_connect:detect_inconsistency(Good, starting_partitioned_network),
    Good.

remote_status(Node) ->
    case rpc:call(Node, mnesia_lib, is_running, []) of
        {badrpc, _} -> no;
        Status -> Status
    end.

handle_cast({announce_all, Nodes}, State) ->
    Serial = ets:lookup_element(mnesia_decision, serial, 3),
    [gen_server:abcast([Node], mnesia_recover, {decisions, node(), [{trans_tid, serial, Serial}]}) || Node <- Nodes],
    {noreply, State};
handle_cast({decisions, Node, Decisions}, State) ->
    set(recover_nodes, [Node | lists:delete(Node, val(recover_nodes))]),
    [sync_serial(N) || {trans_tid, serial, N} <- Decisions],
    {noreply, State};
handle_cast({what_decision, Node, #decision{tid = Tid} = Asked}, State) ->
    sync_serial(Tid#tid.counter),
    Decision = decision(Tid, Asked#decision{outcome = outcome(Tid, Asked#decision.outcome, State)}),
    gen_server:abcast([Node], mnesia_recover, {decisions, node(), [Decision]}),
    {noreply, State};
handle_cast({note_decision, Tid, Outcome}, State) ->
    {noreply, noted(Tid, Outcome, State)};
% A commit over several nodes was logged here (MIM1H10a): its decision names the disc nodes not yet certain, and the
% other disc nodes hear that this one is, unless its coordinator told them already (MIM1H10b).
handle_cast({committed, #decision{tid = Tid, disc_nodes = Disc} = Decision}, #state{certain = Certain} = State) ->
    Others = lists:delete(node(), Disc),
    Kept = maps:get(Tid, Certain, []),
    Remaining = Others -- known(Kept),
    Logged = Decision#decision{outcome = committed, disc_nodes = Remaining, ram_nodes = []},
    clause_mnesia_disc:log_decision(Logged),
    is_tuple(Kept) orelse gen_server:abcast(Others, mnesia_recover, {im_certain, node(), Logged}),
    {noreply, State#state{certain = still_unsure(Tid, {committed, Remaining}, Certain)}};
% Node is certain of a commit: logged again without it, or kept for when this node commits.
handle_cast({im_certain, Node, #decision{tid = Tid} = Decision}, #state{certain = Certain} = State) ->
    case maps:get(Tid, Certain, []) of
        {committed, Remaining} ->
            Left = lists:delete(Node, Remaining),
            clause_mnesia_disc:log_decision(Decision#decision{outcome = committed, disc_nodes = Left, ram_nodes = []}),
            {noreply, State#state{certain = still_unsure(Tid, {committed, Left}, Certain)}};
        Known ->
            clause_mnesia_disc:log_decision(Decision#decision{outcome = committed, disc_nodes = [], ram_nodes = []}),
            {noreply, State#state{certain = Certain#{Tid => with_known(Node, Known)}}}
    end;
handle_cast({mnesia_down, Node}, State) ->
    del(recover_nodes, Node),
    clause_mnesia_monitor:is_running() =:= yes andalso log_mnesia_down(Node),
    del({current, db_nodes}, Node),
    % Its replicas are no longer active, and its transactions' locks go (MIM1H5).
    [clause_mnesia_schema:del_active(Tab, Node) || Tab <- tables()],
    clause_mnesia_locker:mnesia_down(Node),
    ets:delete(?GVAR, {node_up, Node}),
    clause_mnesia_subscr:report_system_event({mnesia_down, Node}),
    {noreply, State};
handle_cast(_Request, State) ->
    {noreply, State}.

handle_info(_Message, State) ->
    {noreply, State}.

% The nodes certain of a commit before this node logged it, announced or not.
known({announced, Known}) -> Known;
known(Known) -> Known.

with_known(Node, {announced, Known}) -> {announced, [Node | Known]};
with_known(Node, Known) -> [Node | Known].

still_unsure(Tid, {committed, []}, Certain) -> maps:remove(Tid, Certain);
still_unsure(Tid, Unsure, Certain) -> Certain#{Tid => Unsure}.

% A transaction's outcome in the newest generation; a full one becomes the older generation.
noted(Tid, Outcome, #state{latest = Latest, previous = Previous} = State) ->
    ets:insert(Latest, {Tid, Outcome}),
    case ets:info(Latest, size) >= ?GENERATION_SIZE of
        true ->
            ets:delete(Previous),
            State#state{latest = decisions(), previous = Latest};
        false ->
            State
    end.

% The outcome kept for a transaction, or Default.
outcome(Tid, Default, #state{latest = Latest, previous = Previous}) ->
    case ets:lookup(Latest, Tid) ++ ets:lookup(Previous, Tid) of
        [{_, Outcome} | _] -> Outcome;
        [] -> Default
    end.

% This node's decision on a transaction, or the asker's own when this node has none.
decision(Tid, Asked) ->
    case ets:lookup(mnesia_decision, Tid) of
        [#decision{} = Decision] -> Decision;
        _ -> Asked
    end.

tables() ->
    try
        val({schema, tables})
    catch
        error:badarg -> []
    end.

% Another node's transaction serial: this node's next transactions come after it.
sync_serial(Serial) ->
    Serial > ets:lookup_element(mnesia_decision, serial, 3) andalso
        ets:insert(mnesia_decision, {trans_tid, serial, Serial + 1}).

% {mnesia_down, {node, Node}, Date, Time} in mnesia_decision, and in the log on a disc schema, as OTP's.
log_mnesia_down(Node) ->
    {Date, Time} = erlang:localtime(),
    clause_mnesia_disc:log_node({mnesia_down, Node, Date, Time}),
    ets:insert(mnesia_decision, {mnesia_down, {node, Node}, Date, Time}).

% The elements of Old not in New, then New, as OTP's list variables grow.
union(Old, New) -> [N || N <- Old, not lists:member(N, New)] ++ New.

val(Name) -> ets:lookup_element(?GVAR, Name, 2).

set(Name, Value) -> ets:insert(?GVAR, {Name, Value}).

del(Name, Node) ->
    try
        set(Name, lists:delete(Node, val(Name)))
    catch
        error:badarg -> ok
    end.
