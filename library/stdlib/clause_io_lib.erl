-module(clause_io_lib).
-export([sort_maps/6, apply_ordered/4]).

%% maps_order comparison funs (plan 21 step LIB4F, docs/builtins.md#io-lib): an io_lib or io builtin that wrote maps
%% ordered by funs continues here with the maps it met. Their keys are sorted as maps:iterator/2 sorts them, and the
%% call runs again with every sorted key list so far. Badarg is true where the call's errors become badarg. The
%% module calls builtins only, since every program that formats text includes it.
sort_maps(Missing, Known, Module, Function, Args, Badarg) ->
    Sorted = sorted(Missing, Badarg),
    clause_io_lib:apply_ordered(Sorted ++ Known, Module, Function, Args).

sorted(Missing, false) ->
    [{Fun, Map, sort(Fun, keys(Map))} || {Fun, Map} <- Missing];
sorted(Missing, true) ->
    try
        sorted(Missing, false)
    catch
        _:_ -> erlang:error(badarg)
    end.

%% Run the builtin Module:Function(Args) with the sorted keys [{Fun, Map, Keys}]; a runtime builtin.
apply_ordered(_, _, _, _) -> clause_builtin_placeholder.

%% The keys of a map in its key order, as maps:keys/1 lists them, from the {K1, V1, ...} tuple of its entries.
keys(Map) ->
    Entries = clause_maps:entries(Map),
    keys(Entries, tuple_size(Entries) - 1, []).

keys(Entries, At, Keys) when At > 0 -> keys(Entries, At - 2, [element(At, Entries) | Keys]);
keys(_, _, Keys) -> Keys.

%% lists:sort/2's stable merge sort: halves sorted and merged, the left element first when Fun says it goes first.
sort(Fun, [_, _ | _] = List) -> merge_sort(Fun, List, length(List));
sort(_, List) -> List.

merge_sort(_, [Head | _], 1) ->
    [Head];
merge_sort(Fun, List, Length) ->
    Half = Length div 2,
    {Front, Back} = split(Half, List, []),
    merge(Fun, merge_sort(Fun, Front, Half), merge_sort(Fun, Back, Length - Half)).

split(0, Rest, Front) -> {reverse(Front, []), Rest};
split(N, [Head | Rest], Front) -> split(N - 1, Rest, [Head | Front]).

reverse([Head | Rest], Acc) -> reverse(Rest, [Head | Acc]);
reverse([], Acc) -> Acc.

merge(Fun, [A | As] = Left, [B | Bs] = Right) ->
    case Fun(A, B) of
        true -> [A | merge(Fun, As, Right)];
        false -> [B | merge(Fun, Left, Bs)]
    end;
merge(_, [], Right) ->
    Right;
merge(_, Left, []) ->
    Left.
