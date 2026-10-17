-module(quick_builtins).
-export([main/1]).

%% Quick builtins (plan 20 step OPT3.13B): length/1, binary_to_list/1, list_to_binary/1 and
%% iolist_to_binary/1 run a bounded fast path first; long inputs, unusual shapes and errors take the
%% portion path, which must give the same results and errors.
main(_) ->
    Long = lists:seq(1, 1000),
    Big = list_to_binary(lists:duplicate(100, $b)),
    <<_:8, Sub:40/binary, _/binary>> = Big,
    <<_:3, Unaligned:13/bits, _/bits>> = <<"unaligned">>,
    io:format("~p~n", [
        [
            length([]),
            length([a]),
            length(lists:seq(1, 256)),
            length(lists:seq(1, 257)),
            length(Long)
        ]
    ]),
    io:format("~p~n", [
        [
            binary_to_list(<<>>),
            binary_to_list(<<"abc">>),
            length(binary_to_list(Big)),
            binary_to_list(Sub) =:= lists:duplicate(40, $b),
            length(binary_to_list(list_to_binary(Long -- lists:seq(256, 1000)))),
            length(binary_to_list(binary:copy(<<"x">>, 300)))
        ]
    ]),
    Nested = [$a, [<<"bc">>, [], [[$d]]], <<>> | <<"ef">>],
    io:format("~p~n", [
        [
            list_to_binary([]),
            list_to_binary("plain"),
            list_to_binary(Nested),
            iolist_to_binary(Nested),
            iolist_to_binary(<<"as is">>),
            iolist_to_binary([Sub, Big]) =:= <<Sub/binary, Big/binary>>,
            byte_size(list_to_binary(lists:duplicate(300, $z))),
            byte_size(iolist_to_binary(lists:duplicate(20, Big))),
            byte_size(iolist_to_binary([binary:copy(<<"y">>, 3000), binary:copy(<<"y">>, 3000)])),
            byte_size(list_to_binary([binary:copy(<<"w">>, 5000)])),
            byte_size(iolist_to_binary([lists:seq(1, 200), binary:copy(<<"v">>, 4000)])),
            list_to_binary(deep(40)),
            byte_size(list_to_binary([lists:seq(0, 255) | <<"tail">>]))
        ]
    ]),
    io:format("~p~n", [
        [
            reason(fun() -> length(id([a | b])) end),
            reason(fun() -> length(id(not_a_list)) end),
            reason(fun() -> length(id(Long ++ improper)) end),
            reason(fun() -> binary_to_list(id(Unaligned)) end),
            reason(fun() -> binary_to_list(id("list")) end),
            reason(fun() -> list_to_binary(id(<<"binary">>)) end),
            reason(fun() -> list_to_binary(id([256])) end),
            reason(fun() -> list_to_binary(id([-1])) end),
            reason(fun() -> list_to_binary(id([$a | $b])) end),
            reason(fun() -> list_to_binary(id([Unaligned])) end),
            reason(fun() -> list_to_binary(id([atom])) end),
            reason(fun() -> iolist_to_binary(id(Unaligned)) end),
            reason(fun() -> iolist_to_binary(id(42)) end)
        ]
    ]),
    %% The same builtins as funs and applied dynamically take the frame entry.
    Funs = [
        {fun erlang:length/1, "fun"},
        {fun erlang:list_to_binary/1, "fun"},
        {fun erlang:binary_to_list/1, <<"fun">>}
    ],
    io:format("~p~n", [
        [F(id(A)) || {F, A} <- Funs] ++ [apply(erlang, iolist_to_binary, [id(["ap", $p])])]
    ]),
    io:format("quick_builtins: ok~n").

deep(0) -> <<"bottom">>;
deep(N) -> [deep(N - 1)].

reason(Fun) ->
    try Fun() of
        Value -> {value, Value}
    catch
        error:Reason -> Reason
    end.

id(Value) -> Value.
