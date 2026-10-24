%% Clause's net_adm (plan 21 step LIB25, docs/library.md): the answers of an OTP node that is not alive. Pings are
%% pang, epmd is never reached ({error, address}), so worlds are empty; .hosts.erlang is read from the current
%% directory, then the home directory.
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

ping(Node) when is_atom(Node) -> pang.

localhost() ->
    {ok, Host} = inet:gethostname(),
    Host.

names() -> names(localhost()).

%% Clause has no epmd client: epmd is not operational for it.
names(_) -> {error, address}.

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
