-module(qlc_queries).
-moduledoc """
qlc (plan 21 step LIB73): compiled query list comprehensions over lists, ets, dets and qlc:table/2
tables. Covers evaluation, folds and cursors, constant lookups and match specifications, merge and
lookup joins (with equal keys on temporary files), cache and unique options (lists spilled to
files), sorting, info/1,2 in all formats, and the runtime-only paths of qlc_plain. Clause compiles
qlc_queries.abstr, these forms after OTP's qlc transform; OTP compiles this file.
""".
-export([main/1]).
-include_lib("stdlib/include/qlc.hrl").

-import(qlc_plain, [t/2, flat/1]).

main(_) ->
    basics(),
    tables(),
    joins(),
    caches(),
    infos(),
    qlc_plain:run(),
    io:format("qlc_queries: ok~n").

basics() ->
    L = [{1, a}, {2, b}, {3, c}, {2, d}],
    t(simple, fun() -> qlc:e(qlc:q([X || {X, _} <- L])) end),
    t(filter, fun() -> qlc:e(qlc:q([Y || {X, Y} <- L, X > 1])) end),
    t(nested_generators, fun() -> qlc:e(qlc:q([{X, Y} || X <- [1, 2], Y <- [a, b], X =/= 2 orelse Y =:= b])) end),
    t(pattern_constant, fun() -> qlc:e(qlc:q([X || {X, b} <- L])) end),
    t(repeated_variable, fun() -> qlc:e(qlc:q([X || {X, X} <- [{1, 1}, {1, 2}, {3, 3}]])) end),
    t(unique, fun() -> qlc:e(qlc:q([X || {X, _} <- L], unique)) end),
    t(fold, fun() -> qlc:fold(fun(X, A) -> X + A end, 0, qlc:q([X || {X, _} <- L])) end),
    t(fold_options, fun() -> qlc:fold(fun(X, A) -> [X | A] end, [], qlc:q([X || {X, _} <- L]), unique_all) end),
    t(eval_aliases, fun() -> [qlc:e(qlc:q([X || X <- [1]]), []), qlc:eval(qlc:q([X || X <- [2]]))] end),
    C = qlc:cursor(qlc:q([X || {X, _} <- L])),
    t(cursor, fun() -> {qlc:next_answers(C, 2), qlc:next_answers(C), qlc:delete_cursor(C)} end),
    t(sort_query, fun() -> qlc:e(qlc:sort(qlc:q([X || {_, X} <- L]), {order, descending})) end),
    t(append_query, fun() -> qlc:e(qlc:append(qlc:q([X || {X, _} <- L]), [x])) end),
    t(lookup_true, fun() -> qlc:e(qlc:q([X || X <- L], {lookup, true})) end),
    t(bad_le, fun() -> qlc:e(qlc:q([X || X <- foo])) end),
    t(bad_option, fun() -> qlc:q([X || X <- []], {max_lookup, -1}) end),
    t(le_error, fun() -> qlc:e(qlc:q([X || X <- qlc_error()])) end).

qlc_error() -> {error, qlc, {tmpdir_usage, testing}}.

