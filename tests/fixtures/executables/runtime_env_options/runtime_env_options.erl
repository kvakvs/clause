%% The flags of the node start options and --env as init reports them, and the --env values applications see over
%% their .app resources (plan 30 step DIST4a1). OTP is given the same options as emulator flags.
-module(runtime_env_options).
-export([main/1]).

-define(FLAGS, [kernel, stdlib, envapp, setcookie, nocookie, hidden, proto_dist, dist_listen, start_epmd, sname, name]).

main(_) ->
    KernelEnv = [application:get_env(kernel, Key) || Key <- [net_ticktime, logger_level, extra]],
    [io:format("~p: ~p~n", [Flag, init:get_argument(Flag)]) || Flag <- ?FLAGS],
    io:format("kernel_env: ~p~n", [KernelEnv]),
    io:format("stdlib_env: ~p~n", [application:get_all_env(stdlib)]),
    io:format("load: ~p~n", [application:load(envapp)]),
    io:format("envapp_env: ~p~n", [lists:sort(application:get_all_env(envapp))]),
    io:format("runtime_env_options: ok~n").
