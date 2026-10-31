%% Distribution help for the runtime's builtins (plan 30 step DIST2c, docs/distribution.md#node-monitors):
%% monitor_node/3 and erlang:dmonitor_node/3 continue here for a passive connect. start_epmd/0 and start_daemon/1 are
%% runtime builtins (plan 30 steps DIST3b and DIST3d, docs/distribution.md#epmd) that replace their bodies at
%% registration; start_external_epmd/0 finds the epmd program of --epmd external.
-module(clause_dist).
-export([passive_monitor/3, start_epmd/0, start_daemon/1, start_external_epmd/0]).

%% What a registration that found no epmd does under --epmd: ok once the bundled epmd (or another that won its port)
%% listens, none under --epmd none or without the bundled epmd, error when the bundled epmd cannot listen, external
%% under --epmd external.
start_epmd() -> clause_builtin_placeholder.

%% Start the epmd program at the UTF-8 path Program with -daemon: ok when it started, else error.
start_daemon(_Program) -> clause_builtin_placeholder.

%% Start the epmd program of --epmd external: clause-epmd beside the program, else on PATH, else OTP's epmd on PATH;
%% error when none is found or it did not start.
start_external_epmd() ->
    case epmd_program() of
        false -> error;
        % A remote call: the builtin replaces the exported function, not local calls.
        Program -> clause_dist:start_daemon(unicode:characters_to_binary(Program))
    end.

epmd_program() ->
    case os:find_executable("clause-epmd", filename:dirname(init:script_name())) of
        false -> program_on_path(["clause-epmd", "epmd"]);
        Program -> Program
    end.

program_on_path([]) ->
    false;
program_on_path([Name | Names]) ->
    case os:find_executable(Name) of
        false -> program_on_path(Names);
        Program -> Program
    end.

%% Ask net_kernel whether a connection to Node is up: monitor it again when it is, else {nodedown, Node} at once.
passive_monitor(Node, Flag, Options) ->
    case net_kernel:passive_cnct(Node) of
        true ->
            erlang:monitor_node(Node, Flag, Options);
        false ->
            self() ! {nodedown, Node},
            true
    end.
