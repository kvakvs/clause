%% A user carrier (plan 30 step DIST12b): nodes a and b start distribution over uc_dist (--proto-dist uc), a carrier
%% of this test in the shape of OTP's carrier guide, then connect, call and message each other over it. Node x names
%% a carrier the program lacks.
-module(dist_user_carrier).
-export([main/1, echo/1]).

main(["a"]) ->
    start("uca"),
    show("children", [Id || {Id, _, _, _} <- supervisor:which_children(net_sup)]),
    B = peer("ucb"),
    show("ping", net_adm:ping(B)),
    {ok, Address} = net_kernel:node_info(B, address),
    show("address", [element(N, Address) || N <- [3, 4, 5]]),
    show("getopts", net_kernel:getopts(B, [nodelay])),
    show("setopts", net_kernel:setopts(B, [{nodelay, true}])),
    show("rpc", rpc:call(B, erlang, node, [])),
    Pid = spawn(B, ?MODULE, echo, [self()]),
    Big = binary:copy(<<"uc">>, 50000),
    Pid ! {echo, Big},
    show(
        "echo",
        receive
            {echoed, Pid, Big} -> byte_size(Big)
        after 30000 -> timeout
        end
    ),
    stop(B);
main(["b"]) ->
    start("ucb"),
    register(dist_user_carrier, self()),
    % A node a never stops ends on its own, so that a failed run leaves no node behind.
    receive
        stop -> done()
    after 120000 -> erlang:halt(1)
    end;
main(["x"]) ->
    case net_kernel:start(node_of("ucx"), #{name_domain => shortnames}) of
        {error, {{shutdown, Reason}, _}} -> show("not supported", Reason);
        Other -> show("not supported", Other)
    end,
    done().

start(Name) ->
    {ok, _} = net_kernel:start(node_of(Name), #{name_domain => shortnames}),
    show("node", node()).

echo(Parent) ->
    receive
        {echo, Data} -> Parent ! {echoed, self(), Data}
    end.

% Stop node B's process and wait until it ends: a program's end loses queued packets.
stop(B) ->
    Monitor = erlang:monitor(process, {dist_user_carrier, B}),
    {dist_user_carrier, B} ! stop,
    receive
        {'DOWN', Monitor, process, _, _} -> done()
    end.

% Node Name once it is registered with epmd, which its carrier does when it listens.
peer(Name) ->
    {ok, Names} = net_adm:names(localhost),
    case lists:keymember(Name, 1, Names) of
        true ->
            node_of(Name);
        false ->
            timer:sleep(10),
            peer(Name)
    end.

% Test nodes are named @localhost: they listen on loopback only.
node_of(Name) -> list_to_atom(Name ++ "@localhost").

done() -> io:format("dist_user_carrier: ok~n").

show(Label, Term) -> io:format("~s: ~p~n", [Label, Term]).
