%% mnesia's checkpoints and backups (plan 21 step MIM1F): activating and deactivating checkpoints with their retainers,
%% backups of the database and of checkpoints (full and incremental) through the default backup module, traversals
%% that read or rewrite a backup, restores with each table option, and fallbacks installed, uninstalled and taken at
%% the next start, each with OTP's results.
-module(mnesia_backups).
-export([main/1]).

-define(OTHER, 'other@nohost').

main(_) ->
    ok = application:load(mnesia),
    ok = mnesia:delete_schema([node()]),
    ok = file:write_file("data.txt", <<"not a backup">>),
    ram_schema(),
    ok = mnesia:create_schema([node()]),
    ok = mnesia:start(),
    tables(),
    checkpoints(),
    backups(),
    incremental(),
    retained(),
    traversals(),
    restores(),
    fallbacks(),
    remote_table(),
    stopped = mnesia:stop(),
    ok = mnesia:delete_schema([node()]),
    io:format("mnesia_backups: ok~n").

% A RAM schema: checkpoints and backups work, fallbacks need a disc.
ram_schema() ->
    ok = application:set_env(mnesia, schema_location, ram),
    ok = mnesia:start(),
    {atomic, ok} = mnesia:create_table(r, []),
    ok = mnesia:dirty_write({r, 1, a}),
    t(ram_schema, fun() ->
        [
            mnesia:backup("ram.BUP"),
            items("ram.BUP"),
            ok(mnesia:activate_checkpoint([{name, rcp}, {max, [r]}, {ram_overrides_dump, true}])),
            mnesia:backup_checkpoint(rcp, "ram2.BUP"),
            items("ram2.BUP"),
            mnesia:deactivate_checkpoint(rcp),
            mnesia:install_fallback("ram2.BUP"),
            mnesia:uninstall_fallback()
        ]
    end),
    stopped = mnesia:stop(),
    ok = application:set_env(mnesia, schema_location, opt_disc).

tables() ->
    {atomic, ok} = mnesia:create_table(dc, [{disc_copies, [node()]}, {index, [3]}]),
    {atomic, ok} = mnesia:create_table(dco, [{disc_only_copies, [node()]}, {type, bag}]),
    {atomic, ok} = mnesia:create_table(rc, [{type, ordered_set}]),
    {atomic, ok} = mnesia:create_table(rn, [{disc_copies, [node()]}, {record_name, rec}]),
    [ok = mnesia:dirty_write({T, K, V}) || T <- [dc, dco, rc], {K, V} <- [{1, a}, {2, b}, {3, c}]],
    ok = mnesia:dirty_write(rn, {rec, 1, x}),
    ok.

% Activation and deactivation with OTP's errors; table information while a checkpoint is active.
checkpoints() ->
    t(checkpoint_errors, fun() ->
        [
            mnesia:activate_checkpoint(foo),
            norm_name(mnesia:activate_checkpoint([])),
            mnesia:activate_checkpoint([{name, e1}, {min, [dc]}, {max, [dc]}]),
            mnesia:activate_checkpoint([{name, e2}, {bogus, 1}]),
            mnesia:activate_checkpoint([{name, e3}, {max, [nope]}]),
            mnesia:activate_checkpoint([{name, e6}, {max, [dc]}, {ram_overrides_dump, [rc]}]),
            mnesia:activate_checkpoint([{name, e7}, {min, [dc]}, {ram_overrides_dump, [rc]}]),
            mnesia:activate_checkpoint([{name, e8}, {max, dc}]),
            mnesia:system_info(checkpoints)
        ]
    end),
    t(activate, fun() ->
        [
            mnesia:activate_checkpoint([{name, cp1}, {max, [schema, dc, dco, rn]}, {min, [rc]}]),
            mnesia:activate_checkpoint([{name, cp1}, {max, [dc]}]),
            ok(mnesia:activate_checkpoint([{max, [dc]}])),
            mnesia:activate_checkpoint([{name, {cp, 2}}, {max, [rc]}, {ram_overrides_dump, true}]),
            generated(lists:sort(mnesia:system_info(checkpoints))),
            generated([{T, lists:sort(mnesia:table_info(T, checkpoints))} || T <- [dc, dco, rc, rn, schema]]),
            generated([{T, work(T)} || T <- [dc, rc, schema]])
        ]
    end),
    [ok = mnesia:deactivate_checkpoint(Cp) || Cp <- mnesia:system_info(checkpoints), is_integer(element(1, Cp))],
    t(deactivate, fun() ->
        [
            mnesia:deactivate_checkpoint({cp, 2}),
            mnesia:deactivate_checkpoint({cp, 2}),
            mnesia:deactivate_checkpoint(nope),
            mnesia:system_info(checkpoints),
            mnesia:table_info(rc, checkpoints),
            work(rc)
        ]
    end).

