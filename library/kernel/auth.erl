%% Clause's auth (plan 21 step LIB25, docs/libraries/library.md): OTP's auth server of a distributed node, started under
%% erl_distribution's net_sup (plan 30 step DIST4b1c, docs/distribution.md#cookies). It keeps this node's cookie, from
%% --setcookie, --nocookie or the home directory's .erlang.cookie (created when missing), and cookies set for other
%% nodes. Without the server, as on a node without distribution, there is no cookie and setting one raises
%% distribution_not_started.
-module(auth).
-behaviour(gen_server).
-export([
    cookie/0,
    cookie/1,
    get_cookie/0,
    get_cookie/1,
    is_auth/1,
    node_cookie/1,
    node_cookie/2,
    print/3,
    set_cookie/1,
    set_cookie/2,
    sync_cookie/0,
    start_link/0,
    init/1,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    terminate/2,
    code_change/3
]).

-include_lib("kernel/include/file.hrl").

% The length of a created cookie, of letters A to Z.
-define(COOKIE_LENGTH, 20).

% This node's cookie, and the cookies set for other nodes by name.
-record(state, {cookie, nodes = #{}}).

%% Authorization is checked by connecting, which comes with the handshake (DIST4b2).
is_auth(_) -> no.

cookie() -> get_cookie().

cookie([Cookie]) -> set_cookie(Cookie);
cookie(Cookie) -> set_cookie(Cookie).

node_cookie([Node, Cookie]) -> node_cookie(Node, Cookie).

node_cookie(Node, Cookie) ->
    set_cookie(Node, Cookie),
    is_auth(Node).

get_cookie() -> get_cookie(node()).

% The cookie set for Node, else this node's; nocookie without the server.
get_cookie(Node) ->
    case whereis(auth) of
        undefined -> nocookie;
        _ -> gen_server:call(auth, {get_cookie, Node}, infinity)
    end.

set_cookie(Cookie) -> set_cookie(node(), Cookie).

% Set the cookie of Node, or this node's (the one of every node without a cookie of its own).
set_cookie(Node, Cookie) when is_atom(Node), is_atom(Cookie) ->
    case whereis(auth) of
        undefined -> erlang:error(distribution_not_started);
        _ -> gen_server:call(auth, {set_cookie, Node, Cookie}, infinity)
    end.

sync_cookie() -> gen_server:call(auth, sync_cookie, infinity).

print(_, _, _) -> ok.

start_link() -> gen_server:start_link({local, auth}, ?MODULE, [], []).

% The server starts with this node's cookie; a cookie file it cannot use stops it with OTP's log line.
init([]) ->
    process_flag(trap_exit, true),
    case initial_cookie() of
        {ok, Cookie} ->
            {ok, #state{cookie = Cookie}};
        {error, Text} ->
            error_logger:error_msg("~ts~n", [Text]),
            {stop, {error, Text}}
    end.

% --nocookie, else --setcookie, else the cookie file.
initial_cookie() ->
    case {init:get_argument(nocookie), init:get_argument(setcookie)} of
        {{ok, _}, _} -> {ok, nocookie};
        {_, {ok, [[Cookie | _] | _]}} -> {ok, list_to_atom(Cookie)};
        _ -> file_cookie()
    end.

% The cookie of .erlang.cookie in the home directory, created with a random cookie readable by its owner alone.
file_cookie() ->
    case init:get_argument(home) of
        {ok, [[Home | _] | _]} -> file_cookie(filename:join(Home, ".erlang.cookie"));
        _ -> {error, "No home for cookie file"}
    end.

file_cookie(File) ->
    case file:read_file_info(File) of
        {ok, #file_info{mode = Mode}} -> read_cookie(File, Mode);
        {error, enoent} -> create_cookie(File);
        {error, Reason} -> {error, io_lib:format("Error when reading cookie file ~ts: ~p", [File, Reason])}
    end.

% A cookie file others may read is refused on Unix, as OTP refuses it; Windows has no such modes.
read_cookie(File, Mode) ->
    case os:type() of
        {unix, _} when Mode band 8#077 =/= 0 ->
            {error, io_lib:format("Cookie file ~ts must be accessible by owner only", [File])};
        _ ->
            % The first line, without its end and trailing blanks; leading blanks stay, as OTP keeps them.
            {ok, Text} = file:read_file(File),
            [Line | _] = binary:split(Text, <<"\n">>),
            {ok, binary_to_atom(string:trim(Line, trailing, " \t\r"))}
    end.

create_cookie(File) ->
    Cookie = [$A + rand:uniform(26) - 1 || _ <- lists:seq(1, ?COOKIE_LENGTH)],
    case file:write_file(File, Cookie) of
        ok ->
            _ = file:change_mode(File, 8#400),
            {ok, list_to_atom(Cookie)};
        {error, Reason} ->
            {error, io_lib:format("Failed to create cookie file '~ts': ~p", [File, Reason])}
    end.

handle_call({get_cookie, Node}, _From, #state{cookie = Cookie, nodes = Nodes} = State) ->
    {reply, maps:get(Node, Nodes, Cookie), State};
handle_call({set_cookie, Node, Cookie}, _From, State) when Node =:= node() ->
    {reply, true, State#state{cookie = Cookie}};
handle_call({set_cookie, Node, Cookie}, _From, #state{nodes = Nodes} = State) ->
    {reply, true, State#state{nodes = Nodes#{Node => Cookie}}};
handle_call(sync_cookie, _From, State) ->
    {reply, true, State}.

handle_cast(_, State) -> {noreply, State}.

handle_info(_, State) -> {noreply, State}.

terminate(_, _) -> ok.

code_change(_, State, _) -> {ok, State}.
