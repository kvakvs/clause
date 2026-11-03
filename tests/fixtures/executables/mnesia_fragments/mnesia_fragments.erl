%% mnesia's fragmented tables and SNMP indexes (plan 21 step MIM1G): the default hash module's states and fragment
%% numbers, fragmented tables created, read and written through the mnesia_frag access module, their information,
%% fragments added and deleted with their records rehashed, activation and deactivation, node pools, foreign keys, and
%% tables found and walked by their SNMP row indexes, each with OTP's results.
-module(mnesia_fragments).
-export([main/1]).

main(_) ->
    hash_module(),
    ok = mnesia:start(),
    create(),
    access(),
    information(),
    add_and_delete(),
    activation(),
    foreign_keys(),
    snmp(),
    stopped = mnesia:stop(),
    io:format("mnesia_fragments: ok~n").

% mnesia_frag_hash: linear hashing over phash2, and the phash states of old tables.
hash_module() ->
    S1 = mnesia_frag_hash:init_state(t, undefined),
    {States, _} = lists:mapfoldl(
        fun(_, S) ->
            {S2, From, To} = mnesia_frag_hash:add_frag(S),
            {{S2, From, To}, S2}
        end,
        S1,
        lists:seq(1, 6)
    ),
    {_, _, _} = Last = lists:last(States),
    S7 = element(1, Last),
    t(hash_module, fun() ->
        [
            S1,
            States,
            [mnesia_frag_hash:key_to_frag_number(S7, K) || K <- [1, 2, 3, a, "x", {k, 1}, 100, 1000]],
            mnesia_frag_hash:del_frag(S7),
            element(1, mnesia_frag_hash:del_frag(element(1, mnesia_frag_hash:del_frag(S7)))),
            mnesia_frag_hash:match_spec_to_frag_numbers(S7, [{{t, 5, '_'}, [], ['$_']}]),
            mnesia_frag_hash:match_spec_to_frag_numbers(S7, [{{t, '$1', '_'}, [], ['$_']}]),
            mnesia_frag_hash:match_spec_to_frag_numbers(S7, [{'_', [], ['$_']}, {'_', [], []}]),
            mnesia_frag_hash:add_frag({hash_state, 3, 2, 1}),
            [mnesia_frag_hash:key_to_frag_number({hash_state, 3, 2, 1}, K) || K <- [1, 2, 3, a]],
            catch_all(fun() -> mnesia_frag_hash:init_state(t, bogus) end)
        ]
    end).

% A fragmented table and the errors of its fragmentation properties.
create() ->
    t(create, fun() ->
        [
            mnesia:create_table(f, [{frag_properties, [{n_fragments, 4}, {node_pool, [node()]}]}, {index, [3]}]),
            [{T, mnesia:table_info(T, frag_properties)} || T <- [f, f_frag2, f_frag3, f_frag4]],
            lists:sort(mnesia:system_info(tables)),
            mnesia:table_info(f, ram_copies),
            mnesia:create_table(fd, [{frag_properties, [{n_fragments, 2}, {n_disc_copies, 1}]}]),
            mnesia:create_table(fb, [{type, bag}, {frag_properties, [{n_fragments, 3}]}])
        ]
    end),
    t(create_errors, fun() ->
        [
            mnesia:create_table(e1, [{frag_properties, [{bogus, 1}]}]),
            mnesia:create_table(e2, [{frag_properties, [foo]}]),
            mnesia:create_table(e3, [{frag_properties, [{n_fragments, 1}, {n_fragments, 2}]}]),
            mnesia:create_table(e4, [{frag_properties, [{n_fragments, 0}]}]),
            mnesia:create_table(e5, [{frag_properties, [{node_pool, foo}]}]),
            mnesia:create_table(e6, [{frag_properties, [{n_ram_copies, -1}]}]),
            mnesia:create_table(e7, [{frag_properties, [{n_ram_copies, 2}]}]),
            mnesia:create_table(e8, [{frag_properties, [{foreign_key, bogus}]}]),
            mnesia:create_table(e9, [{frag_properties, [{foreign_key, {e9, key}}]}]),
            mnesia:create_table(e10, [{frag_properties, [{n_fragments, 2}]}, {local_content, true}]),
            mnesia:create_table(e11, [{frag_properties, [{node_pool, [other@nohost]}]}]),
            mnesia:create_table(f, [{frag_properties, [{n_fragments, 2}]}]),
            mnesia:create_table(f_frag2, []),
            mnesia:create_table(e12, [{frag_properties, foo}]),
            lists:sort([
                T
             || T <- mnesia:system_info(tables), lists:member(T, [e1, e2, e3, e4, e5, e6, e7, e8, e9, e10])
            ])
        ]
    end).

