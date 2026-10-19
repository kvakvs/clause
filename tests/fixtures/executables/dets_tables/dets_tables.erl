%% dets (plan 21 step LIB53): tables of every type, their options and errors, reads, updates, matches and selects in
%% chunks, traversals, fixes, bulk loads, repairs and users. The MD5 of a file after it is closed shows that Clause
%% writes the bytes OTP writes.
-module(dets_tables).
-export([main/1]).

main(_) ->
    ok = file:write_file(
        "plain.txt", <<"not a dets file, but long enough to have a dets header in it.">>
    ),
    files(),
    sets(),
    bags(),
    matches(),
    traversals(),
    bulk(),
    repairs(),
    users(),
    errors(),
    io:format("dets_tables: ok~n").

%% Delete the file, run Fun on it and print the MD5 of what is left.
fresh(Name) ->
    _ = file:delete(Name),
    Name.

digest(File) ->
    {ok, Bin} = file:read_file(File),
    {byte_size(Bin), binary:encode_hex(erlang:md5(Bin))}.

%% The class and reason of an exception, without its stack.
attempt(Fun) ->
    try
        Fun()
    catch
        Class:Reason -> {Class, Reason}
    end.

show(Label, Value) ->
    io:format("~s: ~p~n", [Label, Value]).

files() ->
    F = fresh("files.dets"),
    {ok, files} = dets:open_file(files, [{file, F}, {min_no_slots, 1000}, {auto_save, infinity}]),
    show("new info", dets:info(files)),
    Items = [
        access,
        auto_save,
        filename,
        fixed,
        hash,
        keypos,
        no_keys,
        no_objects,
        no_slots,
        ram_file,
        size,
        type
    ],
    show("items", [{Item, dets:info(files, Item)} || Item <- Items]),
    show("unknown item", dets:info(files, nonsense)),
    show("delayed write", dets:info(files, delayed_write)),
    show("version", dets:info(files, version)),
    show("bchunk format", is_binary(dets:info(files, bchunk_format))),
    show("is_dets_file", [
        dets:is_dets_file(F), dets:is_dets_file("plain.txt"), dets:is_dets_file("none")
    ]),
    ok = dets:close(files),
    show("closed info", [dets:info(files), dets:info(files, size)]),
    show("closed file", digest(F)),
    {ok, Ref} = dets:open_file(F),
    show("open_file/1", [
        is_reference(Ref), dets:info(Ref, type), dets:info(Ref, no_slots), dets:info(Ref, auto_save)
    ]),
    ok = dets:close(Ref),
    R = fresh("ram.dets"),
    {ok, ram} = dets:open_file(ram, [{file, R}, {ram_file, true}, {type, duplicate_bag}]),
    ok = dets:insert(ram, [{k, 1}, {k, 1}, {j, 2}]),
    show("ram", [
        dets:info(ram, ram_file), dets:lookup(ram, k), file:read_file_info(R) =/= {error, enoent}
    ]),
    ok = dets:sync(ram),
    show("ram synced", digest(R)),
    ok = dets:close(ram),
    show("ram closed", digest(R)),
    {ok, ram} = dets:open_file(ram, [{file, R}, {type, duplicate_bag}, {access, read}]),
    show("read only", [dets:lookup(ram, k), dets:insert(ram, {x, 1}), dets:info(ram, access)]),
    ok = dets:close(ram).

sets() ->
    F = fresh("set.dets"),
    {ok, s} = dets:open_file(s, [{file, F}, {keypos, 2}]),
    ok = dets:insert(s, [{x, a, 1}, {x, b, 2}, {y, c, 3}]),
    ok = dets:insert(s, {z, a, 9}),
    show("lookup", [dets:lookup(s, a), dets:lookup(s, b), dets:lookup(s, none)]),
    show("insert_new", [
        dets:insert_new(s, {q, a, 0}), dets:insert_new(s, [{q, d, 0}, {q, e, 0}]), dets:lookup(s, d)
    ]),
    show("member", [dets:member(s, a), dets:member(s, zz)]),
    show("update_counter", [
        dets:update_counter(s, a, 5), dets:update_counter(s, a, {3, -2}), dets:lookup(s, a)
    ]),
    show("counter errors", [
        attempt(fun() -> dets:update_counter(s, none, 1) end),
        attempt(fun() -> dets:update_counter(s, a, {1, 1}) end)
    ]),
    ok = dets:delete(s, b),
    ok = dets:delete_object(s, {q, d, 0}),
    ok = dets:delete_object(s, {q, e, 1}),
    show("after deletes", lists:sort(dets:match_object(s, '_'))),
    show("bad objects", [
        attempt(fun() -> dets:insert(s, {short}) end),
        attempt(fun() -> dets:insert(s, [{a, b}, x]) end)
    ]),
    show("info", [dets:info(s, size), dets:info(s, no_keys)]),
    ok = dets:close(s),
    show("set file", digest(F)),
    G = fresh("grow.dets"),
    {ok, g} = dets:open_file(g, [{file, G}]),
    [ok = dets:insert(g, {N, integer_to_list(N)}) || N <- lists:seq(1, 1500)],
    ok = dets:insert(g, [{N, N} || N <- lists:seq(1000, 3000)]),
    show("grown", [
        dets:info(g, no_slots), dets:info(g, size), dets:lookup(g, 1200), dets:lookup(g, 7)
    ]),
    [ok = dets:delete(g, N) || N <- lists:seq(1, 3000, 3)],
    show("shrunk", [dets:info(g, no_slots), dets:info(g, size), dets:first(g)]),
    show("slots", [dets:slot(g, 0), dets:slot(g, 5), dets:slot(g, 100000)]),
    ok = dets:close(g),
    show("grown file", digest(G)).

