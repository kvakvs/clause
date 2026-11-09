%% mnesia's backups (plan 21 step MIM1F, docs/libraries/mnesia.md#backups): backups of checkpoints written through a
%% backup module, reading and traversing backups, and fallbacks installed for the next start. A backup is a header,
%% the schema's {schema, Tab, CreateList} items, then records named after their tables and {Tab, Key} deletes; a
%% checkpoint's table is its records now followed by what its retainer kept, which restores the table at activation.
-module(clause_mnesia_backup).
-export([backup/2, backup_checkpoint/3, traverse_backup/6, iterate/4, read_schema/2, lookup_schema/2]).
-export([install_fallback/2, uninstall_fallback/1, backup_module/1, restore_args/2, restore_op/2, restore_tables/3]).
-export([fallback_receiver/2, install_on_nodes/2, tab_copier/3]).

-include("clause_mnesia.hrl").

%% A backup being written: the checkpoint, the backup module and its opaque data, the tables, the checkpoint an
%% incremental backup starts from, and the cookie.
-record(backup_args, {name, module, opaque, scope = global, prev_name, tables = all, cookie}).
%% A backup being read: the backup module and its opaque data.
-record(restore, {module, opaque}).
%% A fallback's installation: its arguments as OTP's install_fallback/2 and uninstall_fallback/1 take them.
-record(fallback_args, {opaque, scope = global, module, use_default_dir = true, mnesia_dir, bup, tmp}).

%% The number of keys whose records are written at once.
-define(KEYS, 500).

%% Writing backups.

% The whole database: a checkpoint of every table, backed up and deactivated.
backup(Opaque, Args) ->
    Tabs = clause_mnesia_monitor:val({schema, tables}),
    case clause_mnesia_checkpoint:activate([{ram_overrides_dump, false}, {max, Tabs}]) of
        {ok, Name, _} ->
            Result = backup_checkpoint(Name, Opaque, Args),
            _ = clause_mnesia_checkpoint:deactivate(Name),
            Result;
        Error ->
            Error
    end.

backup_checkpoint(Name, Opaque, Module) when is_atom(Module) ->
    backup_checkpoint(Name, Opaque, [{module, Module}]);
backup_checkpoint(Name, Opaque, Args) when is_list(Args) ->
    Default = #backup_args{name = Name, module = default_module(), opaque = Opaque, prev_name = Name},
    case backup_args(Args, Default) of
        {ok, B} -> in_process(fun() -> master(B) end);
        Error -> Error
    end.

backup_args([Arg | Rest], B) ->
    try backup_arg(Arg, B) of
        B2 -> backup_args(Rest, B2)
    catch
        error:_ -> {error, {badarg, Arg}}
    end;
backup_args([], B) ->
    {ok, B}.

backup_arg({scope, Scope}, B) when Scope =:= global; Scope =:= local -> B#backup_args{scope = Scope};
backup_arg({module, Module}, B) -> B#backup_args{module = backup_module(Module)};
backup_arg({incremental, Name}, B) -> B#backup_args{prev_name = Name};
backup_arg({tables, Tabs}, B) when is_list(Tabs) -> B#backup_args{tables = Tabs}.

backup_module(Module) when is_atom(Module) -> Module.

default_module() -> clause_mnesia_monitor:get_env(backup_module).

% F run in a process of its own, as OTP's backup master runs: its result, or {error, {'EXIT', Reason}} for its crash.
in_process(F) ->
    Self = self(),
    {Pid, Ref} = spawn_monitor(fun() ->
        Result =
            try
                F()
            catch
                _:Reason -> {error, {'EXIT', Reason}}
            end,
        Self ! {self(), Result}
    end),
    receive
        {Pid, Result} ->
            erlang:demonitor(Ref, [flush]),
            Result
    end.