% Reads and writes through mnesia_frag reach the key's fragment; reads without a key every fragment.
access() ->
    Frag = fun(F, Args) -> mnesia:activity(transaction, F, Args, mnesia_frag) end,
    Dirty = fun(F, Args) -> mnesia:activity(async_dirty, F, Args, mnesia_frag) end,
    Write = fun(Keys) ->
        [mnesia:write({f, K, K rem 3}) || K <- Keys],
        ok
    end,
    ok = Frag(Write, [lists:seq(1, 40)]),
    ok = Dirty(
        fun() ->
            [mnesia:write({fb, K rem 5, K}) || K <- lists:seq(1, 10)],
            ok
        end,
        []
    ),
    t(access, fun() ->
        [
            Frag(fun() -> mnesia:read({f, 7}) end, []),
            Frag(fun() -> mnesia:read(f, 41) end, []),
            [{T, lists:sort(mnesia:dirty_match_object(T, {f, '_', '_'}))} || T <- [f, f_frag2]],
            length(lists:usort(Frag(fun() -> mnesia:all_keys(f) end, []))),
            lists:sort(Frag(fun() -> mnesia:match_object({f, '_', 1}) end, [])),
            lists:sort(Frag(fun() -> mnesia:select(f, [{{f, '$1', 2}, [{'<', '$1', 12}], ['$1']}]) end, [])),
            Frag(fun() -> mnesia:select(f, [{{f, 5, '$1'}, [], ['$1']}]) end, []),
            Dirty(fun() -> lists:sort(mnesia:index_read(f, 0, 3)) end, []),
            Frag(fun() -> mnesia:foldl(fun({f, K, _}, A) -> K + A end, 0, f) end, []),
            Frag(fun() -> walk(mnesia:first(f), 0) end, []),
            Frag(fun() -> walk_back(mnesia:last(f), 0) end, []),
            Frag(fun() -> chunks(mnesia:select(f, [{{f, '$1', '_'}, [], ['$1']}], 7, read), []) end, []),
            Frag(
                fun() ->
                    mnesia:delete({f, 7}),
                    mnesia:read({f, 7})
                end,
                []
            ),
            Frag(
                fun() ->
                    mnesia:delete_object({f, 8, 2}),
                    mnesia:read({f, 8})
                end,
                []
            ),
            Frag(fun() -> lists:sort(mnesia:read({fb, 3})) end, []),
            Frag(
                fun() ->
                    mnesia:write({f, 7, 1}),
                    lists:sort(mnesia:match_object({f, 7, '_'}))
                end,
                []
            ),
            Frag(
                fun() ->
                    mnesia:lock({table, f}, write),
                    lists:sort([{O, K} || {O, K, _} <- mnesia:system_info(held_locks)])
                end,
                []
            ),
            mnesia:activity(ets, fun() -> mnesia:read({f, 9}) end, [], mnesia_frag),
            catch_all(fun() -> Frag(fun() -> mnesia:read({nofrag, 1}) end, []) end)
        ]
    end).

walk('$end_of_table', N) -> N;
walk(Key, N) -> walk(mnesia:next(f, Key), N + 1).

walk_back('$end_of_table', N) -> N;
walk_back(Key, N) -> walk_back(mnesia:prev(f, Key), N + 1).

chunks('$end_of_table', Acc) -> lists:sort(lists:append(Acc));
chunks({Keys, Cont}, Acc) -> chunks(mnesia:select(Cont), [Keys | Acc]).

% The table information mnesia_frag gives, and what the default access module gives.
information() ->
    Info = fun(Tab, Item) ->
        mnesia:activity(async_dirty, fun() -> mnesia:table_info(Tab, Item) end, [], mnesia_frag)
    end,
    Items = [
        base_table,
        node_pool,
        n_fragments,
        foreign_key,
        foreigners,
        n_ram_copies,
        n_disc_copies,
        n_disc_only_copies,
        frag_names,
        frag_dist,
        frag_size,
        size,
        type,
        {f, 5}
    ],
    t(information, fun() ->
        [
            [{I, catch_all(fun() -> info_of(Info, I) end)} || I <- Items],
            mnesia:table_info(f, size),
            mnesia:table_info(f, frag_properties),
            catch_all(fun() -> Info(f_frag2, n_fragments) end),
            catch_all(fun() -> Info(plain, n_fragments) end),
            Info(fb, frag_size),
            lists:sort([I || {I, _} <- mnesia:table_info(f, all), I =:= frag_hash orelse I =:= frag_properties])
        ]
    end).

info_of(Info, {Tab, Key}) -> Info({Tab, Key}, type);
info_of(Info, Item) -> Info(f, Item).

