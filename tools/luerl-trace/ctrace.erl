-module(ctrace).
-export([run/3]).

%% OTP-side twin of Clause's CLAUSE_CALL_TRACE: logs calls from quiet (program)
%% modules into other modules, with bounded arguments and results.
-define(DEPTH, 5).
-define(WIDTH, 16).
-define(MAPS, 10).
-define(BIN, 40).

run(M, F, Out) ->
    preload(),
    {ok, Fd} = file:open(Out, [write, {delayed_write, 1 bsl 20, 2000}]),
    Tracer = spawn(fun() -> loop(Fd, #{}) end),
    erlang:trace_pattern({'_', '_', '_'}, [{'_', [], [{exception_trace}, {message, {caller}}]}], [global]),
    erlang:trace_pattern(on_load, [{'_', [], [{exception_trace}, {message, {caller}}]}], [global]),
    Pid = spawn(fun() -> receive go -> ok end, M:F() end),
    erlang:trace(Pid, true, [call, set_on_spawn, {tracer, Tracer}]),
    Ref = monitor(process, Pid),
    Pid ! go,
    receive {'DOWN', Ref, _, _, Why} -> io:format("ctrace: driver ended ~p~n", [Why]) end,
    erlang:trace(all, false, [call]),
    Tracer ! {done, self()},
    receive done -> ok end.

%% Load every module first, so code loading does not show up in the trace.
preload() ->
    [code:ensure_loaded(list_to_atom(filename:basename(B, ".beam"))) || B <- filelib:wildcard("*.beam")],
    [begin
         application:load(App),
         {ok, Ms} = application:get_key(App, modules),
         [code:ensure_loaded(Mod) || Mod <- Ms]
     end
     || App <- [kernel, stdlib, compiler]],
    ok.

quiet(M) ->
    S = atom_to_list(M),
    lists:any(fun(P) -> lists:prefix(P, S) end, ["luerl", "ttdict", "ttsets"]).

loop(Fd, Stacks) ->
    receive
        {done, From} ->
            file:close(Fd),
            From ! done;
        {trace, P, call, {M, F, Args}, Caller} ->
            Stack = maps:get(P, Stacks, []),
            Boundary = caller_quiet(Caller) andalso not quiet(M),
            Name = name(M, F, length(Args)),
            Boundary andalso
                out(Fd, Stack, ["CALL ", Name, "(", lists:join(", ", [p(A, ?DEPTH) || A <- Args]), ")"]),
            loop(Fd, Stacks#{P => [{Name, Boundary} | Stack]});
        {trace, P, return_from, {M, F, A}, Val} ->
            loop(Fd, pop(Fd, P, Stacks, name(M, F, A), ["RET ", name(M, F, A), " -> ", p(Val, ?DEPTH)]));
        {trace, P, exception_from, {M, F, A}, {C, R}} ->
            loop(Fd, pop(Fd, P, Stacks, name(M, F, A), ["EXC ", name(M, F, A), " ", atom_to_list(C), ":", p(R, ?DEPTH)]));
        _ ->
            loop(Fd, Stacks)
    end.

caller_quiet({CM, _, _}) -> quiet(CM);
caller_quiet(_) -> false.

pop(Fd, P, Stacks, _Name, Text) ->
    case maps:get(P, Stacks, []) of
        [{_, true} | Rest] ->
            out(Fd, Rest, Text),
            Stacks#{P => Rest};
        [_ | Rest] ->
            Stacks#{P => Rest};
        [] ->
            Stacks
    end.

out(Fd, Stack, Text) ->
    Indent = lists:duplicate(2 * length([x || {_, true} <- Stack]), $\s),
    file:write(Fd, unicode:characters_to_binary([Indent, Text, $\n], latin1)).

name(M, F, A) -> io_lib:format("~w:~w/~w", [M, F, A]).

p(_, 0) -> "_";
p(T, _) when is_integer(T); is_float(T); is_atom(T) -> io_lib:write(T);
p(T, _) when is_function(T) -> "#Fun";
p(T, _) when is_pid(T) -> "#Pid";
p(T, _) when is_port(T) -> "#Port";
p(T, _) when is_reference(T) -> "#Ref";
p(T, _) when is_binary(T) -> bin(T);
p(T, _) when is_bitstring(T) -> "#Bits";
p(T, D) when is_list(T) -> ["[", list(T, D, 0), "]"];
p(T, D) when is_tuple(T) -> ["{", tuple(tuple_to_list(T), D, 0), "}"];
p(T, _) when is_map(T), map_size(T) > ?MAPS -> ["#{", integer_to_list(map_size(T)), "}"];
p(T, D) when is_map(T) ->
    ["#{", lists:join(",", [[p(K, D - 1), "=>", p(V, D - 1)] || {K, V} <- lists:sort(maps:to_list(T))]), "}"];
p(_, _) -> "#Other".

list([], _, _) -> [];
list([_ | _], _, ?WIDTH) -> "|...";
list([H | T], D, N) -> [sep(N), p(H, D - 1) | list(T, D, N + 1)];
list(T, D, _) -> ["|", p(T, D - 1)].

tuple([], _, _) -> [];
tuple(_, _, ?WIDTH) -> ",...";
tuple([H | T], D, N) -> [sep(N), p(H, D - 1) | tuple(T, D, N + 1)].

sep(0) -> "";
sep(_) -> ",".

bin(B) when byte_size(B) =< ?BIN -> bytes(B);
bin(B) -> ["<<", integer_to_list(byte_size(B)), "B:", bytes(binary:part(B, 0, ?BIN)), ">>"].

bytes(B) ->
    L = binary_to_list(B),
    case lists:all(fun(C) -> C >= 32 andalso C =< 126 end, L) of
        true -> ["<<\"", [esc(C) || C <- L], "\">>"];
        false -> ["<<", lists:join(",", [integer_to_list(C) || C <- L]), ">>"]
    end.

esc($") -> "\\\"";
esc(92) ->"\\\\";
esc(C) -> C.
