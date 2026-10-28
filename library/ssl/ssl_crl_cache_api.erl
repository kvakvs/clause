%% Clause's ssl_crl_cache_api (plan 21 step LIB81D4a, docs/libraries/ssl.md#crl-checks): the behaviour of crl_cache
%% modules, callbacks only.
-module(ssl_crl_cache_api).
-export_type([dist_point/0, crl_cache_ref/0, logger_info/0]).

-include_lib("public_key/include/public_key.hrl").

-type crl_cache_ref() :: any().
-type dist_point() :: #'DistributionPoint'{}.
-type logger_info() :: {logger:level(), Report :: #{description => string(), reason => term()}, logger:metadata()}.

-callback lookup(DistPoint :: dist_point(), CacheRef :: crl_cache_ref()) ->
    not_available | [public_key:der_encoded()] | {logger, logger_info(), [public_key:der_encoded()]}.
-callback lookup(DistPoint :: dist_point(), Issuer :: public_key:issuer_name(), CacheRef :: crl_cache_ref()) ->
    not_available | [public_key:der_encoded()] | {logger, logger_info(), [public_key:der_encoded()]}.
-callback select(IssuerOrDPLocations, CacheRef) -> [CRL] | {logger, logger_info(), [CRL]} when
    CRL :: public_key:der_encoded(),
    IssuerOrDPLocations :: public_key:issuer_name() | list(),
    CacheRef :: crl_cache_ref().
-callback fresh_crl(DistPoint :: dist_point(), CRL :: public_key:der_encoded()) ->
    public_key:der_encoded() | {logger, logger_info(), public_key:der_encoded()}.
-optional_callbacks([lookup/2]).