tables() ->
    Log = ets:new(log, [public, ordered_set]),
    T = gb_table([{K, K * 10} || K <- lists:seq(1, 8)], Log),
    t(gb_all, fun() -> qlc:e(qlc:q([V || {_, V} <- T])) end),
    t(gb_lookup, fun() -> qlc:e(qlc:q([V || {K, V} <- T, K =:= 3])) end),
    t(gb_lookup_or, fun() -> qlc:e(qlc:q([V || {K, V} <- T, (K =:= 3) or (K =:= 5) or (K =:= 99)])) end),
    t(gb_lookup_equal, fun() -> qlc:e(qlc:q([V || {K, V} <- T, K == 4.0])) end),
    t(gb_max_lookup, fun() -> qlc:info(qlc:q([V || {K, V} <- T, (K =:= 3) or (K =:= 5)], {max_lookup, 1})) end),
    t(gb_info, fun() -> qlc:info(qlc:q([V || {K, V} <- T, K =:= 3])) end),
    t(gb_info_all, fun() -> qlc:info(qlc:q([V || {_, V} <- T]), {n_elements, 2}) end),
    t(gb_lookup_false, fun() -> qlc:info(qlc:q([V || {K, V} <- T, K =:= 3], {lookup, false})) end),
    t(log, fun() -> ets:tab2list(Log) end),
    Idx = idx_table([{a, 1, x}, {b, 2, y}, {c, 1, z}]),
    t(idx_lookup, fun() -> qlc:e(qlc:q([K || {K, N, _} <- Idx, N =:= 1])) end),
    t(idx_info, fun() -> qlc:info(qlc:q([K || {K, N, _} <- Idx, N =:= 1])) end),
    E = ets:new(e, [set]),
    ets:insert(E, [{1, a}, {2, b}, {3, c}]),
    t(ets_lookup, fun() -> qlc:e(qlc:q([Y || {X, Y} <- ets:table(E), X =:= 2])) end),
    t(ets_lookup_many, fun() -> lists:sort(qlc:e(qlc:q([Y || {X, Y} <- ets:table(E), X =:= 1 orelse X =:= 3]))) end),
    t(ets_ms, fun() -> lists:sort(qlc:e(qlc:q([Y || {X, Y} <- ets:table(E), X > 1]))) end),
    t(ets_ms_info, fun() -> flat(qlc:info(qlc:q([Y || {X, Y} <- ets:table(E), X > 1]))) end),
    t(ets_n_objects, fun() -> lists:sort(qlc:e(qlc:q([X || {X, _} <- ets:table(E, {n_objects, 1})]))) end),
    O = ets:new(o, [ordered_set]),
    ets:insert(O, [{1.0, float}, {2, int}, {3, three}]),
    t(ets_ordered_equal, fun() -> qlc:e(qlc:q([V || {K, V} <- ets:table(O), K == 1])) end),
    t(ets_ordered_match, fun() -> qlc:e(qlc:q([V || {K, V} <- ets:table(O), K =:= 1])) end),
    File = "qlc_queries.dets",
    _ = file:delete(File),
    {ok, D} = dets:open_file(qlc_queries_dets, [{file, File}]),
    ok = dets:insert(D, [{N, N * N} || N <- lists:seq(1, 6)]),
    t(dets_all, fun() -> lists:sort(qlc:e(qlc:q([V || {_, V} <- dets:table(D)]))) end),
    t(dets_lookup, fun() -> qlc:e(qlc:q([V || {K, V} <- dets:table(D), K =:= 4])) end),
    t(dets_lookup_many, fun() -> qlc:e(qlc:q([V || {K, V} <- dets:table(D), K =:= 4 orelse K =:= 5])) end),
    t(dets_ms, fun() -> lists:sort(qlc:e(qlc:q([K || {K, V} <- dets:table(D), V > 10]))) end),
    t(dets_info, fun() -> qlc:info(qlc:q([V || {K, V} <- dets:table(D), K =:= 4])) end),
    t(dets_info_many, fun() -> qlc:info(qlc:q([V || {K, V} <- dets:table(D), K =:= 4 orelse K =:= 5])) end),
    t(dets_info_ms, fun() -> qlc:info(qlc:q([K || {K, V} <- dets:table(D), V > 10])) end),
    ok = dets:close(D),
    _ = file:delete(File),
    T3 = qlc:table(fun(_MS) -> [{1, a}] end, [
        {lookup_fun, fun(_, _) -> {error, nolookup} end},
        {info_fun, fun
            (keypos) -> 1;
            (_) -> undefined
        end}
    ]),
    t(lookup_error, fun() -> qlc:e(qlc:q([V || {K, V} <- T3, K =:= 1])) end),
    t(lookup_error_ms, fun() -> qlc:e(qlc:q([V || {K, V} <- T3, K =:= 1, V =/= b])) end).

