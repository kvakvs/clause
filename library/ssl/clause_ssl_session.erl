%% Session caches of ssl (plan 21 step LIB81D2a, docs/libraries/ssl.md#session-reuse): the TLS 1.2 sessions saved for
%% reuse, as OTP's ssl keeps them. The client cache (registered as ssl_manager, as OTP's) keys sessions by
%% {{Host, Port}, SessionId}; each listening socket has a server cache keyed by SessionId, and upgraded server sockets
%% share ssl_unknown_listener's. Each cache is a process over the ssl_session_cache_api module the client_session_cb
%% and server_session_cb environment names. A session is a map: session_id, data (libssl's DER), cipher_suite (code),
%% own_certificate, peer_certificate, sni, is_resumable, time_stamp and internal_id.
-module(clause_ssl_session).
-behaviour(gen_server).
-export([start_link/0, start_unknown_listener/0, start_listener/0, stop_listener/1, unknown_listener/0, lifetime/0]).
-export([client_sessions/1, client_lookup/1, register_client/3, reusable/2, register_server/2]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2, terminate/2]).

-define(DEFAULT_LIFETIME, 86400).
-define(DEFAULT_MAX, 1000).

%% The client cache under ssl_sup.
start_link() ->
    gen_server:start_link({local, ssl_manager}, ?MODULE, {client, undefined}, []).

%% The server cache of upgraded sockets under ssl_sup.
start_unknown_listener() ->
    gen_server:start_link({local, ssl_unknown_listener}, ?MODULE, {server, undefined}, []).

%% A listening socket's server cache, which ends with the caller or stop_listener/1.
start_listener() ->
    {ok, Pid} = gen_server:start(?MODULE, {server, self()}, []),
    Pid.

stop_listener(undefined) -> ok;
stop_listener(Cache) -> gen_server:cast(Cache, stop).

%% The server cache of upgraded sockets, started on first use; undefined when ssl is not started.
unknown_listener() ->
    case whereis(ssl_unknown_listener) of
        undefined ->
            Spec = #{id => ssl_unknown_listener, start => {?MODULE, start_unknown_listener, []}, modules => [?MODULE]},
            case catch supervisor:start_child(ssl_sup, Spec) of
                {ok, Pid} -> Pid;
                {error, {already_started, Pid}} -> Pid;
                _ -> undefined
            end;
        Pid ->
            Pid
    end.

%% The lifetime of sessions in seconds, from the session_lifetime environment.
lifetime() -> env(session_lifetime, fun is_integer/1, ?DEFAULT_LIFETIME).

%% The valid sessions of the client cache to {Host, Port}.
client_sessions(Server) -> call(ssl_manager, {select, Server}, []).

%% The client cache's session of a {{Host, Port}, SessionId} key, else undefined.
client_lookup(Key) -> call(ssl_manager, {lookup, Key}, undefined).

%% Save a client's new session: save at once (reuse_sessions save), or true unless an equivalent one is cached.
register_client(Key, Session, save) -> call(ssl_manager, {register, Key, Session}, ok);
register_client(Key, Session, true) -> gen_server:cast(ssl_manager, {register_unique, Key, Session}).

%% A server cache's valid session of an id, else not_reusable.
reusable(undefined, _SessionId) -> not_reusable;
reusable(Cache, SessionId) -> call(Cache, {reusable, SessionId}, not_reusable).

%% Save a server's new session.
register_server(undefined, _Session) -> ok;
register_server(Cache, #{session_id := Id} = Session) -> gen_server:cast(Cache, {register, Id, Session}).

call(Cache, Request, Default) ->
    try
        gen_server:call(Cache, Request, infinity)
    catch
        exit:_ -> Default
    end.

%%--------------------------------------------------------------------------------------------------------------------
%% The cache process
%%--------------------------------------------------------------------------------------------------------------------

%% The state: role, store (the callback module), cache (its reference), lifetime, max (entries kept) and order (the
%% keys by internal id, oldest first).
init({Role, Owner}) ->
    Owner =/= undefined andalso erlang:monitor(process, Owner),
    {Store, Args} = store(Role),
    {ok, #{
        role => Role,
        store => Store,
        cache => Store:init([{role, Role} | Args]),
        lifetime => lifetime(),
        max => env(max_key(Role), fun is_integer/1, ?DEFAULT_MAX),
        order => gb_trees:empty()
    }}.

