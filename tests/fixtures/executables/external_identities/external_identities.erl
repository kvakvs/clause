%% Pids, ports and references of other nodes (plan 30 step DIST1): decoding from every encoding, guards, node/1,
%% printing, text, order and equality with local identities, hashes, encoding, copies in messages, ETS, maps and
%% persistent terms, and the answers of process builtins on a node that is not distributed.
-module(external_identities).
-export([main/1]).
-compile({nowarn_deprecated_function, [{erlang, phash, 2}]}).

main(_) ->
    Ids = identities(),
    types(Ids),
    printing(Ids),
    order(Ids),
    hashes(Ids),
    encoding(Ids),
    old_encodings(),
    bad_encodings(),
    copies(Ids),
    builtins(Ids),
    io:format("external_identities: ok~n").

%% A call's result, or its exception class and reason.
safe(Fun) ->
    try
        {ok, Fun()}
    catch
        Class:Reason -> {Class, Reason}
    end.

id(X) -> X.

%% NEW_PID_EXT, V4_PORT_EXT and NEWER_REFERENCE_EXT of a node.
node_atom(Node) -> <<119, (byte_size(atom_to_binary(Node))), (atom_to_binary(Node))/binary>>.
pid(Node, Number, Serial, Creation) ->
    binary_to_term(<<131, 88, (node_atom(Node))/binary, Number:32, Serial:32, Creation:32>>).
port(Node, Number, Creation) -> binary_to_term(<<131, 120, (node_atom(Node))/binary, Number:64, Creation:32>>).
ref(Node, Creation, Words) ->
    binary_to_term(
        <<131, 90, (length(Words)):16, (node_atom(Node))/binary, Creation:32, (<<<<W:32>> || W <- Words>>)/binary>>
    ).

%% This process's pid under another creation of this node's name: OTP keeps it as another incarnation's pid.
old_self(Creation) ->
    Bytes = term_to_binary(self()),
    Prefix = binary:part(Bytes, 0, byte_size(Bytes) - 4),
    binary_to_term(<<Prefix/binary, Creation:32>>).

identities() ->
    #{
        pid => pid('a@host', 88, 0, 7),
        pid_b => pid('b@host', 5, 1, 7),
        pid_new => pid('a@host', 88, 0, 8),
        pid_big => pid('a@host', 4294967295, 4294967295, 4294967295),
        port => port('a@host', 42, 7),
        port_big => port('a@host', 1 bsl 40 + 3, 7),
        ref => ref('a@host', 7, [1, 2, 3]),
        ref5 => ref('a@host', 7, [1, 2, 3, 4, 5]),
        ref0 => ref('a@host', 7, []),
        old_self => old_self(5)
    }.

kind(X) when is_pid(X) -> pid;
kind(X) when is_port(X) -> port;
kind(X) when is_reference(X) -> reference;
kind(_) -> other.

types(#{pid := P, port := Po, ref := R, old_self := Old}) ->
    io:format("guards: ~p~n", [[kind(X) || X <- [P, Po, R, Old, self(), make_ref()]]]),
    io:format("tests: ~p~n", [[{is_pid(id(X)), is_port(id(X)), is_reference(id(X))} || X <- [P, Po, R]]]),
    io:format("node: ~p~n", [[node(X) || X <- [P, Po, R, Old]]]),
    io:format("node badarg: ~p~n", [safe(fun() -> node(id(a)) end)]).

printing(#{
    pid := P,
    pid_b := P2,
    pid_big := Big,
    port := Po,
    port_big := PoBig,
    ref := R,
    ref5 := R5,
    ref0 := R0,
    old_self := Old
}) ->
    % ~w: the masked node parts are narrower than OTP's, so ~p would wrap lines elsewhere.
    io:format("~w~n~w~n", [[P, P2, Big, Po, PoBig, R, R5, R0], {P, Po, R}]),
    io:format("old self prints as this node: ~p~n", [Old =/= self() andalso hd(pid_to_list(Old)) =:= $<]),
    io:format("text: ~p~n", [[pid_to_list(P), port_to_list(Po), ref_to_list(R), ref_to_list(R0)]]),
    io:format("text badarg: ~p~n", [
        [
            safe(fun() -> pid_to_list(id(Po)) end),
            safe(fun() -> port_to_list(id(R)) end),
            safe(fun() -> ref_to_list(id(P)) end)
        ]
    ]).

