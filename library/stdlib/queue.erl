%% Clause's queue (plan 21 step LIB3, docs/builtins.md#collections): OTP's {Rear, Front} pair of lists. The
%% operations on items are runtime builtins with clause_builtin_placeholder bodies; the functions taking funs and the
%% aliases are Erlang.
-module(queue).
-export([
    all/2,
    any/2,
    cons/2,
    daeh/1,
    delete/2,
    delete_r/2,
    delete_with/2,
    delete_with_r/2,
    drop/1,
    drop_r/1,
    filter/2,
    filtermap/2,
    fold/3,
    from_list/1,
    get/1,
    get_r/1,
    head/1,
    in/2,
    in_r/2,
    init/1,
    is_empty/1,
    is_queue/1,
    join/2,
    lait/1,
    last/1,
    len/1,
    liat/1,
    member/2,
    new/0,
    out/1,
    out_r/1,
    peek/1,
    peek_r/1,
    reverse/1,
    snoc/2,
    split/2,
    tail/1,
    to_list/1
]).
-include("queue.types.hrl").

delete(_, _) -> clause_builtin_placeholder.
delete_r(_, _) -> clause_builtin_placeholder.
drop(_) -> clause_builtin_placeholder.
drop_r(_) -> clause_builtin_placeholder.
from_list(_) -> clause_builtin_placeholder.
get(_) -> clause_builtin_placeholder.
get_r(_) -> clause_builtin_placeholder.
in(_, _) -> clause_builtin_placeholder.
in_r(_, _) -> clause_builtin_placeholder.
join(_, _) -> clause_builtin_placeholder.
member(_, _) -> clause_builtin_placeholder.
out(_) -> clause_builtin_placeholder.
out_r(_) -> clause_builtin_placeholder.
peek(_) -> clause_builtin_placeholder.
peek_r(_) -> clause_builtin_placeholder.
split(_, _) -> clause_builtin_placeholder.
to_list(_) -> clause_builtin_placeholder.

%% The empty queue.
new() -> {[], []}.

%% Whether a term is a pair of lists.
is_queue({Rear, Front}) when is_list(Rear), is_list(Front) -> true;
is_queue(_) -> false.

%% Whether the queue has no items; badarg for anything but a queue.
is_empty({[], []}) -> true;
is_empty({Rear, Front}) when is_list(Rear), is_list(Front) -> false;
is_empty(Queue) -> erlang:error(badarg, [Queue]).

%% The number of items.
len({Rear, Front}) when is_list(Rear), is_list(Front) -> length(Rear) + length(Front);
len(Queue) -> erlang:error(badarg, [Queue]).

%% The items in reverse order.
reverse({Rear, Front}) when is_list(Rear), is_list(Front) -> {Front, Rear};
reverse(Queue) -> erlang:error(badarg, [Queue]).

%% The Okasaki API names.
cons(Item, Queue) -> queue:in_r(Item, Queue).
head(Queue) -> queue:get(Queue).
tail(Queue) -> queue:drop(Queue).
snoc(Queue, Item) -> queue:in(Item, Queue).
daeh(Queue) -> queue:get_r(Queue).
last(Queue) -> queue:get_r(Queue).
liat(Queue) -> queue:drop_r(Queue).
lait(Queue) -> queue:drop_r(Queue).
init(Queue) -> queue:drop_r(Queue).

%% The items Fun keeps: true keeps the item, false drops it, a list replaces it by its elements.
filter(Fun, {Rear, Front}) when is_function(Fun, 1), is_list(Rear), is_list(Front) ->
    NewFront = lists:append([kept(Fun, Item) || Item <- Front]),
    NewRear = lists:reverse(lists:append([kept(Fun, Item) || Item <- lists:reverse(Rear)])),
    balanced(NewRear, NewFront);
filter(Fun, Queue) ->
    erlang:error(badarg, [Fun, Queue]).

kept(Fun, Item) ->
    case Fun(Item) of
        true -> [Item];
        false -> [];
        Items when is_list(Items) -> Items
    end.

%% The items Fun keeps (true) or replaces ({true, New}).
filtermap(Fun, {Rear, Front}) when is_function(Fun, 1), is_list(Rear), is_list(Front) ->
    NewFront = lists:filtermap(Fun, Front),
    NewRear = lists:reverse([Value || Item <- lists:reverse(Rear), Value <- mapped(Fun, Item)]),
    balanced(NewRear, NewFront);
filtermap(Fun, Queue) ->
    erlang:error(badarg, [Fun, Queue]).

mapped(Fun, Item) ->
    case Fun(Item) of
        true -> [Item];
        {true, Value} -> [Value];
        false -> []
    end.

%% A queue of new lists, as OTP's filter and filtermap rebalance them when one side is empty.
balanced([], Front) -> queue:from_list(Front);
balanced(Rear, []) -> queue:reverse(queue:from_list(Rear));
balanced(Rear, Front) -> {Rear, Front}.

%% Fun(Item, Acc) over the items in queue order.
fold(Fun, Acc, {Rear, Front}) when is_function(Fun, 2), is_list(Rear), is_list(Front) ->
    lists:foldr(Fun, lists:foldl(Fun, Acc, Front), Rear);
fold(Fun, Acc, Queue) ->
    erlang:error(badarg, [Fun, Acc, Queue]).

%% Whether Pred holds for some item, and for every item.
any(Pred, {Rear, Front}) when is_function(Pred, 1), is_list(Rear), is_list(Front) ->
    lists:any(Pred, Front) orelse lists:any(Pred, Rear);
any(Pred, Queue) ->
    erlang:error(badarg, [Pred, Queue]).

all(Pred, {Rear, Front}) when is_function(Pred, 1), is_list(Rear), is_list(Front) ->
    lists:all(Pred, Front) andalso lists:all(Pred, Rear);
all(Pred, Queue) ->
    erlang:error(badarg, [Pred, Queue]).

%% The first item in queue order for which Pred is true removed; the queue itself when none is.
delete_with(Pred, {Rear, Front} = Queue) when is_function(Pred, 1), is_list(Rear), is_list(Front) ->
    case without_first(Pred, Front) of
        {ok, []} ->
            queue:reverse(queue:from_list(Rear));
        {ok, NewFront} ->
            {Rear, NewFront};
        error ->
            case without_first(Pred, lists:reverse(Rear)) of
                {ok, []} -> queue:from_list(Front);
                {ok, Older} -> {lists:reverse(Older), Front};
                error -> Queue
            end
    end;
delete_with(Pred, Queue) ->
    erlang:error(badarg, [Pred, Queue]).

%% The same from the rear end.
delete_with_r(Pred, {Rear, Front}) when is_function(Pred, 1), is_list(Rear), is_list(Front) ->
    {NewFront, NewRear} = delete_with(Pred, {Front, Rear}),
    {NewRear, NewFront};
delete_with_r(Pred, Queue) ->
    erlang:error(badarg, [Pred, Queue]).

without_first(Pred, [Item | Rest]) ->
    case Pred(Item) of
        true ->
            {ok, Rest};
        false ->
            case without_first(Pred, Rest) of
                {ok, Others} -> {ok, [Item | Others]};
                error -> error
            end
    end;
without_first(_, []) ->
    error.
