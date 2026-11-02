%% mnesia's transactions and activities (plan 21 step MIM1B): transaction results and abort terms, reads that see the
%% transaction's own writes, nested transactions, the activity kinds and access modules, locks, conflicts between
%% concurrent transactions, schema changes as schema transactions, and select continuations.
-module(mnesia_transactions).
-export([main/1]).
%% The access module callbacks of activity/4, which log each call and forward it to mnesia.
-export([
    lock/4,
    write/5,
    delete/5,
    delete_object/5,
    read/5,
    match_object/5,
    all_keys/4,
    first/3,
    last/3,
    next/4,
    prev/4,
    index_match_object/6,
    index_read/6,
    table_info/4,
    foldl/6,
    foldr/6,
    select/5,
    select/6,
    select_cont/3,
    select_reverse/5,
    select_reverse/6
]).

-record(acct, {id, owner, balance}).

main(_) ->
    ok = mnesia:start(),
    tables(),
    basics(),
    aborts(),
    own_writes(),
    iteration(),
    indexed(),
    nested(),
    activities(),
    access_module(),
    locks(),
    conflicts(),
    schema_transactions(),
    continuations(),
    counters(),
    stopped(),
    io:format("mnesia_transactions: ok~n").

tables() ->
    {atomic, ok} = mnesia:create_table(acct, [{attributes, record_info(fields, acct)}, {index, [owner]}]),
    {atomic, ok} = mnesia:create_table(ord, [{type, ordered_set}, {attributes, [k, v]}, {index, [v]}]),
    {atomic, ok} = mnesia:create_table(bg, [{type, bag}, {attributes, [k, v]}, {index, [v]}]),
    {atomic, ok} = mnesia:create_table(ro, [{access_mode, read_only}]),
    [ok = mnesia:dirty_write(#acct{id = I, owner = O, balance = B}) || {I, O, B} <- [{1, ann, 10}, {2, bob, 20}]],
    [ok = mnesia:dirty_write({ord, K, V}) || {K, V} <- [{10, a}, {20, b}, {30, c}, {40, b}]],
    [ok = mnesia:dirty_write({bg, K, V}) || {K, V} <- [{1, a}, {1, b}, {2, a}]],
    ok.

% transaction/1,2,3 and sync_transaction/1,2,3: results and argument checks.
basics() ->
    t(atomic, fun() -> mnesia:transaction(fun() -> mnesia:read(acct, 1) end) end),
    t(write_read, fun() ->
        mnesia:transaction(fun() ->
            ok = mnesia:write(#acct{id = 3, owner = cy, balance = 30}),
            {mnesia:read({acct, 3}), mnesia:wread({acct, 3}), mnesia:read(acct, 3, write)}
        end)
    end),
    t(committed, fun() -> mnesia:dirty_read(acct, 3) end),
    t(with_args, fun() -> mnesia:transaction(fun(T, K) -> mnesia:read(T, K) end, [acct, 2]) end),
    t(with_retries, fun() ->
        [
            mnesia:transaction(fun() -> ok end, 3),
            mnesia:transaction(fun() -> ok end, infinity),
            mnesia:transaction(fun(X) -> X end, [one], 0)
        ]
    end),
    t(sync, fun() ->
        [
            mnesia:sync_transaction(fun() -> mnesia:write({acct, 4, dan, 40}) end),
            mnesia:sync_transaction(fun() -> ok end, 2),
            mnesia:sync_transaction(fun(X) -> X end, [two]),
            mnesia:sync_transaction(fun(X) -> X end, [three], 1)
        ]
    end),
    t(badarg, fun() ->
        funless([
            mnesia:transaction(notfun),
            mnesia:transaction(fun() -> ok end, -1),
            mnesia:transaction(fun() -> ok end, [a], foo),
            mnesia:transaction(fun() -> ok end, notlist, 1),
            mnesia:sync_transaction(notfun, [], 1)
        ])
    end),
    t(is_transaction, fun() ->
        [mnesia:is_transaction(), mnesia:transaction(fun mnesia:is_transaction/0)]
    end),
    t(activity_id, fun() ->
        [
            mnesia:get_activity_id(),
            mnesia:transaction(fun() -> activity_shape(mnesia:get_activity_id()) end)
        ]
    end),
    t(put_activity_id, fun() ->
        {atomic, Id} = mnesia:transaction(fun mnesia:get_activity_id/0),
        mnesia:put_activity_id(Id),
        Inside = mnesia:is_transaction(),
        mnesia:put_activity_id(undefined),
        [Inside, mnesia:is_transaction(), get(mnesia_activity_state)]
    end).

% The abort terms of aborts, throws, exits and errors, and writes rolled back.
aborts() ->
    t(abort, fun() -> mnesia:transaction(fun() -> mnesia:abort(no_way) end) end),
    t(abort_aborted, fun() -> mnesia:transaction(fun() -> mnesia:abort({aborted, inner}) end) end),
    t(throw, fun() -> mnesia:transaction(fun() -> throw(thrown) end) end),
    t(exit, fun() -> mnesia:transaction(fun() -> exit(gone) end) end),
    t(exit_tuple, fun() -> mnesia:transaction(fun() -> exit({gone, 1}) end) end),
    t(error, fun() -> stack_shape(mnesia:transaction(fun() -> error(broken) end)) end),
    t(badarith, fun() -> stack_shape(mnesia:transaction(fun() -> 1 / zero() end)) end),
    t(returned, fun() ->
        [mnesia:transaction(fun() -> {aborted, x} end), mnesia:transaction(fun() -> {'EXIT', y} end)]
    end),
    t(rolled_back, fun() ->
        R = mnesia:transaction(fun() ->
            ok = mnesia:write({acct, 9, zed, 90}),
            ok = mnesia:delete({acct, 1}),
            mnesia:abort(undo)
        end),
        [R, mnesia:dirty_read(acct, 9), mnesia:dirty_read(acct, 1)]
    end),
    t(bad_access, fun() ->
        [
            tx(fun() -> mnesia:write({acct, 1}) end),
            tx(fun() -> mnesia:write({acct, 1, x}) end),
            tx(fun() -> mnesia:write({other, 1, x}) end),
            tx(fun() -> mnesia:write(acct, {acct, 1, x, y}, read) end),
            tx(fun() -> mnesia:write(schema, {schema, 1, x}, write) end),
            tx(fun() -> mnesia:write({ro, 1, x}) end),
            tx(fun() -> mnesia:read(nope, 1) end),
            tx(fun() -> mnesia:read(acct, 1, foo) end),
            tx(fun() -> mnesia:read(1) end),
            tx(fun() -> mnesia:read(ro, 1) end),
            tx(fun() -> mnesia:delete({nope, 1}) end),
            tx(fun() -> mnesia:delete(acct, 1, read) end),
            tx(fun() -> mnesia:delete_object({acct, '_', x, y}) end),
            tx(fun() -> mnesia:delete_object(acct) end),
            tx(fun() -> mnesia:match_object({nope, '_', '_'}) end),
            tx(fun() -> mnesia:select(nope, [{'_', [], ['$_']}]) end),
            tx(fun() -> mnesia:select(acct, notspec) end),
            tx(fun() -> mnesia:all_keys(nope) end),
            tx(fun() -> mnesia:first(nope) end)
        ]
    end),
    t(no_transaction, fun() ->
        [
            catch_all(fun() -> mnesia:read(acct, 1) end),
            catch_all(fun() -> mnesia:write({acct, 1, a, 1}) end),
            catch_all(fun() -> mnesia:delete({acct, 1}) end),
            catch_all(fun() -> mnesia:match_object({acct, '_', '_', '_'}) end),
            catch_all(fun() -> mnesia:select(acct, [{'_', [], ['$_']}]) end),
            catch_all(fun() -> mnesia:all_keys(acct) end),
            catch_all(fun() -> mnesia:first(acct) end),
            catch_all(fun() -> mnesia:foldl(fun(_, A) -> A end, 0, acct) end),
            catch_all(fun() -> mnesia:index_read(acct, ann, owner) end),
            catch_all(fun() -> mnesia:lock({table, acct}, read) end),
            catch_all(fun() -> mnesia:select(acct, [{'_', [], ['$_']}], 1, read) end),
            catch_all(fun() -> mnesia:select({cont}) end)
        ]
    end).

% Reads in a transaction see its own writes and deletes.
own_writes() ->
    t(set_writes, fun() ->
        tx(fun() ->
            ok = mnesia:write({acct, 1, ann, 11}),
            ok = mnesia:delete({acct, 2}),
            ok = mnesia:write({acct, 5, eve, 50}),
            [
                mnesia:read(acct, 1),
                mnesia:read(acct, 2),
                lists:sort(mnesia:match_object({acct, '_', '_', '_'})),
                lists:sort(mnesia:select(acct, [{{acct, '$1', '_', '$2'}, [{'>', '$2', 15}], [{{'$1', '$2'}}]}])),
                lists:sort(mnesia:all_keys(acct)),
                lists:sort(mnesia:match_object({acct, '_', eve, '_'}))
            ]
        end)
    end),
    t(set_delete_object, fun() ->
        tx(fun() ->
            ok = mnesia:delete_object({acct, 1, ann, 99}),
            A = mnesia:read(acct, 1),
            ok = mnesia:delete_object({acct, 1, ann, 10}),
            B = mnesia:read(acct, 1),
            ok = mnesia:write({acct, 6, fay, 60}),
            ok = mnesia:delete_object({acct, 6, fay, 60}),
            [A, B, mnesia:read(acct, 6)]
        end)
    end),
    t(bag_writes, fun() ->
        tx(fun() ->
            ok = mnesia:write({bg, 1, c}),
            ok = mnesia:write({bg, 1, a}),
            A = mnesia:read(bg, 1),
            ok = mnesia:delete_object({bg, 1, b}),
            B = mnesia:read(bg, 1),
            ok = mnesia:delete({bg, 2}),
            ok = mnesia:write({bg, 2, z}),
            [A, B, mnesia:read(bg, 2), lists:sort(mnesia:match_object({bg, '_', a})), lists:sort(mnesia:all_keys(bg))]
        end)
    end),
    t(bag_committed, fun() -> [lists:sort(mnesia:dirty_read(bg, K)) || K <- [1, 2]] end),
    t(ordered_writes, fun() ->
        tx(fun() ->
            ok = mnesia:write({ord, 25, x}),
            ok = mnesia:delete({ord, 30}),
            ok = mnesia:write({ord, 5, y}),
            [
                mnesia:match_object({ord, '_', '_'}),
                mnesia:select(ord, [{{ord, '$1', '_'}, [], ['$1']}]),
                mnesia:select_reverse(ord, [{{ord, '$1', '_'}, [], ['$1']}]),
                mnesia:all_keys(ord)
            ]
        end)
    end),
    t(ordered_committed, fun() -> mnesia:dirty_select(ord, [{'_', [], ['$_']}]) end),
    t(counted_after, fun() -> lists:sort(mnesia:dirty_all_keys(acct)) end).

% first, next, last, prev, foldl and foldr in a transaction, with its own writes.
iteration() ->
    t(walk, fun() ->
        tx(fun() ->
            ok = mnesia:write({ord, 15, w}),
            ok = mnesia:delete({ord, 20}),
            {walk(fun mnesia:first/1, fun mnesia:next/2), walk(fun mnesia:last/1, fun mnesia:prev/2)}
        end)
    end),
    t(walk_end, fun() ->
        tx(fun() -> [mnesia:next(ord, 1000), mnesia:prev(ord, -1), mnesia:next(ord, 12)] end)
    end),
    t(folds, fun() ->
        tx(fun() ->
            ok = mnesia:write({ord, 50, e}),
            ok = mnesia:delete({ord, 10}),
            [
                mnesia:foldl(fun({ord, K, _}, Acc) -> [K | Acc] end, [], ord),
                mnesia:foldr(fun({ord, K, _}, Acc) -> [K | Acc] end, [], ord),
                lists:sort(mnesia:foldl(fun(R, Acc) -> [R | Acc] end, [], bg)),
                mnesia:foldl(fun(_, N) -> N + 1 end, 0, acct, write)
            ]
        end)
    end),
    t(fold_abort, fun() -> tx(fun() -> mnesia:foldl(fun(_, _) -> mnesia:abort(stop) end, 0, ord) end) end),
    t(fold_error, fun() -> stack_shape(tx(fun() -> mnesia:foldl(fun(_, _) -> error(bad) end, 0, ord) end)) end),
    t(set_walk, fun() ->
        tx(fun() ->
            ok = mnesia:write({acct, 7, gus, 70}),
            lists:sort(walk_keys(acct, mnesia:first(acct)))
        end)
    end).

% index_read and index_match_object in a transaction, with its own writes.
indexed() ->
    t(index_read, fun() ->
        tx(fun() ->
            ok = mnesia:write({acct, 8, ann, 80}),
            ok = mnesia:write({acct, 1, zoe, 10}),
            [lists:sort(mnesia:index_read(acct, ann, owner)), mnesia:index_read(acct, zoe, #acct.owner)]
        end)
    end),
    t(index_match, fun() ->
        tx(fun() ->
            ok = mnesia:write({ord, 60, b}),
            ok = mnesia:delete({ord, 40}),
            [
                mnesia:index_match_object({ord, '_', b}, v),
                mnesia:index_match_object(ord, {ord, '_', b}, 3, read),
                lists:sort(mnesia:index_read(bg, a, v))
            ]
        end)
    end),
    t(index_errors, fun() ->
        [
            tx(fun() -> mnesia:index_read(acct, ann, nope) end),
            tx(fun() -> mnesia:index_read(acct, '_', owner) end),
            tx(fun() -> mnesia:index_read(acct, ann, balance) end),
            tx(fun() -> mnesia:index_match_object(acct, {acct, '_', ann, '_'}, owner, write) end),
            tx(fun() -> mnesia:index_match_object({acct, '_', ann}, 9) end),
            tx(fun() -> mnesia:index_match_object(nope, owner) end)
        ]
    end).

% Nested transactions: an inner commit or abort, what the outer one sees, and their counts.
nested() ->
    Before = counts(),
    t(inner_commit, fun() ->
        tx(fun() ->
            Inner = mnesia:transaction(fun() ->
                ok = mnesia:write({acct, 20, hal, 1}),
                mnesia:read(acct, 20)
            end),
            [Inner, mnesia:read(acct, 20), mnesia:is_transaction()]
        end)
    end),
    t(inner_abort, fun() ->
        tx(fun() ->
            ok = mnesia:write({acct, 21, ian, 1}),
            Inner = mnesia:transaction(fun() ->
                ok = mnesia:write({acct, 22, jo, 1}),
                ok = mnesia:delete({acct, 21}),
                mnesia:abort(inner)
            end),
            [Inner, mnesia:read(acct, 21), mnesia:read(acct, 22)]
        end)
    end),
    t(inner_error, fun() -> tx(fun() -> stack_shape(mnesia:transaction(fun() -> error(inner) end)) end) end),
    t(inner_throw, fun() -> tx(fun() -> mnesia:transaction(fun() -> throw(t) end) end) end),
    t(same_tid, fun() ->
        tx(fun() ->
            {_, Outer, _} = mnesia:get_activity_id(),
            {atomic, {_, Inner, Store}} = mnesia:transaction(fun mnesia:get_activity_id/0),
            [Outer =:= Inner, element(1, Store), tuple_size(Store), element(4, Store)]
        end)
    end),
    t(nested_levels, fun() ->
        tx(fun() ->
            mnesia:transaction(fun() ->
                {_, _, Ts} = mnesia:get_activity_id(),
                [element(4, Ts), length(element(3, Ts))]
            end)
        end)
    end),
    t(committed, fun() -> [mnesia:dirty_read(acct, K) || K <- [20, 21, 22]] end),
    t(nested_counts, fun() -> deltas(Before) end).

% activity/2,3,4, async_dirty, sync_dirty and ets: their results, errors and nesting.
activities() ->
    t(kinds, fun() ->
        [
            mnesia:activity(K, fun() -> {mnesia:is_transaction(), mnesia:read(acct, 2)} end)
         || K <- [transaction, {transaction, 2}, sync_transaction, {sync_transaction, 1}, async_dirty, sync_dirty, ets]
        ]
    end),
    t(kind_ids, fun() ->
        [
            activity_shape(mnesia:activity(K, fun mnesia:get_activity_id/0))
         || K <- [transaction, async_dirty, sync_dirty, ets]
        ]
    end),
    t(bad_kind, fun() -> mnesia:activity(foo, fun() -> ok end) end),
    t(with_args, fun() -> mnesia:activity(async_dirty, fun(T, K) -> mnesia:read(T, K) end, [acct, 2]) end),
    t(activity_abort, fun() -> catch_all(fun() -> mnesia:activity(transaction, fun() -> mnesia:abort(no) end) end) end),
    t(dirty_writes, fun() ->
        [
            mnesia:async_dirty(fun() ->
                ok = mnesia:write({acct, 30, kim, 3}),
                [mnesia:read(acct, 30), mnesia:lock({table, acct}, write), mnesia:dirty_read(acct, 30)]
            end),
            mnesia:sync_dirty(fun() ->
                ok = mnesia:delete({acct, 30}),
                [mnesia:read(acct, 30), mnesia:index_read(acct, bob, owner)]
            end),
            mnesia:ets(fun() ->
                ok = mnesia:write({ord, 70, g}),
                [mnesia:read(ord, 70), mnesia:first(ord), mnesia:last(ord)]
            end)
        ]
    end),
    t(dirty_select, fun() ->
        mnesia:async_dirty(fun() ->
            [
                mnesia:select(ord, [{{ord, '$1', '_'}, [], ['$1']}]),
                mnesia:all_keys(ord),
                mnesia:foldl(fun({ord, K, _}, A) -> [K | A] end, [], ord),
                mnesia:match_object({ord, '_', b}),
                mnesia:next(ord, 40)
            ]
        end)
    end),
    t(dirty_errors, fun() ->
        funless([
            catch_all(fun() -> mnesia:async_dirty(fun() -> mnesia:abort(no) end) end),
            catch_all(fun() -> mnesia:sync_dirty(fun() -> throw(t) end) end),
            catch_all(fun() -> mnesia:ets(fun() -> exit(e) end) end),
            stack_shape(catch_all(fun() -> mnesia:async_dirty(fun() -> error(bad) end) end)),
            catch_all(fun() -> mnesia:async_dirty(fun() -> {aborted, r} end) end),
            catch_all(fun() -> mnesia:async_dirty(fun() -> {'EXIT', r} end) end),
            catch_all(fun() -> mnesia:async_dirty(fun() -> mnesia:read(nope, 1) end) end),
            catch_all(fun() -> mnesia:async_dirty(notfun) end),
            catch_all(fun() -> mnesia:async_dirty(fun() -> ok end, notlist) end)
        ])
    end),
    t(dirty_state_after, fun() ->
        catch_all(fun() -> mnesia:async_dirty(fun() -> error(bad) end) end),
        get(mnesia_activity_state)
    end),
    t(transaction_in_dirty, fun() ->
        mnesia:async_dirty(fun() ->
            R = mnesia:transaction(fun() ->
                ok = mnesia:write({acct, 31, lu, 3}),
                activity_shape(mnesia:get_activity_id())
            end),
            [R, activity_shape(mnesia:get_activity_id())]
        end)
    end),
    t(dirty_in_transaction, fun() ->
        tx(fun() ->
            R = mnesia:async_dirty(fun() ->
                ok = mnesia:write({acct, 32, mo, 3}),
                [mnesia:is_transaction(), mnesia:read(acct, 32)]
            end),
            [R, mnesia:dirty_read(acct, 32), mnesia:read(acct, 32)]
        end)
    end),
    t(dirty_in_transaction_abort, fun() ->
        tx(fun() -> catch_all(fun() -> mnesia:sync_dirty(fun() -> mnesia:abort(inner) end) end) end)
    end),
    t(committed, fun() -> [mnesia:dirty_read(acct, K) || K <- [31, 32]] end).

% activity/4 with an access module: which callbacks each function reaches.
access_module() ->
    t(callbacks, fun() ->
        put(calls, []),
        Result = mnesia:activity(
            transaction,
            fun() ->
                ok = mnesia:write({ord, 80, h}),
                ok = mnesia:s_write({ord, 81, h}),
                [
                    mnesia:read(ord, 80),
                    mnesia:wread({ord, 81}),
                    mnesia:match_object({ord, '_', h}),
                    mnesia:all_keys(ord),
                    mnesia:first(ord),
                    mnesia:last(ord),
                    mnesia:next(ord, 80),
                    mnesia:prev(ord, 80),
                    mnesia:index_read(ord, h, v),
                    mnesia:index_match_object({ord, '_', h}, v),
                    mnesia:table_info(ord, arity),
                    mnesia:foldl(fun(_, N) -> N + 1 end, 0, ord),
                    mnesia:foldr(fun(_, N) -> N + 1 end, 0, ord),
                    mnesia:select(ord, [{{ord, '$1', h}, [], ['$1']}]),
                    mnesia:select_reverse(ord, [{{ord, '$1', h}, [], ['$1']}]),
                    element(1, mnesia:select(ord, [{{ord, '$1', h}, [], ['$1']}], 1, read)),
                    mnesia:read_lock_table(ord),
                    mnesia:write_lock_table(ord),
                    mnesia:delete({ord, 80}),
                    mnesia:delete_object({ord, 81, h})
                ]
            end,
            [],
            ?MODULE
        ),
        [Result, lists:reverse(get(calls))]
    end),
    t(dirty_callbacks, fun() ->
        put(calls, []),
        Result = mnesia:activity(async_dirty, fun() -> mnesia:read(ord, 50) end, [], ?MODULE),
        [Result, lists:reverse(get(calls))]
    end),
    t(table_info_outside, fun() ->
        put(calls, []),
        [mnesia:transaction(fun() -> mnesia:table_info(ord, arity) end), get(calls)]
    end).

% lock/2 and the table locks: what they return, the locks held and the transactions running.
locks() ->
    t(lock_results, fun() ->
        tx(fun() ->
            [
                mnesia:lock({table, acct}, read),
                mnesia:lock({table, acct}, read),
                mnesia:lock({record, ord, 10}, read),
                mnesia:lock({record, ord, 10}, write),
                mnesia:lock({record, ord, 10}, write),
                mnesia:lock({table, bg}, write),
                mnesia:lock({table, bg}, write),
                mnesia:lock({record, bg, 1}, read),
                mnesia:lock({table, ord}, none),
                mnesia:read_lock_table(acct),
                mnesia:write_lock_table(acct)
            ]
        end)
    end),
    t(global_locks, fun() ->
        tx(fun() ->
            [
                mnesia:lock({global, g1, [node()]}, write),
                mnesia:lock({global, g2, [node(), other@host]}, read),
                mnesia:lock({global, g3, []}, write)
            ]
        end)
    end),
    t(lock_errors, fun() ->
        [
            tx(fun() -> mnesia:lock({table, acct}, foo) end),
            tx(fun() -> mnesia:lock({record, acct, 1}, foo) end),
            tx(fun() -> mnesia:lock({tab, acct}, read) end),
            tx(fun() -> mnesia:lock({table, nope}, read) end),
            tx(fun() -> mnesia:lock({table, nope}, write) end),
            tx(fun() -> mnesia:lock({table, "acct"}, write) end),
            tx(fun() -> mnesia:lock({global, g, notlist}, write) end),
            tx(fun() -> mnesia:lock({global, g, [node()]}, sticky_write) end),
            tx(fun() -> mnesia:write_lock_table(ro) end)
        ]
    end),
    t(held_locks, fun() ->
        tx(fun() ->
            ok = mnesia:write({acct, 1, ann, 10}),
            _ = mnesia:read(ord, 10),
            mnesia:read_lock_table(bg),
            _ = mnesia:wread({acct, 2}),
            Self = self(),
            [{Oid, Op, own_tid(Tid, Self)} || {Oid, Op, Tid} <- mnesia:system_info(held_locks)]
        end)
    end),
    t(queue_and_transactions, fun() ->
        tx(fun() ->
            Self = self(),
            [
                mnesia:system_info(lock_queue),
                [{is_integer(N), P =:= Self, S} || {N, P, S} <- mnesia:system_info(transactions)]
            ]
        end)
    end),
    t(released, fun() ->
        wait_until(fun() -> mnesia:system_info(held_locks) =:= [] end),
        [mnesia:system_info(held_locks), mnesia:system_info(transactions)]
    end),
    t(tid_order, fun() ->
        {atomic, {_, {tid, A, _}, _}} = mnesia:transaction(fun mnesia:get_activity_id/0),
        {atomic, {_, {tid, B, _}, _}} = mnesia:transaction(fun mnesia:get_activity_id/0),
        B - A
    end).

% Concurrent transactions: an older one waits for a younger one's lock, a younger one restarts, retries run out,
% shared read locks, and a killed holder's locks go.
conflicts() ->
    Parent = self(),
    Restarts = mnesia:system_info(transaction_restarts),
    t(older_waits, fun() ->
        Older = spawn_tx(Parent, fun() ->
            Parent ! {started, self()},
            receive_go(),
            ok = mnesia:write({acct, 40, old, 1}),
            mnesia:read(acct, 41)
        end),
        receive
            {started, Older} -> ok
        end,
        Younger = spawn_tx(Parent, fun() ->
            ok = mnesia:write({acct, 41, young, 2}),
            Parent ! {locked, self()},
            receive_go()
        end),
        receive
            {locked, Younger} -> ok
        end,
        Older ! go,
        wait_until(fun() -> mnesia:system_info(lock_queue) =/= [] end),
        Queue = [
            {Oid, Op, P =:= Older, element(3, Tid) =:= Older, element(3, Waited) =:= Younger}
         || {Oid, Op, P, Tid, Waited} <- mnesia:system_info(lock_queue)
        ],
        Younger ! go,
        [Queue, result(Younger), result(Older)]
    end),
    t(younger_restarts, fun() ->
        Older = spawn_tx(Parent, fun() ->
            ok = mnesia:write({acct, 42, old, 1}),
            Parent ! {locked, self()},
            receive_go(),
            done
        end),
        receive
            {locked, Older} -> ok
        end,
        Younger = spawn_tx(Parent, fun() ->
            Parent ! {run, self()},
            mnesia:read(acct, 42, write)
        end),
        [
            receive
                {run, Younger} -> ok
            end
         || _ <- [1, 2]
        ],
        Older ! go,
        [result(Older), result(Younger), mnesia:system_info(transaction_restarts) > Restarts]
    end),
    t(retries_out, fun() ->
        Older = spawn_tx(Parent, fun() ->
            ok = mnesia:write({acct, 43, old, 1}),
            Parent ! {locked, self()},
            receive_go()
        end),
        receive
            {locked, Older} -> ok
        end,
        Runs = [
            mnesia:transaction(fun() -> mnesia:read(acct, 43, write) end, N)
         || N <- [0, 1]
        ],
        put(runs, 0),
        Two = mnesia:transaction(
            fun() ->
                put(runs, get(runs) + 1),
                mnesia:write({acct, 43, me, 3})
            end,
            2
        ),
        Older ! go,
        [Runs, Two, get(runs), result(Older)]
    end),
    t(shared_reads, fun() ->
        Reader = spawn_tx(Parent, fun() ->
            _ = mnesia:read(acct, 1),
            Parent ! {locked, self()},
            receive_go()
        end),
        receive
            {locked, Reader} -> ok
        end,
        Mine = tx(fun() ->
            _ = mnesia:read(acct, 1),
            [
                {Oid, Op, holder(element(3, Tid), Reader)}
             || {{acct, 1} = Oid, Op, Tid} <- mnesia:system_info(held_locks)
            ]
        end),
        Reader ! go,
        [Mine, result(Reader)]
    end),
    t(killed_holder, fun() ->
        Holder = spawn(fun() ->
            mnesia:transaction(fun() ->
                ok = mnesia:write({acct, 44, gone, 1}),
                Parent ! {locked, self()},
                receive
                after infinity -> ok
                end
            end)
        end),
        receive
            {locked, Holder} -> ok
        end,
        Ref = monitor(process, Holder),
        exit(Holder, kill),
        receive
            {'DOWN', Ref, process, Holder, _} -> ok
        end,
        [tx(fun() -> mnesia:read(acct, 44, write) end), mnesia:dirty_read(acct, 44)]
    end).

% Schema changes are schema transactions: never inside an activity, and they wait for the locks of transactions.
schema_transactions() ->
    t(nested_schema, fun() ->
        [
            tx(fun() -> mnesia:create_table(tmp, []) end),
            tx(fun() -> mnesia:delete_table(acct) end),
            tx(fun() -> mnesia:add_table_index(acct, balance) end),
            tx(fun() -> mnesia:del_table_index(acct, owner) end),
            catch_all(fun() -> mnesia:async_dirty(fun() -> mnesia:create_table(tmp, []) end) end),
            catch_all(fun() -> mnesia:ets(fun() -> {mnesia:delete_table(acct)} end) end)
        ]
    end),
    t(read_only_delete, fun() -> mnesia:delete_table(ro) end),
    {atomic, ok} = mnesia:create_table(tmp, []),
    Parent = self(),
    t(schema_waits, fun() ->
        Holder = spawn_tx(Parent, fun() ->
            mnesia:read_lock_table(tmp),
            Parent ! {locked, self()},
            timer:sleep(100),
            held
        end),
        receive
            {locked, Holder} -> ok
        end,
        [mnesia:delete_table(tmp), result(Holder), lists:member(tmp, mnesia:system_info(tables))]
    end),
    t(table_info_in_transaction, fun() ->
        tx(fun() -> [mnesia:table_info(acct, type), mnesia:table_info(acct, size)] end)
    end).

% select/4 and select/1 continuations in a transaction, with and without its own writes, and in dirty activities.
continuations() ->
    Spec = [{{ord, '$1', '_'}, [], ['$1']}],
    t(chunks, fun() -> tx(fun() -> chunks(mnesia:select(ord, Spec, 2, read)) end) end),
    t(chunks_reverse, fun() -> tx(fun() -> chunks(mnesia:select_reverse(ord, Spec, 2, read)) end) end),
    t(chunks_written, fun() ->
        tx(fun() ->
            ok = mnesia:write({ord, 35, n}),
            ok = mnesia:delete({ord, 15}),
            [chunks(mnesia:select(ord, Spec, 2, read)), chunks(mnesia:select_reverse(ord, Spec, 3, read))]
        end)
    end),
    t(chunks_dirty, fun() ->
        [mnesia:activity(K, fun() -> chunks(mnesia:select(ord, Spec, 3, read)) end) || K <- [async_dirty, ets]]
    end),
    t(chunks_errors, fun() ->
        [
            tx(fun() -> mnesia:select(ord, Spec, foo, read) end),
            tx(fun() -> mnesia:select(foo) end),
            tx(fun() -> mnesia:select('$end_of_table') end),
            element(1, mnesia:activity(async_dirty, fun() -> mnesia:select(ord, [{'_', [], ['$_']}], 100, read) end))
        ]
    end),
    t(chunk_other_transaction, fun() ->
        {atomic, {_, Cont}} = mnesia:transaction(fun() -> mnesia:select(ord, Spec, 1, read) end),
        tx(fun() -> mnesia:select(Cont) end)
    end).

% The transaction counters after a run without conflicts.
counters() ->
    Before = counts(),
    tx(fun() -> ok end),
    tx(fun() -> mnesia:abort(x) end),
    tx(fun() -> mnesia:transaction(fun() -> ok end) end),
    tx(fun() -> mnesia:transaction(fun() -> mnesia:abort(y) end) end),
    _ = mnesia:create_table(tmp2, []),
    _ = mnesia:create_table(tmp2, []),
    _ = tx(fun() -> mnesia:create_table(tmp3, []) end),
    _ = mnesia:async_dirty(fun() -> ok end),
    t(counted, fun() -> deltas(Before) end).

% Transactions once mnesia has stopped.
stopped() ->
    stopped = mnesia:stop(),
    t(stopped, fun() ->
        [
            mnesia:transaction(fun() -> ok end),
            mnesia:transaction(fun() -> mnesia:read(acct, 1) end),
            catch_all(fun() -> mnesia:async_dirty(fun() -> mnesia:read(acct, 1) end) end),
            catch_all(fun() -> mnesia:activity(transaction, fun() -> ok end) end),
            mnesia:is_transaction()
        ]
    end).

% Access module callbacks: log and forward to mnesia.

lock(Tid, Ts, Item, Kind) -> logged(lock, [Tid, Ts, Item, Kind]).
write(Tid, Ts, Tab, Val, Kind) -> logged(write, [Tid, Ts, Tab, Val, Kind]).
delete(Tid, Ts, Tab, Key, Kind) -> logged(delete, [Tid, Ts, Tab, Key, Kind]).
delete_object(Tid, Ts, Tab, Val, Kind) -> logged(delete_object, [Tid, Ts, Tab, Val, Kind]).
read(Tid, Ts, Tab, Key, Kind) -> logged(read, [Tid, Ts, Tab, Key, Kind]).
match_object(Tid, Ts, Tab, Pat, Kind) -> logged(match_object, [Tid, Ts, Tab, Pat, Kind]).
all_keys(Tid, Ts, Tab, Kind) -> logged(all_keys, [Tid, Ts, Tab, Kind]).
first(Tid, Ts, Tab) -> logged(first, [Tid, Ts, Tab]).
last(Tid, Ts, Tab) -> logged(last, [Tid, Ts, Tab]).
next(Tid, Ts, Tab, Key) -> logged(next, [Tid, Ts, Tab, Key]).
prev(Tid, Ts, Tab, Key) -> logged(prev, [Tid, Ts, Tab, Key]).
index_match_object(Tid, Ts, Tab, Pat, Attr, Kind) -> logged(index_match_object, [Tid, Ts, Tab, Pat, Attr, Kind]).
index_read(Tid, Ts, Tab, Key, Attr, Kind) -> logged(index_read, [Tid, Ts, Tab, Key, Attr, Kind]).
table_info(Tid, Ts, Tab, Item) -> logged(table_info, [Tid, Ts, Tab, Item]).
foldl(Tid, Ts, Fun, Acc, Tab, Kind) -> logged(foldl, [Tid, Ts, Fun, Acc, Tab, Kind]).
foldr(Tid, Ts, Fun, Acc, Tab, Kind) -> logged(foldr, [Tid, Ts, Fun, Acc, Tab, Kind]).
select(Tid, Ts, Tab, Spec, Kind) -> logged(select, [Tid, Ts, Tab, Spec, Kind]).
select(Tid, Ts, Tab, Spec, N, Kind) -> logged(select, [Tid, Ts, Tab, Spec, N, Kind]).
select_cont(Tid, Ts, Cont) -> logged(select_cont, [Tid, Ts, Cont]).
select_reverse(Tid, Ts, Tab, Spec, Kind) -> logged(select_reverse, [Tid, Ts, Tab, Spec, Kind]).
select_reverse(Tid, Ts, Tab, Spec, N, Kind) -> logged(select_reverse, [Tid, Ts, Tab, Spec, N, Kind]).

logged(Name, Args) ->
    put(calls, [{Name, length(Args)} | get(calls)]),
    apply(mnesia, Name, Args).

% Helpers.

t(Label, F) ->
    io:format("~p: ~p~n", [Label, catch_all(F)]).

catch_all(F) ->
    try
        F()
    catch
        Class:Reason -> {Class, Reason}
    end.

tx(F) ->
    mnesia:transaction(F).

% A term with its funs as the atom 'fun': OTP prints a fun with its module's numbering.
funless(F) when is_function(F) -> 'fun';
funless(T) when is_tuple(T) -> list_to_tuple(funless(tuple_to_list(T)));
funless([H | T]) -> [funless(H) | funless(T)];
funless(X) -> X.

zero() -> 0.

% An error's stack trace as its shape: OTP's and Clause's frames differ.
stack_shape({aborted, {Reason, Stack}}) when is_list(Stack) -> {aborted, {Reason, stack}};
stack_shape({exit, {Reason, Stack}}) when is_list(Stack) -> {exit, {Reason, stack}};
stack_shape({atomic, Result}) -> {atomic, stack_shape(Result)};
stack_shape(Other) -> Other.

% An activity id without the run's pids and tables.
activity_shape({Mod, {tid, N, Pid}, {tidstore, Store, Up, Level}}) ->
    {Mod, {tid, is_integer(N), Pid =:= self()}, {tidstore, is_reference(Store), Up, Level}};
activity_shape({Mod, {Kind, Pid}, Ts}) ->
    {Mod, {Kind, Pid =:= self()}, Ts};
activity_shape(Other) ->
    Other.

own_tid({tid, _, Pid}, Self) when Pid =:= Self -> own;
own_tid(Tid, _) -> Tid.

holder(Pid, Reader) when Pid =:= Reader -> reader;
holder(Pid, _) when Pid =:= self() -> own;
holder(_, _) -> other.

walk(First, Next) ->
    walk_keys(fun(K) -> Next(ord, K) end, First(ord), []).

walk_keys(_, '$end_of_table', Keys) -> lists:reverse(Keys);
walk_keys(Next, Key, Keys) -> walk_keys(Next, Next(Key), [Key | Keys]).

walk_keys(Tab, Key) ->
    walk_keys(fun(K) -> mnesia:next(Tab, K) end, Key, []).

chunks('$end_of_table') -> ['$end_of_table'];
chunks({Matches, Cont}) -> [Matches | chunks(mnesia:select(Cont))].

counts() ->
    [mnesia:system_info(I) || I <- [transaction_commits, transaction_failures, transaction_restarts]].

deltas(Before) ->
    lists:zipwith(fun(A, B) -> A - B end, counts(), Before).

% A transaction in a new process; its result comes back as {result, Pid, Result}.
spawn_tx(Parent, F) ->
    spawn(fun() -> Parent ! {result, self(), mnesia:transaction(F)} end).

result(Pid) ->
    receive
        {result, Pid, R} -> R
    after 5000 -> timeout
    end.

receive_go() ->
    receive
        go -> ok
    end.

wait_until(F) ->
    case F() of
        true ->
            ok;
        false ->
            timer:sleep(5),
            wait_until(F)
    end.
