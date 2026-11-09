%% OTP's inets services API (plan 21 step MIM4, docs/libraries/inets.md#inets): the application started and stopped,
%% and httpc profiles started as services under it or stand-alone. The httpd, ftp and tftp services are not provided.
-module(inets).
-export([
    service_names/0,
    services/0,
    services_info/0,
    start/0,
    start/1,
    start/2,
    start/3,
    stop/0,
    stop/2
]).
%% OTP 29 exports declined: stubs raising error({not_implemented, F, A})
%% (docs/libraries/library.md#runtime-module-replacements).
-export([
    disable_trace/0,
    enable_trace/2,
    enable_trace/3,
    print_version_info/0,
    print_version_info/1,
    report_event/4,
    set_trace/1,
    versions/0
]).

start() ->
    application:start(inets).

start(Type) ->
    application:start(inets, Type).

stop() ->
    application:stop(inets).

start(Service, Config) ->
    start(Service, Config, inets).

% A service under the inets application, or stand-alone; the service module starts it, as in OTP.
start(Service, Config, inets) ->
    % The literal funs link httpc into programs that start its profiles only through inets.
    _ = {fun httpc:start_service/1, fun httpc:start_standalone/1, fun httpc:stop_service/1},
    Service:start_service(Config);
start(Service, Config, stand_alone) ->
    Service:start_standalone(Config).

% Stop a service: a stand-alone one by its process, others through their module.
stop(stand_alone, Pid) ->
    exit(Pid, shutdown),
    ok;
stop(Service, Instance) ->
    Service:stop_service(Instance).

service_names() ->
    [httpc, httpd].

services() ->
    httpc:services().

services_info() ->
    [{Service, Pid, info(httpc:service_info(Pid))} || {Service, Pid} <- services()].

info({ok, Info}) -> Info;
info({error, Reason}) -> {error, Reason}.

disable_trace() -> erlang:error({not_implemented, disable_trace, 0}).
enable_trace(_, _) -> erlang:error({not_implemented, enable_trace, 2}).
enable_trace(_, _, _) -> erlang:error({not_implemented, enable_trace, 3}).
print_version_info() -> erlang:error({not_implemented, print_version_info, 0}).
print_version_info(_) -> erlang:error({not_implemented, print_version_info, 1}).
report_event(_, _, _, _) -> erlang:error({not_implemented, report_event, 4}).
set_trace(_) -> erlang:error({not_implemented, set_trace, 1}).
versions() -> erlang:error({not_implemented, versions, 0}).
