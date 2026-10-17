%% Clause's array (plan 21 step LIB3B, docs/builtins.md#collections): OTP 29's #array{} record over a tuple tree.
%% The record operations are runtime builtins with clause_builtin_placeholder bodies; the functions taking funs are
%% Erlang over the private clause_array builtins, which read and rebuild the tree as OTP's folds and mapfolds do.
-module(array).
-export([
    append/2,
    concat/1,
    concat/2,
    default/1,
    fix/1,
    foldl/3,
    foldl/5,
    foldr/3,
    foldr/5,
    from/2,
    from/3,
    from_list/1,
    from_list/2,
    from_orddict/1,
    from_orddict/2,
    get/2,
    is_array/1,
    is_fix/1,
    map/2,
    mapfoldl/3,
    mapfoldl/5,
    mapfoldr/3,
    mapfoldr/5,
    new/0,
    new/1,
    new/2,
    prepend/2,
    relax/1,
    reset/2,
    resize/1,
    resize/2,
    set/3,
    shift/2,
    size/1,
    slice/3,
    sparse_foldl/3,
    sparse_foldl/5,
    sparse_foldr/3,
    sparse_foldr/5,
    sparse_map/2,
    sparse_mapfoldl/3,
    sparse_mapfoldl/5,
    sparse_mapfoldr/3,
    sparse_mapfoldr/5,
    sparse_size/1,
    sparse_to_list/1,
    sparse_to_orddict/1,
    to_list/1,
    to_orddict/1
]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/library.md#runtime-module-replacements).
-export([
    upgrade/1
]).
-include("array.types.hrl").

append(_, _) -> clause_builtin_placeholder.
from_list(_) -> clause_builtin_placeholder.
from_list(_, _) -> clause_builtin_placeholder.
from_orddict(_) -> clause_builtin_placeholder.
from_orddict(_, _) -> clause_builtin_placeholder.
get(_, _) -> clause_builtin_placeholder.
new() -> clause_builtin_placeholder.
new(_) -> clause_builtin_placeholder.
new(_, _) -> clause_builtin_placeholder.
prepend(_, _) -> clause_builtin_placeholder.
reset(_, _) -> clause_builtin_placeholder.
resize(_) -> clause_builtin_placeholder.
resize(_, _) -> clause_builtin_placeholder.
set(_, _, _) -> clause_builtin_placeholder.
shift(_, _) -> clause_builtin_placeholder.
slice(_, _, _) -> clause_builtin_placeholder.
sparse_size(_) -> clause_builtin_placeholder.
sparse_to_list(_) -> clause_builtin_placeholder.
sparse_to_orddict(_) -> clause_builtin_placeholder.
to_list(_) -> clause_builtin_placeholder.
to_orddict(_) -> clause_builtin_placeholder.
upgrade(_) -> erlang:error({not_implemented, upgrade, 1}).

%% Whether a term is an array.
is_array(#array{size = Size}) when is_integer(Size) -> true;
is_array(_) -> false.

%% The number of entries, and the default value.
size(#array{size = Size}) -> Size;
size(_) -> erlang:error(badarg).

default(#array{default = Default}) -> Default;
default(_) -> erlang:error(badarg).

%% The array fixed, whether it is, and the array relaxed.
fix(#array{} = Array) -> Array#array{fix = true}.

is_fix(#array{fix = true}) -> true;
is_fix(#array{}) -> false.

relax(#array{size = Size} = Array) when is_integer(Size), Size >= 0 -> Array#array{fix = false}.

%% The entries of Left followed by those of Right: Left's prepended to a longer Right of the same default, else
%% Right's appended to Left.
concat(
    #array{size = LeftSize, fix = Fix, default = Default} = Left,
    #array{size = RightSize, default = Other} = Right
) ->
    case RightSize > LeftSize andalso Default =:= Other of
        true ->
            foldr(
                fun(_, Value, Acc) -> array:prepend(Value, Acc) end, Right#array{fix = Fix}, Left
            );
        false ->
            foldl(fun(_, Value, Acc) -> array:append(Value, Acc) end, Left, Right)
    end;
concat(_, _) ->
    erlang:error(badarg).

concat([First | Rest]) -> lists:foldl(fun(Array, Acc) -> concat(Acc, Array) end, First, Rest);
concat(_) -> erlang:error(badarg).

%% The array of the values Fun generates from State until it returns done.
from(Fun, State) -> from(Fun, State, undefined).

from(Fun, State, Default) when is_function(Fun, 1) ->
    clause_array:from_values(generated(Fun, Fun(State)), Default);
from(_, _, _) ->
    erlang:error(badarg).

generated(Fun, {Value, State}) -> [Value | generated(Fun, Fun(State))];
generated(_, done) -> [];
generated(_, _) -> erlang:error(badarg).

%% Every value replaced by Fun(Index, Value); the sparse form skips the defaults.
map(Fun, Array) when is_function(Fun, 2) ->
    {Mapped, _} = mapfoldl(fun(Index, Value, Acc) -> {Fun(Index, Value), Acc} end, [], Array),
    Mapped;
map(_, _) ->
    erlang:error(badarg).

sparse_map(Fun, Array) when is_function(Fun, 2) ->
    {Mapped, _} = sparse_mapfoldl(
        fun(Index, Value, Acc) -> {Fun(Index, Value), Acc} end, [], Array
    ),
    Mapped;
sparse_map(_, _) ->
    erlang:error(badarg).

%% Fun(Index, Value, Acc) over every entry, or the entries Low..High, upwards (foldl) or downwards (foldr); the
%% sparse forms skip the defaults.
foldl(Fun, Acc, #array{size = Size} = Array) when is_integer(Size) ->
    foldl(0, Size - 1, Fun, Acc, Array);
foldl(_, _, _) ->
    erlang:error(badarg).

foldl(Low, High, Fun, Acc, Array) when is_function(Fun, 3) ->
    up(Fun, Acc, Low, clause_array:values(Array, Low, High));
foldl(_, _, _, _, _) ->
    erlang:error(badarg).

foldr(Fun, Acc, #array{size = Size} = Array) when is_integer(Size) ->
    foldr(0, Size - 1, Fun, Acc, Array);
foldr(_, _, _) ->
    erlang:error(badarg).

foldr(Low, High, Fun, Acc, Array) when is_function(Fun, 3) ->
    down(Fun, Acc, High, lists:reverse(clause_array:values(Array, Low, High)));
foldr(_, _, _, _, _) ->
    erlang:error(badarg).

sparse_foldl(Fun, Acc, #array{size = Size} = Array) when is_integer(Size) ->
    sparse_foldl(0, Size - 1, Fun, Acc, Array);
sparse_foldl(_, _, _) ->
    erlang:error(badarg).

sparse_foldl(Low, High, Fun, Acc, Array) when is_function(Fun, 3) ->
    lists:foldl(
        fun({Index, Value}, A) -> Fun(Index, Value, A) end,
        Acc,
        clause_array:sparse(Array, Low, High)
    );
sparse_foldl(_, _, _, _, _) ->
    erlang:error(badarg).

sparse_foldr(Fun, Acc, #array{size = Size} = Array) when is_integer(Size) ->
    sparse_foldr(0, Size - 1, Fun, Acc, Array);
sparse_foldr(_, _, _) ->
    erlang:error(badarg).

sparse_foldr(Low, High, Fun, Acc, Array) when is_function(Fun, 3) ->
    lists:foldr(
        fun({Index, Value}, A) -> Fun(Index, Value, A) end,
        Acc,
        clause_array:sparse(Array, Low, High)
    );
sparse_foldr(_, _, _, _, _) ->
    erlang:error(badarg).

up(Fun, Acc, Index, [Value | Values]) -> up(Fun, Fun(Index, Value, Acc), Index + 1, Values);
up(_, Acc, _, []) -> Acc.

down(Fun, Acc, Index, [Value | Values]) -> down(Fun, Fun(Index, Value, Acc), Index - 1, Values);
down(_, Acc, _, []) -> Acc.

%% {Array2, Acc}: every value, or the values Low..High, replaced by the first element of Fun(Index, Value, Acc),
%% visited upwards (mapfoldl) or downwards (mapfoldr); the sparse forms leave the defaults alone.
mapfoldl(Fun, Acc, #array{size = Size} = Array) when is_integer(Size) ->
    mapfoldl(0, Size - 1, Fun, Acc, Array);
mapfoldl(_, _, _) ->
    erlang:error(badarg).

mapfoldl(Low, High, Fun, Acc, Array) when is_function(Fun, 3) ->
    {Mapped, Acc1} = map_up(Fun, Acc, Low, clause_array:values(Array, Low, High), []),
    {rebuilt(Array, Low, High, lists:reverse(Mapped), false), Acc1};
mapfoldl(_, _, _, _, _) ->
    erlang:error(badarg).

mapfoldr(Fun, Acc, #array{size = Size} = Array) when is_integer(Size) ->
    mapfoldr(0, Size - 1, Fun, Acc, Array);
mapfoldr(_, _, _) ->
    erlang:error(badarg).

mapfoldr(Low, High, Fun, Acc, Array) when is_function(Fun, 3) ->
    {Mapped, Acc1} = map_down(
        Fun, Acc, High, lists:reverse(clause_array:values(Array, Low, High)), []
    ),
    {rebuilt(Array, Low, High, Mapped, false), Acc1};
mapfoldr(_, _, _, _, _) ->
    erlang:error(badarg).

sparse_mapfoldl(Fun, Acc, #array{size = Size} = Array) when is_integer(Size) ->
    sparse_mapfoldl(0, Size - 1, Fun, Acc, Array);
sparse_mapfoldl(_, _, _) ->
    erlang:error(badarg).

sparse_mapfoldl(Low, High, Fun, Acc, Array) when is_function(Fun, 3) ->
    Pairs = clause_array:sparse(Array, Low, High),
    {Mapped, Acc1} = map_pairs(Fun, Acc, Pairs, []),
    {rebuilt(Array, Low, High, lists:reverse(Mapped), true), Acc1};
sparse_mapfoldl(_, _, _, _, _) ->
    erlang:error(badarg).

sparse_mapfoldr(Fun, Acc, #array{size = Size} = Array) when is_integer(Size) ->
    sparse_mapfoldr(0, Size - 1, Fun, Acc, Array);
sparse_mapfoldr(_, _, _) ->
    erlang:error(badarg).

sparse_mapfoldr(Low, High, Fun, Acc, Array) when is_function(Fun, 3) ->
    Pairs = clause_array:sparse(Array, Low, High),
    {Mapped, Acc1} = map_pairs(Fun, Acc, lists:reverse(Pairs), []),
    {rebuilt(Array, Low, High, Mapped, true), Acc1};
sparse_mapfoldr(_, _, _, _, _) ->
    erlang:error(badarg).

%% The new values (last first) and the accumulator of Fun over values from Index up or down.
map_up(Fun, Acc, Index, [Value | Values], Mapped) ->
    {New, Acc1} = Fun(Index, Value, Acc),
    map_up(Fun, Acc1, Index + 1, Values, [New | Mapped]);
map_up(_, Acc, _, [], Mapped) ->
    {Mapped, Acc}.

map_down(Fun, Acc, Index, [Value | Values], Mapped) ->
    {New, Acc1} = Fun(Index, Value, Acc),
    map_down(Fun, Acc1, Index - 1, Values, [New | Mapped]);
map_down(_, Acc, _, [], Mapped) ->
    {Mapped, Acc}.

map_pairs(Fun, Acc, [{Index, Value} | Pairs], Mapped) ->
    {New, Acc1} = Fun(Index, Value, Acc),
    map_pairs(Fun, Acc1, Pairs, [New | Mapped]);
map_pairs(_, Acc, [], Mapped) ->
    {Mapped, Acc}.

%% The array with the new values of Low..High in index order; unchanged for an empty range.
rebuilt(Array, Low, High, Values, Sparse) when Low =< High ->
    clause_array:replace(Array, {Low, High}, Values, Sparse);
rebuilt(Array, _, _, _, _) ->
    Array.
