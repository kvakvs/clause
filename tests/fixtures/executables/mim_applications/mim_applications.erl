%% The OTP applications MongooseIM's resource lists (plan 21 step MIM6, docs/libraries/library.md#applications): an
%% application resource that only depends on them starts them in order, with their keys, processes and supervision
%% trees as OTP's; a dependency not started yet is named; sasl's alarm handler keeps, clears and hands over alarms.
-module(mim_applications).
-behaviour(gen_event).
-export([main/1]).
-export([init/1, handle_event/2, handle_call/2, handle_info/2, terminate/2]).

-define(APPS, [sasl, inets, tools, runtime_tools, asn1, eldap, xmerl, compiler, syntax_tools]).
-define(NAMES, [
    sasl_sup,
    sasl_safe_sup,
    alarm_handler,
    release_handler,
    inets_sup,
    httpc_sup,
    httpc_profile_sup,
    httpc_manager,
    httpc_handler_sup,
    httpd_sup,
    runtime_tools_sup,
    asn1_ns,
    asn1db
]).

main(_) ->
    show("eldap_alone", application:start(eldap)),
    show("mimapp_alone", application:start(mimapp)),
    show("ensure_all_started", application:ensure_all_started(mimapp)),
    show("which_applications", application:which_applications()),
    [show(App, keys(App)) || App <- ?APPS],
    show("registered", [Name || Name <- ?NAMES, whereis(Name) =/= undefined]),
    [tree(Sup) || Sup <- [sasl_sup, inets_sup, runtime_tools_sup]],
    alarms(),
    handlers(),
    show("stopped", [application:stop(App) || App <- [mimapp | lists:reverse(?APPS)]]),
    show("registered_after", [Name || Name <- ?NAMES, whereis(Name) =/= undefined]),
    show("alarms_after", catch_exit(fun alarm_handler:get_alarms/0)),
    io:format("mim_applications: ok~n").

keys(App) ->
    [{Key, application:get_key(App, Key)} || Key <- [description, vsn, registered, applications, env]].

%% A registered supervisor's children, its child supervisors' too (all registered): their specifications without the
%% start function.
tree(Sup) ->
    [
        begin
            {ok, Spec} = supervisor:get_childspec(Sup, Id),
            % Sorted: OTP prints a map's atom keys in atom-table order (docs/differences.md).
            show(Sup, {Id, Type, Mods, name(Pid), lists:sort(maps:to_list(maps:without([start], Spec)))}),
            Type =:= supervisor andalso tree(name(Pid))
        end
     || {Id, Pid, Type, Mods} <- supervisor:which_children(Sup)
    ],
    ok.

name(Pid) ->
    case process_info(Pid, registered_name) of
        {registered_name, Name} -> Name;
        _ -> unregistered
    end.

%% Alarms set and cleared: the newest first, a clear removing the newest of its id.
alarms() ->
    show("set", [alarm_handler:set_alarm(A) || A <- [{a, 1}, {a, 2}, {b, x}, {{c, 1}, "text"}]]),
    show("get", alarm_handler:get_alarms()),
    show("clear", [alarm_handler:clear_alarm(a), alarm_handler:clear_alarm(z)]),
    show("set_any", alarm_handler:set_alarm(notatuple)),
    show("get", alarm_handler:get_alarms()),
    show("unknown_call", gen_event:call(alarm_handler, alarm_handler, foo)).

%% Other handlers added beside the default one, deleted, and the default one swapped for this module.
handlers() ->
    show("add", [alarm_handler:add_alarm_handler(?MODULE), alarm_handler:add_alarm_handler(?MODULE, args)]),
    show("handlers", gen_event:which_handlers(alarm_handler)),
    show("set", alarm_handler:set_alarm({d, 1})),
    show("get", alarm_handler:get_alarms()),
    show("delete", [alarm_handler:delete_alarm_handler(?MODULE), alarm_handler:delete_alarm_handler(?MODULE)]),
    show("delete_default", alarm_handler:delete_alarm_handler(alarm_handler)),
    show("get_without", alarm_handler:get_alarms()),
    show("add_default", [alarm_handler:add_alarm_handler(alarm_handler), alarm_handler:set_alarm({e, 1})]),
    show("get", alarm_handler:get_alarms()),
    show("swap", gen_event:swap_handler(alarm_handler, {alarm_handler, swap}, {?MODULE, swapped})),
    show("handlers", gen_event:which_handlers(alarm_handler)),
    show("swap_back", gen_event:swap_handler(alarm_handler, {?MODULE, swap}, {alarm_handler, []})),
    show("get", alarm_handler:get_alarms()).

catch_exit(Fun) ->
    try
        Fun()
    catch
        exit:Reason -> {exit, Reason}
    end.

%% This module as an alarm handler: what it is given and sees.
init(Args) ->
    show("handler_init", Args),
    {ok, Args}.

handle_event(Event, State) ->
    show("handler_event", {Event, State}),
    {ok, State}.

handle_call(Request, State) ->
    {ok, {call, Request}, State}.

handle_info(_, State) ->
    {ok, State}.

terminate(swap, {_, {alarm_handler, Alarms}}) ->
    {alarm_handler, Alarms};
terminate(Reason, State) ->
    show("handler_terminate", {Reason, State}),
    {terminated, State}.

show(Label, Value) -> io:format("~s: ~p~n", [Label, Value]).
