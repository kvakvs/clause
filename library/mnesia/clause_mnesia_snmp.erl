%% mnesia's SNMP index (plan 21 step MIM1G, docs/libraries/mnesia.md#snmp): a table with an snmp structure,
%% [{key, Types}], keeps an ordered_set of {RowIndex, Key} in mnesia_gvar as {Tab, {index, snmp}}, as OTP's
%% mnesia_snmp_hook does, so that its rows can be found and walked by their SNMP indexes. The key types are integer,
%% string (its length first) and fix_string, alone or in a tuple.
-module(clause_mnesia_snmp).
-export([check_ustruct/1, sync/2, drop/1, check/2, update/3, key_to_oid/3, oid_to_key/2]).
-export([get_row/2, get_next_index/2, get_mnesia_key/2]).

-include("clause_mnesia.hrl").

% Whether an snmp structure is valid: none, or the key's types.
check_ustruct([]) -> true;
check_ustruct([{key, Types}]) when is_tuple(Types) -> lists:all(fun snmp_type/1, tuple_to_list(Types));
check_ustruct([{key, Type}]) -> snmp_type(Type);
check_ustruct(_) -> false.

snmp_type(T) -> lists:member(T, [integer, string, fix_string]).

% A table's index as its definition and replica here want it: built from the records, or gone (in mnesia_monitor).
sync(#cstruct{name = Tab, snmp = Snmp}, Storage) ->
    case {Snmp =/= [] andalso Storage =/= unknown, ets:lookup(?GVAR, {Tab, {index, snmp}})} of
        {true, []} -> build(Tab, Snmp);
        {false, [_]} -> drop(Tab);
        _ -> ok
    end.

build(Tab, [{key, Types}]) ->
    Index = ets:new(mnesia_snmp_hook, [public, ordered_set]),
    [ets:insert(Index, {key_to_oid_i(K, Types), K}) || K <- keys(Tab)],
    ets:insert(?GVAR, {{Tab, {index, snmp}}, Index}),
    ok.

keys(Tab) ->
    lists:usort([element(2, R) || R <- clause_mnesia_db:match_object(Tab, '_')]).

drop(Tab) ->
    case ets:lookup(?GVAR, {Tab, {index, snmp}}) of
        [{_, Index}] ->
            ets:delete(Index),
            ets:delete(?GVAR, {Tab, {index, snmp}});
        [] ->
            ok
    end.

% A key written to a table with an index must make a row index; an update aborts before it changes anything otherwise.
check(Tab, Key) ->
    case snmp(Tab) of
        [] -> ok;
        _ when Key =:= '_' -> ok;
        Snmp -> _ = key_to_oid(Tab, Key, Snmp)
    end,
    ok.

% The index follows an update: a written key enters it, a deleted key or record leaves it, a cleared table empties it;
% counters do not change it, as in OTP.
update(Tab, Key, Update) ->
    case ets:lookup(?GVAR, {Tab, {index, snmp}}) of
        [{_, Index}] -> indexed(Index, Tab, Key, Update);
        [] -> ok
    end.

indexed(Index, _, _, {clear_table, _}) -> ets:delete_all_objects(Index);
indexed(_, _, _, {update_counter, _, _}) -> ok;
indexed(Index, Tab, Key, {write, _}) -> ets:insert(Index, {key_to_oid(Tab, Key, snmp(Tab)), Key});
indexed(Index, Tab, Key, _) -> ets:delete(Index, key_to_oid(Tab, Key, snmp(Tab))).

snmp(Tab) ->
    try
        ets:lookup_element(?GVAR, {Tab, snmp}, 2)
    catch
        error:_ -> []
    end.

%% Keys and row indexes.

key_to_oid(Tab, Key, [{key, Types}]) ->
    try
        key_to_oid_i(Key, Types)
    catch
        _:_ -> mnesia:abort({bad_snmp_key, {Tab, Key}, Types})
    end.

key_to_oid_i(Key, integer) when is_integer(Key) -> [Key];
key_to_oid_i(Key, fix_string) when is_list(Key) -> Key;
key_to_oid_i(Key, string) when is_list(Key) -> [length(Key) | Key];
key_to_oid_i(Key, Types) when is_tuple(Key) ->
    lists:append([key_to_oid_i(element(I, Key), element(I, Types)) || I <- lists:seq(1, tuple_size(Key))]).

% A row index's key, or unknown when a fix_string inside a tuple hides where the parts end.
oid_to_key(Oid, Tab) ->
    [{key, Types}] = clause_mnesia_monitor:val({Tab, snmp}),
    to_key(Types, Oid).

to_key(integer, [Key]) ->
    Key;
to_key(fix_string, Key) ->
    Key;
to_key(string, [_ | Key]) ->
    Key;
to_key(Types, Oid) ->
    try
        list_to_tuple(parts(tuple_to_list(Types), Oid))
    catch
        _:_ -> unknown
    end.

parts([integer | Types], [Key | Oid]) ->
    [Key | parts(Types, Oid)];
parts([fix_string], Oid) ->
    [Oid];
parts([string | Types], [Length | Oid]) ->
    {Key, Rest} = lists:split(Length, Oid),
    [Key | parts(Types, Rest)];
parts([], []) ->
    [].

%% The index's lookups, run where the table is read.

get_row(Tab, RowIndex) ->
    case ets:lookup(index(Tab), RowIndex) of
        [{_, Key}] ->
            [Row] = mnesia:dirty_read({Tab, Key}),
            {ok, Row};
        [] ->
            undefined
    end.

% {NextRowIndex, Key} after RowIndex, or {endOfTable, undefined}.
get_next_index(Tab, RowIndex) ->
    Index = index(Tab),
    case ets:next(Index, RowIndex) of
        '$end_of_table' -> {endOfTable, undefined};
        Next -> hd(ets:lookup(Index, Next))
    end.

get_mnesia_key(Tab, RowIndex) ->
    case ets:lookup(index(Tab), RowIndex) of
        [{_, Key}] -> {ok, Key};
        [] -> undefined
    end.

index(Tab) ->
    clause_mnesia_monitor:val({Tab, {index, snmp}}).
