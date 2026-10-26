-module(clause_inet_config).
-export([config/0, get_rc/0]).

% The resolver configuration of inet_res and inet:get_rc/0 (plan 21 step LIB60B, docs/libraries/library.md): what OTP's
% inet_config loads on a node that is not distributed. Windows has none (lookup [native]); on Linux and macOS the
% resolver reads resolv.conf (nameserver, domain, search) and get_rc/0 also lists the hosts file. Read once, kept in
% a persistent term.

-define(KEY, {?MODULE, config}).

%% #{domain, nameservers, search, resolv_conf, hosts_file, hosts}.
config() ->
    case persistent_term:get(?KEY, undefined) of
        undefined ->
            Config = load(os:type()),
            persistent_term:put(?KEY, Config),
            Config;
        Config ->
            Config
    end.

load({unix, _}) ->
    Etc = os:getenv("ERL_INET_ETC_DIR", "/etc"),
    Resolv = filename:join(Etc, "resolv.conf"),
    Hosts = filename:join(Etc, "hosts"),
    maps:merge(
        #{
            domain => "",
            nameservers => [],
            search => [],
            resolv_conf => Resolv,
            hosts_file => Hosts,
            hosts => hosts(Hosts)
        },
        resolv(Resolv)
    );
load(_) ->
    #{domain => "", nameservers => [], search => [], resolv_conf => undefined, hosts_file => undefined, hosts => []}.

% The nameserver, domain and search lines of a resolv.conf file.
resolv(File) ->
    Lines = [split(Line) || Line <- lines(File)],
    Servers = [{IP, 53} || ["nameserver", Address | _] <- Lines, {ok, IP} <- [inet:parse_address(Address)]],
    Domains = [Domain || ["domain", Domain | _] <- Lines],
    Search = [Names || ["search" | Names] <- Lines],
    maps:from_list(
        [{nameservers, Servers}] ++
            [{domain, lists:last(Domains)} || Domains =/= []] ++
            [{search, lists:last(Search)} || Search =/= []]
    ).

% The {IP, Names} entries of a hosts file.
hosts(File) ->
    [
        {IP, Names}
     || [Address | Names] <- [split(Line) || Line <- lines(File)],
        Names =/= [],
        {ok, IP} <- [inet:parse_address(Address)]
    ].

lines(File) ->
    case file:read_file(File) of
        {ok, Bytes} -> string:split(binary_to_list(Bytes), "\n", all);
        {error, _} -> []
    end.

% The words of a line before a comment.
split(Line) ->
    [Content | _] = string:split(Line, "#"),
    string:lexemes(Content, " \t\r").

%% inet:get_rc/0: OTP's keys in its order, each only when it differs from its default.
get_rc() ->
    #{domain := Domain, nameservers := Servers, search := Search, resolv_conf := Resolv, hosts_file := HostsFile} =
        Config = config(),
    [{host, IP, Names} || {IP, Names} <- maps:get(hosts, Config)] ++
        [{domain, Domain} || Domain =/= ""] ++
        [server(Server) || Server <- Servers] ++
        [{search, Search} || Search =/= []] ++
        [{resolv_conf, Resolv} || Resolv =/= undefined] ++
        [{hosts_file, HostsFile} || HostsFile =/= undefined] ++
        [{lookup, [native]}].

server({IP, 53}) -> {nameservers, IP};
server({IP, Port}) -> {nameservers, IP, Port}.
