%% Clause's erl_epmd (plan 30 step DIST3a, docs/distribution.md#epmd): OTP's epmd client over the epmd protocol. The
%% erl_epmd server holds this node's registration on the connection to the host's epmd, and the client functions ask
%% an epmd on their own connections. The epmd port is ERL_EPMD_PORT's, else 4369. A registration that finds no epmd
%% starts the bundled one under --epmd bundled (plan 30 step DIST3b).
-module(erl_epmd).
-behaviour(gen_server).
-export([
    address_please/3,
    code_change/3,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    init/1,
    listen_port_please/2,
    names/0,
    names/1,
    open/0,
    open/1,
    open/2,
    port_please/2,
    port_please/3,
    register_node/2,
    register_node/3,
    start/0,
    start_link/0,
    stop/0,
    terminate/2
]).

-define(EPMD_PORT, 4369).
% Request codes and reply tags of the epmd protocol.
-define(ALIVE2_REQ, 120).
-define(ALIVE2_X_RESP, 118).
-define(ALIVE2_RESP, 121).
-define(PORT_PLEASE2_REQ, 122).
-define(PORT2_RESP, 119).
-define(NAMES_REQ, 110).
% The distribution protocol versions this node speaks, as OTP 29 registers them.
-define(HIGHEST_VERSION, 6).
-define(LOWEST_VERSION, 5).
% The version a fixed listen port answers with (epmd's dist low).
-define(DIST_VERSION, 5).
% How long a broken registration waits before it registers again.
-define(RECONNECT_MS, 1000).
% How often, and how far apart, a registration connects again after starting an epmd that may still be starting.
-define(EPMD_ATTEMPTS, 20).
-define(EPMD_RETRY_MS, 50).

% The registration this node holds: the epmd connection, and the name, port and family it registered.
-record(state, {socket, name, port, family}).

start() -> gen_server:start({local, erl_epmd}, ?MODULE, [], []).

start_link() -> gen_server:start_link({local, erl_epmd}, ?MODULE, [], []).

stop() -> gen_server:call(erl_epmd, stop, infinity).

port_please(Node, Host) -> port_please(Node, Host, infinity).

% A node's distribution port: the configured listen port, else the host's epmd's answer.
port_please(Node, Host, Timeout) ->
    case listen_port_please(Node, Host) of
        {ok, 0} -> ask_port(to_string(Node), Host, Timeout);
        {ok, Port} -> {port, Port, ?DIST_VERSION}
    end.

% The listen port of kernel's erl_epmd_node_listen_port, 0 when unset.
listen_port_please(_Name, _Host) ->
    case application:get_env(kernel, erl_epmd_node_listen_port) of
        {ok, Port} when is_integer(Port), Port >= 0 -> {ok, Port};
        {ok, Invalid} -> erlang:error({invalid_parameter_value, erl_epmd_node_listen_port, Invalid});
        undefined -> {ok, 0}
    end.

names() ->
    {ok, Host} = inet:gethostname(),
    names(Host).

% The names an epmd registered, oldest first; {error, address} when no epmd answers.
names(Host) ->
    case inet:getaddr(Host, family(Host)) of
        {ok, Address} -> ask_names(Address);
        Error -> Error
    end.

address_please(_Name, Host, AddressFamily) -> inet:getaddr(Host, AddressFamily).

register_node(Name, Port) -> register_node(Name, Port, inet).

register_node(Name, Port, inet_tcp) -> register_node(Name, Port, inet);
register_node(Name, Port, inet6_tcp) -> register_node(Name, Port, inet6);
register_node(Name, Port, Family) -> gen_server:call(erl_epmd, {register, Name, Port, Family}, infinity).

open() -> open({127, 0, 0, 1}).

open(Address) -> open(Address, infinity).

% A connection to the epmd at Address.
open(Address, Timeout) -> gen_tcp:connect(Address, epmd_port(), [family(Address), {packet, 0}], Timeout).

init(_) -> {ok, #state{}}.

handle_call({register, Name, Port, Family}, _From, #state{socket = undefined} = State) ->
    case register_name(Name, Port, Family) of
        {ok, Socket, Creation} ->
            {reply, {ok, Creation}, State#state{socket = Socket, name = Name, port = Port, family = Family}};
        Error ->
            {reply, Error, State}
    end;
handle_call({register, _, _, _}, _From, State) ->
    {reply, {error, already_registered}, State};
% The server ends with shutdown, as OTP's: a linked caller that does not trap exits ends too.
handle_call(stop, _From, State) ->
    {stop, shutdown, ok, State}.

handle_cast(_, State) -> {noreply, State}.

% A lost epmd connection registers again until an epmd answers.
handle_info({tcp_closed, Socket}, #state{socket = Socket} = State) ->
    erlang:send_after(?RECONNECT_MS, self(), reconnect),
    {noreply, State#state{socket = reconnecting}};
handle_info(reconnect, #state{socket = reconnecting, name = Name, port = Port, family = Family} = State) ->
    case register_name(Name, Port, Family) of
        {ok, Socket, _} ->
            {noreply, State#state{socket = Socket}};
        _ ->
            erlang:send_after(?RECONNECT_MS, self(), reconnect),
            {noreply, State}
    end;
handle_info(_, State) ->
    {noreply, State}.

terminate(_, #state{socket = Socket}) when is_port(Socket) -> gen_tcp:close(Socket);
terminate(_, _) -> ok.

code_change(_, State, _) -> {ok, State}.

%% Registration: ALIVE2_REQ on a connection to the local epmd, kept while the name is registered.
register_name(Name, Port, Family) ->
    case connect_local(Family) of
        {ok, Socket} ->
            NameBytes = unicode:characters_to_binary(to_string(Name)),
            Request =
                <<?ALIVE2_REQ, Port:16, $M, 0, ?HIGHEST_VERSION:16, ?LOWEST_VERSION:16, (byte_size(NameBytes)):16,
                    NameBytes/binary, 0:16>>,
            ok = gen_tcp:send(Socket, [<<(byte_size(Request)):16>>, Request]),
            alive_reply(Socket, gen_tcp:recv(Socket, 0));
        Error ->
            Error
    end.

% A connection to the local epmd. When none listens, the --epmd mode may start the bundled epmd, which gives way to
% another that wins its port; the connection is then tried again for a short while.
connect_local(Family) ->
    case connect_epmd(Family) of
        {error, econnrefused} = Refused ->
            case clause_dist:start_epmd() of
                ok -> connect_again(Family, ?EPMD_ATTEMPTS);
                _ -> Refused
            end;
        Result ->
            Result
    end.

connect_again(Family, Attempts) ->
    case connect_epmd(Family) of
        {error, econnrefused} when Attempts > 1 ->
            timer:sleep(?EPMD_RETRY_MS),
            connect_again(Family, Attempts - 1);
        Result ->
            Result
    end.

connect_epmd(Family) -> gen_tcp:connect(loopback(Family), epmd_port(), [Family, binary, {packet, 0}, {active, false}]).

% The creation of a registration; its connection then reports when epmd closes it.
alive_reply(Socket, {ok, <<?ALIVE2_X_RESP, 0, Creation:32>>}) -> registered(Socket, Creation);
alive_reply(Socket, {ok, <<?ALIVE2_RESP, 0, Creation:16>>}) -> registered(Socket, Creation);
alive_reply(Socket, {ok, <<_, _, _/binary>>}) -> refused(Socket, {error, duplicate_name});
alive_reply(Socket, {error, closed}) -> refused(Socket, {error, epmd_close});
alive_reply(Socket, Error) -> refused(Socket, Error).

registered(Socket, Creation) ->
    ok = inet:setopts(Socket, [{active, true}]),
    {ok, Socket, Creation}.

refused(Socket, Error) ->
    gen_tcp:close(Socket),
    Error.

%% Port lookup: PORT_PLEASE2_REQ to the host's epmd; noport when it cannot be asked or does not know the name.
ask_port(Name, Host, Timeout) ->
    case request(Host, [?PORT_PLEASE2_REQ, unicode:characters_to_binary(Name)], Timeout) of
        {ok, <<?PORT2_RESP, 0, Port:16, _Type, _Protocol, Highest:16, _Lowest:16, _/binary>>} ->
            {port, Port, min(Highest, ?HIGHEST_VERSION)};
        {ok, <<?PORT2_RESP, _, _/binary>>} ->
            noport;
        {ok, <<>>} ->
            closed;
        {error, connect} ->
            noport;
        Error ->
            Error
    end.

%% Name listing: NAMES_REQ, answered with the epmd's port and a "name N at port P" line per name, newest first.
ask_names(Address) ->
    case request(Address, [?NAMES_REQ], infinity) of
        {ok, <<_EpmdPort:32, Lines/binary>>} -> {ok, lists:reverse(name_lines(Lines))};
        _ -> {error, address}
    end.

name_lines(Lines) ->
    [
        {unicode:characters_to_list(Name), binary_to_integer(Port)}
     || Line <- binary:split(Lines, <<"\n">>, [global, trim_all]),
        [<<"name ", Name/binary>>, Port] <- [binary:split(Line, <<" at port ">>)]
    ].

% One request on a new connection to the epmd of Host, and everything the epmd sent until it closed.
request(Host, Request, Timeout) ->
    case gen_tcp:connect(Host, epmd_port(), [family(Host), binary, {packet, 0}, {active, false}], Timeout) of
        {ok, Socket} ->
            Body = iolist_to_binary(Request),
            ok = gen_tcp:send(Socket, [<<(byte_size(Body)):16>>, Body]),
            receive_all(Socket, Timeout, []);
        {error, _} ->
            {error, connect}
    end.

receive_all(Socket, Timeout, Acc) ->
    case gen_tcp:recv(Socket, 0, Timeout) of
        {ok, Data} ->
            receive_all(Socket, Timeout, [Acc | Data]);
        {error, closed} ->
            gen_tcp:close(Socket),
            {ok, iolist_to_binary(Acc)};
        Error ->
            gen_tcp:close(Socket),
            Error
    end.

% The epmd port this node uses: ERL_EPMD_PORT, else 4369.
epmd_port() ->
    case os:getenv("ERL_EPMD_PORT") of
        false -> ?EPMD_PORT;
        Text -> list_to_integer(Text)
    end.

loopback(inet6) -> {0, 0, 0, 0, 0, 0, 0, 1};
loopback(_) -> {127, 0, 0, 1}.

family({_, _, _, _, _, _, _, _}) -> inet6;
family(_) -> inet.

to_string(Name) when is_atom(Name) -> atom_to_list(Name);
to_string(Name) when is_list(Name) -> Name.