bags() ->
    F = fresh("bag.dets"),
    {ok, b} = dets:open_file(b, [{file, F}, {type, bag}]),
    ok = dets:insert(b, [{k, 1}, {k, 2}, {k, 1}, {j, 3}]),
    ok = dets:insert(b, {k, 3}),
    show("bag", [dets:lookup(b, k), dets:lookup(b, j)]),
    ok = dets:delete_object(b, {k, 2}),
    ok = dets:insert(b, {k, 1}),
    show("bag reinserted", dets:lookup(b, k)),
    show("bag counter", attempt(fun() -> dets:update_counter(b, k, 1) end)),
    ok = dets:close(b),
    show("bag file", digest(F)),
    D = fresh("dup.dets"),
    {ok, d} = dets:open_file(d, [{file, D}, {type, duplicate_bag}]),
    ok = dets:insert(d, [{k, 1}, {k, 1}, {k, 2}]),
    ok = dets:insert(d, [{k, 1}, {j, 5}]),
    show("dup", dets:lookup(d, k)),
    ok = dets:delete_object(d, {k, 1}),
    show("dup deleted", [dets:lookup(d, k), dets:info(d, size)]),
    [ok = dets:insert(d, {N rem 37, N}) || N <- lists:seq(1, 2000)],
    show("dup many", [
        dets:info(d, size), dets:info(d, no_keys), dets:info(d, no_slots), length(dets:lookup(d, 3))
    ]),
    ok = dets:close(d),
    show("dup file", digest(D)).

matches() ->
    F = fresh("match.dets"),
    {ok, m} = dets:open_file(m, [{file, F}]),
    ok = dets:insert(m, [{N, N * N, [N]} || N <- lists:seq(1, 400)]),
    show("match", lists:sort(dets:match(m, {'$1', 16, '_'}))),
    show("match_object", dets:match_object(m, {3, '_', '_'})),
    show(
        "select",
        lists:sort(dets:select(m, [{{'$1', '$2', '_'}, [{'<', '$2', 50}], [{{'$1', '$2'}}]}]))
    ),
    show("select keys", dets:select(m, [{{K, '_', '_'}, [], ['$_']} || K <- [7, 2, 9]])),
    show("full order", [element(1, O) || O <- dets:match_object(m, '_')]),
    {C1, Cont1} = dets:select(m, [{{'$1', '_', '_'}, [], ['$1']}], 100),
    {C2, Cont2} = dets:select(Cont1),
    show("chunks", [length(C1), length(C2), C1 -- lists:seq(1, 400), lists:sublist(C2, 5)]),
    show("chunks end", chunks(dets:select(Cont2), 2)),
    {M1, _} = dets:match(m, {'$1', '$2', '_'}, 7),
    show("match chunk", lists:sublist(M1, 3)),
    {O1, _} = dets:match_object(m, {'_', '_', [5]}, 50),
    show("object chunk", O1),
    show("empty select", element(1, dets:select(m, [{{nope, '_', '_'}, [], ['$_']}], 10))),
    show("select_delete", dets:select_delete(m, [{{'$1', '_', '_'}, [{'>', '$1', 390}], [true]}])),
    ok = dets:match_delete(m, {'_', '_', [3]}),
    show("after match_delete", [dets:info(m, size), dets:lookup(m, 3), dets:lookup(m, 395)]),
    show("bad specs", [
        attempt(fun() -> dets:select(m, nonsense) end),
        attempt(fun() -> dets:select(m, [{'_', [], []}], -1) end)
    ]),
    show("delete all", [dets:delete_all_objects(m), dets:info(m, size), dets:match_object(m, '_')]),
    ok = dets:close(m),
    show("match file", digest(F)).

