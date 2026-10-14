-module(init_module).
-export([main/1]).

%% The init module of a started system without a boot script (plan step 41L).
%% OTP runs the oracle's entry from an escript while init still boots, so the
%% plain arguments start with the escript's own, and loading and status
%% answers that change once OTP has started are checked for shape only. The
%% "stop" runs end the program with OTP's exit status rules; the "exact" run
%% (Clause only) checks the plain arguments themselves.
main(["stop", Status]) ->
    io:format("stopping~n"),
    init:stop(list_to_integer(Status)),
    receive
        never -> ok
    end;
main(["stop_text"]) ->
    io:format("stopping~n"),
    init:stop("bye"),
    receive
        never -> ok
    end;
main(["reboot"]) ->
    io:format("rebooting~n"),
    init:reboot(),
    receive
        never -> ok
    end;
main(["exact" | _] = Args) ->
    io:format("~p~n", [init:get_plain_arguments()]),
    io:format("~w~n", [init:get_plain_arguments() =:= Args]),
    io:format("init_module: ok~n");
main(Args) ->
    io:format("~p~n", [
        [
            lists:suffix(Args, init:get_plain_arguments()),
            init:get_argument(home) =:= {ok, [[home()]]},
            lists:member({home, [home()]}, init:get_arguments()),
            init:get_argument(no_such_flag),
            init:get_argument("home"),
            absolute(init:script_name()),
            element(2, init:get_status()),
            init:notify_when_started(self()),
            lists:member(init:ensure_loaded(lists), [not_allowed, {module, lists}]),
            init:fetch_loaded(),
            init:objfile_extension(),
            init:archive_extension(),
            init:code_path_choice(),
            init:run_on_load_handlers(),
            is_tuple(init:script_id()),
            init:get_configfd(7),
            init:set_configfd(7, config),
            init:get_configfd(7),
            init:make_permanent(x, y),
            outcome(fun() -> init:stop(-1) end),
            outcome(fun() -> init:stop([256]) end),
            outcome(fun() -> init:stop(x) end),
            outcome(fun() -> init:stop(1.0) end),
            outcome(fun() -> init:restart(x) end),
            outcome(fun() -> init:restart([x]) end)
        ]
    ]),
    % A request to OTP's init would take this message as its reply: send it last.
    self() ! {init, started},
    io:format("~w~n", [init:wait_until_started()]),
    io:format("init_module: ok~n").

%% The home directory erlexec gives OTP: HOMEDRIVE and HOMEPATH on Windows,
%% HOME elsewhere.
home() ->
    case os:type() of
        {win32, _} -> os:getenv("HOMEDRIVE") ++ os:getenv("HOMEPATH");
        _ -> os:getenv("HOME")
    end.

%% Whether a path is absolute with forward slashes, as OTP's script names are.
absolute([$/ | _]) -> true;
absolute([_, $:, $/ | _]) -> true;
absolute(_) -> false.

%% The value of Fun(), or its error class and reason.
outcome(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.
