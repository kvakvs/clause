%% clause-epmd beside programs (plan 30 step DIST3c): built with --epmd-executable, the program asks the epmd program
%% that CLAUSE_TEST_EPMD names (clause-epmd beside it; OTP's epmd under the oracle). -names with and without a
%% registered name, -kill refused while a name is registered and accepted after, -names once nothing listens, and
%% -names with an argument after it. The names run asks the runner's epmd; the daemon run (authored: OTP's Windows
%% epmd does not stay up after -daemon) starts one with -daemon first. Port numbers of epmd print as PORT.
%% Built with --no-bundled-epmd too (plan 30 step DIST3d): with no epmd running, a registration fails as OTP's, and
%% warns once that --epmd bundled acts as none; under --epmd external (authored) it starts the clause-epmd beside the
%% program, which the run kills at the end.
-module(epmd_executable).
-export([main/1]).

-define(WAIT_MS, 30000).

main(["names"]) ->
    questions(),
    io:format("epmd_executable: ok~n");
main(["register"]) ->
    {ok, _} = erl_epmd:start(),
    show("register", erl_epmd:register_node(alpha, 40001)),
    show("register again", erl_epmd:register_node(alpha, 40001)),
    io:format("epmd_executable: ok~n");
main(["external"]) ->
    {ok, _} = erl_epmd:start(),
    {ok, Creation} = erl_epmd:register_node(alpha, 40001),
    show("creation", is_integer(Creation)),
    show("names", erl_epmd:names()),
    ok = erl_epmd:stop(),
    wait_names({ok, []}),
    show("kill", tool(["-kill"])),
    wait_names({error, address}),
    io:format("epmd_executable: ok~n");
main(["daemon"]) ->
    show("daemon", tool(["-daemon"])),
    wait_names({ok, []}),
    questions(),
    io:format("epmd_executable: ok~n").

questions() ->
    show("names", tool(["-names"])),
    {ok, _} = erl_epmd:start(),
    {ok, _} = erl_epmd:register_node(alpha, 40001),
    show("names alpha", tool(["-names"])),
    show("kill refused", tool(["-kill"])),
    ok = erl_epmd:stop(),
    wait_names({ok, []}),
    show("kill", tool(["-kill"])),
    wait_names({error, address}),
    show("names without epmd", tool(["-names"])),
    {Status, _Usage} = tool(["-names", "extra"]),
    show("names extra", Status).

%% Run the epmd program with Args; its exit status and its output, standard error included.
tool(Args) ->
    Port = open_port({spawn_executable, os:getenv("CLAUSE_TEST_EPMD")}, [
        {args, Args}, exit_status, stderr_to_stdout, binary, hide
    ]),
    collect(Port, <<>>).

collect(Port, Data) ->
    receive
        {Port, {data, More}} -> collect(Port, <<Data/binary, More/binary>>);
        {Port, {exit_status, Status}} -> {Status, masked(Data)}
    after ?WAIT_MS -> {timeout, masked(Data)}
    end.

%% Output with LF line ends and the epmd port as PORT.
masked(Data) ->
    Text = binary:replace(Data, <<"\r\n">>, <<"\n">>, [global]),
    binary:replace(Text, list_to_binary(os:getenv("ERL_EPMD_PORT")), <<"PORT">>, [global]).

%% Wait until erl_epmd:names() answers Expected.
wait_names(Expected) ->
    wait_names(Expected, ?WAIT_MS div 50).

wait_names(Expected, 0) ->
    error({names, Expected, erl_epmd:names()});
wait_names(Expected, Attempts) ->
    case erl_epmd:names() of
        Expected ->
            ok;
        _ ->
            timer:sleep(50),
            wait_names(Expected, Attempts - 1)
    end.

show(Label, Value) ->
    io:format("~s: ~p~n", [Label, Value]).