%% A qlc table over a gb_tree, as in the qlc manual, logging its pre and post funs.
gb_table(L, Log) ->
    Tree = gb_trees:from_orddict(lists:sort(L)),
    TF = fun() -> gb_next(gb_trees:next(gb_trees:iterator(Tree))) end,
    InfoFun = fun
        (num_of_objects) -> gb_trees:size(Tree);
        (keypos) -> 1;
        (is_sorted_key) -> true;
        (is_unique_objects) -> true;
        (_) -> undefined
    end,
    LookupFun = fun(1, Ks) -> lists:flatmap(fun(K) -> gb_lookup(K, Tree) end, Ks) end,
    FormatFun = fun
        ({all, NElements, ElementFun}) ->
            io_lib:format("gb_table:table(gb_trees:from_orddict(~w))", [gb_head(Tree, NElements, ElementFun)]);
        ({lookup, 1, KeyValues, _NElements, ElementFun}) ->
            io_lib:format("lists:flatmap(fun(K) -> lookup(K) end, ~w)", [ElementFun(KeyValues)])
    end,
    qlc:table(TF, [
        {info_fun, InfoFun},
        {format_fun, FormatFun},
        {lookup_fun, LookupFun},
        {key_equality, '=='},
        {pre_fun, fun(Args) -> ets:insert(Log, {pre, length(Args)}) end},
        {post_fun, fun() -> ets:insert(Log, {post, done}) end}
    ]).

gb_lookup(K, Tree) ->
    case gb_trees:lookup(K, Tree) of
        {value, V} -> [{K, V}];
        none -> []
    end.

gb_head(Tree, infinity, _) -> gb_trees:to_list(Tree);
gb_head(Tree, N, F) -> F(lists:sublist(gb_trees:to_list(Tree), N)).

gb_next({X, V, S}) -> [{X, V} | fun() -> gb_next(gb_trees:next(S)) end];
gb_next(none) -> [].

%% A table with an index on position 2 and no key position.
idx_table(L) ->
    qlc:table(fun() -> L end, [
        {info_fun, fun
            (indices) -> [2];
            (_) -> undefined
        end},
        {lookup_fun, fun(2, Ks) -> [T || T <- L, lists:member(element(2, T), Ks)] end},
        {format_fun, fun
            ({lookup, 2, Ks, _, _}) -> io_lib:format("idx(~w)", [Ks]);
            (_) -> "idx()"
        end}
    ]).

