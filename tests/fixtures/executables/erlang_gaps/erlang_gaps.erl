-module(erlang_gaps).
-export([main/1]).

%% erlang functions of plan 21 step LIB9A: checksums, tuple edits, is_integer/3, iolist_to_iovec/1, cookies of a
%% node that is not distributed, sends and signals with options, spawns naming a node, process flags of another
%% process, the process iterator, system flags, bump_reductions/1 and hibernate/0.
main(_) ->
    checksums(),
    tuples(),
    iovecs(),
    cookies(),
    sends(),
    signals(),
    spawns(),
    iterator(),
    flags(),
    io:format("erlang_gaps: ok~n").

checksums() ->
    A = erlang:adler32(<<"ab">>),
    C = erlang:crc32(<<"ab">>),
    show(checksums, [
        erlang:adler32(<<"abc">>),
        erlang:adler32(["a", [$b | <<"c">>]]),
        erlang:adler32(A, <<"c">>),
        erlang:adler32_combine(A, erlang:adler32(<<"c">>), 1),
        erlang:adler32(<<>>),
        erlang:crc32(<<"abc">>),
        erlang:crc32(C, [<<"c">>]),
        erlang:crc32_combine(C, erlang:crc32(<<"c">>), 1),
        erlang:crc32([]),
        erlang:crc32(16#FFFFFFFF, <<"x">>),
        catching(fun() -> erlang:adler32(id(x)) end),
        catching(fun() -> erlang:adler32(id(-1), <<>>) end),
        catching(fun() -> erlang:adler32(id(1 bsl 32), <<>>) end),
        catching(fun() -> erlang:crc32(id([1 | 2])) end),
        catching(fun() -> erlang:crc32(1, id(256)) end),
        catching(fun() -> erlang:crc32_combine(1, 2, id(-1)) end),
        catching(fun() -> erlang:adler32_combine(id(a), 2, 3) end)
    ]).

tuples() ->
    show(tuples, [
        erlang:append_element({a}, b),
        erlang:append_element({}, b),
        erlang:insert_element(1, {a}, b),
        erlang:insert_element(2, {a}, b),
        erlang:delete_element(1, {a, b}),
        erlang:delete_element(2, {a, b}),
        erlang:delete_element(1, {a}),
        is_integer(5, 1, 10),
        erlang:is_integer(id(5.0), 1, 10),
        erlang:is_integer(5, 6, 10),
        erlang:is_integer(5, 10, 1),
        erlang:is_integer(1 bsl 70, 0, 1 bsl 71),
        erlang:is_integer(-3, -3, -3),
        [X || X <- [1, 5, 11, a, 7.0], is_integer(X, 2, 10)],
        apply(erlang, is_integer, id([5, 1, 10])),
        catching(fun() -> apply(erlang, is_integer, id([5, a, 10])) end),
        catching(fun() -> erlang:append_element(id(x), b) end),
        catching(fun() -> erlang:insert_element(3, {a}, id(b)) end),
        catching(fun() -> erlang:insert_element(0, {a}, id(b)) end),
        catching(fun() -> erlang:delete_element(3, id({a, b})) end),
        catching(fun() -> erlang:delete_element(1, id(x)) end),
        catching(fun() -> erlang:is_integer(5, id(a), 10) end),
        catching(fun() -> erlang:is_integer(5, 1, id(1.0e10)) end)
    ]).

iovecs() ->
    Big = binary:copy(<<"x">>, 300),
    show(iovecs, [
        erlang:iolist_to_iovec(<<"abc">>),
        erlang:iolist_to_iovec([]),
        erlang:iolist_to_iovec([<<>>, [[]]]),
        erlang:iolist_to_iovec([<<"ab">>, $c, [<<"de">>], <<>>]),
        [byte_size(B) || B <- erlang:iolist_to_iovec([<<"ab">>, $c, Big, <<"ab">>, Big])],
        [byte_size(B) || B <- erlang:iolist_to_iovec(lists:duplicate(70000, $a))],
        iolist_to_binary(erlang:iolist_to_iovec([Big, "tail", Big])) =:=
            iolist_to_binary([Big, "tail", Big]),
        catching(fun() -> erlang:iolist_to_iovec(id(abc)) end),
        catching(fun() -> erlang:iolist_to_iovec(id([1 | 2])) end),
        catching(fun() -> erlang:iolist_to_iovec(id([256])) end)
    ]).

cookies() ->
    show(cookies, [
        erlang:get_cookie(),
        erlang:get_cookie(node()),
        erlang:get_cookie(other@host),
        catching(fun() -> erlang:get_cookie(id(1)) end),
        catching(fun() -> erlang:set_cookie(abc) end),
        catching(fun() -> erlang:set_cookie(node(), abc) end),
        catching(fun() -> erlang:set_cookie(other@host, abc) end),
        catching(fun() -> erlang:set_cookie(id(1)) end),
        catching(fun() -> erlang:set_cookie(id(1), abc) end)
    ]).

sends() ->
    register(gaps_box, self()),
    show(sends, [
        erlang:send(self(), m1, []),
        erlang:send(self(), m2, [nosuspend]),
        erlang:send(gaps_box, m3, [noconnect]),
        erlang:send({gaps_box, node()}, m4, [nosuspend, noconnect]),
        erlang:send({x, other@host}, lost, [noconnect]),
        erlang:send({x, other@host}, lost, []),
        erlang:send_nosuspend(self(), m5),
        erlang:send_nosuspend(self(), m6, [noconnect]),
        erlang:send_nosuspend({x, other@host}, lost, [noconnect]),
        erlang:send_nosuspend({x, other@host}, lost, []),
        catching(fun() -> erlang:send(self(), lost, [id(bad)]) end),
        catching(fun() -> erlang:send(self(), lost, id(nolist)) end),
        catching(fun() -> erlang:send(id(nobody), lost, []) end),
        catching(fun() -> erlang:send_nosuspend(id(nobody), lost) end),
        catching(fun() -> erlang:send_nosuspend(self(), lost, [id(bad)]) end),
        drain()
    ]),
    unregister(gaps_box).

signals() ->
    process_flag(trap_exit, true),
    Pid = spawn(fun() ->
        receive
            stop -> ok
        end
    end),
    show(signals, [
        erlang:link(Pid, []),
        erlang:link(Pid, [priority]),
        erlang:exit(Pid, kill, []),
        receive
            {'EXIT', Pid, Why} -> Why
        after 5000 -> timeout
        end,
        erlang:exit(self(), normal, [priority]),
        erlang:exit_signal(self(), shutdown, []),
        receive
            {'EXIT', Self, Reason} when Self =:= self() -> Reason
        after 5000 -> timeout
        end,
        receive
            {'EXIT', Self, Reason} when Self =:= self() -> Reason
        after 5000 -> timeout
        end,
        catching(fun() -> erlang:link(self(), [id(bad)]) end),
        catching(fun() -> erlang:link(self(), id(x)) end),
        catching(fun() -> erlang:exit(self(), normal, [id(bad)]) end),
        catching(fun() -> erlang:exit_signal(self(), normal, id(x)) end)
    ]),
    process_flag(trap_exit, false).

spawns() ->
    {P1, R1} = spawn_monitor(node(), lists, seq, [1, 2]),
    {P2, R2} = spawn_monitor(node(), fun() -> exit(done) end),
    Local = [
        receive
            {'DOWN', R1, process, P1, X1} -> X1
        end,
        receive
            {'DOWN', R2, process, P2, X2} -> X2
        end
    ],
    Self = self(),
    P3 = spawn(node(), fun() -> Self ! {spawned, self()} end),
    P4 = spawn_link(node(), erlang, send, [Self, {spawned, linked}]),
    show(spawns, [
        Local,
        %% P3 only: {spawned, linked} may arrive first and would match {spawned, P}.
        receive
            {spawned, P3} -> true
        end,
        receive
            {spawned, linked} -> is_pid(P4)
        end,
        catching(fun() -> spawn(id(1), fun() -> ok end) end),
        catching(fun() -> spawn(node(), id(x)) end),
        catching(fun() -> spawn_link(id("node"), lists, seq, [1, 2]) end)
    ]).

iterator() ->
    Pids = lists:sort(processes()),
    show(iterator, [
        walk(erlang:processes_iterator()) =:= Pids,
        lists:member(self(), walk(erlang:processes_iterator())),
        catching(fun() -> erlang:processes_next(id(x)) end)
    ]).

walk(Iterator) ->
    case erlang:processes_next(Iterator) of
        none -> [];
        {Pid, Next} -> lists:sort([Pid | walk(Next)])
    end.

flags() ->
    Other = spawn(fun() ->
        receive
            stop -> ok
        end
    end),
    show(flags, [
        erlang:process_flag(Other, save_calls, 5),
        erlang:process_flag(Other, save_calls, 0),
        erlang:process_flag(self(), save_calls, 0),
        catching(fun() -> erlang:process_flag(Other, trap_exit, true) end),
        catching(fun() -> erlang:process_flag(Other, save_calls, 10001) end),
        catching(fun() -> erlang:process_flag(id(x), save_calls, 1) end),
        erlang:bump_reductions(10),
        erlang:bump_reductions(0),
        catching(fun() -> erlang:bump_reductions(id(-1)) end),
        catching(fun() -> erlang:bump_reductions(id(x)) end),
        erlang:system_flag(backtrace_depth, 10),
        erlang:system_flag(backtrace_depth, 8),
        erlang:system_flag(fullsweep_after, 65535),
        catching(fun() -> erlang:system_flag(id(nope), 1) end),
        catching(fun() -> erlang:system_flag(backtrace_depth, id(x)) end),
        catching(fun() -> erlang:halt(0, [id(bad)]) end),
        catching(fun() -> erlang:halt(id(x), []) end),
        hibernated()
    ]),
    Other ! stop.

%% hibernate/0 returns at once when a message waits, and after the next message otherwise; both stay queued.
hibernated() ->
    self() ! first,
    ok = erlang:hibernate(),
    Self = self(),
    spawn(fun() -> Self ! second end),
    ok = erlang:hibernate(),
    drain().

drain() ->
    receive
        M -> [M | drain()]
    after 0 -> []
    end.

catching(Fun) ->
    try Fun() of
        Value -> Value
    catch
        Class:Reason -> {Class, Reason}
    end.

show(Label, Values) -> io:format("~s: ~w~n", [Label, Values]).

id(X) -> X.
