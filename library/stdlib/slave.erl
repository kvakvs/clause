%% Clause's slave (plan 21, docs/libraries/library.md#runtime-module-replacements; deprecated in OTP): a Clause
%% program is a node without distribution, so starting a slave node exits with not_alive as on a non-distributed OTP
%% node. stop/1, pseudo/1,2 and relay/1 work as OTP's; the slave side of the protocol is declined.
-module(slave).
-export([
    pseudo/1,
    pseudo/2,
    relay/1,
    slave_start/1,
    start/1,
    start/2,
    start/3,
    start/5,
    start_link/1,
    start_link/2,
    start_link/3,
    stop/1,
    wait_for_master_to_die/2,
    wait_for_slave/7
]).

pseudo([Master | Servers]) -> pseudo(Master, Servers);
pseudo(_) -> error_logger:error_msg("No master node given to slave:pseudo/1~n", []).
% Register a relay to the master's server under each server name not registered here.
pseudo(_, []) ->
    ok;
pseudo(Master, [Server | Servers]) ->
    case whereis(Server) of
        undefined -> register(Server, spawn(slave, relay, [rpc:call(Master, erlang, whereis, [Server])]));
        _ -> ok
    end,
    pseudo(Master, Servers).
% Pass every message on to Pid, the master's server.
relay({badrpc, Reason}) ->
    error_logger:error_msg(" ** exiting relay server ~w :~tw  **~n", [self(), Reason]),
    exit(Reason);
relay(undefined) ->
    error_logger:error_msg(" ** exiting relay server ~w  **~n", [self()]),
    exit(undefined);
relay(Pid) when is_pid(Pid) ->
    receive
        Message -> Pid ! Message
    end,
    relay(Pid).
slave_start(_) -> erlang:error({not_implemented, slave_start, 1}).
start(Host) -> start(Host, own_name(), [], no_link, erl).
start(Host, Name) -> start(Host, Name, []).
start(Host, Name, Args) -> start(Host, Name, Args, no_link, erl).
% Starting a node needs distribution, which a Clause node never runs.
start(_Host, _Name, _Args, _LinkTo, _Prog) -> exit(not_alive).

own_name() -> hd(string:split(atom_to_list(node()), "@")).
start_link(Host) -> start(Host, own_name(), [], self(), erl).
start_link(Host, Name) -> start_link(Host, Name, []).
start_link(Host, Name, Args) -> start(Host, Name, Args, self(), erl).
stop(Node) ->
    rpc:call(Node, erlang, halt, []),
    ok.
wait_for_master_to_die(_, _) -> erlang:error({not_implemented, wait_for_master_to_die, 2}).
wait_for_slave(_, _, _, _, _, _, _) -> erlang:error({not_implemented, wait_for_slave, 7}).
