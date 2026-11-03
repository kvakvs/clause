%% mnesia's indexes (plan 21 step MIM1A, docs/libraries/mnesia.md#indexes): one ordered_set ets table of {{Value, Key}}
%% per index position, owned by mnesia_monitor, kept on every write and delete of the table. A disc_only_copies table's
%% index is a dets bag of {Value, Key} in Tab_Pos.DAT, named {Tab, index, {Pos, bag}} as in OTP (MIM1D). A table's
%% index_info lists its indexes newest first, as OTP's does.
-module(clause_mnesia_index).
-export([add/2, drop/2, drop_all/1, written/4, deleted/2, object_deleted/3, cleared/2, read/3, match_object/3]).
-export([records/3]).

-include("clause_mnesia.hrl").

% A new index of a position, filled from the table's records (runs in mnesia_monitor).
add(Tab, {Pos, _Type}) ->
    {Entry, Name, Ix} = new_index(Tab, Pos, clause_mnesia_db:kind(Tab)),
    clause_mnesia_db:foldl(fun(Record, ok) -> put_values(Ix, Pos, Record, element(2, Record)) end, ok, Tab),
    set({Tab, Name}, element(2, Ix)),
    Info = val({Tab, index_info}),
    store(Tab, Info#index{pos_list = [{Entry, Ix} | Info#index.pos_list]}).

% A RAM index, or a disc_only_copies table's dets index in a new file.
new_index(_, Pos, ets) ->
    {{Pos, ordered}, {index, Pos}, {ram, ets:new(mnesia_index, [ordered_set, public])}};
new_index(Tab, Pos, dets) ->
    Name = {Tab, index, {Pos, bag}},
    File = clause_mnesia_disc:index_file(Tab, Pos),
    _ = file:delete(File),
    {ok, Name} = dets:open_file(Name, [{file, File}, {type, bag}, {keypos, 1}]),
    {{Pos, bag}, {index, {Pos, bag}}, {dets, Name}}.

% Every index of a position goes from index_info, with its table.
drop(Tab, Pos) ->
    Info = val({Tab, index_info}),
    [close(Tab, Entry, Ix) || {{P, _} = Entry, Ix} <- Info#index.pos_list, P =:= Pos],
    store(Tab, Info#index{pos_list = [I || {{P, _}, _} = I <- Info#index.pos_list, P =/= Pos]}).

% Every index of a table goes; the positions to add again, oldest first (MIM1D).
drop_all(Tab) ->
    case ets:lookup(?GVAR, {Tab, index_info}) of
        [{_, #index{pos_list = Positions} = Info}] ->
            [close(Tab, Entry, Ix) || {Entry, Ix} <- Positions],
            store(Tab, Info#index{pos_list = []}),
            lists:reverse([{P, ordered} || {{P, _}, _} <- Positions]);
        [] ->
            []
    end.

close(Tab, {Pos, _}, {ram, Ixt}) ->
    ets:delete(Ixt),
    ets:delete(?GVAR, {Tab, {index, Pos}});
close(Tab, Entry, {dets, Name}) ->
    ok = dets:close(Name),
    _ = file:delete(clause_mnesia_disc:index_file(Tab, element(1, Entry))),
    ets:delete(?GVAR, {Tab, {index, Entry}}).

% index_info and what follows from it: the index positions and the commit work, in OTP's order (MIM1E).
store(Tab, #index{pos_list = Positions} = Info) ->
    set({Tab, index_info}, Info),
    set({Tab, index}, [P || {{P, _}, _} <- Positions]),
    Work = val({Tab, commit_work}),
    New =
        case {Positions, lists:keymember(index, 1, Work)} of
            {[], _} -> lists:keydelete(index, 1, Work);
            {_, true} -> lists:keyreplace(index, 1, Work, Info);
            {_, false} -> [Info | Work]
        end,
    set({Tab, commit_work}, clause_mnesia_subscr:sort_commit(New)).

% A record written under Key; Old is the replaced records, or undefined to read them from the table first.
written(Tab, Key, Record, Old) ->
    case work(Tab) of
        none ->
            ok;
        #index{setorbag = bag, pos_list = Positions} ->
            [put_values(Ix, Pos, Record, Key) || {{Pos, _}, Ix} <- Positions];
        #index{pos_list = Positions} ->
            [replace(Tab, Key, Record, Old, Index) || Index <- Positions]
    end,
    ok.

% A set table's record replaces the old ones: entries of changed values go, the new value's entry comes.
replace(Tab, Key, Record, Old0, {{Pos, _}, Ix}) ->
    Old =
        case Old0 of
            undefined -> clause_mnesia_db:lookup(Tab, Key);
            _ -> Old0
        end,
    Value = element(Pos, Record),
    case [O || O <- Old, element(Pos, O) =/= Value] of
        [] when Old =/= [] -> ok;
        Changed ->
            [delete_entry(Ix, element(Pos, O), Key) || O <- Changed],
            put_values(Ix, Pos, Record, Key)
    end.

% The records under Key are deleted.
deleted(Tab, Key) ->
    case work(Tab) of
        none ->
            ok;
        #index{pos_list = Positions} ->
            Records = clause_mnesia_db:lookup(Tab, Key),
            [delete_entry(Ix, element(Pos, R), Key) || {{Pos, _}, Ix} <- Positions, R <- Records],
            ok
    end.

% One record under Key is deleted: a value's entry goes when no other record under Key holds the value.
object_deleted(Tab, Key, Record) ->
    case work(Tab) of
        none ->
            ok;
        #index{pos_list = Positions} ->
            Records = clause_mnesia_db:lookup(Tab, Key),
            [
                delete_entry(Ix, element(Pos, Record), Key)
             || {{Pos, _}, Ix} <- Positions,
                [R || R <- Records, element(Pos, R) =:= element(Pos, Record)] =:= [Record]
            ],
            ok
    end.

% Records a clear_table removed: a value's entry goes unless a record left under its key holds the value (MIM1C).
cleared(Tab, Records) ->
    case work(Tab) of
        none ->
            ok;
        #index{pos_list = Positions} ->
            [
                delete_entry(Ix, element(Pos, R), element(2, R))
             || {{Pos, _}, Ix} <- Positions,
                R <- Records,
                not lists:keymember(element(Pos, R), Pos, clause_mnesia_db:lookup(Tab, element(2, R)))
            ],
            ok
    end.

put_values({ram, Ixt}, Pos, Record, Key) ->
    true = ets:insert(Ixt, {{element(Pos, Record), Key}}),
    ok;
put_values({dets, Name}, Pos, Record, Key) ->
    ok = dets:insert(Name, {element(Pos, Record), Key}).

delete_entry({ram, Ixt}, Value, Key) -> ets:delete(Ixt, {Value, Key});
delete_entry({dets, Name}, Value, Key) -> dets:delete_object(Name, {Value, Key}).

% A table's index work; none without indexes.
work(Tab) ->
    case lists:keyfind(index, 1, val({Tab, commit_work})) of
        false -> none;
        Info -> Info
    end.

% dirty_index_read/3: the records whose position holds Value, in key order.
read(Tab, Value, Pos) ->
    [R || K <- keys(Tab, Pos, Value), R <- clause_mnesia_db:lookup(Tab, K), element(Pos, R) =:= Value].

% A match through the index of Pos when the pattern's key is '_': the matches of each indexed key, last key first.
match_object(Tab, Pattern, Pos) ->
    case element(2, Pattern) of
        '_' ->
            lists:foldl(
                fun(Key, Found) -> clause_mnesia_db:match_object(Tab, setelement(2, Pattern, Key)) ++ Found end,
                [],
                keys(Tab, Pos, element(Pos, Pattern))
            );
        _ ->
            clause_mnesia_db:match_object(Tab, Pattern)
    end.

% The records whose position holds the head pattern's value, for a select to filter.
records(Tab, Head, Pos) ->
    lists:append([clause_mnesia_db:lookup(Tab, K) || K <- keys(Tab, Pos, element(Pos, Head))]).

keys(Tab, Pos, Value) ->
    case table(Tab, Pos) of
        {ram, Ixt} -> ets:select(Ixt, [{{{Value, '$1'}}, [], ['$1']}]);
        {dets, Name} -> lists:usort([K || {_, K} <- dets:lookup(Name, Value)])
    end.

% The index table of Pos.
table(Tab, Pos) ->
    #index{pos_list = Positions} = val({Tab, index_info}),
    case [Ix || {{P, _}, Ix} <- Positions, P =:= Pos] of
        [Ix | _] -> Ix;
        [] -> mnesia:abort({no_exist, Tab, {index, Pos}})
    end.

val(Name) -> clause_mnesia_monitor:val(Name).

set(Name, Value) -> ets:insert(?GVAR, {Name, Value}).
