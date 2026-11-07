%% mnesia's table controller and late loader, registered as mnesia_controller and mnesia_late_loader as in OTP (plan
%% 21 step MIM1H3, docs/libraries/mnesia.md#processes): the controller answers the table definitions that other nodes
%% fetch (mnesia_controller:get_cstructs/0). After a schema merge (MIM1H5) the controllers of the nodes tell each
%% other which tables they hold, and each reports the other's mnesia up. Later steps give them the table loading.
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
% Another node loading its tables at start asks which tables this node holds (MIM1H5).
handle_call(disc_load_intents, _From, controller) ->
    {reply, {ok, node(), lists:reverse(val({schema, local_tables}))}, controller}.

% A merge made Node's side run with this node's (MIM1H5): Friends learn the tables this node holds.
handle_cast({im_running, _Node, Friends}, controller) ->
    Tabs = lists:delete(schema, val({schema, local_tables})),
    gen_server:abcast(Friends, mnesia_controller, {adopt_orphans, node(), Tabs}),
    {noreply, controller};
% Node runs with this node and holds Tabs: they are read and written there, and its mnesia is up (MIM1H5).
handle_cast({adopt_orphans, Node, Tabs}, controller) ->
    [clause_mnesia_schema:add_active(Tab, Node) || Tab <- [schema | Tabs], ets:member(schema, Tab)],
    ok = clause_mnesia_recover:log_mnesia_up(Node),
    clause_mnesia_subscr:report_system_event({mnesia_up, Node}),
    {noreply, controller};
% Node loaded its replica of Tab (MIM1H5).
handle_cast({i_have_tab, Tab, Node}, controller) ->
    ets:member(schema, Tab) andalso clause_mnesia_schema:add_active(Tab, Node),
    {noreply, controller};
handle_cast(_Request, Role) ->
    {noreply, Role}.

handle_info(_Message, Role) ->
    {noreply, Role}.

val(Name) -> clause_mnesia_monitor:val(Name).
