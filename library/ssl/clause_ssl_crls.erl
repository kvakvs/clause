%% The internal CRL store of ssl (plan 21 step LIB81D4a, docs/libraries/ssl.md#crl-checks): ssl_crl_cache's CRLs by
%% cache key (host:port/path of a distribution point) and by issuer, in two ETS tables of this process, which ssl_sup
%% runs. The tables are readable by connections; changes go through the process. An insert past crl_cache_max_size
%% empties the store first, as OTP's.
-module(clause_ssl_crls).
-behaviour(gen_server).
-export([start_link/0, insert/2, delete/1, delete_issued/1, lookup/2, issued/2, tables/0]).
-export([init/1, handle_call/3, handle_cast/2]).

-include_lib("public_key/include/public_key.hrl").

-define(CACHE, clause_ssl_crl_cache).
-define(ISSUERS, clause_ssl_crl_issuers).
-define(DEFAULT_MAX_SIZE, 100000).

start_link() ->
    gen_server:start_link({local, ?MODULE}, ?MODULE, [], []).

%% The tables connections read, as the internal cache's handle.
tables() -> {?CACHE, ?ISSUERS}.

%% Keep CRLs under a cache key and by their issuers.
insert(Key, CRLs) -> call({insert, Key, CRLs}).

%% Forget the CRLs of a cache key.
delete(Key) -> call({delete, Key}).

%% Forget CRLs by their issuers.
delete_issued(CRLs) -> call({delete_issued, CRLs}).

call(Request) ->
    try
        gen_server:call(?MODULE, Request, infinity)
    catch
        exit:{noproc, _} -> {error, {ssl_app, not_started}}
    end.

%% The CRLs of a cache key, else [].
lookup(Key, {Cache, _}) ->
    case ets:lookup(Cache, Key) of
        [{_, CRLs}] -> CRLs;
        [] -> []
    end.

%% The CRLs of an issuer name, else [].
issued(Issuer, {_, Issuers}) -> [CRL || {_, CRL} <- ets:lookup(Issuers, Issuer)].

init([]) ->
    ets:new(?CACHE, [named_table, set, protected]),
    ets:new(?ISSUERS, [named_table, bag, protected]),
    {ok, #{}}.

handle_call({insert, Key, CRLs}, _From, S) ->
    % As OTP's: the new CRLs' bytes and the number of keys kept, against crl_cache_max_size.
    case lists:sum([byte_size(CRL) || CRL <- CRLs]) + ets:info(?CACHE, size) >= max_size() of
        true -> [ets:delete_all_objects(T) || T <- [?CACHE, ?ISSUERS]];
        false -> ok
    end,
    ets:insert(?CACHE, {Key, CRLs}),
    [ets:insert(?ISSUERS, {Issuer, CRL}) || CRL <- CRLs, Issuer <- [issuer(CRL)], Issuer =/= bad_crl],
    {reply, ok, S};
handle_call({delete, Key}, _From, S) ->
    case ets:lookup(?CACHE, Key) of
        [{_, CRLs}] ->
            ets:delete(?CACHE, Key),
            forget(CRLs);
        [] ->
            ok
    end,
    {reply, ok, S};
handle_call({delete_issued, CRLs}, _From, S) ->
    forget(CRLs),
    {reply, ok, S}.

handle_cast(_Request, S) ->
    {noreply, S}.

forget(CRLs) -> [ets:delete_object(?ISSUERS, {Issuer, CRL}) || CRL <- CRLs, Issuer <- [issuer(CRL)]].

% A CRL's issuer as its encoding names it, else bad_crl.
issuer(CRL) ->
    try public_key:der_decode('CertificateList', CRL) of
        #'CertificateList'{tbsCertList = #'TBSCertList'{issuer = Issuer}} -> Issuer
    catch
        _:_ -> bad_crl
    end.

max_size() ->
    case application:get_env(ssl, crl_cache_max_size) of
        {ok, Size} when is_integer(Size) -> Size;
        _ -> ?DEFAULT_MAX_SIZE
    end.