handle_call({select, Server}, _From, #{store := Store, cache := Cache} = S) ->
    {reply, [Session || Session <- Store:select_session(Cache, Server), valid(Session, S)], S};
handle_call({lookup, Key}, _From, #{store := Store, cache := Cache} = S) ->
    {reply, Store:lookup(Cache, Key), S};
handle_call({register, Key, Session}, _From, S) ->
    {reply, ok, register_session(Key, Session, S)};
handle_call({reusable, Id}, _From, #{store := Store, cache := Cache} = S) ->
    case Store:lookup(Cache, Id) of
        undefined ->
            {reply, not_reusable, S};
        Session ->
            case valid(Session, S) of
                true -> {reply, Session, S};
                false -> {reply, not_reusable, forget(Id, Session, S)}
            end
    end.

handle_cast({register_unique, {Server, _} = Key, Session}, #{store := Store, cache := Cache} = S) ->
    case lists:any(fun(Cached) -> equivalent(Cached, Session) end, Store:select_session(Cache, Server)) of
        true -> {noreply, S};
        false -> {noreply, register_session(Key, Session, S)}
    end;
handle_cast({register, Key, Session}, S) ->
    {noreply, register_session(Key, Session, S)};
handle_cast(stop, S) ->
    {stop, normal, S}.

handle_info({'DOWN', _, process, _, _}, #{role := server} = S) ->
    {stop, normal, S};
handle_info(_Message, S) ->
    {noreply, S}.

terminate(_Reason, #{store := Store, cache := Cache}) ->
    Store:terminate(Cache).

% The store of a role: OTP's default modules are Clause's clause_ssl_session_db, others get their init arguments.
store(client) -> store(client_session_cb, ssl_client_session_cache_db, client_session_cb_init_args);
store(server) -> store(server_session_cb, ssl_server_session_cache_db, server_session_cb_init_args).

store(Key, Default, ArgsKey) ->
    case env(Key, fun is_atom/1, Default) of
        Default -> {clause_ssl_session_db:name(), []};
        Store -> {Store, env(ArgsKey, fun is_list/1, env(session_cb_init_args, fun is_list/1, []))}
    end.

max_key(client) -> session_cache_client_max;
max_key(server) -> session_cache_server_max.

env(Key, Valid, Default) ->
    case application:get_env(ssl, Key) of
        {ok, Value} ->
            case Valid(Value) of
                true -> Value;
                false -> Default
            end;
        undefined ->
            Default
    end.

valid(#{time_stamp := Stamp}, #{lifetime := Lifetime}) -> now_seconds() - Stamp < Lifetime;
valid(_, _) -> false.

% Sessions that differ in nothing a new connection would choose by, as OTP's client keeps only one of them.
equivalent(#{peer_certificate := Peer, own_certificate := Own, cipher_suite := Suite}, Session) ->
    maps:with([peer_certificate, own_certificate, cipher_suite], Session) ==
        #{peer_certificate => Peer, own_certificate => Own, cipher_suite => Suite};
equivalent(_, _) ->
    false.

% Save a session under Key, replacing the key's old entry; a full cache drops its oldest session first.
register_session(Key, Session0, #{store := Store, cache := Cache, max := Max, order := Order0} = S) ->
    Stamp = now_seconds(),
    InternalId = {Stamp, erlang:unique_integer([monotonic])},
    Session = Session0#{time_stamp => Stamp, internal_id => InternalId},
    Order1 =
        case Store:lookup(Cache, Key) of
            #{internal_id := Old} -> gb_trees:delete_any(Old, Order0);
            _ -> Order0
        end,
    Order2 =
        case Store:size(Cache) >= Max andalso not gb_trees:is_empty(Order1) of
            true ->
                {_, OldKey, Rest} = gb_trees:take_smallest(Order1),
                Store:delete(Cache, OldKey),
                Rest;
            false ->
                Order1
        end,
    Store:update(Cache, Key, Session),
    S#{order => gb_trees:insert(InternalId, Key, Order2)}.

forget(Key, Session, #{store := Store, cache := Cache, order := Order} = S) ->
    Store:delete(Cache, Key),
    S#{order => gb_trees:delete_any(maps:get(internal_id, Session, undefined), Order)}.

now_seconds() -> erlang:monotonic_time(second).