chunks('$end_of_table', N) -> {done, N};
chunks({Found, Cont}, N) -> chunks(dets:select(Cont), N + length(Found)).

traversals() ->
    F = fresh("walk.dets"),
    {ok, w} = dets:open_file(w, [{file, F}, {type, bag}]),
    ok = dets:insert(w, [{N rem 50, N} || N <- lists:seq(1, 300)]),
    show("foldl", dets:foldl(fun({K, V}, Acc) -> Acc + K * V end, 0, w)),
    show(
        "foldr",
        dets:foldr(fun({K, _}, Acc) -> [K | Acc] end, [], w) =:=
            dets:foldl(fun({K, _}, Acc) -> [K | Acc] end, [], w)
    ),
    show(
        "traverse",
        length(
            dets:traverse(w, fun
                ({K, V}) when K < 3 -> {continue, V};
                (_) -> continue
            end)
        )
    ),
    show(
        "traverse done",
        dets:traverse(w, fun
            ({K, _}) when K =:= 7 -> {done, seven};
            (_) -> continue
        end)
    ),
    show("traverse other", dets:traverse(w, fun(_) -> stop end)),
    Keys = walk(w, dets:first(w), []),
    show("first next", [length(Keys), lists:sublist(Keys, 8)]),
    E = ets:new(e, [bag]),
    E = dets:to_ets(w, E),
    show("to_ets", [ets:info(E, size), lists:sort(ets:lookup(E, 4))]),
    show("safe_fixtable", [
        dets:safe_fixtable(w, true), dets:info(w, fixed), dets:safe_fixtable(w, true)
    ]),
    ok = dets:insert(w, {1000, 1}),
    {_, Counters} = dets:info(w, safe_fixed_monotonic_time),
    show("fix counters", [Count || {_, Count} <- Counters]),
    show("unfix", [
        dets:safe_fixtable(w, false),
        dets:info(w, fixed),
        dets:safe_fixtable(w, false),
        dets:info(w, fixed)
    ]),
    show("delete fixed", [
        dets:safe_fixtable(w, true), dets:delete_all_objects(w), dets:info(w, size)
    ]),
    ok = dets:safe_fixtable(w, false),
    Source = ets:new(source, [bag]),
    true = ets:insert(Source, [{K rem 9, K} || K <- lists:seq(1, 40)]),
    show("still fixed", [dets:info(w, fixed), attempt(fun() -> ets:to_dets(Source, w) end)]),
    {ok, conv} = dets:open_file(conv, [{file, fresh("conv.dets")}, {type, bag}]),
    show("ets:to_dets", [ets:to_dets(Source, conv), dets:info(conv, size), dets:lookup(conv, 3)]),
    Back = ets:new(back, [bag]),
    show("ets:from_dets", [
        ets:from_dets(Back, conv), ets:info(Back, size), lists:sort(ets:lookup(Back, 4))
    ]),
    show("ets conversion errors", [
        attempt(fun() -> ets:to_dets(Source, none) end),
        attempt(fun() -> ets:from_dets(Back, none) end),
        attempt(fun() -> ets:to_dets(none, conv) end)
    ]),
    ok = dets:close(conv),
    ok = dets:close(w),
    show("walk file", digest(F)).

walk(_T, '$end_of_table', Acc) -> lists:reverse(Acc);
walk(T, Key, Acc) -> walk(T, dets:next(T, Key), [Key | Acc]).