joins() ->
    A = [{K, K rem 3} || K <- lists:seq(1, 9)],
    B = [{R, list_to_atom([$a + R])} || R <- [0, 1, 2, 1]],
    t(merge, fun() -> qlc:e(qlc:q([{X, N} || {X, R} <- A, {R2, N} <- B, R =:= R2])) end),
    t(merge_info, fun() -> qlc:info(qlc:q([{X, N} || {X, R} <- A, {R2, N} <- B, R =:= R2])) end),
    t(merge_option, fun() -> qlc:e(qlc:q([{X, N} || {X, R} <- A, {R2, N} <- B, R == R2], {join, merge})) end),
    t(nested_loop, fun() -> qlc:e(qlc:q([{X, N} || {X, R} <- A, {R2, N} <- B, R =:= R2], {join, nested_loop})) end),
    t(nested_info, fun() -> qlc:info(qlc:q([{X, N} || {X, R} <- A, {R2, N} <- B, R =:= R2], {join, nested_loop})) end),
    t(lookup_impossible, fun() -> qlc:e(qlc:q([{X, N} || {X, R} <- A, {R2, N} <- B, R =:= R2], {join, lookup})) end),
    t(no_join, fun() -> qlc:e(qlc:q([X || {X, _} <- A], {join, merge})) end),
    t(merge_const, fun() -> qlc:e(qlc:q([{X, N} || {X, R} <- A, {R2, N} <- B, R =:= R2, N =:= b])) end),
    t(merge_const_info, fun() -> qlc:info(qlc:q([{X, N} || {X, R} <- A, {R2, N} <- B, R =:= R2, N =:= b])) end),
    Sorted = qlc:sort([{b, 2}, {a, 1}, {c, 3}]),
    MJ = qlc:q([{X, Y} || {X, _} <- Sorted, {Y, _} <- qlc:keysort(1, [{c, 9}, {a, 8}]), X =:= Y]),
    t(merge_sorted, fun() -> qlc:e(MJ) end),
    t(merge_sorted_info, fun() -> qlc:info(MJ) end),
    E = ets:new(j, [set]),
    ets:insert(E, [{0, zero, 10}, {1, one, 20}, {2, two, 30}]),
    t(lookup, fun() -> qlc:e(qlc:q([{X, N} || {X, R} <- A, {R2, N, _} <- ets:table(E), R =:= R2], {join, lookup})) end),
    t(lookup_rev, fun() -> qlc:e(qlc:q([{X, N} || {R2, N, _} <- ets:table(E), {X, R} <- A, R =:= R2])) end),
    LJ = qlc:q([{X, N} || {X, R} <- A, {R2, N, W} <- ets:table(E), R =:= R2, W > 15]),
    t(lookup_ms, fun() -> qlc:e(LJ) end),
    t(lookup_ms_info, fun() -> flat(qlc:info(LJ)) end),
    LJU = qlc:q([{X, N} || {X, R} <- A, {R2, N, _} <- ets:table(E), R =:= R2], [unique, {cache, list}]),
    t(lookup_unique, fun() -> qlc:e(LJU) end),
    t(lookup_unique_info, fun() -> flat(qlc:info(LJU)) end),
    Many = [{1, I} || I <- lists:seq(1, 300)],
    Few = [{1, a}, {1, b}, {2, c}],
    J = qlc:q([{I, Y} || {K, I} <- Many, {K2, Y} <- Few, K =:= K2]),
    t(merge_file, fun() -> length(qlc:e(J, {max_list_size, 100})) end),
    J2 = qlc:q([{I, Y} || {K2, Y} <- Few, {K, I} <- Many, K =:= K2]),
    t(merge_file2, fun() ->
        R = qlc:e(J2, {max_list_size, 100}),
        {length(R), lists:sum([I || {I, _} <- R])}
    end),
    t(merge_not_allowed, fun() -> qlc:e(J2, [{max_list_size, 100}, {tmpdir_usage, not_allowed}]) end),
    t(merge_cursor, fun() ->
        C = qlc:cursor(J2, {max_list_size, 100}),
        R = qlc:next_answers(C, 5),
        ok = qlc:delete_cursor(C),
        R
    end),
    Odd = [{K rem 2, I} || I <- lists:seq(1, 200), K <- [I]],
    Odd2 = [{K rem 2, -I} || I <- lists:seq(1, 150), K <- [I]],
    J3 = qlc:q([{P, Q} || {K, P} <- Odd, {K2, Q} <- Odd2, K =:= K2]),
    t(merge_both_file, fun() ->
        R = qlc:e(J3, {max_list_size, 64}),
        {length(R), lists:sum([P * Q || {P, Q} <- R])}
    end),
    t(merge_both_memory, fun() ->
        R = qlc:e(J3),
        {length(R), lists:sum([P * Q || {P, Q} <- R])}
    end).

