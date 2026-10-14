%% Project-owned subset of the lists module (plan 11 step 39): original
%% implementations whose results and error reasons follow OTP's documented
%% behavior. Compiled from source into every program that calls it.
-module(lists).
-export([
    append/1,
    append/2,
    filter/2,
    foldl/3,
    foldr/3,
    foreach/2,
    keyfind/3,
    map/2,
    mapfoldl/3,
    mapfoldr/3,
    member/2,
    nth/2,
    partition/2,
    reverse/1,
    reverse/2,
    seq/2,
    seq/3,
    sort/1,
    splitwith/2
]).
%% OTP 29 exports Clause does not implement yet: stubs raising error({not_implemented, F, A}),
%% or catalog builtins the runtime puts in their place (docs/library.md#runtime-module-replacements).
-export([
    all/2,
    any/2,
    concat/1,
    delete/2,
    droplast/1,
    dropwhile/2,
    duplicate/2,
    enumerate/1,
    enumerate/2,
    enumerate/3,
    filtermap/2,
    flatlength/1,
    flatmap/2,
    flatten/1,
    flatten/2,
    join/2,
    keydelete/3,
    keymap/3,
    keymember/3,
    keymerge/3,
    keyreplace/4,
    keysearch/3,
    keysort/2,
    keystore/4,
    keytake/3,
    last/1,
    max/1,
    merge/1,
    merge/2,
    merge/3,
    merge3/3,
    min/1,
    nthtail/2,
    prefix/2,
    rkeymerge/3,
    rmerge/2,
    rmerge/3,
    rmerge3/3,
    rukeymerge/3,
    rumerge/2,
    rumerge/3,
    rumerge3/3,
    search/2,
    sort/2,
    split/2,
    sublist/2,
    sublist/3,
    subtract/2,
    suffix/2,
    sum/1,
    takewhile/2,
    ukeymerge/3,
    ukeysort/2,
    umerge/1,
    umerge/2,
    umerge/3,
    umerge3/3,
    uniq/1,
    uniq/2,
    unzip/1,
    unzip3/1,
    usort/1,
    usort/2,
    zf/2,
    zip/2,
    zip/3,
    zip3/3,
    zip3/4,
    zipwith/3,
    zipwith/4,
    zipwith3/4,
    zipwith3/5
]).

%% The concatenation of a list of lists.
append([Last]) -> Last;
append([First | Rest]) -> First ++ append(Rest);
append([]) -> [].

%% Two lists joined: List1 ++ List2.
append(List1, List2) -> List1 ++ List2.

%% The elements for which Pred returns true; anything else from Pred is an error.
filter(Pred, List) when is_function(Pred, 1) ->
    [Element || Element <- List, Pred(Element)].

%% Fold from the first element: Fun(Element, Acc) returns the next Acc. A non-list
%% is a case_clause error, an improper tail a function_clause error, as in OTP.
foldl(Fun, Acc, List) when is_function(Fun, 2) ->
    case List of
        [Element | Rest] -> fold_left(Fun, Fun(Element, Acc), Rest);
        [] -> Acc
    end.

fold_left(Fun, Acc, [Element | Rest]) -> fold_left(Fun, Fun(Element, Acc), Rest);
fold_left(_, Acc, []) -> Acc.

%% Fold from the last element.
foldr(Fun, Acc, [Element | Rest]) when is_function(Fun, 2) ->
    Fun(Element, foldr(Fun, Acc, Rest));
foldr(Fun, Acc, []) when is_function(Fun, 2) ->
    Acc.

%% The first tuple whose Nth element compares equal (==) to Key, or false.
keyfind(Key, N, List) when is_integer(N), N >= 1 ->
    keyfind_from(Key, N, List);
keyfind(_, _, _) ->
    erlang:error(badarg).

keyfind_from(Key, N, [Tuple | _]) when
    is_tuple(Tuple), tuple_size(Tuple) >= N, element(N, Tuple) == Key
->
    Tuple;
keyfind_from(Key, N, [_ | Rest]) ->
    keyfind_from(Key, N, Rest);
keyfind_from(_, _, []) ->
    false;
keyfind_from(_, _, _) ->
    erlang:error(badarg).

%% Fun applied to every element, in order. A non-list is a case_clause error, an
%% improper tail a function_clause error, as in OTP.
map(Fun, List) when is_function(Fun, 1) ->
    case List of
        [Element | Rest] -> [Fun(Element) | map_rest(Fun, Rest)];
        [] -> []
    end.

map_rest(Fun, [Element | Rest]) -> [Fun(Element) | map_rest(Fun, Rest)];
map_rest(_, []) -> [].

%% Whether an element matches (=:=) Element.
member(Element, [Element | _]) -> true;
member(Element, [_ | Rest]) -> member(Element, Rest);
member(_, []) -> false;
member(_, _) -> erlang:error(badarg).

%% The Nth element, counting from 1.
nth(1, [Element | _]) -> Element;
nth(N, [_ | _] = List) when is_integer(N), N > 1 -> nth_from(N, List).

nth_from(1, [Element | _]) -> Element;
nth_from(N, [_ | Rest]) -> nth_from(N - 1, Rest).

%% Fun applied to every element for its effects, then ok (plan step 41T).
foreach(Fun, List) when is_function(Fun, 1) -> for_each(Fun, List).

for_each(Fun, [Element | Rest]) ->
    Fun(Element),
    for_each(Fun, Rest);
for_each(_, []) ->
    ok.

%% Map and fold in one pass from the first element: Fun(Element, Acc) returns
%% {Mapped, NextAcc} (plan step 41T).
mapfoldl(Fun, Acc, [Element | Rest]) ->
    {Mapped, Next} = Fun(Element, Acc),
    {MappedRest, Last} = mapfoldl(Fun, Next, Rest),
    {[Mapped | MappedRest], Last};
mapfoldl(Fun, Acc, []) when is_function(Fun, 2) ->
    {[], Acc}.

%% Map and fold from the last element (plan step 41T).
mapfoldr(Fun, Acc, [Element | Rest]) ->
    {MappedRest, Next} = mapfoldr(Fun, Acc, Rest),
    {Mapped, Last} = Fun(Element, Next),
    {[Mapped | MappedRest], Last};
mapfoldr(Fun, Acc, []) when is_function(Fun, 2) ->
    {[], Acc}.

%% The elements for which Pred is true and those for which it is false, each in
%% order (plan step 41T).
partition(Pred, List) when is_function(Pred, 1) -> partition(Pred, List, [], []).

partition(Pred, [Element | Rest], Satisfying, Others) ->
    case Pred(Element) of
        true -> partition(Pred, Rest, [Element | Satisfying], Others);
        false -> partition(Pred, Rest, Satisfying, [Element | Others])
    end;
partition(_, [], Satisfying, Others) ->
    {reverse(Satisfying), reverse(Others)}.

%% The longest prefix whose elements satisfy Pred, and the rest (plan step 41T).
splitwith(Pred, List) when is_function(Pred, 1) -> split_while(Pred, List, []).

split_while(Pred, [Element | Rest] = List, Prefix) ->
    case Pred(Element) of
        true -> split_while(Pred, Rest, [Element | Prefix]);
        false -> {reverse(Prefix), List}
    end;
split_while(_, [], Prefix) ->
    {reverse(Prefix), []}.

%% The elements in reverse order.
reverse([] = List) -> List;
reverse([_] = List) -> List;
reverse([A, B]) -> [B, A];
reverse([A, B | Rest]) -> reverse(Rest, [B, A]).

%% The elements of List in reverse order, followed by Tail.
reverse([Element | Rest], Tail) -> reverse(Rest, [Element | Tail]);
reverse([], Tail) -> Tail;
reverse(_, _) -> erlang:error(badarg).

%% The integers from First to Last.
seq(First, Last) when is_integer(First), is_integer(Last), First - 1 =< Last ->
    count_down(Last, First, 1, []).

%% The integers from First stepping by Increment while not passing Last.
seq(First, Last, Increment) when
    is_integer(First),
    is_integer(Last),
    is_integer(Increment),
    (Increment > 0 andalso First - Increment =< Last) orelse
        (Increment < 0 andalso First - Increment >= Last)
->
    Count = (Last - First + Increment) div Increment,
    count_down(First + Increment * (Count - 1), First, Increment, []);
seq(Same, Same, 0) when is_integer(Same) ->
    [Same];
seq(_, _, _) ->
    erlang:error(badarg).

%% Prepend From, From - Step, ... while it has not passed To.
count_down(From, To, Step, Acc) when Step > 0, From >= To; Step < 0, From =< To ->
    count_down(From - Step, To, Step, [From | Acc]);
count_down(_, _, _, Acc) ->
    Acc.

%% The elements in ascending term order; equal elements keep their order.
sort([_, _ | _] = List) ->
    merge_all(runs(List, []));
sort([_] = List) ->
    List;
sort([] = List) ->
    List.

%% Split into one-element runs, in order.
runs([Element | Rest], Acc) -> runs(Rest, [[Element] | Acc]);
runs([], Acc) -> reverse(Acc, []).

%% Merge runs pairwise until one remains.
merge_all([Run]) -> Run;
merge_all(Runs) -> merge_all(merge_pairs(Runs, [])).

merge_pairs([A, B | Rest], Acc) -> merge_pairs(Rest, [merge(A, B, []) | Acc]);
merge_pairs([A], Acc) -> reverse(Acc, [A]);
merge_pairs([], Acc) -> reverse(Acc, []).

%% Merge two sorted runs; on ties the left run comes first.
merge([X | Xs] = Left, [Y | Ys] = Right, Acc) ->
    case X =< Y of
        true -> merge(Xs, Right, [X | Acc]);
        false -> merge(Left, Ys, [Y | Acc])
    end;
merge([], Right, Acc) ->
    reverse(Acc, Right);
merge(Left, [], Acc) ->
    reverse(Acc, Left).

all(_, _) -> erlang:error({not_implemented, all, 2}).
any(_, _) -> erlang:error({not_implemented, any, 2}).
concat(_) -> erlang:error({not_implemented, concat, 1}).
delete(_, _) -> erlang:error({not_implemented, delete, 2}).
droplast(_) -> erlang:error({not_implemented, droplast, 1}).
dropwhile(_, _) -> erlang:error({not_implemented, dropwhile, 2}).
duplicate(_, _) -> erlang:error({not_implemented, duplicate, 2}).
enumerate(_) -> erlang:error({not_implemented, enumerate, 1}).
enumerate(_, _) -> erlang:error({not_implemented, enumerate, 2}).
enumerate(_, _, _) -> erlang:error({not_implemented, enumerate, 3}).
%% The elements Fun keeps, as themselves (true) or replaced ({true, Value}); erl_features needs it (step 41L).
filtermap(Fun, List) when is_function(Fun, 1) -> filtermap_1(Fun, List).

filtermap_1(Fun, [Head | Tail]) ->
    case Fun(Head) of
        true -> [Head | filtermap_1(Fun, Tail)];
        {true, Value} -> [Value | filtermap_1(Fun, Tail)];
        false -> filtermap_1(Fun, Tail)
    end;
filtermap_1(_, []) ->
    [].
flatlength(_) -> erlang:error({not_implemented, flatlength, 1}).
flatmap(_, _) -> erlang:error({not_implemented, flatmap, 2}).
flatten(_) -> erlang:error({not_implemented, flatten, 1}).
flatten(_, _) -> erlang:error({not_implemented, flatten, 2}).
join(_, _) -> erlang:error({not_implemented, join, 2}).
keydelete(_, _, _) -> erlang:error({not_implemented, keydelete, 3}).
keymap(_, _, _) -> erlang:error({not_implemented, keymap, 3}).
keymember(_, _, _) -> erlang:error({not_implemented, keymember, 3}).
keymerge(_, _, _) -> erlang:error({not_implemented, keymerge, 3}).
keyreplace(_, _, _, _) -> erlang:error({not_implemented, keyreplace, 4}).
keysearch(_, _, _) -> erlang:error({not_implemented, keysearch, 3}).
%% A stable sort by the N-th element of tuples (step 41O2).
keysort(N, List) when is_integer(N), N > 0 ->
    lists:sort(fun(A, B) -> element(N, A) =< element(N, B) end, List).
keystore(_, _, _, _) -> erlang:error({not_implemented, keystore, 4}).
keytake(_, _, _) -> erlang:error({not_implemented, keytake, 3}).
last(_) -> erlang:error({not_implemented, last, 1}).
max(_) -> erlang:error({not_implemented, max, 1}).
merge(_) -> erlang:error({not_implemented, merge, 1}).
merge(_, _) -> erlang:error({not_implemented, merge, 2}).
merge3(_, _, _) -> erlang:error({not_implemented, merge3, 3}).
min(_) -> erlang:error({not_implemented, min, 1}).
nthtail(_, _) -> erlang:error({not_implemented, nthtail, 2}).
%% Whether List starts with Prefix; function_clause unless both are lists (step 41L).
prefix([X | PrefixTail], [X | Tail]) -> prefix(PrefixTail, Tail);
prefix([], List) when is_list(List) -> true;
prefix([_ | _], List) when is_list(List) -> false.
rkeymerge(_, _, _) -> erlang:error({not_implemented, rkeymerge, 3}).
rmerge(_, _) -> erlang:error({not_implemented, rmerge, 2}).
rmerge(_, _, _) -> erlang:error({not_implemented, rmerge, 3}).
rmerge3(_, _, _) -> erlang:error({not_implemented, rmerge3, 3}).
rukeymerge(_, _, _) -> erlang:error({not_implemented, rukeymerge, 3}).
rumerge(_, _) -> erlang:error({not_implemented, rumerge, 2}).
rumerge(_, _, _) -> erlang:error({not_implemented, rumerge, 3}).
rumerge3(_, _, _) -> erlang:error({not_implemented, rumerge3, 3}).
search(_, _) -> erlang:error({not_implemented, search, 2}).
%% A stable merge sort by Fun(A, B), true when A sorts before or with B; maps:iterator/2 needs it (step 41N).
sort(Fun, List) when is_function(Fun, 2) ->
    case List of
        [_, _ | _] -> merge_sort(Fun, List, length(List));
        [_] -> List;
        [] -> List
    end.

merge_sort(_, [Head | _], 1) ->
    [Head];
merge_sort(Fun, List, Length) ->
    Half = Length div 2,
    {Front, Back} = lists:split(Half, List),
    sort_merge(Fun, merge_sort(Fun, Front, Half), merge_sort(Fun, Back, Length - Half)).

sort_merge(Fun, [A | As] = Left, [B | Bs] = Right) ->
    case Fun(A, B) of
        true -> [A | sort_merge(Fun, As, Right)];
        false -> [B | sort_merge(Fun, Left, Bs)]
    end;
sort_merge(_, [], Right) ->
    Right;
sort_merge(_, Left, []) ->
    Left.
%% The first N elements and the rest; badarg when the list is shorter (step 41N).
split(N, List) when is_integer(N), N >= 0, is_list(List) ->
    case split_at(N, List, []) of
        {_, _} = Result -> Result;
        badarg -> erlang:error(badarg, [N, List])
    end;
split(N, List) ->
    erlang:error(badarg, [N, List]).

split_at(0, Rest, Front) -> {lists:reverse(Front), Rest};
split_at(N, [Head | Tail], Front) -> split_at(N - 1, Tail, [Head | Front]);
split_at(_, _, _) -> badarg.
sublist(_, _) -> erlang:error({not_implemented, sublist, 2}).
sublist(_, _, _) -> erlang:error({not_implemented, sublist, 3}).
subtract(_, _) -> erlang:error({not_implemented, subtract, 2}).
%% Whether List ends with Suffix (step 41L); nthtail/2 is a builtin, so the call is qualified.
suffix(Suffix, List) ->
    Delta = length(List) - length(Suffix),
    Delta >= 0 andalso lists:nthtail(Delta, List) =:= Suffix.
sum(_) -> erlang:error({not_implemented, sum, 1}).
takewhile(_, _) -> erlang:error({not_implemented, takewhile, 2}).
ukeymerge(_, _, _) -> erlang:error({not_implemented, ukeymerge, 3}).
ukeysort(_, _) -> erlang:error({not_implemented, ukeysort, 2}).
umerge(_) -> erlang:error({not_implemented, umerge, 1}).
umerge(_, _) -> erlang:error({not_implemented, umerge, 2}).
umerge(_, _, _) -> erlang:error({not_implemented, umerge, 3}).
umerge3(_, _, _) -> erlang:error({not_implemented, umerge3, 3}).
%% The list without repeated elements, first occurrences kept in order; erl_features needs it (step 41L).
uniq(List) -> uniq_1(List, #{}).

uniq_1([Head | Tail], Seen) ->
    case is_map_key(Head, Seen) of
        true -> uniq_1(Tail, Seen);
        false -> [Head | uniq_1(Tail, Seen#{Head => true})]
    end;
uniq_1([], _) ->
    [].

%% The list without elements whose Fun key repeats an earlier one.
uniq(Fun, List) when is_function(Fun, 1) -> uniq_2(List, Fun, #{}).

uniq_2([Head | Tail], Fun, Seen) ->
    Key = Fun(Head),
    case is_map_key(Key, Seen) of
        true -> uniq_2(Tail, Fun, Seen);
        false -> [Head | uniq_2(Tail, Fun, Seen#{Key => true})]
    end;
uniq_2([], _, _) ->
    [].
unzip(_) -> erlang:error({not_implemented, unzip, 1}).
unzip3(_) -> erlang:error({not_implemented, unzip3, 1}).
usort(_) -> erlang:error({not_implemented, usort, 1}).
usort(_, _) -> erlang:error({not_implemented, usort, 2}).
zf(Fun, List) -> filtermap(Fun, List).
zip(_, _) -> erlang:error({not_implemented, zip, 2}).
zip(_, _, _) -> erlang:error({not_implemented, zip, 3}).
zip3(_, _, _) -> erlang:error({not_implemented, zip3, 3}).
zip3(_, _, _, _) -> erlang:error({not_implemented, zip3, 4}).
zipwith(_, _, _) -> erlang:error({not_implemented, zipwith, 3}).
zipwith(_, _, _, _) -> erlang:error({not_implemented, zipwith, 4}).
zipwith3(_, _, _, _) -> erlang:error({not_implemented, zipwith3, 4}).
zipwith3(_, _, _, _, _) -> erlang:error({not_implemented, zipwith3, 5}).
