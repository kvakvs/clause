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
    show(guarded(id(?BRANCH_STEPS), 0));
main(["peers"]) ->
    show(left(id(?BRANCH_STEPS), [], 0)),
    show(failing_peer(id(1001))).

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

%% A tail-call cycle whose members keep different numbers of terms: right/3 keeps more across a try and a
%% non-tail call, so each tail call grows or shrinks the frame in place while the list built in a slot survives
%% collections. The list restarts every 64 steps, so the old ones are garbage.
left(0, Acc, Sum) ->
    {left, length(Acc), Sum};
left(N, _Acc, Sum) when N rem 64 =:= 0 ->
    right(N - 1, [N], Sum + 1);
left(N, Acc, Sum) ->
    right(N - 1, [N | Acc], Sum + 1).

right(0, Acc, Sum) ->
    {right, length(Acc), Sum};
right(N, Acc, Sum) ->
    Kept = {N, Acc},
    Step =
        try
            checked(N)
        catch
            error:odd -> 1
        end,
    Extra = id([Step, element(1, Kept)]),
    left(N - 1, element(2, Kept), Sum + Step + length(Extra)).

%% An error raised inside a tail-call cycle names the member that raised it.
failing_peer(N) ->
    try
        up(N)
    catch
        error:Reason:Stack ->
            [{Module, Function, Arity, _} | _] = Stack,
            {Reason, Module, Function, Arity}
    end.

up(N) -> down(N - 1).

down(0) -> error(peer_failed);
down(N) -> up(N - 1).

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
