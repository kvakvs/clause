%% mnesia's RAM indexes (plan 21 step MIM1A, docs/libraries/mnesia.md#indexes): one ordered_set ets table of
%% {{Value, Key}} per index position, owned by mnesia_monitor, kept on every write and delete of the table. A table's
%% index_info lists its indexes newest first, as OTP's does.
-module(clause_mnesia_index).
-export([add/2, drop/2, written/4, deleted/2, object_deleted/3, read/3, match_object/3, records/3]).

-include("clause_mnesia.hrl").

% A new index of a position, filled from the table's records (runs in mnesia_monitor).
add(Tab, {Pos, _Type}) ->
    Ixt = ets:new(mnesia_index, [ordered_set, public]),
    ets:foldl(fun(Record, ok) -> put_values(Ixt, Pos, Record, element(2, Record)) end, ok, Tab),
    set({Tab, {index, Pos}}, Ixt),
    Info = val({Tab, index_info}),
    store(Tab, Info#index{pos_list = [{{Pos, ordered}, {ram, Ixt}} | Info#index.pos_list]}).

% A position's index goes: the table of its last index and every index of the position from index_info.
drop(Tab, Pos) ->
    ets:delete(val({Tab, {index, Pos}})),
    Info = val({Tab, index_info}),
    store(Tab, Info#index{pos_list = [I || {{P, _}, _} = I <- Info#index.pos_list, P =/= Pos]}),
    ets:delete(?GVAR, {Tab, {index, Pos}}).

% index_info and what follows from it: the index positions and the commit work.
store(Tab, #index{pos_list = Positions} = Info) ->
    set({Tab, index_info}, Info),
    set({Tab, index}, [P || {{P, _}, _} <- Positions]),
    Work = lists:keydelete(index, 1, val({Tab, commit_work})),
    case Positions of
        [] -> set({Tab, commit_work}, Work);
        _ -> set({Tab, commit_work}, [Info | Work])
    end.

% A record written under Key; Old is the replaced records, or undefined to read them from the table first.
written(Tab, Key, Record, Old) ->
    case work(Tab) of
        none ->
            ok;
        #index{setorbag = bag, pos_list = Positions} ->
            [put_values(Ixt, Pos, Record, Key) || {{Pos, _}, {ram, Ixt}} <- Positions];
        #index{pos_list = Positions} ->
            [replace(Tab, Key, Record, Old, Index) || Index <- Positions]
    end,
    ok.

% A set table's record replaces the old ones: entries of changed values go, the new value's entry comes.
replace(Tab, Key, Record, Old0, {{Pos, _}, {ram, Ixt}}) ->
    Old =
        case Old0 of
            undefined -> ets:lookup(Tab, Key);
            _ -> Old0
        end,
    Value = element(Pos, Record),
    case [O || O <- Old, element(Pos, O) =/= Value] of
        [] when Old =/= [] -> ok;
        Changed ->
            [ets:delete(Ixt, {element(Pos, O), Key}) || O <- Changed],
            put_values(Ixt, Pos, Record, Key)
    end.

% The records under Key are deleted.
deleted(Tab, Key) ->
    case work(Tab) of
        none ->
            ok;
        #index{pos_list = Positions} ->
            Records = ets:lookup(Tab, Key),
            [ets:delete(Ixt, {element(Pos, R), Key}) || {{Pos, _}, {ram, Ixt}} <- Positions, R <- Records],
            ok
    end.

% One record under Key is deleted: a value's entry goes when no other record under Key holds the value.
object_deleted(Tab, Key, Record) ->
    case work(Tab) of
        none ->
            ok;
        #index{pos_list = Positions} ->
            Records = ets:lookup(Tab, Key),
            [
                ets:delete(Ixt, {element(Pos, Record), Key})
             || {{Pos, _}, {ram, Ixt}} <- Positions,
                [R || R <- Records, element(Pos, R) =:= element(Pos, Record)] =:= [Record]
            ],
            ok
    end.

put_values(Ixt, Pos, Record, Key) ->
    true = ets:insert(Ixt, {{element(Pos, Record), Key}}),
    ok.

% A table's index work; none without indexes.
work(Tab) ->
    case lists:keyfind(index, 1, val({Tab, commit_work})) of
        false -> none;
        Info -> Info
    end.

% dirty_index_read/3: the records whose position holds Value, in key order.
read(Tab, Value, Pos) ->
    Keys = ets:select(table(Tab, Pos), [{{{Value, '$1'}}, [], ['$1']}]),
    [R || K <- Keys, R <- ets:lookup(Tab, K), element(Pos, R) =:= Value].

% A match through the index of Pos when the pattern's key is '_': the matches of each indexed key, last key first.
match_object(Tab, Pattern, Pos) ->
    case element(2, Pattern) of
        '_' ->
            lists:foldl(
                fun(Key, Found) -> ets:match_object(Tab, setelement(2, Pattern, Key)) ++ Found end,
                [],
                keys(Tab, Pos, element(Pos, Pattern))
            );
        _ ->
            ets:match_object(Tab, Pattern)
    end.

% The records whose position holds the head pattern's value, for a select to filter.
records(Tab, Head, Pos) ->
    lists:append([ets:lookup(Tab, K) || K <- keys(Tab, Pos, element(Pos, Head))]).

keys(Tab, Pos, Value) ->
    ets:select(table(Tab, Pos), [{{{Value, '$1'}}, [], ['$1']}]).

% The index table of Pos.
table(Tab, Pos) ->
    #index{pos_list = Positions} = val({Tab, index_info}),
    case [Ixt || {{P, _}, {ram, Ixt}} <- Positions, P =:= Pos] of
        [Ixt | _] -> Ixt;
        [] -> mnesia:abort({no_exist, Tab, {index, Pos}})
    end.

val(Name) -> clause_mnesia_monitor:val(Name).

set(Name, Value) -> ets:insert(?GVAR, {Name, Value}).
