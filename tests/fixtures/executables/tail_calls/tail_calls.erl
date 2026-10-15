-module(tail_calls).
-export([main/1, back/2]).

%% Long enough that a loop keeping one frame per iteration would need more
%% than 2^24 stack words (128 MiB on 64 bits): these frames take at least 11
%% words each, branch/2 frames 41.
-define(STEPS, 2000000).
-define(BRANCH_STEPS, 500000).

%% The first argument selects a loop of tail calls.
main(["local"]) ->
    show(count(id(?STEPS), 0));
main(["mutual"]) ->
    show(ping(id(?STEPS)));
main(["remote"]) ->
    show(tail_peer:bounce(id(?STEPS), 0));
main(["branches"]) ->
    show(branch(id(?BRANCH_STEPS), 0));
main(["self"]) ->
    show(wide(id(?BRANCH_STEPS), 1, 2, 3, 4, 5, 6, 7, [])),
    show(guarded(id(?BRANCH_STEPS), 0)).

show(Value) -> erlang:display(Value).

id(Value) -> Value.

%% Self tail call carrying an accumulator.
count(0, Acc) -> {local, Acc};
count(N, Acc) -> count(N - 1, Acc + 1).

%% Mutual tail calls between two local functions.
ping(0) -> {mutual, ping};
ping(N) -> pong(N - 1).

pong(0) -> {mutual, pong};
pong(N) -> ping(N - 1).

%% Remote tail calls bouncing between two modules.
back(N, Acc) -> tail_peer:bounce(N, Acc).

%% A self tail call with more arguments than registers, keeping a list built in a slot across collections.
wide(0, A, B, C, D, E, F, G, Acc) ->
    {self, A + B + C + D + E + F + G, length(Acc), lists:sum(lists:sublist(Acc, 5))};
wide(N, A, B, C, D, E, F, G, Acc) ->
    wide(N - 1, B, C, D, E, F, G, A, [N rem 7 | Acc]).

%% A self tail call after a non-tail call and a try in the same body, which reset its frame's resume and handler.
guarded(0, Acc) ->
    {guarded, Acc};
guarded(N, Acc) ->
    Step =
        try
            checked(N)
        catch
            error:odd -> 1
        end,
    guarded(N - 1, Acc + id(Step)).

checked(N) when N rem 2 =:= 1 -> error(odd);
checked(_) -> 2.

%% Tail calls from case, if and begin/end clause bodies.
branch(0, Acc) ->
    {branches, Acc};
branch(N, Acc) ->
    case N rem 3 of
        0 ->
            branch(N - 1, Acc + 1);
        1 ->
            if
                Acc >= 0 -> branch(N - 1, Acc + 2)
            end;
        _ ->
            begin
                branch(N - 1, Acc)
            end
    end.
