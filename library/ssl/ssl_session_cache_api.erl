%% Clause's ssl_session_cache_api (plan 21 step LIB81D2a, docs/libraries/ssl.md#session-reuse): the behaviour of the
%% modules that store ssl's TLS 1.2 session caches, callbacks only.
-module(ssl_session_cache_api).
-export_type([session_cache_key/0, session/0, partial_key/0, session_cache_ref/0]).

-type session_cache_ref() :: any().
-type session_cache_key() :: {partial_key(), ssl:session_id()}.
-opaque session() :: map().
-opaque partial_key() :: {ssl:host(), inet:port_number()} | inet:port_number().

-callback init(InitArgs) -> CacheRef when InitArgs :: list(), CacheRef :: session_cache_ref().
-callback terminate(CacheRef) -> DoNotCare when CacheRef :: session_cache_ref(), DoNotCare :: any().
-callback lookup(CacheRef, Key) -> Session when
    CacheRef :: session_cache_ref(), Key :: session_cache_key(), Session :: session() | undefined.
-callback update(CacheRef, Key, Session) -> DoNotCare when
    CacheRef :: session_cache_ref(), Key :: session_cache_key(), Session :: session() | undefined, DoNotCare :: any().
-callback delete(CacheRef, Key) -> DoNotCare when
    CacheRef :: session_cache_ref(), Key :: session_cache_key(), DoNotCare :: any().
-callback foldl(Fun, Acc0, CacheRef) -> Acc when
    Fun :: fun(), Acc0 :: term(), CacheRef :: session_cache_ref(), Acc :: term().
-callback select_session(CacheRef, Server) -> Sessions when
    CacheRef :: session_cache_ref(),
    Server :: {ssl:host(), inet:port_number()} | inet:port_number(),
    Sessions :: [session()].
-callback size(CacheRef) -> Size when CacheRef :: session_cache_ref(), Size :: non_neg_integer().
