-module(message_handoff).
-export([main/1]).

%% Large messages (lists, maps above 32 keys, off-heap binaries and their slices) sent by several processes at
%% once, so receivers take some while idle and some while busy; every received value is checked and sent back.
main(_) ->
    Self = self(),
    Echo = spawn(fun() -> echo(0) end),
    Senders = [spawn(fun() -> sender(Self, Echo, N) end) || N <- lists:seq(1, 8)],
    Results = [
        receive
            {done, S, Result} -> Result
        end
     || S <- Senders
    ],
    io:format("~p~n", [Results]),
    Echo ! {stop, Self},
    receive
        {echoed, Count} -> io:format("echoed ~p~n", [Count])
    end,
    io:format("~p~n", [kept(Self)]),
    io:format("message_handoff: ok~n").

sender(Parent, Echo, N) ->
    Terms = [payload(N, Round) || Round <- lists:seq(1, 20)],
    Sums = [round_trip(Echo, Term) || Term <- Terms],
    Parent ! {done, self(), {N, lists:sum(Sums)}}.

payload(N, Round) ->
    List = lists:seq(1, 1000 + N * 10 + Round),
    Map = maps:from_list([{K, K * N} || K <- lists:seq(1, 40 + Round)]),
    Binary = binary:copy(<<N, Round>>, 200),
    Slice = binary:part(Binary, 10, 100),
    {List, Map, Binary, Slice, [List, Map]}.

round_trip(Echo, Term) ->
    Echo ! {self(), Term},
    receive
        {echo, Back} ->
            Back = Term,
            {List, Map, Binary, Slice, [List, Map]} = Back,
            lists:sum(List) + lists:sum(maps:values(Map)) + byte_size(Binary) + binary:first(Slice)
    end.

echo(Count) ->
    receive
        {stop, From} ->
            From ! {echoed, Count};
        {From, Term} ->
            From ! {echo, Term},
            _ = erlang:garbage_collect(),
            echo(Count + 1)
    end.

%% A large message kept across collections of the receiver.
kept(Parent) ->
    Big = {lists:seq(1, 5000), maps:from_list([{K, K} || K <- lists:seq(1, 100)])},
    Child = spawn(fun() ->
        receive
            {Value} ->
                _ = [lists:seq(1, 1000) || _ <- lists:seq(1, 50)],
                true = erlang:garbage_collect(),
                Parent ! {kept, Value}
        end
    end),
    Child ! {Big},
    receive
        {kept, Value} -> Value =:= Big
    end.