% Fragments added and deleted, the records moved to the fragments they now hash to.
add_and_delete() ->
    Info = fun(Item) -> mnesia:activity(async_dirty, fun() -> mnesia:table_info(f, Item) end, [], mnesia_frag) end,
    t(add_and_delete, fun() ->
        [
            mnesia:change_table_frag(f, {add_frag, [node()]}),
            Info(frag_size),
            Info(n_fragments),
            mnesia:table_info(f, frag_properties),
            mnesia:change_table_frag(f, {add_frag, Info(frag_dist)}),
            Info(frag_size),
            mnesia:dirty_index_read(f_frag6, 1, 3) =:= [R || R <- mnesia:dirty_match_object(f_frag6, {f, '_', 1})],
            mnesia:change_table_frag(f, del_frag),
            mnesia:change_table_frag(f, del_frag),
            mnesia:change_table_frag(f, del_frag),
            Info(frag_size),
            mnesia:activity(transaction, fun() -> length(mnesia:all_keys(f)) end, [], mnesia_frag),
            lists:sort([T || T <- mnesia:system_info(tables), T =/= schema]),
            mnesia:change_table_frag(fb, {add_frag, [node(), other@nohost]}),
            mnesia:change_table_frag(fb, {add_frag, [other@nohost]}),
            mnesia:change_table_frag(fb, {add_frag, foo}),
            mnesia:change_table_frag(fb, {add_frag, [{node(), x}]}),
            mnesia:change_table_frag(fb, bogus),
            mnesia:change_table_frag(schema, del_frag),
            mnesia:change_table_frag(nope, del_frag)
        ]
    end).

% A plain table made fragmented and back; the node pool.
activation() ->
    {atomic, ok} = mnesia:create_table(p, []),
    [ok = mnesia:dirty_write({p, K, K}) || K <- lists:seq(1, 10)],
    Info = fun(Item) -> mnesia:activity(async_dirty, fun() -> mnesia:table_info(p, Item) end, [], mnesia_frag) end,
    t(activation, fun() ->
        [
            mnesia:change_table_frag(p, deactivate),
            mnesia:change_table_frag(p, {add_frag, [node()]}),
            mnesia:change_table_frag(p, {activate, [{n_fragments, 2}]}),
            mnesia:change_table_frag(p, {activate, []}),
            mnesia:table_info(p, frag_properties),
            mnesia:change_table_frag(p, {activate, []}),
            mnesia:change_table_frag(p, {add_frag, [node()]}),
            Info(frag_size),
            mnesia:change_table_frag(p, deactivate),
            mnesia:change_table_frag(p_frag2, deactivate),
            mnesia:change_table_frag(p, {add_node, other@nohost}),
            Info(node_pool),
            Info(frag_dist),
            mnesia:change_table_frag(p, {add_node, other@nohost}),
            mnesia:change_table_frag(p, {del_node, other@nohost}),
            mnesia:change_table_frag(p, {del_node, other@nohost}),
            mnesia:change_table_majority(p, true),
            [mnesia:table_info(T, majority) || T <- [p, p_frag2]],
            mnesia:change_table_majority(p_frag2, false),
            mnesia:change_table_majority(p, false),
            mnesia:change_table_frag(p, {add_node, "x"}),
            mnesia:change_table_frag(p, del_frag),
            mnesia:change_table_frag(p, deactivate),
            mnesia:table_info(p, frag_properties),
            lists:sort(mnesia:dirty_all_keys(p)),
            mnesia:delete_table(f_frag2),
            mnesia:delete_table(fb),
            lists:sort([T || T <- mnesia:system_info(tables), T =/= schema])
        ]
    end).

% A table whose records go to the fragment of another table's key, and follow its fragments.
foreign_keys() ->
    Frag = fun(F, Args) -> mnesia:activity(transaction, F, Args, mnesia_frag) end,
    Info = fun(Tab, Item) ->
        mnesia:activity(async_dirty, fun() -> mnesia:table_info(Tab, Item) end, [], mnesia_frag)
    end,
    t(foreign_keys, fun() ->
        [
            mnesia:create_table(prim, [{frag_properties, [{n_fragments, 3}, {node_pool, [node()]}]}]),
            mnesia:create_table(sec, [{attributes, [sk, pk]}, {frag_properties, [{foreign_key, {prim, pk}}]}]),
            mnesia:table_info(sec, frag_properties),
            Frag(
                fun() ->
                    [mnesia:write({prim, K, -K}) || K <- [11, 12]],
                    mnesia:write({sec, 42, 11})
                end,
                []
            ),
            Frag(fun() -> mnesia:read({sec, 11}, 42, read) end, []),
            mnesia:change_table_frag(prim, {add_frag, [node()]}),
            Info(sec, n_fragments),
            Info(prim, foreigners),
            Frag(fun() -> mnesia:read({sec, 11}, 42, read) end, []),
            [{T, Info(T, frag_size)} || T <- [prim, sec]],
            mnesia:change_table_frag(sec, {add_frag, [node()]}),
            mnesia:change_table_frag(prim, del_frag),
            Info(sec, n_fragments),
            mnesia:create_table(bad1, [{frag_properties, [{foreign_key, {prim, pk}}, {n_fragments, 9}]}]),
            mnesia:create_table(bad2, [{attributes, [k, pk]}, {frag_properties, [{foreign_key, {sec, pk}}]}]),
            mnesia:create_table(bad3, [{frag_properties, [{foreign_key, {nope, pk}}]}]),
            mnesia:change_table_frag(prim, deactivate),
            mnesia:delete_table(prim),
            mnesia:delete_table(sec),
            mnesia:delete_table(prim)
        ]
    end).