bulk() ->
    F = fresh("bulk.dets"),
    {ok, i} = dets:open_file(i, [{file, F}]),
    ok = dets:insert(i, {old, 1}),
    Chunks = [[{N, N} || N <- lists:seq(S, S + 99)] || S <- lists:seq(1, 1000, 100)],
    show("init_table", dets:init_table(i, reader(Chunks))),
    show("loaded", [
        dets:info(i, size), dets:info(i, no_slots), dets:lookup(i, old), dets:lookup(i, 555)
    ]),
    show("loaded order", lists:sublist([K || {K, _} <- dets:match_object(i, '_')], 10)),
    ok = dets:close(i),
    show("bulk file", digest(F)),
    {ok, i} = dets:open_file(i, [{file, F}]),
    E = ets:new(e, [set]),
    true = ets:insert(E, [{K, a} || K <- lists:seq(1, 300)]),
    show("from_ets", [
        dets:from_ets(i, E), dets:info(i, size), dets:lookup(i, 600), dets:lookup(i, 3)
    ]),
    show("init errors", [
        dets:init_table(i, fun
            (read) -> {[{1, 2}], bad};
            (close) -> ok
        end),
        dets:info(i, size),
        dets:insert(i, {1, 1})
    ]),
    show("corrupt close", dets:close(i)),
    G = fresh("bulk_bag.dets"),
    {ok, j} = dets:open_file(j, [{file, G}, {type, bag}]),
    show(
        "bag init",
        dets:init_table(j, reader([[{K rem 7, K} || K <- lists:seq(1, 50)], [{1, 1}, {1, 1}]]), [
            {min_no_slots, 512}
        ])
    ),
    show("bag loaded", [dets:info(j, size), dets:lookup(j, 1), dets:info(j, no_slots)]),
    {Start, Data} = dets:bchunk(j, start),
    show("bchunk", [
        length(Data),
        dets:is_compatible_bchunk_format(j, hd(Data)),
        dets:is_compatible_bchunk_format(j, <<"x">>)
    ]),
    show("bchunk rest", bchunks(j, dets:bchunk(j, Start), 0)),
    K = fresh("copy.dets"),
    {ok, k} = dets:open_file(k, [{file, K}, {type, bag}]),
    show("bchunk copy", dets:init_table(k, bchunk_reader(j, start), [{format, bchunk}])),
    show("copied", [dets:info(k, size), dets:info(k, no_slots), dets:lookup(k, 1)]),
    ok = dets:close(k),
    show("copy file", digest(K)),
    {ok, wrong} = dets:open_file(wrong, [{file, fresh("wrong.dets")}]),
    show(
        "wrong format",
        element(1, dets:init_table(wrong, bchunk_reader(j, start), [{format, bchunk}]))
    ),
    _ = dets:close(wrong),
    ok = dets:close(j),
    show("bulk bag file", digest(G)),
    H = fresh("big.dets"),
    {ok, h} = dets:open_file(h, [{file, H}]),
    ok = dets:insert(h, [{N, binary:copy(<<N:16>>, 100)} || N <- lists:seq(1, 3000)]),
    L = fresh("big_copy.dets"),
    {ok, l} = dets:open_file(l, [{file, L}]),
    show("big copy", dets:init_table(l, bchunk_reader(h, start), [{format, bchunk}])),
    show("big copied", [
        dets:info(l, size), dets:info(l, no_slots), dets:lookup(l, 2999) =:= dets:lookup(h, 2999)
    ]),
    ok = dets:close(l),
    ok = dets:close(h),
    show("big files", [digest(H), digest(L)]).

bchunk_reader(T, Cont) ->
    fun
        (read) ->
            case dets:bchunk(T, Cont) of
                '$end_of_table' -> end_of_input;
                {Next, Data} -> {Data, bchunk_reader(T, Next)}
            end;
        (close) ->
            ok
    end.

reader([]) ->
    fun
        (read) -> end_of_input;
        (close) -> ok
    end;
reader([Chunk | Chunks]) ->
    fun
        (read) -> {Chunk, reader(Chunks)};
        (close) -> ok
    end.

bchunks(_T, '$end_of_table', N) -> {done, N};
bchunks(T, {Cont, Data}, N) -> bchunks(T, dets:bchunk(T, Cont), N + length(Data)).

repairs() ->
    F = fresh("repair.dets"),
    {ok, r} = dets:open_file(r, [{file, F}, {type, set}]),
    ok = dets:insert(r, [{N, lists:duplicate(N rem 20, x)} || N <- lists:seq(1, 600)]),
    ok = dets:insert(r, {extra, 1}),
    ok = dets:delete(r, 5),
    ok = dets:close(r),
    show("before repair", digest(F)),
    C = fresh("crashed.dets"),
    not_closed(F, C),
    show("needs repair", dets:open_file(c, [{file, C}, {repair, false}])),
    {ok, c} = dets:open_file(c, [{file, C}]),
    show("repaired", [
        dets:info(c, size), dets:info(c, no_slots), dets:lookup(c, extra), dets:lookup(c, 5)
    ]),
    show("repaired order", lists:sublist([K || {K, _} <- dets:match_object(c, '_')], 10)),
    ok = dets:close(c),
    show("repaired file", digest(C)),
    not_closed(F, C),
    {ok, Ref} = dets:open_file(C),
    show("open_file/1 repaired", [dets:info(Ref, size), dets:info(Ref, no_slots)]),
    ok = dets:close(Ref),
    show("repaired again", digest(C)),
    {ok, _} = file:copy(F, C),
    {ok, c} = dets:open_file(c, [{file, C}, {repair, force}]),
    show("forced", [dets:info(c, size), dets:info(c, no_slots), dets:lookup(c, 77)]),
    show("forced order", lists:sublist([K || {K, _} <- dets:match_object(c, '_')], 10)),
    ok = dets:close(c),
    show("forced file", digest(C)),
    {ok, _} = file:copy(F, C),
    show("forced read", dets:open_file(c, [{file, C}, {repair, force}, {access, read}])).