% The header, the schema first, then each table of the checkpoint.
master(#backup_args{name = Name} = B) ->
    B2 = safe_apply(B, open_write, [B#backup_args.opaque]),
    B3 = safe_write(B2, [header()]),
    case clause_mnesia_checkpoint:tables_and_cookie(Name) of
        {ok, AllTabs, Cookie} ->
            Tabs = selected(AllTabs, B3),
            B4 = schema_items(B3#backup_args{cookie = Cookie}, Tabs),
            B5 = lists:foldl(fun backup_table/2, B4, Tabs -- [schema]),
            _ = safe_apply(B5, commit_write, [B5#backup_args.opaque]),
            ok;
        {error, Reason} ->
            abort_write(B3, {error, Reason})
    end.

selected(AllTabs, #backup_args{tables = all}) -> AllTabs;
selected(_, #backup_args{tables = Tabs}) -> Tabs.

% The schema's records when the checkpoint holds the schema, else each table's definition.
schema_items(B, Tabs) ->
    case lists:member(schema, Tabs) of
        true -> backup_table(schema, B);
        false -> safe_write(B, [{schema, T, clause_mnesia_schema:cs2list(cstruct(T))} || T <- Tabs])
    end.

cstruct(Tab) ->
    case ets:lookup(?GVAR, {Tab, cstruct}) of
        [{_, Cs}] -> Cs;
        [] -> error(badarg)
    end.

safe_write(B, []) -> B;
safe_write(B, Items) -> safe_apply(B, write, [B#backup_args.opaque, Items]).

% A backup module's callback: its new opaque data, or the backup aborted.
safe_apply(#backup_args{module = Module} = B, Function, Args) ->
    try apply(Module, Function, Args) of
        {ok, Opaque} -> B#backup_args{opaque = Opaque};
        {error, Reason} -> abort_write(B, Reason);
        Other -> abort_write(B, Other)
    catch
        _:Reason -> abort_write(B, Reason)
    end.

% The backup module aborts the writing; a failed abort is reported, as OTP's is.
abort_write(#backup_args{module = Module, opaque = Opaque}, Reason) ->
    try
        {ok, _} = Module:abort_write(Opaque)
    catch
        _:Other ->
            Format = "Failed to abort backup. ~p:~tp~tp -> ~tp~n",
            clause_mnesia_subscr:report_system_event({mnesia_error, Format, [Module, abort_write, [Opaque], Other]})
    end,
    throw({error, Reason}).

% A table's records from its retainer here, or from a copier on the node writing it (MIM1H11a).
backup_table(Tab, #backup_args{name = Name, prev_name = Prev} = B) ->
    case clause_mnesia_checkpoint:retainer(Name, Tab) of
        {remote, Node} ->
            received(Node, Tab, B);
        _ ->
            case passes(Tab, Name, Prev) of
                {First, Last} -> write_chunks(write_chunks(B, First), Last);
                Error -> abort_write(B, Error)
            end
    end.

% A table's backup items in OTP's two passes: all its records now, or for an incremental backup the keys changed since
% the previous checkpoint; then the retained ones. A RAM table without a retainer of its own has its dumped records.
passes(Tab, Name, Prev) ->
    case {clause_mnesia_checkpoint:retainer(Name, Tab), first_source(Tab, Name, Prev)} of
        {{ok, Store, true}, {retainer, PrevStore}} ->
            {keys(Tab, PrevStore, latest), keys(Tab, Store, checkpoint)};
        {{ok, Store, true}, table} ->
            {[setelement(1, R, Tab) || R <- table_records(Tab)], keys(Tab, Store, checkpoint)};
        {{ok, Store, false}, _} ->
            {[], keys(Tab, Store, checkpoint)};
        {{remote, _}, _} ->
            {error, {"No retainer attached to table", [Tab, Name]}};
        {Error, _} ->
            Error
    end.

% Where a backup's records start: the table, or the previous checkpoint's retainer for an incremental backup.
first_source(schema, _, _) ->
    table;
first_source(_, Name, Name) ->
    table;
first_source(Tab, _, Prev) ->
    case clause_mnesia_checkpoint:retainer(Prev, Tab) of
        {ok, Store, _} -> {retainer, Store};
        _ -> table
    end.

table_records(schema) -> [defined(R) || R <- ets:tab2list(schema)];
table_records(Tab) -> clause_mnesia_db:match_object(Tab, '_').

% A schema record with its table's whole definition, as OTP's backups write it even where the schema table keeps the
% list read from disc (MIM1H3).
defined({schema, Tab, _}) -> {schema, Tab, clause_mnesia_schema:cs2list(clause_mnesia_monitor:val({Tab, cstruct}))}.

% Each retained key as a delete of the key, then its records now (latest) or at activation (checkpoint).
keys(Tab, Store, Val) ->
    lists:append([key_items(Tab, Key, Old, Val) || {_, Key, Old} <- ets:tab2list(Store)]).

key_items(Tab, Key, _, latest) -> [{Tab, Key} | [setelement(1, R, Tab) || R <- table_records(Tab, Key)]];
key_items(Tab, Key, Old, checkpoint) -> [{Tab, Key} | [setelement(1, R, Tab) || R <- Old]].

table_records(schema, Key) -> [defined(R) || R <- ets:lookup(schema, Key)];
table_records(Tab, Key) -> clause_mnesia_db:lookup(Tab, Key).

write_chunks(B, Items) ->
    lists:foldl(fun(Chunk, B2) -> safe_write(B2, Chunk) end, B, chunks(Items)).

% Items KEYS at a time.
chunks(Items) when length(Items) > ?KEYS ->
    {Chunk, Rest} = lists:split(?KEYS, Items),
    [Chunk | chunks(Rest)];
chunks([]) ->
    [];
chunks(Items) ->
    [Items].

%% Tables backed up from another node (MIM1H11a), with OTP's tab_receiver and tab_copier protocol.

% Tab's items from the copier spawned on the node writing its retainer: each chunk asked for with {self(), more, N}
% and answered {Copier, {more, N2, Items}}, up to {Copier, {last, {ok, _}}}.
received(Node, Tab, B) ->
    % The literal fun links this node's copier into programs, which other nodes spawn by name.
    _ = fun mnesia_log:tab_copier/3,
    Copier = spawn(Node, mnesia_log, tab_copier, [self(), B, Tab]),
    Ref = erlang:monitor(process, Copier),
    try
        received(Copier, Ref, Tab, B, 0)
    after
        erlang:demonitor(Ref, [flush])
    end.

received(Copier, Ref, Tab, B, Count) ->
    Copier ! {self(), more, Count},
    receive
        {Copier, {more, Next, Items}} ->
            received(Copier, Ref, Tab, safe_write(B, [setelement(1, I, Tab) || I <- Items]), Next);
        {Copier, {last, {ok, _}}} ->
            B;
        {Copier, {last, {error, Reason}}} ->
            abort_write(B, Reason);
        {'DOWN', Ref, process, _, Reason} ->
            abort_write(B, {'EXIT', Reason})
    end.

% The copier that a backup on another node spawns here (OTP's mnesia_log:tab_copier/3): its first pass, closed by an
% empty chunk, then its retained items, each chunk sent when the receiver asks for more; then its last answer, after
% it unlinks from the receiver, whose master traps exits.
tab_copier(Receiver, #backup_args{name = Name, prev_name = Prev}, Tab) ->
    Ref = erlang:monitor(process, Receiver),
    {Count, Result} =
        case passes(Tab, Name, Prev) of
            {First, Last} ->
                Chunks = chunks(First) ++ [[]] ++ chunks(Last),
                {lists:foldl(fun(Chunk, N) -> send_chunk(Receiver, Ref, N, Chunk) end, 0, Chunks), {ok, dummy}};
            Error ->
                {0, Error}
        end,
    asked(Receiver, Ref, Count),
    unlink(Receiver),
    Receiver ! {self(), {last, Result}}.

send_chunk(Receiver, Ref, Count, Chunk) ->
    asked(Receiver, Ref, Count),
    Receiver ! {self(), {more, Count + 1, Chunk}},
    Count + 1.

asked(Receiver, Ref, Count) ->
    receive
        {Receiver, more, Count} -> ok;
        {'DOWN', Ref, process, _, Reason} -> exit(Reason)
    end.

header() ->
    {ok, Vsn} = application:get_key(mnesia, vsn),
    #log_header{
        log_kind = backup_log, log_version = "1.2", mnesia_version = Vsn, node = node(), now = erlang:timestamp()
    }.

%% Reading backups.

% Fun(Items, Header, Schema, Acc) on each chunk of records after the schema section, and on [] at the end: {ok, Acc}
% or {error, Reason}.
iterate(Module, Fun, Opaque, Acc) ->
    try schema_section(#restore{module = Module, opaque = Opaque}) of
        {R, Header, Schema, Rest} ->
            try iter(R, Header, Schema, Fun, Acc, Rest) of
                {R2, Result} ->
                    close_read(R2),
                    {ok, Result}
            catch
                throw:Error ->
                    close_read(R),
                    Error;
                _:Reason:Stack ->
                    close_read(R),
                    {error, {Reason, Stack}}
            end
    catch
        throw:{error, _} = Error -> Error
    end.

iter(R, Header, Schema, Fun, Acc, []) ->
    case read_items(R, read, [R#restore.opaque]) of
        {R2, []} -> {R2, Fun([], Header, Schema, Acc)};
        {R2, Items} -> iter(R2, Header, Schema, Fun, Acc, Items)
    end;
iter(R, Header, Schema, Fun, Acc, Items) ->
    iter(R, Header, Schema, Fun, Fun(Items, Header, Schema, Acc), []).

% A backup module's read callback: {R, Items}, or the reading ends with {error, Reason}.
read_items(#restore{module = Module} = R, Function, Args) ->
    try apply(Module, Function, Args) of
        {ok, Opaque, Items} when Function =:= read -> {R#restore{opaque = Opaque}, Items};
        {ok, Opaque} when Function =/= read -> {R#restore{opaque = Opaque}, []};
        {error, Reason} -> abort_read(R, Reason);
        Other -> abort_read(R, Other)
    catch
        _:Reason -> abort_read(R, Reason)
    end.

abort_read(R, Reason) ->
    close_read(R),
    throw({error, Reason}).

close_read(#restore{module = Module, opaque = Opaque}) ->
    try
        Module:close_read(Opaque)
    catch
        _:_ -> ok
    end.

% The backup opened, its header checked and its schema items read: {R, Header, Schema, FirstRecords}.
schema_section(R) ->
    {R2, _} = read_items(R, open_read, [R#restore.opaque]),
    try
        {R3, Items} = read_items(R2, read, [R2#restore.opaque]),
        {Header, Rest} = header(Items),
        schema_items(R3, Header, Rest, [])
    catch
        Class:Reason:Stack ->
            close_read(R2),
            erlang:raise(Class, Reason, Stack)
    end.

header([#log_header{log_kind = backup_log, log_version = Version} = Header | Rest]) ->
    lists:member(Version, ["0.1", "1.1", "1.2"]) orelse
        throw({error, {"Bad header version. Cannot be used as backup.", Header}}),
    {Header, Rest};
header([#log_header{} = Header | _]) ->
    throw({error, {"Bad kind of header. Cannot be used as backup.", Header}});
header(Items) ->
    throw({error, {"Missing header. Cannot be used as backup.", first(Items)}}).

first([Item | _]) -> Item;
first([]) -> {'EXIT', {badarg, [{erlang, hd, [[]], []}]}}.

schema_items(R, Header, [], Schema) ->
    case read_items(R, read, [R#restore.opaque]) of
        {R2, []} -> {R2, Header, Schema, []};
        {R2, Items} -> schema_items(R2, Header, Items, Schema)
    end;
schema_items(R, Header, [Item | Rest], Schema) when element(1, Item) =:= schema ->
    schema_items(R, Header, Rest, Schema ++ [Item]);
schema_items(R, Header, Rest, Schema) ->
    {R, Header, Schema, Rest}.

% A backup's schema items, or {error, Reason}.
read_schema(Module, Opaque) ->
    try schema_section(#restore{module = Module, opaque = Opaque}) of
        {R, _, Schema, _} ->
            close_read(R),
            Schema
    catch
        throw:{error, _} = Error -> Error
    end.

lookup_schema(Key, Schema) ->
    case lists:keyfind(Key, 2, Schema) of
        {schema, Key, Value} -> Value;
        _ -> throw({error, {"Cannot lookup", Key}})
    end.

%% Traversing backups.

% Fun(Item, Acc) on each schema item and record, whose returned items are written to the target, if any.
traverse_backup(Source, SourceMod, Target, TargetMod, Fun, Acc) ->
    Result = in_process(fun() -> traverse(Source, SourceMod, Target, TargetMod, Fun, Acc) end),
    case Result of
        {error, {'EXIT', Reason}} -> {error, {"Backup traversal crashed", Reason}};
        _ -> Result
    end.

traverse(Source, SourceMod, Target, TargetMod, Fun, Acc) ->
    try open_target(TargetMod, Target) of
        Iter ->
            Start = {start, Fun, Acc, TargetMod, Iter},
            traversed(iterate(SourceMod, fun traverse_items/4, Source, Start), TargetMod, Iter)
    catch
        throw:{error, Error} -> {error, Error}
    end.

open_target(read_only, _) -> ignore;
open_target(Module, Target) -> do_apply(Module, open_write, [Target]).

traversed({ok, {iter, _, Acc, read_only, _}}, read_only, _) ->
    {ok, Acc};
traversed({ok, {iter, _, Acc, Module, Iter}}, _, _) ->
    try do_apply(Module, commit_write, [Iter]) of
        _ -> {ok, Acc}
    catch
        throw:{error, Reason} -> {error, Reason}
    end;
traversed({error, Reason}, read_only, _) ->
    {error, {"Backup traversal failed", Reason}};
traversed({error, Reason}, Module, Iter) ->
    try
        do_apply(Module, abort_write, [Iter])
    catch
        _:_ -> ok
    end,
    {error, {"Backup traversal failed", Reason}}.

% The first call writes the header and passes the schema items through Fun, then each chunk of records.
traverse_items(Items, Header, Schema, {start, Fun, Acc, Module, Iter}) ->
    Iter2 = target_write(Module, Iter, [Header]),
    traverse_items(Items, Header, Schema, traverse_items(Schema, Header, Schema, {iter, Fun, Acc, Module, Iter2}));
traverse_items(Items, _, _, {iter, Fun, Acc, Module, Iter}) ->
    {NewItems, Acc2} = filter_foldl(Fun, Acc, Items),
    {iter, Fun, Acc2, Module, target_write(Module, Iter, NewItems)}.

target_write(read_only, Iter, _) -> Iter;
target_write(_, Iter, []) -> Iter;
target_write(Module, Iter, Items) -> do_apply(Module, write, [Iter, Items]).

filter_foldl(Fun, Acc, [Item | Rest]) ->
    case Fun(Item, Acc) of
        {Items, Acc2} when is_list(Items) ->
            {More, Acc3} = filter_foldl(Fun, Acc2, Rest),
            {Items ++ More, Acc3};
        Other ->
            throw({error, {"Fun must return a list", Other}})
    end;
filter_foldl(_, Acc, []) ->
    {[], Acc}.

% A backup module's write callback: its new opaque data, or {error, Reason} thrown.
do_apply(Module, Function, Args) ->
    try apply(Module, Function, Args) of
        {ok, Opaque} -> Opaque;
        {error, Reason} -> throw({error, Reason})
    catch
        throw:{error, _} = Error -> throw(Error);
        _:Reason -> throw({error, {'EXIT', Reason}})
    end.

%% Fallbacks.

% A backup copied to FALLBACK.BUP of mnesia's directory, which the next start installs.
install_fallback(Opaque, Module) when is_atom(Module) ->
    install_fallback(Opaque, [{module, Module}]);
install_fallback(Opaque, Args) when is_list(Args) ->
    case fallback_args(Args, #fallback_args{opaque = Opaque, module = default_module()}) of
        {ok, FA} ->
            case in_process(fun() -> install(FA) end) of
                {ok, _} -> ok;
                {error, Reason} -> {error, {"Cannot install fallback", Reason}}
            end;
        Error ->
            Error
    end;
install_fallback(_, Args) ->
    {error, {badarg, Args}}.

fallback_args([Arg | Rest], FA) ->
    try fallback_arg(Arg, FA) of
        FA2 -> fallback_args(Rest, FA2)
    catch
        error:_ -> {error, {badarg, Arg}}
    end;
fallback_args([], FA) ->
    {ok, FA}.

fallback_arg({scope, Scope}, FA) when Scope =:= global; Scope =:= local -> FA#fallback_args{scope = Scope};
fallback_arg({module, Module}, FA) ->
    FA#fallback_args{module = backup_module(Module)};
fallback_arg({mnesia_dir, Dir}, FA) ->
    FA#fallback_args{mnesia_dir = Dir, use_default_dir = false};
fallback_arg({Tabs, List}, FA) when Tabs =:= keep_tables; Tabs =:= skip_tables ->
    true = atoms(List),
    FA;
fallback_arg({default_op, Op}, FA) when Op =:= keep_tables; Op =:= skip_tables -> FA.

atoms(List) -> lists:all(fun erlang:is_atom/1, List).

% The backup's schema decides that this node holds a disc schema; its items are copied to FALLBACK.TMP, then renamed.
install(#fallback_args{module = Module, opaque = Opaque} = FA) ->
    iterate(Module, fun copy_items/4, Opaque, {start, FA}).

copy_items(Items, Header, Schema, {start, FA}) ->
    Cs =
        try
            clause_mnesia_schema:list2cs(lookup_schema(schema, Schema))
        catch
            throw:Error -> throw(Error);
            _:Reason -> throw({error, {"Bad schema in restore_recs", Reason}})
        end,
    lists:member(node(), Cs#cstruct.disc_copies) orelse
        throw({error, {"No disc resident schema on local node", Cs#cstruct.disc_copies}}),
    FA2 = fallback_dir(FA),
    copy_items(Items, Header, Schema, start_copy(FA2, Header, Schema));
copy_items([], _, _, {copying, FA, Backup}) ->
    {ok, File} = mnesia_backup:commit_write(Backup),
    ok = file:rename(File, FA#fallback_args.bup),
    unregister(mnesia_fallback),
    {done, FA};
copy_items(Items, _, _, {copying, FA, Backup}) ->
    {ok, Backup2} = mnesia_backup:write(Backup, Items),
    {copying, FA, Backup2}.

% The fallback's directory, with no fallback in it; one installation at a time.
start_copy(#fallback_args{mnesia_dir = Dir, bup = Bup, tmp = Tmp} = FA, Header, Schema) ->
    try register(mnesia_fallback, self()) of
        true -> ok
    catch
        error:_ -> throw({error, {already_exists, node()}})
    end,
    filelib:is_file(Bup) andalso throw({error, {already_exists, node()}}),
    ok = filelib:ensure_path(Dir),
    _ = file:delete(Tmp),
    {ok, Backup} = mnesia_backup:open_write(Tmp),
    {ok, Backup2} = mnesia_backup:write(Backup, [Header]),
    {ok, Backup3} = mnesia_backup:write(Backup2, Schema),
    {copying, FA, Backup3}.

%% Fallbacks over nodes (MIM1H10a).

% create_schema/1 over several nodes: under OTP's global lock of the schema, a fallback receiver on each node, OTP's
% or Clause's, gets the backup's header and schema items, then swaps them in as its FALLBACK.BUP; ok or {error, Reason}.
install_on_nodes(Nodes, [Header | Schema]) ->
    in_process(fun() ->
        process_flag(trap_exit, true),
        Lock = {{mnesia_table_lock, schema}, self()},
        true = global:set_lock(Lock, Nodes),
        try
            % OTP's #fallback_args{}: the opaque backup name, scope, module, default dir and table choices.
            Opaque = filename:join(clause_mnesia_monitor:get_env(dir), lists:concat([node(), erlang:unique_integer()])),
            Args =
                {fallback_args, Opaque, global, mnesia_backup, true, undefined, undefined, undefined, [], [],
                    keep_tables},
            % The literal fun links this node's receiver into programs, which other nodes spawn by name.
            _ = fun mnesia_bup:fallback_receiver/2,
            Receivers = [spawn_link(N, mnesia_bup, fallback_receiver, [self(), Args]) || N <- Nodes],
            Result = steps(Receivers, [{start, Header, Schema}, swap]),
            [R ! {self(), stop} || R <- Receivers],
            [ended(R) || R <- Receivers],
            Result
        after
            global:del_lock(Lock, Nodes)
        end
    end).

% Each message to every receiver, which answers it ok before the next one goes.
steps(_, []) ->
    ok;
steps(Receivers, [Message | Rest]) ->
    [R ! {self(), Message} || R <- Receivers],
    case [A || R <- Receivers, A <- [answer(R)], A =/= ok] of
        [] -> steps(Receivers, Rest);
        [Error | _] -> Error
    end.

answer(Receiver) ->
    receive
        {Receiver, Reply} -> Reply;
        {'EXIT', Receiver, Reason} -> {error, {'EXIT', Reason}}
    end.

ended(Receiver) ->
    receive
        {'EXIT', Receiver, _} -> ok
    end.

% A fallback receiver that another node spawns here with OTP's #fallback_args{}: registered as mnesia_fallback, it
% writes the header and schema items it gets to FALLBACK.TMP in mnesia's directory, renames it FALLBACK.BUP at swap
% and answers each step {self(), ok}, or {self(), {error, Reason}}. Its master, linked to it, ends it with stop: it
% exits stopped, which the master waits for, as OTP's receiver does.
fallback_receiver(Master, _Args) ->
    _ = application:load(mnesia),
    receive
        {Master, {start, Header, Schema}} ->
            try start_copy(fallback_dir(#fallback_args{}), Header, Schema) of
                {copying, FA, Backup} ->
                    Master ! {self(), ok},
                    swapped(Master, FA, Backup)
            catch
                throw:Error ->
                    Master ! {self(), Error},
                    receive
                        {Master, stop} -> ok
                    end
            end;
        {Master, stop} ->
            ok
    end,
    exit(stopped).

swapped(Master, #fallback_args{bup = Bup}, Backup) ->
    receive
        {Master, swap} ->
            {ok, File} = mnesia_backup:commit_write(Backup),
            ok = file:rename(File, Bup),
            Master ! {self(), ok},
            receive
                {Master, stop} -> ok
            end;
        {Master, stop} ->
            _ = mnesia_backup:abort_write(Backup),
            ok
    end.

% FALLBACK.BUP and FALLBACK.TMP in mnesia's directory, or in the directory a local fallback names.
fallback_dir(FA) ->
    clause_mnesia_monitor:get_env(schema_location) =:= ram andalso throw({error, {has_no_disc, node()}}),
    Dir = fallback_dir_arg(FA),
    FA#fallback_args{
        mnesia_dir = Dir, bup = filename:join(Dir, "FALLBACK.BUP"), tmp = filename:join(Dir, "FALLBACK.TMP")
    }.

fallback_dir_arg(#fallback_args{use_default_dir = true}) ->
    clause_mnesia_monitor:get_env(dir);
fallback_dir_arg(#fallback_args{scope = local, mnesia_dir = Dir}) ->
    try
        clause_mnesia_monitor:check_env(dir, Dir)
    catch
        _:_ -> throw({error, {badarg, {dir, Dir}, node()}})
    end;
fallback_dir_arg(#fallback_args{scope = global}) ->
    throw({error, {combine_error, global, dir, node()}}).

% The installed fallback removed, when its schema holds this node's disc schema.
uninstall_fallback(Args) ->
    case fallback_args(Args, #fallback_args{module = default_module()}) of
        {ok, FA} -> in_process(fun() -> uninstall(FA) end);
        Error -> Error
    end.

uninstall(FA) ->
    try
        #fallback_args{bup = Bup, tmp = Tmp} = fallback_dir(FA),
        Schema = schema_list(read_schema(mnesia_backup, Bup)),
        Cs = clause_mnesia_schema:list2cs(Schema),
        lists:member(node(), Cs#cstruct.disc_copies) orelse
            throw({error, {"No disc resident schema on local node", Cs#cstruct.disc_copies}}),
        _ = file:delete(Tmp),
        file:delete(Bup)
    catch
        throw:{error, _} = Error -> Error
    end.

schema_list({error, _} = Error) ->
    throw(Error);
schema_list(Schema) ->
    try
        lookup_schema(schema, Schema)
    catch
        throw:_ -> throw({error, "No schema in fallback"})
    end.

%% Restoring backups.

% restore/2's arguments checked and the backup's schema read: {ok, Restore, Schema}, or {aborted, Reason}.
restore_args(Opaque, Args) when is_list(Args) ->
    Restore = #{opaque => Opaque, module => default_module(), options => [], default => clear_tables},
    try lists:foldl(fun restore_arg/2, Restore, Args) of
        #{module := Module} = Checked ->
            case read_schema(Module, Opaque) of
                {error, Reason} -> {aborted, Reason};
                Schema -> {ok, Checked, Schema}
            end
    catch
        exit:Reason -> {aborted, Reason}
    end;
restore_args(_, Args) ->
    {aborted, {badarg, Args}}.

restore_arg({module, Module}, Restore) when is_atom(Module) ->
    Restore#{module := Module};
restore_arg({Op, Tabs}, #{options := Options} = Restore) when
    Op =:= clear_tables; Op =:= recreate_tables; Op =:= keep_tables; Op =:= skip_tables
->
    is_list(Tabs) orelse exit({badarg, {Op, Tabs}}),
    (Op =:= clear_tables orelse Op =:= recreate_tables) andalso lists:member(schema, Tabs) andalso
        exit({badarg, {Op, schema}}),
    Restore#{options := Options ++ [{Tab, Op} || Tab <- Tabs]};
restore_arg({default_op, Op}, Restore) ->
    lists:member(Op, [clear_tables, recreate_tables, keep_tables, skip_tables]) orelse
        exit({badarg, {bad_default_op, Op}}),
    Restore#{default := Op};
restore_arg(Arg, _) ->
    exit({badarg, Arg}).

% A table's operation: the first one an option names, or the default.
restore_op(Tab, #{options := Options, default := Default}) ->
    case lists:keyfind(Tab, 1, Options) of
        {_, Op} -> Op;
        false -> Default
    end.

% The restore applied in mnesia_monitor: tables deleted, cleared or created anew, then the backup's records of the
% restored tables written; {logged, Updates} gives the writes to the log, as OTP's commit holds them (MIM1H2).
restore_tables(Plan, #{module := Module, opaque := Opaque}, Tid) ->
    InOrder = lists:reverse(Plan),
    RecordNames = maps:from_list([{Tab, prepare_table(Tab, Op, Tid)} || {Tab, Op} <- InOrder, Tab =/= deleted]),
    [delete_table(Tab, Tid) || {deleted, Tab} <- InOrder],
    Write = fun(Items, _, _, Acc) ->
        lists:foldl(fun(Item, Updates) -> restore_item(Item, RecordNames, Tid) ++ Updates end, Acc, Items)
    end,
    case iterate(Module, Write, Opaque, []) of
        {ok, Updates} -> {logged, lists:reverse(Updates)};
        {error, Reason} -> mnesia:abort(Reason)
    end.

% A restored table before its records come: its record name.
prepare_table(Tab, {clear, RecordName}, Tid) ->
    clause_mnesia_access:update(Tid, Tab, '_', {clear_table, clause_mnesia_monitor:val({Tab, wild_pattern})}),
    RecordName;
prepare_table(_, {keep, RecordName}, _) ->
    RecordName;
prepare_table(Tab, {recreate, #cstruct{record_name = RecordName} = Cs}, Tid) ->
    delete_table(Tab, Tid),
    clause_mnesia_schema:apply_change({create, Cs}, Tid),
    RecordName.

delete_table(Tab, Tid) ->
    ets:member(?GVAR, {Tab, cstruct}) andalso clause_mnesia_schema:apply_change({delete, Tab}, Tid).

% A backup item of a restored table: a record written under its table's record name, or a {Tab, Key} delete; the
% update made, if any.
restore_item(Item, RecordNames, Tid) ->
    Tab = element(1, Item),
    Key = element(2, Item),
    Update =
        case RecordNames of
            #{Tab := _} when tuple_size(Item) =:= 2 -> delete;
            #{Tab := RecordName} -> {write, setelement(1, Item, RecordName)};
            _ -> none
        end,
    case Update of
        none ->
            [];
        _ ->
            clause_mnesia_access:update(Tid, Tab, Key, Update),
            [{Tab, Key, Update}]
    end.
