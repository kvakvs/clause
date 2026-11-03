%% A table replica's records, wherever this node keeps them (plan 21 step MIM1D, docs/libraries/mnesia.md#disc-storage):
%% ram_copies and disc_copies tables are ets tables, disc_only_copies tables dets tables of the same name. Each function
%% reads the table's storage_type and calls ets or dets; dets walks and selects have no reverse, so last/prev/reverse
%% selects walk forward as in OTP.
-module(clause_mnesia_db).
-export([lookup/2, insert/2, delete/2, match_delete/2, match_object/2, delete_all_objects/1, update_counter/3]).
-export([select/3, select/4, select/1, first/1, last/1, next/2, prev/2, slot/2, foldl/3, info/2, kind/1]).

-include("clause_mnesia.hrl").

% ets or dets: where this node keeps a table's records.
kind(Tab) ->
    case ets:lookup_element(?GVAR, {Tab, storage_type}, 2) of
        disc_only_copies -> dets;
        _ -> ets
    end.

lookup(Tab, Key) ->
    case kind(Tab) of
        ets -> ets:lookup(Tab, Key);
        dets -> ok_list(dets:lookup(Tab, Key))
    end.

insert(Tab, Val) ->
    case kind(Tab) of
        ets -> true = ets:insert(Tab, Val);
        dets -> ok = dets:insert(Tab, Val)
    end,
    ok.

delete(Tab, Key) ->
    case kind(Tab) of
        ets -> true = ets:delete(Tab, Key);
        dets -> ok = dets:delete(Tab, Key)
    end,
    ok.

match_delete(Tab, Pattern) ->
    case kind(Tab) of
        ets -> true = ets:match_delete(Tab, Pattern);
        dets -> ok = dets:match_delete(Tab, Pattern)
    end,
    ok.

match_object(Tab, Pattern) ->
    case kind(Tab) of
        ets -> ets:match_object(Tab, Pattern);
        dets -> ok_list(dets:match_object(Tab, Pattern))
    end.

delete_all_objects(Tab) ->
    case kind(Tab) of
        ets -> true = ets:delete_all_objects(Tab);
        dets -> ok = dets:delete_all_objects(Tab)
    end,
    ok.

update_counter(Tab, Key, Incr) ->
    case kind(Tab) of
        ets -> ets:update_counter(Tab, Key, Incr);
        dets -> dets:update_counter(Tab, Key, Incr)
    end.

% A whole select, forward or reverse.
select(Tab, Spec, forward) ->
    case kind(Tab) of
        ets -> ets:select(Tab, Spec);
        dets -> ok_list(dets:select(Tab, Spec))
    end;
select(Tab, Spec, reverse) ->
    case kind(Tab) of
        ets -> ets:select_reverse(Tab, Spec);
        dets -> ok_list(dets:select(Tab, Spec))
    end.

% The first chunk of a select and its continuation.
select(Tab, Spec, N, forward) ->
    case kind(Tab) of
        ets -> ets:select(Tab, Spec, N);
        dets -> dets:select(Tab, Spec, N)
    end;
select(Tab, Spec, N, reverse) ->
    case kind(Tab) of
        ets -> ets:select_reverse(Tab, Spec, N);
        dets -> dets:select(Tab, Spec, N)
    end.

% The next chunk of an ets or dets continuation.
select(Cont) when element(1, Cont) =:= dets_cont -> dets:select(Cont);
select(Cont) -> ets:select(Cont).

first(Tab) ->
    case kind(Tab) of
        ets -> ets:first(Tab);
        dets -> dets:first(Tab)
    end.

last(Tab) ->
    case kind(Tab) of
        ets -> ets:last(Tab);
        dets -> dets:first(Tab)
    end.

next(Tab, Key) ->
    case kind(Tab) of
        ets -> ets:next(Tab, Key);
        dets -> dets:next(Tab, Key)
    end.

prev(Tab, Key) ->
    case kind(Tab) of
        ets -> ets:prev(Tab, Key);
        dets -> dets:next(Tab, Key)
    end.

slot(Tab, Slot) ->
    case kind(Tab) of
        ets -> ets:slot(Tab, Slot);
        dets -> dets:slot(Tab, Slot)
    end.

foldl(Fun, Acc, Tab) ->
    case kind(Tab) of
        ets -> ets:foldl(Fun, Acc, Tab);
        dets -> dets:foldl(Fun, Acc, Tab)
    end.

% size or memory: dets answers its file's size as memory.
info(Tab, Item) ->
    case kind(Tab) of
        ets -> ets:info(Tab, Item);
        dets -> dets:info(Tab, Item)
    end.

% A dets read's records; its error is a failed read.
ok_list(List) when is_list(List) -> List;
ok_list({error, Reason}) -> error(Reason).
