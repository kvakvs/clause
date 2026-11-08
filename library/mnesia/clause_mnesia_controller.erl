%% mnesia's table controller and late loader, registered as mnesia_controller and mnesia_late_loader as in OTP (plan
%% 21 step MIM1H3, docs/libraries/mnesia.md#processes): the controller answers the table definitions that other nodes
%% fetch (mnesia_controller:get_cstructs/0). After a schema merge (MIM1H5) the controllers of the nodes tell each
%% other which tables they hold, and each reports the other's mnesia up. The controller starts a sender for each
%% table another node loads from this one, and takes the requests of senders that make a replica active (MIM1H9).
%% It runs the loads of this node's replicas, one process per table, once the start decided them or when another node
%% loaded a table that this one waits for (MIM1H10a); its process dictionary keeps {loading, Tab} meanwhile.
-module(clause_mnesia_controller).
-behaviour(gen_server).
-export([start_link/0, start_late_loader/0, call/1]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2]).

-include("clause_mnesia.hrl").

start_link() ->
    gen_server:start_link({local, mnesia_controller}, ?MODULE, controller, [{timeout, infinity}]).

start_late_loader() ->
    gen_server:start_link({local, mnesia_late_loader}, ?MODULE, late_loader, [{timeout, infinity}]).

% A request to the controller; {error, {node_not_running, node()}} when it is not running or ends meanwhile.
call(Request) ->
    case whereis(mnesia_controller) of
        undefined ->
            {error, {node_not_running, node()}};
        Pid ->
            try
                gen_server:call(Pid, Request, infinity)
            catch
                exit:_ -> {error, {node_not_running, node()}}
            end
    end.

% The server's role: controller or late_loader.
init(Role) ->
    {ok, Role}.

handle_call(get_cstructs, _From, controller) ->
    Cstructs = [val({Tab, cstruct}) || Tab <- val({schema, tables})],
    {reply, {cstructs, Cstructs, val({current, db_nodes})}, controller};
% The end of another node's merge, which needs nothing more here (MIM1H5).
handle_call({schema_is_merged, _, _, _}, _From, controller) ->
    {reply, ignore, controller};
% A sender writes the table on the node loading it, then makes the new replica active (MIM1H9); OTP's table blocks
% while it does so are not needed here.
handle_call({update_where_to_write, [add, Tab, Node], _Sender}, _From, controller) ->
    clause_mnesia_schema:add_writer(Tab, Node),
    {reply, true, controller};
handle_call({block_table, _Tabs, _Sender}, _From, controller) ->
    {reply, ok, controller};
handle_call({add_active_replica, [Tab, Node | _], _Sender}, _From, controller) ->
    clause_mnesia_schema:add_active(Tab, Node),
    {reply, true, controller};
% Another node loading its tables at start asks which tables this node holds (MIM1H5): those loaded and those it
% will load from its own disc, which that node then need not load itself (MIM1H10a).
handle_call(disc_load_intents, _From, controller) ->
    {reply, {ok, node(), [T || T <- lists:reverse(val({schema, local_tables})), intends(T)]}, controller}.

% A merge made Node's side run with this node's (MIM1H5): Friends learn the tables this node holds.
handle_cast({im_running, _Node, Friends}, controller) ->
    Tabs = loaded(lists:delete(schema, val({schema, local_tables}))),
    gen_server:abcast(Friends, mnesia_controller, {adopt_orphans, node(), Tabs}),
    {noreply, controller};
% Node runs with this node and holds Tabs: they are read and written there, and its mnesia is up (MIM1H5).
handle_cast({adopt_orphans, Node, Tabs}, controller) ->
    [clause_mnesia_schema:add_active(Tab, Node) || Tab <- [schema | Tabs], ets:member(schema, Tab)],
    ok = clause_mnesia_recover:log_mnesia_up(Node),
    clause_mnesia_subscr:report_system_event({mnesia_up, Node}),
    {noreply, controller};
% Another node loads Tab from this node's replica (MIM1H9).
handle_cast({send_table, Tab, Receiver, Storage, Reason}, controller) ->
    _ = spawn(fun() -> clause_mnesia_loader:send(Tab, Receiver, Storage, Reason) end),
    {noreply, controller};
% Node loaded its replica of Tab (MIM1H5); this node's replica waiting for it loads from it (MIM1H10a).
handle_cast({i_have_tab, Tab, Node}, controller) ->
    ets:member(schema, Tab) andalso clause_mnesia_schema:add_active(Tab, Node),
    start_load(Tab, {remote, Node}),
    {noreply, controller};
% The replicas a start or a merge left unloaded (MIM1H10a): each from another node, from here, or none yet (wait).
handle_cast({load_tables, Tabs}, controller) ->
    [start_load(Tab, clause_mnesia_loader:source(Tab)) || Tab <- Tabs, clause_mnesia_loader:pending(Tab)],
    {noreply, controller};
handle_cast({load_done, Tab}, controller) ->
    erase({loading, Tab}),
    {noreply, controller};
handle_cast(_Request, Role) ->
    {noreply, Role}.

handle_info(_Message, Role) ->
    {noreply, Role}.

% A process loading a replica that waits and is not loading yet (MIM1H10a).
start_load(_, wait) ->
    ok;
start_load(Tab, How) ->
    case get({loading, Tab}) =:= undefined andalso clause_mnesia_loader:pending(Tab) of
        true ->
            put({loading, Tab}, How),
            _ = spawn(fun() ->
                clause_mnesia_loader:load(Tab, How),
                gen_server:cast(mnesia_controller, {load_done, Tab})
            end),
            ok;
        false ->
            ok
    end.

loaded(Tabs) -> [T || T <- Tabs, clause_mnesia_schema:is_loaded(T)].

intends(Tab) ->
    clause_mnesia_schema:is_loaded(Tab) orelse (clause_mnesia_loader:pending(Tab) andalso local_load(Tab)).

local_load(Tab) ->
    case clause_mnesia_loader:source(Tab) of
        {local, _} -> true;
        _ -> false
    end.

val(Name) -> clause_mnesia_monitor:val(Name).
