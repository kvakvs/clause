%% OTP's HTTP client (plan 21 step MIM4, docs/libraries/inets.md#httpc): requests over gen_tcp and ssl through a
%% profile's manager (clause_httpc_manager) and its connection handlers (clause_httpc_handler), with OTP's request
%% bytes, results, errors, options and profiles. Cookies, sessions' internals and streaming once are not provided.
-module(httpc).
-export([
    cancel_request/1,
    cancel_request/2,
    default_profile/0,
    get_option/1,
    get_option/2,
    get_options/1,
    get_options/2,
    profile_name/1,
    profile_name/2,
    request/1,
    request/2,
    request/4,
    request/5,
    service_info/1,
    services/0,
    set_option/2,
    set_option/3,
    set_options/1,
    set_options/2,
    ssl_verify_host_options/1,
    start_service/1,
    start_standalone/1,
    stop_service/1
]).
%% OTP 29 exports declined: stubs raising error({not_implemented, F, A})
%% (docs/libraries/library.md#runtime-module-replacements).
-export([
    cookie_header/1,
    cookie_header/2,
    cookie_header/3,
    info/0,
    info/1,
    reset_cookies/0,
    reset_cookies/1,
    store_cookies/2,
    store_cookies/3,
    stream_next/1,
    which_cookies/0,
    which_cookies/1,
    which_sessions/0,
    which_sessions/1
]).

-define(OPTIONS, [
    proxy,
    https_proxy,
    pipeline_timeout,
    max_pipeline_length,
    max_keep_alive_length,
    keep_alive_timeout,
    max_sessions,
    cookies,
    verbose,
    ipfamily,
    ip,
    port,
    socket_opts,
    unix_socket,
    max_connections_open
]).

default_profile() ->
    default.

profile_name(Profile) ->
    clause_httpc_manager:name(Profile).

profile_name(Prefix, Profile) ->
    list_to_atom(Prefix ++ atom_to_list(Profile)).

%% --- Requests ---

request(Url) ->
    request(Url, default_profile()).

request(Url, Profile) ->
    request(get, {Url, []}, [], [], Profile).

request(Method, Request, HttpOptions, Options) ->
    request(Method, Request, HttpOptions, Options, default_profile()).

request(Method, Request, HttpOptions, Options, Profile) when is_atom(Profile); is_pid(Profile) ->
    case clause_httpc_request:new(Method, Request, HttpOptions, Options) of
        {ok, #{sync := Sync} = Built} ->
            {ok, Id} = gen_server:call(manager(Profile), {request, Built}, infinity),
            answer(Sync, Id);
        {error, _} = Error ->
            Error
    end.

% A synchronous request's result arrives as an asynchronous one's message would.
answer(false, Id) ->
    {ok, Id};
answer(true, Id) ->
    receive
        {http, {Id, {error, _} = Error}} -> Error;
        {http, {Id, Result}} -> {ok, Result}
    end.

cancel_request(Id) ->
    cancel_request(Id, default_profile()).

cancel_request(Id, Profile) ->
    gen_server:call(manager(Profile), {cancel_request, Id}, infinity).

manager(Pid) when is_pid(Pid) -> Pid;
manager(Profile) -> profile_name(Profile).

%% --- Options ---

set_option(Name, Value) ->
    set_options([{Name, Value}]).

set_option(Name, Value, Profile) ->
    set_options([{Name, Value}], Profile).

set_options(Options) ->
    set_options(Options, default_profile()).

set_options(Options, Profile) ->
    case call(Profile, get_options) of
        {error, inets_not_started} = Error ->
            Error;
        Current ->
            case clause_httpc_options:profile_options(Options, Current) of
                ok -> gen_server:cast(manager(Profile), {set_options, Options});
                Error -> Error
            end
    end.

get_option(Name) ->
    get_option(Name, default_profile()).

get_option(Name, Profile) ->
    case get_options([Name], Profile) of
        {ok, [{_, Value}]} -> {ok, Value};
        Error -> Error
    end.

get_options(Names) ->
    get_options(Names, default_profile()).

get_options(all, Profile) ->
    get_options(?OPTIONS, Profile);
get_options(Names, Profile) ->
    case [Name || Name <- Names, not lists:member(Name, ?OPTIONS)] of
        [] -> wrap(call(Profile, {get_options, Names}));
        Invalid -> {error, {invalid_options, Invalid}}
    end.

wrap({error, _} = Error) -> Error;
wrap(Options) -> {ok, Options}.

% A call to Profile's manager; a profile not running answers as OTP's when inets is not started.
call(Profile, Request) ->
    try
        gen_server:call(manager(Profile), Request, infinity)
    catch
        exit:{noproc, _} -> {error, inets_not_started}
    end.

% The TLS options httpc uses by default: the peer verified against the system's certificates, and its host name
% checked as HTTPS does when asked.
ssl_verify_host_options(Check) ->
    Base = [{verify, verify_peer}, {cacerts, public_key:cacerts_get()}],
    case Check of
        true -> Base ++ [{customize_hostname_check, [{match_fun, public_key:pkix_verify_hostname_match_fun(https)}]}];
        false -> Base;
        _ -> erlang:error({case_clause, Check})
    end.

%% --- Profiles as inets services ---

% A profile's manager under httpc_profile_sup (inets:start(httpc, Config)).
start_service(Config) ->
    case proplists:get_value(profile, Config) of
        undefined -> {error, no_profile};
        Profile -> started(supervisor:start_child(httpc_profile_sup, clause_httpc_manager:child_spec(Profile)))
    end.

started({error, {already_started, Pid}}) -> {error, {already_started, Pid}};
started({error, {{already_started, Pid}, _}}) -> {error, {already_started, Pid}};
started(Result) -> Result.

% A profile's manager outside inets (inets:start(httpc, Config, stand_alone)).
start_standalone(Config) ->
    case proplists:get_value(profile, Config) of
        undefined -> {error, no_profile};
        Profile -> clause_httpc_manager:start_link(Profile, only_session_cookies, stand_alone)
    end.

% Stop a profile's manager, named by profile or process.
stop_service(Pid) when is_pid(Pid) ->
    case [Id || {Id, P, _, _} <- supervisor:which_children(httpc_profile_sup), P =:= Pid] of
        [Id] -> stop_child(Id);
        [] -> {error, not_found}
    end;
stop_service(Profile) ->
    stop_child(maps:get(id, clause_httpc_manager:child_spec(Profile))).

stop_child(Id) ->
    case supervisor:terminate_child(httpc_profile_sup, Id) of
        ok -> supervisor:delete_child(httpc_profile_sup, Id);
        Error -> Error
    end.

% The running profiles' managers, as inets lists its services.
services() ->
    [{httpc, Pid} || {_, Pid, _, _} <- supervisor:which_children(httpc_profile_sup), is_pid(Pid)].

service_info(Pid) ->
    case lists:keymember(Pid, 2, services()) of
        true -> {ok, [{profile, gen_server:call(Pid, profile, infinity)}]};
        false -> {error, no_such_service}
    end.

%% --- Declined ---

cookie_header(_) -> erlang:error({not_implemented, cookie_header, 1}).
cookie_header(_, _) -> erlang:error({not_implemented, cookie_header, 2}).
cookie_header(_, _, _) -> erlang:error({not_implemented, cookie_header, 3}).
info() -> erlang:error({not_implemented, info, 0}).
info(_) -> erlang:error({not_implemented, info, 1}).
reset_cookies() -> erlang:error({not_implemented, reset_cookies, 0}).
reset_cookies(_) -> erlang:error({not_implemented, reset_cookies, 1}).
store_cookies(_, _) -> erlang:error({not_implemented, store_cookies, 2}).
store_cookies(_, _, _) -> erlang:error({not_implemented, store_cookies, 3}).
stream_next(_) -> erlang:error({not_implemented, stream_next, 1}).
which_cookies() -> erlang:error({not_implemented, which_cookies, 0}).
which_cookies(_) -> erlang:error({not_implemented, which_cookies, 1}).
which_sessions() -> erlang:error({not_implemented, which_sessions, 0}).
which_sessions(_) -> erlang:error({not_implemented, which_sessions, 1}).
