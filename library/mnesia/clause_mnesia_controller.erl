%% mnesia's table controller and late loader, registered as mnesia_controller and mnesia_late_loader as in OTP (plan
%% 21 step MIM1H3, docs/libraries/mnesia.md#processes): the controller answers the table definitions that other nodes
%% fetch (mnesia_controller:get_cstructs/0). Later steps of MIM1H give them the schema merge and the table loading.
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
    Val = fun clause_mnesia_monitor:val/1,
    Cstructs = [Val({Tab, cstruct}) || Tab <- Val({schema, tables})],
    {reply, {cstructs, Cstructs, Val({current, db_nodes})}, controller}.

handle_cast(_Request, Role) ->
    {noreply, Role}.

handle_info(_Message, Role) ->
    {noreply, Role}.
