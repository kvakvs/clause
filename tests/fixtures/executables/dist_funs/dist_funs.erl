%% Funs across nodes (plan 30 step DIST5d, docs/distribution.md#funs). In `calls` mode a and b run the same program
%% and call each other's funs: in messages, in exit reasons of links and monitors, and spawned with spawn/2. In
%% `echo` mode one node may run other code (an OTP node), so funs only travel there and back: each node gets its own
%% funs again, equal and callable, and sees the other's funs as funs of their module, arity and environment.
-module(dist_funs).
-export([main/1, twice/1]).

main(["a", Mode]) ->
    B = 'dfb@localhost',
    wait_registered("dfb_ready"),
    show("ping", net_adm:ping(B)),
    echoes(B),
    others(B),
    exits(B),
    Mode =:= "calls" andalso calls(B),
    stop(B),
    done();
main(["b"]) ->
    register(server, self()),
    _Ready = mark("dfb_ready"),
    serve().

twice(X) -> 2 * X.

%% b's funs: a closure over a value of this node, a fun of a named function, and a fun capturing another.
own_funs() ->
    K = length(atom_to_list(node())),
    Add = fun(X) -> X + K end,
    [Add, fun twice/1, fun(X) -> {Add(X), K} end].

%% b's server.
serve() ->
    receive
        {echo, From, Term} ->
            From ! {echo, Term},
            serve();
        {own, From} ->
            From ! {own, own_funs()},
            serve();
        {check, From, Funs} ->
            From ! {checked, Funs =:= own_funs(), [F(4) || F <- Funs]},
            serve();
        {call, From, Fun, Args} ->
            From ! {called, failure(fun() -> apply(Fun, Args) end)},
            serve();
        {exit_with, From, Reason} ->
            {Pid, Ref} = spawn_monitor(fun() ->
                receive
                    go -> exit(Reason)
                end
            end),
            From ! {victim, Pid},
            receive
                {'DOWN', Ref, process, Pid, _} -> ok
            end,
            serve();
        {trap, From} ->
            From ! {trapper, spawn(fun() -> trapper(From) end)},
            serve();
        stop ->
            done()
    end.

%% A process of b that traps exits and tells From the reason of the exit signal it gets.
trapper(From) ->
    process_flag(trap_exit, true),
    From ! trapping,
    receive
        {'EXIT', _, Reason} -> From ! {trapped, Reason}
    end.

%% a's funs go to b and come back equal and callable, also inside other terms.
echoes(B) ->
    Funs = local_funs(),
    {server, B} ! {echo, self(), {Funs, #{key => Funs}, [hd(Funs)]}},
    {Back, #{key := Again}, [First]} =
        receive
            {echo, Term} -> Term
        end,
    show("echoed", [Back =:= Funs, Again =:= Funs, First =:= hd(Funs), [F(5) || F <- Back]]).

%% a's funs: a closure over a value of this node, a fun of a named function and a fun capturing another.
local_funs() ->
    K = length(atom_to_list(node())) + 100,
    Add = fun(X) -> X + K end,
    [Add, fun twice/1, fun(X) -> {Add(X), K} end].

%% b's funs here: funs of their module, arity and environment; sent back, b finds its own funs and calls them.
others(B) ->
    {server, B} ! {own, self()},
    Funs =
        receive
            {own, Own} -> Own
        end,
    show("others", [[is_function(F, 1), info(F, module), info(F, type), info(F, env) =/= []] || F <- Funs]),
    {server, B} ! {check, self(), Funs},
    receive
        {checked, Equal, Results} -> show("checked there", [Equal, Results])
    end.

%% Exit reasons holding a's funs: a 'DOWN' of b's process, a link's exit, and an exit signal to b's process.
exits(B) ->
    [Add | _] = local_funs(),
    {server, B} ! {exit_with, self(), {bye, Add}},
    Victim =
        receive
            {victim, V} -> V
        end,
    Ref = monitor(process, Victim),
    Victim ! go,
    receive
        {'DOWN', Ref, process, Victim, {bye, Down}} -> show("down", [Down =:= Add, Down(1)])
    end,
    process_flag(trap_exit, true),
    {server, B} ! {exit_with, self(), {link, Add}},
    Linked =
        receive
            {victim, L} -> L
        end,
    true = link(Linked),
    Linked ! go,
    receive
        {'EXIT', Linked, {link, Exit}} -> show("link exit", [Exit =:= Add, Exit(2)])
    end,
    process_flag(trap_exit, false),
    {server, B} ! {trap, self()},
    Trapper =
        receive
            {trapper, T} -> T
        end,
    receive
        trapping -> ok
    end,
    true = exit(Trapper, {signal, Add}),
    receive
        {trapped, {signal, Signal}} -> show("exit signal", [Signal =:= Add, Signal(3)])
    end.

%% Calls of a's funs on b and of b's funs here, a fun that sends from b, and a fun spawned on b.
calls(B) ->
    [Add, Twice, Pair] = local_funs(),
    {server, B} ! {own, self()},
    [Theirs | _] =
        receive
            {own, Own} -> Own
        end,
    show("theirs here", Theirs(1)),
    show("called there", [call(B, F, [6]) || F <- [Add, Twice, Pair]]),
    show("badarity there", call(B, Add, [1, 2])),
    Self = self(),
    Tell = fun() -> Self ! {from, node(), Add(0)} end,
    {called, {ok, _}} = call(B, Tell, []),
    receive
        {from, Node, Value} -> show("sent from there", [Node, Value])
    end,
    Pid = spawn(B, fun() -> Self ! {spawned, node(), Pair(1)} end),
    receive
        {spawned, Where, Result} -> show("spawned", [Where =:= node(Pid), Where, Result])
    end.

%% End b and wait until it is gone, since packets still queued when a program ends are lost, as OTP's halt loses them.
stop(B) ->
    true = monitor_node(B, true),
    {server, B} ! stop,
    receive
        {nodedown, B} -> ok
    end.

%% Call Fun with Args on b.
call(B, Fun, Args) ->
    {server, B} ! {call, self(), Fun, Args},
    receive
        {called, _} = Called -> Called
    end.

info(Fun, Item) -> element(2, erlang:fun_info(Fun, Item)).

%% The value of Fun(), or its error reason.
failure(Fun) ->
    try Fun() of
        Value -> {ok, Value}
    catch
        error:Reason -> {error, element(1, Reason)}
    end.

wait_registered(Name) ->
    {ok, Names} = net_adm:names(localhost),
    case lists:keymember(Name, 1, Names) of
        true ->
            ok;
        false ->
            timer:sleep(10),
            wait_registered(Name)
    end.

% Register the epmd name Name while the returned socket stays open: this node is ready.
mark(Name) ->
    Port = list_to_integer(os:getenv("ERL_EPMD_PORT", "4369")),
    {ok, Socket} = gen_tcp:connect({127, 0, 0, 1}, Port, [binary, {active, false}]),
    Bytes = list_to_binary(Name),
    Request = <<120, 1:16, 77, 0, 6:16, 5:16, (byte_size(Bytes)):16, Bytes/binary, 0:16>>,
    ok = gen_tcp:send(Socket, [<<(byte_size(Request)):16>>, Request]),
    {ok, <<_, 0, _/binary>>} = gen_tcp:recv(Socket, 0, 5000),
    Socket.

done() -> io:format("dist_funs: ok~n").

show(Label, Term) -> io:format("~s: ~p~n", [Label, Term]).
