%% Clause's ssl_crl_cache (plan 21 step LIB81D4a, docs/libraries/ssl.md#crl-checks): ssl's internal CRL cache, the
%% default crl_cache of connections. CRLs inserted with a distribution point URI are found by that URI, all inserted CRLs
%% by their issuer; clause_ssl_crls keeps them. Clause has no inets, so CRLs are never fetched over HTTP.
-module(ssl_crl_cache).
-behaviour(ssl_crl_cache_api).
-export([insert/1, insert/2, delete/1]).
-export([lookup/3, select/2, fresh_crl/2]).
-export([is_internal_ip/1, validate_host/4, is_allowed/3]).
-export_type([crl_src/0]).

-include_lib("public_key/include/public_key.hrl").

-type crl_src() :: {file, file:filename()} | {der, [public_key:der_encoded()]}.

%% The URI CRLs inserted without a distribution point are kept under, as OTP's.
-define(NO_DIST_POINT, "http://dummy/no_distribution_point").

%% Insert CRLs, found later by their issuer.
insert(CRLSrc) -> insert(?NO_DIST_POINT, CRLSrc).

%% Insert CRLs found by the http distribution point URI and by their issuer.
insert(URI, {file, File}) when is_list(URI) ->
    case file:read_file(File) of
        {ok, Pem} -> do_insert(URI, pem_crls(Pem));
        Error -> Error
    end;
insert(URI, {der, CRLs}) ->
    do_insert(URI, CRLs).

%% Delete CRLs inserted by issuer ({file, File} or {der, CRLs}), or those of a distribution point URI.
delete({file, File}) ->
    case file:read_file(File) of
        {ok, Pem} -> clause_ssl_crls:delete_issued(pem_crls(Pem));
        Error -> Error
    end;
delete({der, CRLs}) ->
    clause_ssl_crls:delete_issued(CRLs);
delete(URI) ->
    case key(URI) of
        {ok, Key} -> clause_ssl_crls:delete(Key);
        Error -> Error
    end.

do_insert(URI, CRLs) ->
    case key(URI) of
        {ok, Key} -> clause_ssl_crls:insert(Key, CRLs);
        Error -> Error
    end.

% The cache key of an http URI: host, port and path.
key(URI) ->
    case uri_string:normalize(URI, [return_map]) of
        #{scheme := "http", host := Host, path := Path} = Map ->
            {ok, Host ++ ":" ++ integer_to_list(maps:get(port, Map, 80)) ++ Path};
        _ ->
            {error, {only_http_distribution_points_supported, URI}}
    end.

pem_crls(Pem) -> [CRL || {'CertificateList', CRL, not_encrypted} <- public_key:pem_decode(Pem)].

%%--------------------------------------------------------------------------------------------------------------------
%% ssl_crl_cache_api callbacks
%%--------------------------------------------------------------------------------------------------------------------

%% The CRLs of a distribution point's http URIs, else not_available.
lookup(#'DistributionPoint'{distributionPoint = {fullName, Names}}, _Issuer, {Tables, _Args}) ->
    crls(Names, Tables);
lookup(_DistributionPoint, _Issuer, _DbInfo) ->
    not_available.

crls([], _Tables) ->
    not_available;
crls([{uniformResourceIdentifier, "http" ++ _ = URI} | Rest], Tables) ->
    case key(URI) of
        {ok, Key} ->
            case clause_ssl_crls:lookup(Key, Tables) of
                [] -> crls(Rest, Tables);
                CRLs -> CRLs
            end;
        _ ->
            crls(Rest, Tables)
    end;
crls([_ | Rest], Tables) ->
    crls(Rest, Tables).

%% The CRLs of an issuer, or of the directory names of a list of general names.
select(Names, DbInfo) when is_list(Names) ->
    lists:flatmap(
        fun
            ({directoryName, Issuer}) -> select(Issuer, DbInfo);
            (_) -> []
        end,
        Names
    );
select(Issuer, {Tables, _Args}) ->
    clause_ssl_crls:issued(Issuer, Tables).

%% A CRL as it is: Clause fetches no newer one.
fresh_crl(#'DistributionPoint'{}, CRL) -> CRL.

%% OTP's checks of the hosts its CRL fetches may reach, which Clause does not make.
is_internal_ip(_) -> erlang:error({not_implemented, is_internal_ip, 1}).
validate_host(_, _, _, _) -> erlang:error({not_implemented, validate_host, 4}).
is_allowed(_, _, _) -> erlang:error({not_implemented, is_allowed, 3}).
