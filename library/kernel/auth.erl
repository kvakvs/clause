%% Clause's auth (plan 21 step LIB25, docs/libraries/library.md): OTP's auth server of a distributed node, started under
%% erl_distribution's net_sup (plan 30 step DIST4b1c, docs/distribution.md#cookies). It keeps this node's cookie, from
%% --setcookie, --nocookie or the .erlang.cookie of the home or user configuration directory (created when missing),
%% and cookies set for other nodes. Without the server, as on a node without distribution, there is no cookie and
%% setting one raises distribution_not_started. The server's requests and cookie checks are OTP's (DIST6).
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

%% A node is authorized when it answers a ping (plan 30 step DIST4b3b1).
is_auth(Node) ->
    case net_adm:ping(Node) of
        pong -> yes;
        pang -> no
    end.

cookie() -> get_cookie().

cookie([Cookie]) -> set_cookie(Cookie);
cookie(Cookie) -> set_cookie(Cookie).

node_cookie([Node, Cookie]) -> node_cookie(Node, Cookie).

node_cookie(Node, Cookie) ->
    set_cookie(Node, Cookie),
    is_auth(Node).

% This node's cookie; nocookie without the server.
get_cookie() -> request(get_our_cookie, nocookie).

% The cookie set for Node, else this node's.
get_cookie(Node) when Node =:= node() -> get_cookie();
get_cookie(Node) -> request({get_cookie, Node}, nocookie).

set_cookie(Cookie) -> request({set_our_cookie, Cookie}, fun() -> erlang:error(distribution_not_started) end).

% Set the cookie of Node, or this node's (the one of every node without a cookie of its own).
set_cookie(Node, Cookie) when Node =:= node() -> set_cookie(Cookie);
set_cookie(Node, Cookie) -> request({set_cookie, Node, Cookie}, fun() -> erlang:error(distribution_not_started) end).

% A call to the server; Without is the answer, or the fun raising it, when no server runs.
request(Request, Without) ->
    case whereis(auth) of
        undefined when is_function(Without) -> Without();
        undefined -> Without;
        Pid -> gen_server:call(Pid, Request, infinity)
    end.

% A cookie set for this node's name at the start (--setcookie Node Cookie) becomes this node's cookie.
sync_cookie() -> gen_server:call(auth, sync_cookie, infinity).

% An error report on Node's auth server (DIST6).
print(Node, Format, Arguments) -> gen_server:cast({auth, Node}, {print, Format, Arguments}).

start_link() -> gen_server:start_link({local, auth}, ?MODULE, [], []).