%% Copy a closed file, marked as not properly closed.
not_closed(From, To) ->
    {ok, <<Head:8/binary, _:4/binary, Rest/binary>>} = file:read_file(From),
    ok = file:write_file(To, <<Head/binary, 0:32, Rest/binary>>).

users() ->
    F = fresh("users.dets"),
    Parent = self(),
    {ok, u} = dets:open_file(u, [{file, F}]),
    {ok, u} = dets:open_file(u, [{file, F}]),
    show("incompatible", dets:open_file(u, [{file, F}, {type, bag}])),
    Pid = spawn(fun() ->
        {ok, u} = dets:open_file(u, [{file, F}]),
        ok = dets:insert(u, {from_other, 1}),
        Parent ! {self(), opened},
        receive
            stop -> ok
        end
    end),
    receive
        {Pid, opened} -> ok
    end,
    show("users", [length(dets:info(u, users)), lists:member(u, dets:all())]),
    show("pid2name", [dets:pid2name(dets:info(u, owner)), dets:pid2name(self())]),
    Ref = erlang:monitor(process, Pid),
    Pid ! stop,
    receive
        {'DOWN', Ref, process, Pid, _} -> ok
    end,
    sync_server(),
    show("after user exit", [length(dets:info(u, users)), dets:lookup(u, from_other)]),
    show("closes", [
        dets:close(u), dets:info(u, size), dets:close(u), dets:info(u, size), dets:close(u)
    ]),
    Q = fresh("orphan.dets"),
    Owner = spawn(fun() ->
        {ok, orphan} = dets:open_file(orphan, [{file, Q}]),
        ok = dets:insert(orphan, {k, v}),
        Parent ! {self(), opened},
        receive
            stop -> ok
        end
    end),
    receive
        {Owner, opened} -> ok
    end,
    OwnerRef = erlang:monitor(process, Owner),
    exit(Owner, kill),
    receive
        {'DOWN', OwnerRef, process, Owner, _} -> ok
    end,
    sync_server(),
    show("owner killed", [dets:info(orphan), lists:member(orphan, dets:all())]),
    show("orphan file", digest(Q)).

%% Wait until the dets server has handled what was sent to it before.
sync_server() ->
    _ = dets:all(),
    ok.

errors() ->
    F = fresh("errors.dets"),
    show("bad options", [
        attempt(fun() -> dets:open_file(e, [{type, ordered_set}]) end),
        attempt(fun() -> dets:open_file(e, [{keypos, 0}]) end),
        attempt(fun() -> dets:open_file(e, [{min_no_slots, 2000}, {max_no_slots, 1000}]) end),
        attempt(fun() -> dets:open_file(e, [nonsense]) end),
        attempt(fun() -> dets:open_file(e, {file, 1}) end)
    ]),
    show("not a dets file", dets:open_file(e, [{file, "plain.txt"}])),
    show("missing", dets:open_file(e, [{file, "missing.dets"}, {access, read}])),
    show("missing open_file/1", dets:open_file("missing.dets")),
    {ok, e} = dets:open_file(e, [{file, F}, {type, bag}, {keypos, 2}]),
    ok = dets:close(e),
    show("mismatch", [
        dets:open_file(e, [{file, F}, {type, set}, {keypos, 2}]),
        dets:open_file(e, [{file, F}, {type, bag}, {keypos, 1}])
    ]),
    show("no table", [
        attempt(fun() -> dets:insert(none, {a}) end),
        attempt(fun() -> dets:lookup(none, a) end),
        attempt(fun() -> dets:first(none) end),
        dets:info(none),
        dets:close(none),
        attempt(fun() -> dets:match(none, '_') end)
    ]),
    show("bad arguments", [
        attempt(fun() -> dets:slot(e, -1) end),
        attempt(fun() -> dets:safe_fixtable(e, 'maybe') end),
        attempt(fun() -> dets:bchunk(e, nope) end)
    ]),
    show("not owner", dets:close(e)),
    show("all", lists:sort(dets:all())).
