%% A release's vm.args read with --args-file (Clause) or -args_file (OTP): init flags, the mapped runtime options,
%% -env and the plain arguments after -extra (plan 30 step DIST4a3a).
-module(runtime_args_files).
-export([main/1]).

-define(FLAGS, [setcookie, hidden, kernel, myflag, nested, proto_dist, sname, dist_listen, start_epmd, heart, eval, s]).

main(Args) ->
    io:format("args: ~p~n", [Args]),
    [io:format("~p: ~p~n", [Flag, init:get_argument(Flag)]) || Flag <- ?FLAGS],
    io:format("env: ~p~n", [os:getenv("CLAUSE_ARGS_VAR")]),
    io:format("kernel_env: ~p~n", [[application:get_env(kernel, Key) || Key <- [net_ticktime, inet_dist_listen_min]]]),
    Info = [atom_limit, schedulers, min_heap_size],
    io:format("system_info: ~p~n", [[erlang:system_info(Item) || Item <- Info]]),
    io:format("runtime_args_files: ok~n").