% Backups of the whole database and of checkpoints, whose records are those at activation.
backups() ->
    t(backup, fun() ->
        [
            mnesia:backup("all.BUP"),
            items("all.BUP"),
            mnesia:system_info(checkpoints),
            mnesia:backup("all_mod.BUP", mnesia_backup),
            mnesia:backup("all_args.BUP", [{module, mnesia_backup}]),
            items("all_args.BUP") =:= items("all.BUP"),
            mnesia:backup("nodir/x.BUP"),
            mnesia:backup("bad.BUP", [{bogus, 1}]),
            filelib:is_file("all.BUP.BUPTMP")
        ]
    end),
    ok = mnesia:dirty_write({dc, 4, d}),
    ok = mnesia:dirty_write({dc, 1, z}),
    ok = mnesia:dirty_delete(dc, 2),
    ok = mnesia:dirty_write({dco, 1, z}),
    ok = mnesia:dirty_write({rc, 9, z}),
    {atomic, ok} = mnesia:transaction(fun() -> mnesia:delete({rn, 1}) end),
    t(checkpoint_backup, fun() ->
        [
            mnesia:backup_checkpoint(cp1, "cp1.BUP"),
            items("cp1.BUP"),
            mnesia:backup_checkpoint(cp1, "cp1_dc.BUP", [{tables, [dc]}]),
            items("cp1_dc.BUP"),
            mnesia:backup_checkpoint(cp1, "cp1_local.BUP", [{scope, local}, {tables, [rc]}]),
            items("cp1_local.BUP"),
            mnesia:backup_checkpoint(nope, "nope.BUP"),
            mnesia:backup_checkpoint(cp1, "bad.BUP", [{tables, [rc, nope]}]),
            mnesia:backup_checkpoint(cp1, "bad.BUP", foo),
            filelib:is_file("bad.BUP")
        ]
    end).

% An incremental backup: what changed between two checkpoints.
incremental() ->
    {ok, cp3, _} = mnesia:activate_checkpoint([{name, cp3}, {max, [dc, dco, rc, rn]}]),
    ok = mnesia:dirty_write({dc, 5, e}),
    ok = mnesia:dirty_delete(dc, 3),
    t(incremental, fun() ->
        [
            mnesia:backup_checkpoint(cp3, "inc.BUP", [{incremental, cp1}]),
            items("inc.BUP"),
            mnesia:backup_checkpoint(cp3, "inc2.BUP", [{incremental, nope}]),
            mnesia:deactivate_checkpoint(cp1),
            mnesia:deactivate_checkpoint(cp3),
            mnesia:system_info(checkpoints)
        ]
    end).

% A cleared table's records are retained; a checkpoint ends with its table's last replica.
retained() ->
    {atomic, ok} = mnesia:create_table(tc, [{disc_copies, [node()]}]),
    [ok = mnesia:dirty_write({tc, K, K}) || K <- [1, 2]],
    {ok, cpc, _} = mnesia:activate_checkpoint([{name, cpc}, {max, [tc]}]),
    {ok, _} = mnesia:subscribe(system),
    t(retained, fun() ->
        [
            mnesia:clear_table(tc),
            mnesia:dirty_write({tc, 3, 3}),
            mnesia:backup_checkpoint(cpc, "cpc.BUP"),
            [I || I <- items("cpc.BUP"), element(1, I) =/= schema],
            mnesia:delete_table(tc),
            mnesia:system_info(checkpoints),
            flush(),
            mnesia:deactivate_checkpoint(cpc)
        ]
    end),
    {ok, _} = mnesia:unsubscribe(system).

