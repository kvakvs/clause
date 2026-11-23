%% Node mode (plan 30 step DIST10a, docs/executables.md#node-mode): node a starts copies of this program as erl nodes,
%% Clause's in node mode and OTP's erl under the oracle. A node of its own runs -s and -run start instructions and
%% stops; one that fails a start instruction halts; a detached distributed one answers a and stops when asked.
-module(dist_node_mode).
-export([main/1, start/0, hello/1, words/1, stop_node/0, serve/1]).

main(["a"]) ->
    {Program, Pre} = node_program(),
    Words = ["-noshell", "-s", "dist_node_mode", "-s", "dist_node_mode", "hello", "x", "y"],
    More = ["-run", "dist_node_mode", "words", "p", "q", "-s", "dist_node_mode", "stop_node", "-extra", "e1"],
    {Status, Output} = run(Program, Pre ++ Words ++ More),
    io:format("node: ~p~n~ts", [Status, Output]),
    {Failed, Failure} = run(Program, Pre ++ ["-noshell", "-s", "nosuchmod"]),
    io:format("failed node: ~p ~p~n", [Failed, string:trim(hd(string:split(Failure, "\n")), trailing, "\r")]),
    detached(Program, Pre),
    io:format("dist_node_mode: ok~n").

% The program that starts this module as a node: this executable in node mode under Clause, OTP's erl with this
% module compiled beside its source under OTP.
node_program() ->
    case erlang:system_info(machine) of
        "Clause" ->
            {init:script_name(), ["--node"]};
        _ ->
            Source = code:which(?MODULE),
            Compile = list_to_atom("compile"),
            {ok, ?MODULE} = Compile:file(Source, [{outdir, filename:dirname(Source)}]),
            Erl = os:find_executable("erl", filename:join(code:root_dir(), "bin")),
            {Erl, ["-pa", filename:dirname(Source)]}
    end.

% Run a node to its end without this node's flags; its exit status and output.
run(Program, Arguments) ->
    Environment = [{"CLAUSE_FLAGS", false}, {"ERL_FLAGS", false}, {"ERL_CRASH_DUMP_SECONDS", "0"}],
    Options = [{args, Arguments}, {env, Environment}, binary, exit_status, stderr_to_stdout, hide],
    collect(open_port({spawn_executable, Program}, Options), <<>>).

collect(Port, Output) ->
    receive
        {Port, {data, Data}} -> collect(Port, <<Output/binary, Data/binary>>);
        {Port, {exit_status, Status}} -> {Status, unicode:characters_to_list(Output)}
    end.

% A detached node b: its starter returns at once, b reports to a, answers calls and stops on init:stop/0.
detached(Program, Pre) ->
    register(nm_waiter, self()),
    Names = ["-sname", "nmb@localhost", "-setcookie", "nm", "-kernel", "inet_dist_use_interface", "{127,0,0,1}"],
    {Status, Output} = run(Program, Pre ++ ["-detached" | Names] ++ ["-s", "dist_node_mode", "serve", "nma@localhost"]),
    io:format("detached starter: ~p ~p~n", [Status, Output]),
    B =
        receive
            {up, Node} -> Node
        after 60000 -> exit(no_detached_node)
        end,
    io:format("up: ~p~n", [B]),
    Flags = [erpc:call(B, init, get_argument, [Flag]) || Flag <- [detached, noshell, noinput, s]],
    io:format("flags: ~p~n", [Flags]),
    io:format("plain: ~p~n", [erpc:call(B, init, get_plain_arguments, [])]),
    true = erlang:monitor_node(B, true),
    ok = erpc:cast(B, init, stop, []),
    receive
        {nodedown, B} -> io:format("b down~n")
    end.

% Start instructions of the node a runs on its own.
start() -> io:format("start~n").

hello(Arguments) -> io:format("hello: ~p~n", [Arguments]).

words(Words) ->
    io:format("words: ~p~n", [Words]),
    io:format("plain: ~p~n", [init:get_plain_arguments()]),
    io:format("flags: ~p~n", [[init:get_argument(Flag) || Flag <- [noshell, s, run, extra]]]).

stop_node() ->
    io:format("stopping~n"),
    init:stop().

% Node b: tell a it is up, and halt should a go first.
serve([Origin]) ->
    spawn(fun() ->
        true = erlang:monitor_node(Origin, true),
        {nm_waiter, Origin} ! {up, node()},
        receive
            {nodedown, Origin} -> erlang:halt()
        end
    end),
    ok.
