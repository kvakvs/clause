%% Clause's net_adm (plan 21 step LIB25, docs/libraries/library.md), as OTP's (plan 30 step DIST7d): pings ask the other
%% node's net_kernel (DIST4b3b1); worlds ping the nodes the epmd of each host names; names ask epmd through the kernel's
%% epmd_module (DIST3a); .hosts.erlang is read from the current directory, the home directory, then the root directory.
-module(net_adm).
-export([
    dns_hostname/1,
    host_file/0,
    localhost/0,
    names/0,
    names/1,
    ping/1,
    ping_list/1,
    world/0,
    world/1,
    world_list/1,
    world_list/2
]).

host_file() ->
    Home =
        case init:get_argument(home) of
            {ok, [[Dir]]} -> [Dir];
            _ -> []
        end,
    case file:path_consult(["." | Home] ++ [code:root_dir()], ".hosts.erlang") of
        {ok, Hosts, _} -> Hosts;
        Error -> Error
    end.

%% A ping asks the other node's net_kernel whether this node is authorized; the call connects the nodes (plan 30 step
%% DIST4b3b1).
ping(Node) when is_atom(Node) ->
    case catch gen:call({net_kernel, Node}, '$gen_call', {is_auth, node()}, infinity) of
        {ok, yes} ->
            pong;
        _ ->
            erlang:disconnect_node(Node),
            pang
    end.

%% The host name with the resolver's domain.
localhost() ->
    {ok, Host} = inet:gethostname(),
    case maps:get(domain, clause_inet_config:config(), "") of
        "" -> Host;
        Domain -> Host ++ "." ++ Domain
    end.

names() -> names(localhost()).

% erl_epmd is called by name so that it is linked with net_adm.
names(Host) ->
    case application:get_env(kernel, epmd_module, erl_epmd) of
        erl_epmd -> erl_epmd:names(Host);
        Module -> Module:names(Host)
    end.

dns_hostname(Host) ->
    case inet:gethostbyname(Host) of
        {ok, {hostent, Name, _, _, _, _}} -> {ok, Name};
        _ -> {error, Host}
    end.

%% Pings until a node answers, then collects the other nodes that come up within 3 seconds (plan 30 step DIST7d).
ping_list(Nodes) ->
    ok = net_kernel:monitor_nodes(true),
    collect_new(ping_first(Nodes, nodes()), Nodes).

ping_first([], _) ->
    [];
ping_first([Node | Nodes], Connected) ->
    case lists:member(Node, Connected) of
        true ->
            [Node | ping_first(Nodes, Connected)];
        false ->
            case ping(Node) of
                pong -> [Node];
                pang -> ping_first(Nodes, Connected)
            end
    end.

collect_new(Found, Nodes) ->
    receive
        {nodeup, Node} ->
            case lists:member(Node, Nodes) of
                true -> collect_new(Found, Nodes);
                false -> collect_new([Node | Found], Nodes)
            end
    after 3000 ->
        ok = net_kernel:monitor_nodes(false),
        Found
    end.

world() -> world(silent).

world(Verbose) ->
    case net_adm:host_file() of
        {error, Reason} -> exit({error, Reason});
        Hosts -> expand_hosts(Hosts, Verbose)
    end.

world_list(Hosts) when is_list(Hosts) -> expand_hosts(Hosts, silent).

world_list(Hosts, Verbose) when is_list(Hosts) -> expand_hosts(Hosts, Verbose).

%% The nodes of the hosts that answer a ping, host by host in the order epmd names them (plan 30 step DIST7d).
expand_hosts(Hosts, Verbose) -> lists:flatten([host_nodes(Host, Verbose) || Host <- Hosts]).

host_nodes(Host, Verbose) ->
    case names(Host) of
        {ok, Names} -> ping_names(Names, atom_to_list(Host), Verbose);
        _ -> []
    end.

ping_names(Names, Host, Verbose) ->
    case longshort(Host) of
        ignored -> [];
        Short -> [Node || {Name, _} <- Names, Node <- ping_name(Name, Short, Verbose)]
    end.

ping_name(Name, Host, Verbose) ->
    Node = list_to_atom(Name ++ "@" ++ longshort(Host)),
    verbose(Verbose, "Pinging ~w -> ", [Node]),
    Result = ping(Node),
    verbose(Verbose, "~p\n", [Result]),
    [Node || Result =:= pong].

verbose(verbose, Format, Args) -> io:format(Format, Args);
verbose(_, _, _) -> ok.

%% The host as the node names it: up to the first dot with short names; ignored when not alive.
longshort(Host) ->
    case net_kernel:longnames() of
        false -> uptodot(Host);
        true -> Host;
        ignored -> ignored
    end.

uptodot([$. | _]) -> [];
uptodot([]) -> [];
uptodot([C | Rest]) -> [C | uptodot(Rest)].