flush() ->
    receive
        Msg -> [Msg | flush()]
    after 100 -> []
    end.

% traverse_backup/4,6: reading a backup, or writing a changed one.
traversals() ->
    Count = fun(Item, N) -> {[Item], N + 1} end,
    NoRc = fun
        ({schema, rc, _}, Acc) -> {[], Acc};
        (Rec, Acc) when element(1, Rec) =:= rc -> {[], Acc};
        ({dc, K, V}, Acc) -> {[{dc, K, {V}}], Acc + 1};
        (Item, Acc) -> {[Item], Acc}
    end,
    t(traverse, fun() ->
        [
            mnesia:traverse_backup("all.BUP", mnesia_backup, dummy, read_only, Count, 0),
            mnesia:traverse_backup("all.BUP", "copy.BUP", Count, 0),
            items("copy.BUP") =:= items("all.BUP"),
            mnesia:traverse_backup("all.BUP", mnesia_backup, "norc.BUP", mnesia_backup, NoRc, 0),
            items("norc.BUP"),
            mnesia:traverse_backup("missing.BUP", mnesia_backup, dummy, read_only, Count, 0),
            mnesia:traverse_backup("all.BUP", mnesia_backup, dummy, read_only, fun(_, A) -> A end, 0),
            mnesia:traverse_backup("all.BUP", mnesia_backup, "nodir/x.BUP", mnesia_backup, Count, 0),
            mnesia:traverse_backup("data.txt", mnesia_backup, dummy, read_only, Count, 0)
        ]
    end).

% restore/2 with each table option, from the backup of the whole database.
restores() ->
    Contents = fun() -> [{T, contents(T)} || T <- [dc, dco, rc, rn]] end,
    t(restore, fun() ->
        [
            Contents(),
            sorted(mnesia:restore("all.BUP", [])),
            Contents(),
            sorted(mnesia:restore("cp1.BUP", [{skip_tables, [dc]}, {keep_tables, [rc]}])),
            Contents(),
            sorted(mnesia:restore("norc.BUP", [{default_op, skip_tables}, {clear_tables, [dc]}])),
            Contents(),
            mnesia:delete_table(rn),
            sorted(mnesia:restore("all.BUP", [{recreate_tables, [rn]}, {default_op, skip_tables}])),
            Contents(),
            sorted(mnesia:restore("cp1.BUP", [{default_op, recreate_tables}])),
            Contents(),
            mnesia:table_info(rn, record_name),
            sorted(mnesia:restore("all.BUP", [{module, mnesia_backup}])),
            Contents()
        ]
    end),
    t(restore_errors, fun() ->
        [
            mnesia:restore("missing.BUP", []),
            mnesia:restore("all.BUP", foo),
            mnesia:restore("all.BUP", [{clear_tables, [schema]}]),
            mnesia:restore("all.BUP", [{recreate_tables, [schema]}]),
            mnesia:restore("all.BUP", [{default_op, bogus}]),
            mnesia:restore("all.BUP", [bogus]),
            mnesia:restore("data.txt", []),
            mnesia:transaction(fun() -> mnesia:restore("all.BUP", []) end)
        ]
    end).