% A table found and walked by SNMP row indexes: integer, string and fix_string keys.
snmp() ->
    t(snmp, fun() ->
        [
            mnesia:create_table(s1, [{snmp, [{key, integer}]}]),
            [mnesia:dirty_write({s1, K, K * 10}) || K <- [3, 1, 2]],
            mnesia:snmp_get_row(s1, [2]),
            mnesia:snmp_get_row(s1, [9]),
            mnesia:snmp_get_next_index(s1, []),
            mnesia:snmp_get_next_index(s1, [1]),
            mnesia:snmp_get_next_index(s1, [3]),
            mnesia:snmp_get_mnesia_key(s1, [3]),
            mnesia:snmp_get_mnesia_key(s1, [4]),
            catch_all(fun() -> mnesia:dirty_write({s1, foo, 1}) end),
            mnesia:transaction(fun() -> mnesia:write({s1, foo, 1}) end),
            mnesia:transaction(fun() ->
                mnesia:write({s1, 5, 50}),
                mnesia:delete({s1, 2}),
                [
                    mnesia:snmp_get_row(s1, [5]),
                    mnesia:snmp_get_row(s1, [2]),
                    mnesia:snmp_get_next_index(s1, [1]),
                    mnesia:snmp_get_next_index(s1, [3]),
                    mnesia:snmp_get_mnesia_key(s1, [5]),
                    mnesia:snmp_get_mnesia_key(s1, [2])
                ]
            end),
            mnesia:snmp_get_next_index(s1, [3]),
            mnesia:dirty_delete(s1, 5),
            mnesia:snmp_get_next_index(s1, [3]),
            mnesia:table_info(s1, snmp)
        ]
    end),
    t(snmp_tables, fun() ->
        [
            mnesia:create_table(s2, [{attributes, [k, v]}]),
            [mnesia:dirty_write({s2, K, 0}) || K <- [{"ab", 7}, {"b", 1}, {"ab", 2}]],
            mnesia:snmp_open_table(s2, [{key, {string, integer}}]),
            mnesia:snmp_get_next_index(s2, []),
            mnesia:snmp_get_next_index(s2, [2, $a, $b, 7]),
            mnesia:snmp_get_row(s2, [1, $b, 1]),
            mnesia:snmp_open_table(s2, [{key, integer}]),
            mnesia:snmp_open_table(s1, [{key, integer}]),
            mnesia:snmp_open_table(s2, [{key, bogus}]),
            mnesia:snmp_open_table(schema, [{key, integer}]),
            mnesia:snmp_open_table(nope, [{key, integer}]),
            mnesia:snmp_close_table(s2),
            catch_all(fun() -> mnesia:snmp_get_row(s2, [1, $b, 1]) end),
            mnesia:snmp_close_table(s2),
            mnesia:create_table(s3, [{snmp, [{key, {integer, fix_string}}]}]),
            mnesia:dirty_write({s3, {1, "xy"}, a}),
            mnesia:snmp_get_row(s3, [1, $x, $y]),
            mnesia:snmp_get_mnesia_key(s3, [1, $x, $y]),
            mnesia:transaction(fun() -> mnesia:snmp_get_row(s3, [1, $x, $y]) end),
            mnesia:clear_table(s3),
            mnesia:snmp_get_next_index(s3, []),
            mnesia:create_table(s4, [{snmp, [{key, bogus}]}]),
            catch_all(fun() -> mnesia:snmp_get_row(s4, [1]) end),
            catch_all(fun() -> mnesia:snmp_get_row(nope, [1]) end),
            catch_all(fun() -> mnesia:snmp_get_row(s1, foo) end)
        ]
    end).

%% Helpers.

t(Label, F) ->
    io:format("~p: ~p~n", [Label, catch_all(F)]).

catch_all(F) ->
    try
        F()
    catch
        Class:Reason -> {Class, hide(Reason)}
    end.

% Exceptions without their stack traces, which differ.
hide({Reason, [{_, _, _, _} | _]}) -> {Reason, stack};
hide({aborted, {Reason, [{_, _, _, _} | _]}}) -> {aborted, {Reason, stack}};
hide(Reason) -> Reason.
