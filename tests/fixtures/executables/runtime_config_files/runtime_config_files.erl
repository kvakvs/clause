%% Application environments from -config files (sys.config naming other files), merged over the .app resource and
%% under -App Key Value as OTP merges them, and OTP's boot failures for bad files (plan 30 step DIST4a4).
-module(runtime_config_files).
-export([main/1]).

main(_) ->
    % The first use of application reads the files, as OTP's boot does.
    Kernel = application:get_env(kernel, net_ticktime),
    % One flag per file on Windows, as OTP's erlexec gives them, one flag with every file elsewhere.
    {ok, Configs} = init:get_argument(config),
    io:format("config: ~p~n", [lists:append(Configs)]),
    io:format("kernel: ~p~n", [Kernel]),
    io:format("load: ~p~n", [application:load(cfgapp)]),
    io:format("cfgapp: ~p~n", [lists:sort(application:get_all_env(cfgapp))]),
    io:format("runtime_config_files: ok~n").