% A fallback: installed and uninstalled while running, then taken at the next start.
fallbacks() ->
    t(fallback_errors, fun() ->
        [
            mnesia:install_fallback("missing.BUP"),
            mnesia:install_fallback("all.BUP", [{scope, global}, {mnesia_dir, "other_dir"}]),
            mnesia:install_fallback("all.BUP", [bogus]),
            mnesia:install_fallback("all.BUP", foo),
            mnesia:uninstall_fallback(),
            mnesia:uninstall_fallback([bogus]),
            mnesia:system_info(fallback_activated)
        ]
    end),
    t(install, fun() ->
        [
            mnesia:install_fallback("cp1.BUP"),
            mnesia:system_info(fallback_activated),
            mnesia:install_fallback("cp1.BUP"),
            mnesia:uninstall_fallback(),
            mnesia:system_info(fallback_activated),
            mnesia:install_fallback("cp1.BUP", mnesia_backup),
            mnesia:install_fallback("all.BUP", [{scope, local}, {mnesia_dir, "other_dir"}]),
            sorted_dir("other_dir"),
            mnesia:uninstall_fallback([{scope, local}, {mnesia_dir, "other_dir"}]),
            sorted_dir("other_dir")
        ]
    end),
    ok = mnesia:dirty_write({dc, 7, after_install}),
    stopped = mnesia:stop(),
    ok = mnesia:start(),
    ok = mnesia:wait_for_tables([dc, dco, rc, rn], 5000),
    t(started_from_fallback, fun() ->
        [
            mnesia:system_info(fallback_activated),
            lists:sort(mnesia:system_info(tables)),
            [{T, contents(T)} || T <- [dc, dco, rc, rn]],
            [{T, mnesia:table_info(T, storage_type)} || T <- [dc, dco, rc, rn]],
            mnesia:dirty_index_read(dc, a, 3)
        ]
    end).

% A table without a replica here: no checkpoint can hold it, so no backup of the whole database can be made.
remote_table() ->
    {atomic, ok} = mnesia:create_table(far, [{ram_copies, [?OTHER]}]),
    t(remote_table, fun() ->
        [
            mnesia:activate_checkpoint([{name, e4}, {max, [far]}]),
            mnesia:activate_checkpoint([{name, e5}, {max, [dc]}, {allow_remote, false}, {min, [far]}]),
            norm_backup(mnesia:backup("far.BUP")),
            mnesia:system_info(checkpoints)
        ]
    end).

norm_backup({error, {Reason, [Tab, _]}}) -> {error, {Reason, [Tab, name]}};
norm_backup(Other) -> Other.

%% Helpers.

sorted({atomic, Tabs}) -> {atomic, lists:sort(Tabs)};
sorted(Other) -> Other.

t(Label, F) ->
    io:format("~p: ~p~n", [Label, catch_all(F)]).

catch_all(F) ->
    try
        F()
    catch
        Class:Reason -> {Class, Reason}
    end.

% {ok, Name, Nodes} with a generated name hidden.
ok({ok, _, Nodes}) -> {ok, name, Nodes};
ok(Other) -> Other.

% Generated checkpoint names, {Integer, Node}, named generated.
generated({N, Node}) when is_integer(N), is_atom(Node) -> generated;
generated(T) when is_tuple(T) -> list_to_tuple(generated(tuple_to_list(T)));
generated([H | T]) -> [generated(H) | generated(T)];
generated(Term) -> Term.

norm_name({error, {combine_error, _, Detail}}) -> {error, {combine_error, name, Detail}};
norm_name(Other) -> Other.

% A backup's items, sorted, with the schema's records cut to the fields that tell tables apart.
items(File) ->
    Collect = fun(Item, Acc) -> {[Item], [hide(Item) | Acc]} end,
    case mnesia:traverse_backup(File, mnesia_backup, dummy, read_only, Collect, []) of
        {ok, Items} -> lists:sort(Items);
        Error -> Error
    end.

hide({schema, Tab, Def}) when is_list(Def) ->
    Kept = [type, ram_copies, disc_copies, disc_only_copies, index, record_name],
    {schema, Tab, [F || {K, _} = F <- Def, lists:member(K, Kept)], length(Def)};
hide(Item) ->
    Item.

contents(Tab) ->
    lists:sort(mnesia:dirty_match_object(Tab, mnesia:table_info(Tab, wild_pattern))).

sorted_dir(Dir) ->
    case file:list_dir(Dir) of
        {ok, Files} -> lists:sort(Files);
        Error -> Error
    end.

% A table's commit work: the kind of each entry, with the checkpoints.
work(Tab) ->
    [
        case W of
            {checkpoints, Names} -> {checkpoints, lists:sort(Names)};
            _ -> element(1, W)
        end
     || W <- mnesia:table_info(Tab, commit_work)
    ].
