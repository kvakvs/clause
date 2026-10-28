%% The default store of ssl's session caches (plan 21 step LIB81D2a, docs/libraries/ssl.md#session-reuse): an
%% ssl_session_cache_api module over an ETS table owned by the cache process, used for OTP's default
%% ssl_client_session_cache_db and ssl_server_session_cache_db. Client keys are {{Host, Port}, SessionId}, server keys
%% are SessionIds.
-module(clause_ssl_session_db).
-behaviour(ssl_session_cache_api).
-export([name/0]).
-export([init/1, terminate/1, lookup/2, update/3, delete/2, foldl/3, select_session/2, size/1]).

%% This module's name, which caches call so that programs with ssl link it.
name() -> ?MODULE.

init(_Args) -> ets:new(?MODULE, [set, protected]).

terminate(Table) -> ets:delete(Table).

lookup(Table, Key) ->
    case ets:lookup(Table, Key) of
        [{_, Session}] -> Session;
        [] -> undefined
    end.

update(Table, Key, Session) -> ets:insert(Table, {Key, Session}).

delete(Table, Key) -> ets:delete(Table, Key).

foldl(Fun, Acc, Table) -> ets:foldl(fun({_, Session}, A) -> Fun(Session, A) end, Acc, Table).

%% The sessions of a client cache to a {Host, Port}.
select_session(Table, {_, _} = Server) -> ets:select(Table, [{{{Server, '_'}, '$1'}, [], ['$1']}]);
select_session(_Table, _Port) -> [].

size(Table) -> ets:info(Table, size).
