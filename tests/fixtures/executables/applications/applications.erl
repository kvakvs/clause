%% application and application_controller (plan 21 step LIB24): loading from a .app file and from terms, keys and
%% environments, starts with dependencies, start phases, masters as group leaders, stops, permissions, restart
%% types, the errors of each call, and with the argument "permanent" a permanent application whose end halts the
%% program.
-module(applications).
-export([main/1]).

main(["permanent"]) ->
    register(applications, self()),
    ok = application:load({application, perm, [{mod, {tapp, {transient_exit, boom}}}]}),
    show("permanent", application:start(perm, permanent)),
    receive
    after 1000 -> io:format("not halted~n")
    end;
main(_) ->
    register(applications, self()),
    virtual(),
    loading(),
    environment(),
    starting(),
    in_application(),
    stopping(),
    dependencies(),
    failures(),
    permissions(),
    transient(),
    errors(),
    io:format("applications: ok~n").

show(Label, Value) -> io:format("~s: ~p~n", [Label, Value]).

attempt(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

%% What tapp's callbacks reported, in order.
reports() ->
    receive
        {tapp, What} -> [What | reports()]
    after 100 -> []
    end.

keys(Name) ->
    {ok, Keys} = application:get_all_key(Name),
    [{Key, Value} || {Key, Value} <- Keys, Key =/= modules, Key =/= registered, Key =/= env].

virtual() ->
    show("which", lists:sort(application:which_applications())),
    show("loaded", lists:sort(application:loaded_applications())),
    show("kernel_keys", keys(kernel)),
    show("stdlib_keys", keys(stdlib)),
    show("kernel_env", [application:get_env(kernel, logger_level), application:get_env(kernel, nothing)]),
    show("module_owner", [application:get_application(lists), application:get_application(no_such_module)]),
    show("no_application", [application:get_application(), application:get_env(color), application:start_type()]),
    show("restart_kernel", [application:start(kernel), application:ensure_started(stdlib)]).

loading() ->
    show("load", application:load(tapp)),
    show("load_again", application:load(tapp)),
    show("tapp_keys", keys(tapp)),
    show("modules", application:get_key(tapp, modules)),
    show("missing_key", [application:get_key(tapp, nothing), application:get_key(nobody, vsn)]),
    show("load_term", application:load({application, plain, [{description, "Plain"}, {vsn, "1.0"}, {env, [{a, 1}]}]})),
    show("plain_keys", keys(plain)),
    show("load_errors", [
        application:load(nosuch),
        application:load({application, "bad", []}),
        application:load({application, bad, not_a_list}),
        application:load({application, bad, [{mod, nonsense}]}),
        application:load({application, bad, [{env, [{a, 1}, {a, 2}]}]}),
        application:load({application, bad, [{env, [not_a_pair]}]}),
        application:load(not_an_application)
    ]),
    show("unload", [application:unload(plain), application:unload(plain), application:get_key(plain, vsn)]),
    show("loaded_after", lists:sort([Name || {Name, _, _} <- application:loaded_applications()])).

environment() ->
    show("get_env", [
        application:get_env(tapp, color), application:get_env(tapp, size, 0), application:get_env(tapp, x, 0)
    ]),
    show("all_env", lists:sort(application:get_all_env(tapp))),
    show("set_env", [application:set_env(tapp, color, red), application:get_env(tapp, color)]),
    show("unset_env", [application:unset_env(tapp, size), application:get_env(tapp, size)]),
    show("set_config", application:set_env([{tapp, [{color, green}, {extra, 1}]}, {other, [{k, v}]}])),
    show("after_config", [
        application:get_env(tapp, color), application:get_env(tapp, extra), application:get_env(other, k)
    ]),
    show("timeouts", [
        application:set_env(tapp, t, 1, 1000),
        application:set_env(tapp, t, 2, infinity),
        application:unset_env(tapp, t, 1000),
        application:unset_env(tapp, t, infinity)
    ]),
    show("persistent", [
        application:load({application, keep, [{env, [{a, spec}, {b, spec}]}]}),
        application:set_env(keep, a, persisted, [{persistent, true}]),
        application:unload(keep),
        application:load({application, keep, [{env, [{a, spec}, {b, spec}]}]}),
        lists:sort(application:get_all_env(keep))
    ]),
    show("config_errors", [
        attempt(fun() -> application:set_env([{tapp, [{a, 1}]}, {tapp, []}]) end),
        attempt(fun() -> application:set_env([{"tapp", []}]) end),
        attempt(fun() -> application:set_env([{tapp, x}]) end),
        attempt(fun() -> application:set_env([{tapp, [{a, 1}, {a, 2}]}]) end),
        attempt(fun() -> application:set_env([{tapp, [{"a", 1}]}]) end),
        attempt(fun() -> application:set_env([{tapp, [a]}]) end),
        attempt(fun() -> application:set_env([oops]) end)
    ]),
    ok = application:set_env(tapp, color, blue).

starting() ->
    show("start", application:start(tapp)),
    show("callbacks", reports()),
    show("running", lists:member(tapp, [Name || {Name, _, _} <- application:which_applications()])),
    show("start_again", application:start(tapp)),
    show("ensure_started", application:ensure_started(tapp)),
    {ok, Top} = application:get_supervisor(tapp),
    show("supervisor", [
        Top =:= whereis(tapp_sup), application:get_supervisor(plain), application:get_supervisor(nobody)
    ]),
    show("info_keys", [Key || {Key, _} <- application:info()]).

in_application() ->
    tapp_worker ! {ask, self()},
    receive
        {answer, Answer} -> show("worker_view", Answer)
    end,
    Worker = whereis(tapp_worker),
    show("worker_application", application:get_application(Worker)),
    show("module_application", application:get_application(tapp_sup)).

stopping() ->
    show("stop", application:stop(tapp)),
    show("stop_callbacks", reports()),
    show("after_stop", [whereis(tapp_sup), whereis(tapp_worker), application:get_supervisor(tapp)]),
    show("stop_again", application:stop(tapp)),
    show("unload_running", [application:start(tapp), application:unload(tapp), application:stop(tapp)]),
    _ = reports(),
    show("unloaded", application:unload(tapp)).

dependencies() ->
    ok = application:load({application, base, [{vsn, "1"}]}),
    ok = application:load({application, middle, [{applications, [kernel, base]}]}),
    ok = application:load({application, top, [{applications, [middle]}, {optional_applications, [absent]}]}),
    ok = application:load({application, wants_absent, [{applications, [absent]}, {optional_applications, [absent]}]}),
    show("not_started", application:start(top)),
    show("all_started", application:ensure_all_started(top)),
    show("all_again", application:ensure_all_started([top, base])),
    show("optional", application:ensure_all_started(wants_absent, temporary, concurrent)),
    ok = application:load({application, needs_missing, [{applications, [missing_app]}]}),
    show("missing_dependency", application:ensure_all_started(needs_missing)),
    show("stop_all", [application:stop(Name) || Name <- [wants_absent, top, middle, base]]).

failures() ->
    Starts = [
        {fail_error, fail},
        {fail_odd, odd},
        {fail_crash, crash}
    ],
    Results = [
        begin
            ok = application:load({application, Name, [{mod, {tapp, Args}}]}),
            {Name, application:start(Name)}
        end
     || {Name, Args} <- Starts
    ],
    show("start_failures", [{Name, strip(Result)} || {Name, Result} <- Results]),
    ok = application:load({application, failed_all, [{applications, [fail_error]}]}),
    show("all_failed", strip(application:ensure_all_started(failed_all))),
    show("still_loaded", [application:get_key(fail_error, mod), application:start(fail_error, bad_type)]).

%% Crash reasons without their stacks.
strip({error, {bad_return, {Call, {'EXIT', {Reason, Stack}}}}}) when is_list(Stack) ->
    {error, {bad_return, {Call, {'EXIT', {Reason, stack}}}}};
strip(Other) ->
    Other.

permissions() ->
    ok = application:load({application, gated, [{mod, {tapp, normal_start}}, {env, [{color, gate}]}]}),
    show("permit_loaded", application:permit(gated, false)),
    show("held_start", [application:start(gated), whereis(tapp_sup)]),
    show("permit", [application:permit(gated, true), is_pid(whereis(tapp_sup))]),
    show("unpermit", [application:permit(gated, false), whereis(tapp_sup)]),
    show("permit_errors", [application:permit(nobody, true), attempt(fun() -> application:permit(gated, perhaps) end)]),
    _ = reports(),
    ok = application:permit(gated, true),
    _ = application:stop(gated),
    _ = reports().

transient() ->
    ok = application:load({application, quits, [{mod, {tapp, {transient_exit, normal}}}]}),
    show("transient_start", application:start(quits, transient)),
    receive
    after 300 -> ok
    end,
    show("transient_exited", lists:member(quits, [Name || {Name, _, _} <- application:which_applications()])),
    ok = application:load({application, temp, [{mod, {tapp, {transient_exit, oops}}}]}),
    show("temporary_start", application:start(temp)),
    receive
    after 300 -> ok
    end,
    show("temporary_exited", lists:member(temp, [Name || {Name, _, _} <- application:which_applications()])).

errors() ->
    show("errors", [
        application:start(nosuch),
        application:stop(nosuch),
        application:unload(nosuch),
        application:start(base, bad),
        application:unload(kernel),
        attempt(fun() -> application:which_applications(-1) end),
        attempt(fun() -> application:get_application("x") end),
        attempt(fun() -> application:load({application, d, []}, [{d, [node()]}]) end),
        application:get_key(d, vsn)
    ]).
