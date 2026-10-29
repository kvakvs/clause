%% Project-owned subset of the lists module (plan 11 step 39): original
%% implementations whose results and error reasons follow OTP's documented
%% behavior. Compiled from source into every program that calls it.
-module(lists).
-export([
    append/1,
    append/2,
    filter/2,
    flatmap/2,
    foldl/3,
    foldr/3,
    foreach/2,
    keyfind/3,
    keymap/3,
    map/2,
    mapfoldl/3,
    mapfoldr/3,
    member/2,
    nth/2,
    partition/2,
    reverse/1,
    reverse/2,
    search/2,
    seq/2,
    seq/3,
    sort/1,
    splitwith/2,
    subtract/2,
    sum/1,
    umerge/3,
    usort/2,
    zipwith/3,
    zipwith/4,
    zipwith3/4,
    zipwith3/5
]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/libraries/library.md#runtime-module-replacements).
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
    flatten/1,
    flatten/2,
    join/2,
    keydelete/3,
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
    sort/2,
    split/2,
    sublist/2,
    sublist/3,
    suffix/2,
    takewhile/2,
    ukeymerge/3,
    ukeysort/2,
    umerge/1,
    umerge/2,
    umerge3/3,
    uniq/1,
    uniq/2,
    unzip/1,
    unzip3/1,
    usort/1,
    zf/2,
    zip/2,
    zip/3,
    zip3/3,
    zip3/4
]).
-deprecated([{zf, 2, "use filtermap/2 instead"}]).

%% The concatenation of a list of lists.
append([Last]) -> Last;
append([First | Rest]) -> First ++ append(Rest);
append([]) -> [].

%% Two lists joined: List1 ++ List2.
append(List1, List2) -> List1 ++ List2.

%% The elements for which Pred returns true; anything else from Pred is an error.
filter(Pred, List) when is_function(Pred, 1) ->
    [Element || Element <- List, Pred(Element)].

