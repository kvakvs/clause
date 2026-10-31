%% Clause's net_adm (plan 21 step LIB25, docs/libraries/library.md): pings ask the other node's net_kernel (plan 30 step
%% DIST4b3b1); worlds are empty; names ask epmd through the kernel's epmd_module (plan 30 step DIST3a); .hosts.erlang is
%% read from the current directory, then the home directory.
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

localhost() ->
    {ok, Host} = inet:gethostname(),
    Host.

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

host_file() ->
    Home =
        case os:getenv("HOME") of
            false -> [];
            Dir -> [Dir]
        end,
    path_consult(["." | Home], ".hosts.erlang").

%% The terms of the first file found, as file:path_consult/2 reads them.
path_consult([Dir | Dirs], Name) ->
    case file:read_file(filename:join(Dir, Name)) of
        {ok, Bin} -> terms(binary_to_list(Bin));
        {error, enoent} -> path_consult(Dirs, Name);
        Error -> Error
    end;
path_consult([], _) ->
    {error, enoent}.

terms(eof) ->
    [];
terms(Text) ->
    case erl_scan:tokens([], Text, 1) of
        {done, {ok, Tokens, _}, Rest} ->
            case erl_parse:parse_term(Tokens) of
                {ok, Term} ->
                    case terms(Rest) of
                        Terms when is_list(Terms) -> [Term | Terms];
                        Error -> Error
                    end;
                Error ->
                    Error
            end;
        {more, _} ->
            [];
        {done, Error, _} ->
            Error
    end.

ping_list(Nodes) -> [Node || Node <- Nodes, ping(Node) =:= pong].

world() -> world(silent).

world(Verbose) ->
    case host_file() of
        {error, Reason} -> exit({error, Reason});
        Hosts -> world_list(Hosts, Verbose)
    end.

world_list(Hosts) when is_list(Hosts) -> world_list(Hosts, silent).

%% No host has an epmd that answers, so no node is found.
world_list(Hosts, _) when is_list(Hosts) -> [].