order(#{pid := P, pid_b := P2, pid_new := P3, port := Po, ref := R, ref5 := R5, old_self := Old} = Ids) ->
    io:format("equal: ~p~n", [
        [P =:= P3, P == pid('a@host', 88, 0, 7), R =:= ref('a@host', 7, [1, 2, 3, 0]), Old =:= self(), Old == self()]
    ]),
    % Process numbers of self() differ between OTP and Clause: it orders only against its other incarnation.
    Pids = [P, P2, P3, pid('a@host', 1, 0, 7), pid('a@host', 88, 1, 7)],
    io:format("incarnations: ~p~n", [[self() < Old, Old > self(), lists:sort([Old, self()]) =:= [self(), Old]]]),
    io:format("pids: ~p~n", [positions(Pids)]),
    Ports = [Po, port('b@host', 1, 7), port('a@host', 1, 7), port('a@host', 42, 8), maps:get(port_big, Ids)],
    io:format("ports: ~p~n", [positions(Ports)]),
    Refs = [R, R5, make_ref(), ref('a@host', 7, [9, 0, 0]), ref('b@host', 1, [1, 1, 1]), maps:get(ref0, Ids)],
    io:format("refs: ~p~n", [positions(Refs)]),
    Mixed = [P, Po, R, 1, a, {}, [], <<>>, fun id/1, make_ref()],
    io:format("mixed: ~p~n", [positions(Mixed)]),
    io:format("compare: ~p~n", [[P < P2, P2 > P3, Po < R, R < Po, P > Po, min(P, P3) =:= P]]).

%% The positions of a list's elements in its sorted order, which do not depend on how identities print.
positions(List) ->
    Sorted = lists:sort(List),
    [index(X, Sorted, 1) || X <- List].

index(X, [Y | _], N) when X =:= Y -> N;
index(X, [_ | Rest], N) -> index(X, Rest, N + 1).

hashes(#{pid := P, pid_b := P2, pid_new := P3, port := Po, port_big := PoBig, ref := R, ref5 := R5}) ->
    All = [P, P2, P3, Po, PoBig, R, R5],
    io:format("phash2: ~p~n", [[erlang:phash2(X) || X <- All]]),
    io:format("phash2 range: ~p~n", [[erlang:phash2(X, 1000) || X <- All]]),
    io:format("phash: ~p~n", [[erlang:phash(X, 1 bsl 32) || X <- All]]),
    io:format("in containers: ~p~n", [[erlang:phash2({X, [X]}) || X <- [P, Po, R]]]).

encoding(#{
    pid := P,
    pid_big := Big,
    port := Po,
    port_big := PoBig,
    ref := R,
    ref5 := R5,
    ref0 := R0,
    old_self := Old
}) ->
    All = [P, Big, Po, PoBig, R, R5, R0],
    io:format("encoded: ~p~n", [[term_to_binary(X) || X <- All]]),
    io:format("minor 1: ~p~n", [[term_to_binary(X, [{minor_version, 1}]) || X <- [P, Po, R]]]),
    io:format("round trips: ~p~n", [[binary_to_term(term_to_binary(X)) =:= X || X <- [Old | All]]]),
    io:format("old self creation: ~p~n", [binary:part(term_to_binary(Old), byte_size(term_to_binary(Old)), -4)]),
    % OTP 29.1.1 cannot decode a reference of 0 words nested in another term: it is left out here.
    Nested = All -- [R0],
    io:format("nested: ~p~n", [binary_to_term(term_to_binary({Nested, #{P => R}})) =:= {Nested, #{P => R}}]),
    io:format("sizes: ~p~n", [[erlang:external_size(X) || X <- [P, Po, R]]]).

old_encodings() ->
    Node = node_atom('a@host'),
    Decoded = [
        binary_to_term(<<131, 103, Node/binary, 1:32, 2:32, 3:8>>),
        binary_to_term(<<131, 102, Node/binary, 1:32, 3:8>>),
        binary_to_term(<<131, 89, Node/binary, 1:32, 3:32>>),
        binary_to_term(<<131, 114, 1:16, Node/binary, 3:8, 9:32>>),
        binary_to_term(<<131, 101, Node/binary, 9:32, 3:8>>)
    ],
    io:format("old: ~p~n", [[{kind(X), node(X)} || X <- Decoded]]),
    io:format("old re-encoded: ~p~n", [[term_to_binary(X) || X <- Decoded]]),
    io:format("old equal: ~p~n", [
        [lists:nth(1, Decoded) =:= pid('a@host', 1, 2, 3), lists:nth(4, Decoded) =:= lists:nth(5, Decoded)]
    ]).

bad_encodings() ->
    Node = node_atom('a@host'),
    io:format("bad: ~p~n", [
        [
            safe(fun() -> binary_to_term(<<131, 88, 106, 1:32, 2:32, 3:32>>) end),
            safe(fun() -> binary_to_term(<<131, 90, 6:16, Node/binary, 7:32, 0:192>>) end),
            safe(fun() -> binary_to_term(<<131, 88, Node/binary, 1:32, 2:32>>) end),
            safe(fun() -> binary_to_term(<<131, 88, 119, 13, "never@created", 1:32, 2:32, 3:32>>, [safe]) end)
        ]
    ]).

copies(#{pid := P, port := Po, ref := R, ref5 := R5} = Ids) ->
    Term = {P, Po, R, [R5, #{P => Po}]},
    self() ! Term,
    Received =
        receive
            M -> M
        end,
    io:format("message: ~p~n", [Received =:= Term]),
    Parent = self(),
    Child = spawn(fun() ->
        receive
            {From, T} -> From ! {back, T}
        end
    end),
    Child ! {Parent, Term},
    Back =
        receive
            {back, T} -> T
        end,
    io:format("round trip through a process: ~p~n", [Back =:= Term]),
    Table = ets:new(t, [set]),
    ets:insert(Table, [{P, 1}, {Po, 2}, {R, 3}, {R5, 4}]),
    io:format("ets set: ~p~n", [[element(2, hd(ets:lookup(Table, X))) || X <- [P, Po, R, R5]]]),
    Ordered = ets:new(o, [ordered_set]),
    ets:insert(Ordered, [{X, kind(X)} || X <- maps:values(Ids)]),
    io:format("ets ordered: ~p~n", [[K || {_, K} <- ets:tab2list(Ordered)]]),
    Map = maps:from_list([{X, kind(X)} || X <- maps:values(Ids)]),
    io:format("map: ~p~n", [[maps:get(X, Map) || X <- [P, Po, R]]]),
    persistent_term:put({?MODULE, ids}, Term),
    io:format("persistent: ~p~n", [persistent_term:get({?MODULE, ids}) =:= Term]),
    Garbage = [binary_to_term(term_to_binary(Term)) || _ <- lists:seq(1, 2000)],
    erlang:garbage_collect(),
    io:format("after collection: ~p~n", [lists:all(fun(X) -> X =:= Term end, Garbage)]).

builtins(#{pid := P, port := Po, ref := R}) ->
    io:format("send: ~p~n", [
        [
            safe(fun() -> id(P) ! hello end),
            safe(fun() -> id(Po) ! hello end),
            safe(fun() -> id(R) ! hello end)
        ]
    ]),
    io:format("send/3: ~p~n", [
        [safe(fun() -> erlang:send(id(P), hello, [noconnect]) end), safe(fun() -> erlang:send(id(P), hello, []) end)]
    ]),
    io:format("send_nosuspend: ~p~n", [safe(fun() -> erlang:send_nosuspend(id(P), hello, [noconnect]) end)]),
    io:format("exit: ~p~n", [[safe(fun() -> exit(id(P), kill) end), safe(fun() -> exit(id(R), kill) end)]]),
    io:format("unlink: ~p~n", [safe(fun() -> unlink(id(P)) end)]),
    io:format("local only: ~p~n", [
        [
            safe(fun() -> is_process_alive(id(P)) end),
            safe(fun() -> process_info(id(P)) end),
            safe(fun() -> process_info(id(P), status) end),
            safe(fun() -> register(foo, id(P)) end),
            safe(fun() -> erlang:send_after(10, id(P), x) end),
            safe(fun() -> demonitor(id(R)) end),
            safe(fun() -> erlang:port_info(id(Po)) end),
            safe(fun() -> port_close(id(Po)) end),
            safe(fun() -> erlang:suspend_process(id(P)) end)
        ]
    ]),
    io:format("timers: ~p~n", [
        [
            safe(fun() -> erlang:cancel_timer(id(R)) end),
            safe(fun() -> erlang:read_timer(id(R)) end)
        ]
    ]),
    receive
        Unexpected -> io:format("unexpected message: ~p~n", [Unexpected])
    after 0 -> ok
    end.