caches() ->
    Log = ets:new(clog, [public, bag]),
    Counted = qlc:table(
        fun() ->
            ets:insert(Log, {traversed}),
            [1, 2, 3, 2]
        end,
        []
    ),
    Q = qlc:q([{X, Y} || X <- [a, b, c], Y <- Counted], cache),
    t(cache_ets, fun() -> qlc:e(Q) end),
    t(cache_count, fun() -> length(ets:lookup(Log, traversed)) end),
    Q2 = qlc:q([{X, Y} || X <- [a, b, c], Y <- qlc:q([Z || Z <- Counted], unique)], {cache, list}),
    t(cache_list, fun() -> qlc:e(Q2) end),
    t(cache_list_info, fun() -> qlc:info(Q2) end),
    Big = [{N, lists:seq(1, 20)} || N <- lists:seq(1, 50)],
    Q3 = qlc:q([{X, N} || X <- [a, b], {N, _} <- qlc:q([E || E <- Big], {cache, list})]),
    t(cache_list_file, fun() -> length(qlc:e(Q3, [{max_list_size, 1000}])) end),
    t(cache_list_not_allowed, fun() -> qlc:e(Q3, [{max_list_size, 1000}, {tmpdir_usage, not_allowed}]) end),
    Q4 = qlc:q([{X, N} || X <- [a, b], N <- qlc:q([E || E <- [3, 1, 3, 2, 1]], [{cache, list}, unique])]),
    t(unique_cache_list, fun() -> qlc:e(Q4) end),
    t(unique_cache_list_file, fun() -> qlc:e(Q4, {max_list_size, 0}) end),
    Q5 = qlc:q([{X, N} || X <- [a, b], N <- qlc:q([E || E <- Counted], [cache, unique])]),
    t(unique_cache_ets, fun() -> qlc:e(Q5) end),
    t(cache_all, fun() -> qlc:info(qlc:q([{X, Y} || X <- [1, 2], Y <- [3, 4]]), cache_all) end),
    t(cache_all_list, fun() -> qlc:info(qlc:q([{X, Y} || X <- [1, 2], Y <- [3, 4]]), {cache_all, list}) end),
    t(unique_all, fun() -> qlc:e(qlc:q([X || X <- [1, 1, 2, 1]]), unique_all) end),
    Nested = qlc:q([X || X <- qlc:q([Y || Y <- qlc:q([Z || Z <- [3, 1, 3]], unique)], cache)], unique),
    t(nested_simple, fun() -> qlc:e(Nested) end),
    t(nested_simple_info, fun() -> qlc:info(Nested) end).

infos() ->
    L = [{1, a}, {2, b}],
    Q = qlc:q([{X, Y} || {X, _} <- L, Y <- qlc:q([Z || Z <- [p, q]], unique), X > 1]),
    t(flat, fun() -> qlc:info(Q) end),
    t(not_flat, fun() -> qlc:info(Q, {flat, false}) end),
    %% The transform numbers annotations around the integers of the module, the header's path included.
    t(abstract, fun() -> erl_parse:map_anno(fun(_) -> 0 end, qlc:info(Q, [{format, abstract_code}, {flat, false}])) end),
    t(abstract_flat, fun() -> erl_parse:map_anno(fun(_) -> 0 end, qlc:info(Q, {format, abstract_code})) end),
    t(info_unique_all, fun() -> qlc:info(qlc:q([{X, Y} || X <- [x, y], Y <- [a, b]]), unique_all) end),
    t(depth, fun() -> qlc:info(qlc:q([X || X <- [{a, {b, {c, d}}}, "text", <<"binary">>, [1, 2, 3]]]), {depth, 2}) end),
    t(n_elements, fun() -> qlc:info(qlc:q([X || X <- lists:seq(1, 10)]), {n_elements, 2}) end),
    t(simple_info, fun() -> qlc:info(qlc:q([X || X <- [1, 2]], unique)) end),
    t(match_spec_info, fun() -> qlc:info(qlc:q([X || {X, _} <- L])) end),
    t(empty_list_info, fun() -> qlc:info(qlc:q([X || X <- []])) end),
    t(map_info, fun() -> qlc:info(qlc:q([X || X <- [#{a => 1}, #{}]])) end),
    t(fun_info, fun() -> qlc:info(qlc:q([X || X <- [fun lists:sum/1]])) end),
    t(append_info, fun() -> qlc:info(qlc:append([[1, 2], qlc:q([X || X <- [3, 4], X > 3])])) end).