%% Whether Pred is true for every element (re's global matches and splits, plan step 41ZB); a non-list is a
%% case_clause error, as in OTP.
all(Pred, List) when is_function(Pred, 1) ->
    case List of
        [Element | Rest] -> all_rest(Pred, [Element | Rest]);
        [] -> true
    end.

all_rest(Pred, [Element | Rest]) ->
    case Pred(Element) of
        true -> all_rest(Pred, Rest);
        false -> false
    end;
all_rest(_, []) ->
    true.

%% Whether Pred is true for some element.
any(Pred, List) when is_function(Pred, 1) ->
    case List of
        [Element | Rest] -> any_rest(Pred, [Element | Rest]);
        [] -> false
    end.

any_rest(Pred, [Element | Rest]) ->
    case Pred(Element) of
        true -> true;
        false -> any_rest(Pred, Rest)
    end;
any_rest(_, []) ->
    false.

%% The list from the first element for which Pred is false.
dropwhile(Pred, List) when is_function(Pred, 1) -> drop_while(Pred, List).

drop_while(Pred, [Element | Rest] = List) ->
    case Pred(Element) of
        true -> drop_while(Pred, Rest);
        false -> List
    end;
drop_while(_, []) ->
    [].

%% The elements before the first one for which Pred is false.
takewhile(Pred, List) when is_function(Pred, 1) -> take_while(Pred, List).

take_while(Pred, [Element | Rest]) ->
    case Pred(Element) of
        true -> [Element | take_while(Pred, Rest)];
        false -> []
    end;
take_while(_, []) ->
    [].

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
% Sum of a list of numbers, added in order as OTP does: badarith for a non-number, function_clause for an improper
% list (OPT0.9 bit_integers fixture).
sum(List) -> sum(List, 0).

sum([Head | Tail], Sum) -> sum(Tail, Sum + Head);
sum([], Sum) -> Sum.

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

sort(_) -> clause_builtin_placeholder.

concat(_) -> clause_builtin_placeholder.
delete(_, _) -> clause_builtin_placeholder.
droplast(_) -> clause_builtin_placeholder.
duplicate(_, _) -> clause_builtin_placeholder.
enumerate(_) -> clause_builtin_placeholder.
enumerate(_, _) -> clause_builtin_placeholder.
enumerate(_, _, _) -> clause_builtin_placeholder.
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
flatlength(_) -> clause_builtin_placeholder.
%% Fun applied to every element, the lists it returns appended in order.
flatmap(Fun, List) when is_function(Fun, 1) -> flatmap_onto(Fun, List, []).

flatmap_onto(Fun, [Element | Rest], Done) ->
    flatmap_onto(Fun, Rest, lists:reverse(Fun(Element), Done));
flatmap_onto(_, [], Done) ->
    lists:reverse(Done).
flatten(_) -> clause_builtin_placeholder.
flatten(_, _) -> clause_builtin_placeholder.
join(_, _) -> clause_builtin_placeholder.
keydelete(_, _, _) -> clause_builtin_placeholder.
%% Every tuple with its N-th element replaced by Fun of that element.
keymap(Fun, N, List) when is_function(Fun, 1), is_integer(N), N >= 1 -> keymap_each(Fun, N, List).

keymap_each(Fun, N, [Tuple | Rest]) ->
    [setelement(N, Tuple, Fun(element(N, Tuple))) | keymap_each(Fun, N, Rest)];
keymap_each(_, _, []) ->
    [].
keymember(_, _, _) -> clause_builtin_placeholder.
keymerge(_, _, _) -> clause_builtin_placeholder.
keyreplace(_, _, _, _) -> clause_builtin_placeholder.
keysearch(_, _, _) -> clause_builtin_placeholder.
%% A stable sort by the N-th element of tuples (step 41O2).
keysort(N, List) when is_integer(N), N > 0 ->
    lists:sort(fun(A, B) -> element(N, A) =< element(N, B) end, List).
keystore(_, _, _, _) -> clause_builtin_placeholder.
keytake(_, _, _) -> clause_builtin_placeholder.
last(_) -> clause_builtin_placeholder.
max(_) -> clause_builtin_placeholder.
merge(_) -> clause_builtin_placeholder.
merge(_, _) -> clause_builtin_placeholder.
%% Merge List1 and List2, both sorted by Fun(A, B) (true when A sorts before or with B); on ties List1 comes first.
merge(Fun, [A | As] = List1, [B | Bs] = List2) when is_function(Fun, 2) ->
    case Fun(A, B) of
        true -> [A | merge(Fun, As, List2)];
        false -> [B | merge(Fun, List1, Bs)]
    end;
merge(Fun, [], List2) when is_function(Fun, 2), is_list(List2) ->
    List2;
merge(Fun, List1, []) when is_function(Fun, 2), is_list(List1) ->
    List1.
merge3(_, _, _) -> clause_builtin_placeholder.
min(_) -> clause_builtin_placeholder.
nthtail(_, _) -> clause_builtin_placeholder.
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
%% {value, Element} for the first element Pred holds for, or false; anything else from Pred is a case_clause error,
%% as in OTP.
search(Pred, List) when is_function(Pred, 1) -> search_1(Pred, List).

search_1(Pred, [Element | Rest]) ->
    case Pred(Element) of
        true -> {value, Element};
        false -> search_1(Pred, Rest)
    end;
search_1(_, []) ->
    false.
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
sublist(_, _) -> clause_builtin_placeholder.
sublist(_, _, _) -> clause_builtin_placeholder.
%% List1 -- List2.
subtract(List1, List2) -> List1 -- List2.
%% Whether List ends with Suffix (step 41L); nthtail/2 is a builtin, so the call is qualified.
suffix(Suffix, List) ->
    Delta = length(List) - length(Suffix),
    Delta >= 0 andalso lists:nthtail(Delta, List) =:= Suffix.
ukeymerge(_, _, _) -> clause_builtin_placeholder.
ukeysort(_, _) -> clause_builtin_placeholder.
umerge(_) -> clause_builtin_placeholder.
umerge(_, _) -> clause_builtin_placeholder.
%% Merge List1 and List2, both sorted by Fun and free of duplicates; of two elements comparing equal (Fun true both
%% ways) the one from List1 stays.
umerge(Fun, List1, List2) when is_function(Fun, 2) -> unique_merge(Fun, List1, List2).

unique_merge(Fun, [A | As] = Left, [B | Bs] = Right) ->
    case Fun(A, B) of
        true ->
            Others =
                case Fun(B, A) of
                    true -> Bs;
                    false -> Right
                end,
            [A | unique_merge(Fun, As, Others)];
        false ->
            [B | unique_merge(Fun, Left, Bs)]
    end;
unique_merge(_, [], Right) when is_list(Right) ->
    Right;
unique_merge(_, Left, []) when is_list(Left) ->
    Left.
umerge3(_, _, _) -> clause_builtin_placeholder.
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
unzip(_) -> clause_builtin_placeholder.
unzip3(_) -> clause_builtin_placeholder.
usort(_) -> clause_builtin_placeholder.
%% Sorted by Fun, keeping the first of the elements comparing equal (Fun true both ways).
usort(Fun, List) when is_function(Fun, 2) -> drop_equal(Fun, sort(Fun, List)).

drop_equal(Fun, [Kept, Next | Rest]) ->
    case Fun(Next, Kept) of
        true -> drop_equal(Fun, [Kept | Rest]);
        false -> [Kept | drop_equal(Fun, [Next | Rest])]
    end;
drop_equal(_, Short) ->
    Short.
%% filtermap/2 under its old, undocumented name, deprecated as in OTP.
zf(Fun, List) -> filtermap(Fun, List).
zip(_, _) -> clause_builtin_placeholder.
zip(_, _, _) -> clause_builtin_placeholder.
zip3(_, _, _) -> clause_builtin_placeholder.
zip3(_, _, _, _) -> clause_builtin_placeholder.
%% Combine applied to the elements at each position, the lengths treated as zip/3 and zip3/4 say. Like OTP's, they
%% call Combine while walking, so a length mismatch fails after the leading elements were combined.
zipwith(Combine, List1, List2) -> zipwith(Combine, List1, List2, fail).

zipwith(Combine, [X | Xs], [Y | Ys], How) ->
    [Combine(X, Y) | zipwith(Combine, Xs, Ys, How)];
zipwith(Combine, [], [], fail) when is_function(Combine, 2) ->
    [];
zipwith(Combine, [], [], trim) when is_function(Combine, 2) ->
    [];
zipwith(Combine, [], [], {pad, {_, _}}) when is_function(Combine, 2) ->
    [];
zipwith(Combine, [_ | _], [], trim) when is_function(Combine, 2) ->
    [];
zipwith(Combine, [], [_ | _], trim) when is_function(Combine, 2) ->
    [];
zipwith(Combine, [], [_ | _] = Ys, {pad, {X, _}}) ->
    [Combine(X, Y) || Y <- Ys];
zipwith(Combine, [_ | _] = Xs, [], {pad, {_, Y}}) ->
    [Combine(X, Y) || X <- Xs].

zipwith3(Combine, List1, List2, List3) -> zipwith3(Combine, List1, List2, List3, fail).

zipwith3(Combine, [X | Xs], [Y | Ys], [Z | Zs], How) ->
    [Combine(X, Y, Z) | zipwith3(Combine, Xs, Ys, Zs, How)];
zipwith3(Combine, [], [], [], fail) when is_function(Combine, 3) ->
    [];
zipwith3(Combine, Xs, Ys, Zs, trim) when is_function(Combine, 3), is_list(Xs), is_list(Ys), is_list(Zs) ->
    [];
zipwith3(Combine, [], [], [], {pad, {_, _, _}}) when is_function(Combine, 3) ->
    [];
zipwith3(Combine, [], [], [_ | _] = Zs, {pad, {X, Y, _}}) ->
    [Combine(X, Y, Z) || Z <- Zs];
zipwith3(Combine, [], [_ | _] = Ys, [], {pad, {X, _, Z}}) ->
    [Combine(X, Y, Z) || Y <- Ys];
zipwith3(Combine, [_ | _] = Xs, [], [], {pad, {_, Y, Z}}) ->
    [Combine(X, Y, Z) || X <- Xs];
zipwith3(Combine, [], [Y | Ys], [Z | Zs], {pad, {X, _, _}} = How) ->
    [Combine(X, Y, Z) | zipwith3(Combine, [], Ys, Zs, How)];
zipwith3(Combine, [X | Xs], [], [Z | Zs], {pad, {_, Y, _}} = How) ->
    [Combine(X, Y, Z) | zipwith3(Combine, Xs, [], Zs, How)];
zipwith3(Combine, [X | Xs], [Y | Ys], [], {pad, {_, _, Z}} = How) ->
    [Combine(X, Y, Z) | zipwith3(Combine, Xs, Ys, [], How)].