% The server starts with this node's cookie; a cookie file it cannot use stops it with OTP's log line.
init([]) ->
    process_flag(trap_exit, true),
    case initial_cookie() of
        {ok, Cookie, Nodes} ->
            {ok, #state{cookie = Cookie, nodes = Nodes}};
        {error, Text} ->
            error_logger:error_msg("~ts~n", [Text]),
            {stop, {error, Text}}
    end.

%% The cookies at the start (plan 30 step DIST6), as OTP's: --setcookie Cookie once gives this node's cookie and
%% --setcookie Node Cookie another node's (a node given twice keeps none); without exactly one cookie of this node,
%% --nocookie, else the cookie file.
initial_cookie() ->
    case init:get_argument(setcookie) of
        {ok, Arguments} -> given_cookies(Arguments, [], #{});
        error -> unset_cookie(#{})
    end.

given_cookies([[] = Cookie | Arguments], Own, Nodes) ->
    given_cookies(Arguments, [Cookie | Own], Nodes);
given_cookies([[_] = Cookie | Arguments], Own, Nodes) ->
    given_cookies(Arguments, [Cookie | Own], Nodes);
given_cookies([[Node, Cookie] | Arguments], Own, Nodes) ->
    given_cookies(Arguments, Own, given_node(Node, Cookie, Nodes));
given_cookies([_ | Arguments], Own, Nodes) ->
    given_cookies(Arguments, Own, Nodes);
given_cookies([], Own, Nodes) ->
    Given = maps:from_list([{list_to_atom(Node), list_to_atom(Cookie)} || Node := Cookie <- Nodes, Cookie =/= undefined]),
    case Own of
        [[Cookie]] -> {ok, list_to_atom(Cookie), Given};
        _ -> unset_cookie(Given)
    end.

given_node(Node, _, Nodes) when is_map_key(Node, Nodes) -> Nodes#{Node := undefined};
given_node(Node, Cookie, Nodes) -> Nodes#{Node => Cookie}.

unset_cookie(Nodes) ->
    case init:get_argument(nocookie) of
        {ok, _} -> {ok, nocookie, Nodes};
        error -> file_cookie(Nodes)
    end.

% The cookie of .erlang.cookie in the home directory, else in the user configuration directory; created in the home
% directory when neither exists, in the configuration directory without a home.
file_cookie(Nodes) ->
    Config = filename:join(filename:basedir(user_config, "erlang"), ".erlang.cookie"),
    Result =
        case init:get_argument(home) of
            {ok, [[Home]]} -> home_cookie(filename:join(Home, ".erlang.cookie"), Config);
            _ -> config_cookie(Config)
        end,
    case Result of
        {ok, Cookie} -> {ok, list_to_atom(Cookie), Nodes};
        Error -> Error
    end.

home_cookie(Home, Config) ->
    case read_cookie(Home) of
        {error, enoent} ->
            case read_cookie(Config) of
                {error, enoent} -> created_cookie(Home);
                Result -> Result
            end;
        Result ->
            Result
    end.

config_cookie(Config) ->
    case read_cookie(Config) of
        {error, enoent} -> created_cookie(Config);
        {ok, _} = Result -> Result;
        _ -> {error, "No home for cookie file"}
    end.

created_cookie(File) ->
    case create_cookie(File) of
        ok -> read_cookie(File);
        Error -> Error
    end.

% A cookie file others may read is refused on Unix, as OTP refuses it; Windows has no such modes.
read_cookie(File) ->
    case file:read_file_info(File) of
        {ok, #file_info{type = regular, mode = Mode}} -> owned_cookie(File, Mode, os:type());
        {ok, #file_info{type = Type}} -> {error, "Cookie file " ++ File ++ " is of type " ++ atom_to_list(Type)};
        {error, enoent} -> {error, enoent};
        {error, Reason} -> read_error(File, Reason)
    end.

owned_cookie(File, Mode, {unix, _}) when Mode band 8#077 =/= 0 ->
    {error, "Cookie file " ++ File ++ " must be accessible by owner only"};
owned_cookie(File, _, _) ->
    case file:read_file(File) of
        {ok, Text} -> check_cookie(binary_to_list(Text), []);
        {error, Reason} -> read_error(File, Reason)
    end.

read_error(File, Reason) -> {error, "Error when reading " ++ File ++ ": " ++ atom_to_list(Reason)}.

% The cookie is the printable characters up to the first other one; only line ends and blanks may follow, as OTP
% checks it.
check_cookie([Char | Rest], Cookie) when Char >= $\s, Char =< $~ -> check_cookie(Rest, [Char | Cookie]);
check_cookie(Rest, Cookie) -> check_rest(Rest, Cookie).

check_rest([Char | Rest], Cookie) when Char =:= $\n; Char =:= $\r; Char =:= $\s -> check_rest(Rest, Cookie);
check_rest([_ | _], _) -> {error, "Bad characters in cookie"};
check_rest([], []) -> {error, "Too short cookie string"};
check_rest([], Cookie) -> {ok, lists:reverse(Cookie)}.

create_cookie(File) ->
    Cookie = [$A + rand:uniform(26) - 1 || _ <- lists:seq(1, ?COOKIE_LENGTH)],
    case file:write_file(File, Cookie) of
        ok ->
            case file:change_mode(File, 8#400) of
                ok -> ok;
                {error, Reason} -> {error, "Failed to change mode: " ++ atom_to_list(Reason)}
            end;
        {error, Reason} ->
            {error, lists:flatten(io_lib:format("Failed to create cookie file '~ts': ~p", [File, Reason]))}
    end.

handle_call(get_our_cookie, _From, #state{cookie = Cookie} = State) ->
    {reply, Cookie, State};
handle_call({get_cookie, Node}, _From, #state{cookie = Cookie, nodes = Nodes} = State) ->
    {reply, maps:get(Node, Nodes, Cookie), State};
handle_call({set_our_cookie, Cookie}, _From, State) ->
    {reply, true, State#state{cookie = Cookie}};
handle_call({set_cookie, Node, Cookie}, _From, #state{nodes = Nodes} = State) ->
    {reply, true, State#state{nodes = Nodes#{Node => Cookie}}};
handle_call(sync_cookie, _From, #state{nodes = Nodes} = State) ->
    case maps:take(node(), Nodes) of
        {Cookie, Rest} -> {reply, true, State#state{cookie = Cookie, nodes = Rest}};
        error -> {reply, true, State}
    end;
handle_call(echo, _From, State) ->
    {reply, hello, State}.

handle_cast({print, Format, Arguments}, State) ->
    error_logger:error_msg(Format, Arguments),
    {noreply, State}.

% Anything else is ignored, exit signals too. OTP's answers to badcookie messages are left out: no runtime sends them.
handle_info(_, State) -> {noreply, State}.

terminate(_, _) -> ok.

code_change(_, State, _) -> {ok, State}.
