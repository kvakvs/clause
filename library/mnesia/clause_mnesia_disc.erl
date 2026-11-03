%% mnesia's disc storage (plan 21 step MIM1D, docs/libraries/mnesia.md#disc-storage): the files of mnesia's directory,
%% with OTP's names and terms. schema.DAT is a dets table of the schema's records; LATEST.LOG a disk_log of #commit{}
%% records with each commit's disc writes, which a dump moves into the tables' files; Tab.DCD a disk_log of a table's
%% records and Tab.DCL of its writes since; Tab.DAT the dets table of a disc_only_copies table; FALLBACK.BUP the
%% backup create_schema/2 writes, installed at the next start; DECISION_TAB.LOG the transaction serial and master nodes
%% each dump finds (MIM1H1). mnesia_monitor runs every change of these files.
-module(clause_mnesia_disc).
-export([use_dir/0, fallback_activated/0, create_schema/2, delete_schema/1, schema_source/0, purge_local/0]).
-export([start/0, stop/0, to_disc_schema/0, to_ram_schema/0, save_schema/1, open_dat/1, load/2, created/2]).
-export([deleted/1, ets2dcd/1, dump_exists/1, index_file/2, log/2, dump_log/1, sync_log/0, item_update/1]).
-export([dumped_records/1, set_master_nodes/2]).

-include("clause_mnesia.hrl").

-define(LATEST, "LATEST.LOG").
-define(PREVIOUS, "PREVIOUS.LOG").
-define(FALLBACK, "FALLBACK.BUP").
-define(DECISION_TAB, "DECISION_TAB.LOG").
-define(DECISION_TMP, "DECISION_TAB.TMP").
%% The suffixes of mnesia's files, which delete_schema/1 removes from the directory.
-define(SUFFIXES, [".DAT", ".LOG", ".BUP", ".DCL", ".DCD", ".TMP", ".BUPTMP", ".RET", ".DMP", "."]).
%% A chunk of records written to a log at once.
-define(CHUNK, 1000).

%% The directory.

% Whether mnesia keeps its schema on disc: the running value, else the schema_location's answer.
use_dir() ->
    try
        ets:lookup_element(?GVAR, use_dir, 2)
    catch
        error:_ ->
            case clause_mnesia_monitor:get_env(schema_location) of
                disc -> true;
                opt_disc -> non_empty_dir();
                ram -> false
            end
    end.

non_empty_dir() ->
    exists(file(?FALLBACK)) orelse exists(tab_file(schema, ".DMP")) orelse exists(tab_file(schema, ".DAT")).

% A fallback waits for the next start.
fallback_activated() ->
    use_dir() andalso exists(file(?FALLBACK)).

dir() -> clause_mnesia_monitor:get_env(dir).

file(Name) -> filename:join(dir(), Name).

tab_file(Tab, Suffix) -> file(lists:concat([Tab, Suffix])).

% The dets file of a disc_only_copies table's index.
index_file(Tab, Pos) -> file(lists:concat([Tab, "_", Pos, ".DAT"])).

exists(File) -> filelib:is_file(File).

%% Creating and deleting schemas.

% create_schema/2: a fallback holding a disc schema of the nodes, for the next start; OTP's checks and errors.
create_schema([], Props) ->
    create_schema([node()], Props);
create_schema(Nodes, Props) when is_list(Nodes), is_list(Props) ->
    case length(lists:usort(Nodes)) =:= length(Nodes) of
        true -> new_schema(no_schema(Nodes), Nodes, Props);
        false -> {error, {combine_error, Nodes}}
    end;
create_schema(Nodes, _) ->
    {error, {badarg, Nodes}}.

% ok, or the first node with a schema or without mnesia.
no_schema([Node | Rest]) when is_atom(Node) ->
    case source(Node) of
        {badrpc, Reason} -> {Node, {"All nodes not running", Node, Reason}};
        {ok, Source, _} when Source =/= default -> {Node, {already_exists, Node}};
        _ -> no_schema(Rest)
    end;
no_schema([Node | _]) ->
    {error, {badarg, Node}};
no_schema([]) ->
    ok.

source(Node) when Node =:= node() -> schema_source();
source(Node) -> rpc:call(Node, ?MODULE, schema_source, []).

% Where a start would take the schema from: {ok, ram | default | fallback | disc, CreateList} or {error, Reason}.
schema_source() ->
    _ = application:load(mnesia),
    case clause_mnesia_monitor:is_running() of
        yes ->
            {ok, ram, []};
        _ ->
            case use_dir() of
                false -> {ok, default, []};
                true -> disc_source()
            end
    end.

disc_source() ->
    case {exists(file(?FALLBACK)), exists(tab_file(schema, ".DAT"))} of
        {true, _} -> {ok, fallback, []};
        {_, true} -> {ok, disc, []};
        _ -> {error, "No schema file exists"}
    end.

new_schema(ok, Nodes, Props) ->
    _ = application:load(mnesia),
    case clause_mnesia_monitor:get_env(schema_location) of
        ram ->
            {error, {has_no_disc, node()}};
        _ ->
            case create_dir(dir()) of
                ok -> write_fallback(Nodes, Props);
                Error -> Error
            end
    end;
new_schema({error, Reason}, _, _) ->
    {error, Reason};
new_schema(Reason, _, _) ->
    {error, Reason}.

% The directory, made when missing.
create_dir(Dir) ->
    case filelib:is_dir(Dir) of
        true ->
            ok;
        false ->
            case file:make_dir(Dir) of
                ok -> ok;
                {error, Reason} -> {error, {"Cannot create Mnesia dir", Dir, Reason}}
            end
    end.

% FALLBACK.BUP: a backup log of the initial schema with the schema properties.
write_fallback(Nodes, Props) ->
    Cs = #cstruct{
        name = schema,
        record_name = schema,
        attributes = [table, cstruct],
        disc_copies = Nodes,
        cookie = clause_mnesia_schema:unique_cookie(),
        user_properties = schema_properties(Props)
    },
    List = [P || {Key, _} = P <- clause_mnesia_schema:cs2list(Cs), Key =/= majority, Key =/= storage_properties],
    File = file(?FALLBACK),
    write_log(File, [header(backup_log, "1.2"), {schema, schema, List}]).

% The schema parameter's properties with create_schema/2's over them.
schema_properties(Props) ->
    Merged = lists:foldl(
        fun({Key, Value}, Acc) -> lists:keystore(Key, 1, Acc, {Key, Value}) end,
        unique_keys(clause_mnesia_monitor:get_env(schema)),
        unique_keys(Props)
    ),
    [schema_property(P) || P <- Merged].

unique_keys([{Key, _} = P | Rest]) -> [P | unique_keys([Q || {K, _} = Q <- Rest, K =/= Key])];
unique_keys([]) -> [].

schema_property({backend_types, Types}) -> {mnesia_backend_types, Types};
schema_property({index_plugins, Plugins}) -> {mnesia_index_plugins, Plugins};
schema_property(P) -> mnesia:abort({bad_schema_property, P}).

% delete_schema/1: every node's files go, when mnesia runs on none.
delete_schema(Nodes) when is_list(Nodes), Nodes =/= [] ->
    case [N || N <- Nodes, running_at(N)] of
        [] ->
            case rpc:multicall(Nodes, ?MODULE, purge_local, []) of
                {Replies, []} ->
                    case [R || R <- Replies, R =/= ok] of
                        [] -> ok;
                        Bad -> {error, {"All nodes not running", Bad}}
                    end;
                {_, BadNodes} ->
                    {error, {"All nodes not running", BadNodes}}
            end;
        Running ->
            {error, {"Mnesia is not stopped everywhere", Running}}
    end;
delete_schema(Nodes) ->
    {error, {badarg, Nodes}}.

running_at(Node) when Node =:= node() -> clause_mnesia_monitor:is_running() =:= yes;
running_at(Node) -> rpc:call(Node, clause_mnesia_monitor, is_running, []) =:= yes.

% This node's mnesia files go; the directory stays.
purge_local() ->
    _ = application:load(mnesia),
    case clause_mnesia_monitor:is_running() of
        no -> purge_dir();
        _ -> {error, {"Mnesia still running", node()}}
    end.

purge_dir() ->
    Dir = dir(),
    case file:list_dir(Dir) of
        {ok, Files} -> [delete_file(filename:join(Dir, F)) || F <- Files, known_suffix(F)];
        {error, _} -> ok
    end,
    ok.

known_suffix(File) -> lists:any(fun(Suffix) -> lists:suffix(Suffix, File) end, ?SUFFIXES).

delete_file(Path) ->
    case filelib:is_dir(Path) of
        true -> file:del_dir_r(Path);
        false -> file:delete(Path)
    end.

%% Starting and stopping on a disc schema (in mnesia_monitor).

% The disc schema's start: the fallback installed, the tables loaded, the logs replayed and the log opened.
start() ->
    ok = create_dir(dir()),
    install_fallback(),
    File = tab_file(schema, ".DAT"),
    exists(File) orelse exit({"No schema file exists", File}),
    {ok, schema} = dets:open_file(schema, [{file, File}, {keypos, 2}, {type, set}]),
    Records = dets:match_object(schema, '_'),
    {value, {schema, schema, SchemaList}, Tables} = lists:keytake(schema, 2, Records),
    clause_mnesia_schema:init_schema(clause_mnesia_schema:list2cs(SchemaList)),
    [clause_mnesia_schema:load_table(clause_mnesia_schema:list2cs(List)) || {schema, _, List} <- Tables],
    read_decisions(),
    replay_logs(),
    open_latest().

% The fallback replaces the directory's database: its schema becomes schema.DAT, and the records of each table held
% here its files (MIM1F): a disc_only_copies table's DAT, a disc_copies table's DCL after an empty DCD as OTP's, which
% the start's dump merges (MIM1H1), a RAM table's DCD, none for a RAM table without records.
install_fallback() ->
    File = file(?FALLBACK),
    case exists(File) andalso not clause_mnesia_monitor:get_env(ignore_fallback_at_startup) of
        true ->
            [#log_header{log_kind = backup_log} | Items] = read_log(File),
            [
                delete_file(file(F))
             || {ok, Files} <- [file:list_dir(dir())], F <- Files, F =/= ?FALLBACK, known_suffix(F)
            ],
            {SchemaItems, Records} = lists:splitwith(fun(I) -> element(1, I) =:= schema end, Items),
            Schema = [{schema, Tab, List} || {schema, Tab, List} <- SchemaItems],
            write_dat(tab_file(schema, ".DAT"), Schema),
            [
                fallback_table(clause_mnesia_schema:list2cs(List), Records)
             || {schema, Tab, List} <- Schema, Tab =/= schema
            ],
            ok = file:delete(File);
        false ->
            ok
    end.

% A table's records in the fallback, written and deleted in order, to the files of its storage here.
fallback_table(#cstruct{name = Tab, type = Type, record_name = RecordName} = Cs, Items) ->
    Own = [I || I <- Items, element(1, I) =:= Tab],
    Contents = ets:new(fallback, [Type, {keypos, 2}]),
    [fallback_item(Contents, I, RecordName) || I <- Own],
    Records = ets:tab2list(Contents),
    ets:delete(Contents),
    case clause_mnesia_schema:storage(Cs) of
        disc_only_copies -> write_table_dat(Cs, Records);
        disc_copies -> fallback_dcl(Tab, Records);
        ram_copies when Own =/= [] -> write_log(tab_file(Tab, ".DCD"), [header(dcd_log, "1.0") | Records]);
        _ -> ok
    end.

% A disc_copies table's records as OTP's fallback leaves them: write items of a DCL file after a DCD of a header only.
fallback_dcl(Tab, Records) ->
    write_log(tab_file(Tab, ".DCD"), [header(dcd_log, "1.0")]),
    write_log(tab_file(Tab, ".DCL"), [header(dcl_log, "1.0") | [item(Tab, element(2, R), {write, R}) || R <- Records]]).

fallback_item(Contents, {_, Key}, _) -> ets:delete(Contents, Key);
fallback_item(Contents, Record, RecordName) -> ets:insert(Contents, setelement(1, Record, RecordName)).

write_table_dat(#cstruct{name = Tab, type = Type}, Records) ->
    Name = make_ref(),
    {ok, Name} = dets:open_file(Name, [{file, tab_file(Tab, ".DAT")}, {keypos, 2}, {type, Type}]),
    ok = dets:insert(Name, Records),
    ok = dets:close(Name).

write_dat(File, Records) ->
    Name = make_ref(),
    {ok, Name} = dets:open_file(Name, [{file, File}, {keypos, 2}, {type, set}]),
    ok = dets:insert(Name, Records),
    ok = dets:close(Name).

% The log since the last dump: its committed writes made again and its master nodes set, then the tables' files dumped,
% the decisions written when the log held entries, as OTP's start dumps it (MIM1H1), and the logs started afresh.
replay_logs() ->
    Entries = [E || F <- [?PREVIOUS, ?LATEST], E <- read_log(file(F)), not is_record(E, log_header)],
    Serials = [ets:lookup_element(mnesia_decision, serial, 3) | [N + 1 || {decision, {tid, N, _}, _, _, _} <- Entries]],
    ets:insert(mnesia_decision, {trans_tid, serial, lists:max(Serials)}),
    Replayed = [I || {{T, _}, _, _} = I <- committed_items(Entries), storage(T) =/= ram_copies],
    [item_update(I) || I <- Replayed],
    [masters(Tab, Nodes) || {master_nodes, Tab, Nodes} <- Entries],
    dump_replayed(Replayed, val(dump_disc_copies_at_startup)),
    Entries =/= [] andalso write_decisions(),
    [file:delete(file(F)) || F <- [?PREVIOUS, ?LATEST]],
    ok.

% The disc writes of the log's commits that committed: presume_commit ones, and those of OTP's schema transactions,
% whose committed decision follows them in the log.
committed_items(Entries) ->
    Committed = [Tid || {decision, Tid, committed, _, _} <- Entries],
    [
        I
     || #commit{decision = Decision, disc_copies = Disc, disc_only_copies = DiscOnly} <- Entries,
        committed(Decision, Committed),
        I <- Disc ++ DiscOnly
    ].

committed(presume_commit, _) -> true;
committed({decision, Tid, _, _, _}, Committed) -> lists:member(Tid, Committed);
committed(_, _) -> false.

% With dump_disc_copies_at_startup, each disc_copies table the log touched or that has a DCL file gets a new DCD;
% without, the log's writes go to the tables' DCL files (MIM1H1).
dump_replayed(Items, true) ->
    Local = [T || T <- val({schema, local_tables}), storage(T) =:= disc_copies],
    Touched = lists:usort([T || {T, _} <- by_table(Items)] ++ [T || T <- Local, exists(tab_file(T, ".DCL"))]),
    [touched(T) || T <- Touched];
dump_replayed(Items, false) ->
    [append_dcl(T, TabItems) || {T, TabItems} <- by_table(Items), storage(T) =:= disc_copies],
    [dets:sync(T) || {T, _} <- by_table(Items), storage(T) =:= disc_only_copies].

% Log items grouped by table, in the log's order.
by_table(Items) ->
    [{T, [I || {{Tab, _}, _, _} = I <- Items, Tab =:= T]} || T <- lists:usort([T || {{T, _}, _, _} <- Items])].

% A table's storage on this node; ram_copies also for a table that is gone.
storage(Tab) ->
    case ets:lookup(?GVAR, {Tab, storage_type}) of
        [{_, Storage}] when Storage =/= unknown -> Storage;
        _ -> ram_copies
    end.

touched(Tab) ->
    case val({Tab, storage_type}) of
        disc_copies -> ets2dcd(Tab);
        disc_only_copies -> dets:sync(Tab);
        _ -> ok
    end.

open_latest() ->
    {ok, latest_log} = disk_log:open([{name, latest_log}, {file, file(?LATEST)}]),
    ok = disk_log:log(latest_log, header(trans_log, "4.3")).

% The log and the dets tables close as mnesia stops.
stop() ->
    case use_dir() of
        true ->
            _ = disk_log:close(latest_log),
            [dets:close(T) || T <- val({schema, local_tables}), val({T, storage_type}) =:= disc_only_copies],
            _ = dets:close(schema),
            ok;
        false ->
            ok
    end.

% change_table_copy_type(schema, Node, disc_copies): the RAM schema goes to a new directory.
to_disc_schema() ->
    ok = create_dir(dir()),
    purge_dir(),
    {ok, schema} = dets:open_file(schema, [{file, tab_file(schema, ".DAT")}, {keypos, 2}, {type, set}]),
    ok = dets:insert(schema, ets:tab2list(schema)),
    ok = dets:sync(schema),
    ets:insert(?GVAR, {use_dir, true}),
    open_latest().

% change_table_copy_type(schema, Node, ram_copies): the directory's files go.
to_ram_schema() ->
    _ = disk_log:close(latest_log),
    _ = dets:close(schema),
    purge_dir(),
    ets:insert(?GVAR, {use_dir, false}).

% A changed table's schema record on disc.
save_schema(Tab) ->
    case use_dir() of
        true ->
            case ets:lookup(schema, Tab) of
                [] -> ok = dets:delete(schema, Tab);
                [Record] -> ok = dets:insert(schema, Record)
            end,
            ok = dets:sync(schema);
        false ->
            ok
    end.

%% A table's files.

% The dets table of a disc_only_copies table, named as the table.
open_dat(#cstruct{name = Tab, type = Type, storage_properties = Properties}) ->
    Options = [{file, tab_file(Tab, ".DAT")}, {type, Type}, {keypos, 2} | proplists:get_value(dets, Properties, [])],
    {ok, Tab} = dets:open_file(Tab, Options),
    ok.

% A replica's records at start: a disc_copies table's DCD and DCL files, a RAM table's dump.
load(Tab, Storage) when Storage =:= disc_copies; Storage =:= ram_copies ->
    [ets:insert(Tab, R) || R <- read_log(tab_file(Tab, ".DCD")), not is_record(R, log_header)],
    [item_update(I) || I <- read_log(tab_file(Tab, ".DCL")), not is_record(I, log_header)],
    ok;
load(_, _) ->
    ok.

% A new replica's files: a disc_copies table starts with a DCD file without terms, as OTP's (MIM1H1).
created(Tab, disc_copies) -> write_log(tab_file(Tab, ".DCD"), []);
created(_, _) -> ok.

% A replica's files go with it.
deleted(Tab) ->
    case use_dir() of
        true -> [file:delete(tab_file(Tab, S)) || S <- [".DCD", ".DCL", ".DAT", ".DMP", ".TMP"]];
        false -> ok
    end,
    ok.

% A RAM table dumped by dump_tables/1 keeps its DCD file.
dump_exists(Tab) -> exists(tab_file(Tab, ".DCD")).

% A table's records in its DCD file, which a checkpoint of a RAM table keeps (MIM1F); none without a directory.
dumped_records(Tab) ->
    case use_dir() of
        true -> [R || R <- read_log(tab_file(Tab, ".DCD")), not is_record(R, log_header)];
        false -> []
    end.

% The table's records become its DCD file, whose writes since then the DCL file held.
ets2dcd(Tab) ->
    Tmp = tab_file(Tab, ".TMP"),
    write_log(Tmp, [header(dcd_log, "1.0")]),
    Name = make_ref(),
    {ok, Name} = disk_log:open([{name, Name}, {file, Tmp}]),
    log_chunks(Name, ets:select(Tab, [{'_', [], ['$_']}], ?CHUNK)),
    ok = disk_log:close(Name),
    ok = file:rename(Tmp, tab_file(Tab, ".DCD")),
    _ = file:delete(tab_file(Tab, ".DCL")),
    ok.

log_chunks(_, '$end_of_table') ->
    ok;
log_chunks(Name, {Records, Cont}) ->
    ok = disk_log:log_terms(Name, Records),
    log_chunks(Name, ets:select(Cont)).

%% The transaction log.

% A commit's updates of disc tables in LATEST.LOG, before they are applied, synchronously for a sync transaction or
% sync_dirty activity (MIM1H1); a dump follows each dump_log_write_threshold writes.
log(Updates, Kind) ->
    case use_dir() of
        true -> log_items([{val({Tab, storage_type}), item(Tab, Key, U)} || {Tab, Key, U} <- Updates], Kind);
        false -> ok
    end.

log_items(Items, Kind) ->
    case {[I || {disc_copies, I} <- Items], [I || {disc_only_copies, I} <- Items]} of
        {[], []} ->
            ok;
        {Disc, DiscOnly} ->
            Commit = #commit{node = node(), decision = presume_commit, disc_copies = Disc, disc_only_copies = DiscOnly},
            ok = log_term(Kind, Commit),
            Writes = clause_mnesia_monitor:increment(trans_log_writes),
            Writes rem clause_mnesia_monitor:get_env(dump_log_write_threshold) =:= 0 andalso
                gen_server:cast(mnesia_monitor, {dump_log, write_threshold}),
            ok
    end.

log_term(sync, Term) -> disk_log:log(latest_log, Term);
log_term(_, Term) -> disk_log:alog(latest_log, Term).

% A table's master nodes, none for [], logged on a disc schema as OTP's {master_nodes, Tab, Nodes} so that the next
% start keeps them (MIM1H1).
set_master_nodes(Tab, Nodes) ->
    masters(Tab, Nodes),
    use_dir() andalso disk_log:alog(latest_log, {master_nodes, Tab, Nodes}),
    ok.

masters(Tab, []) -> ets:delete(mnesia_decision, {tab, Tab});
masters(Tab, Nodes) -> ets:insert(mnesia_decision, {master_nodes, {tab, Tab}, Nodes}).

%% The decision log (MIM1H1).

% DECISION_TAB.LOG's transaction serial, which new transactions continue (or the last in the log's decisions), after
% the start's first one; and master nodes. The decisions of other nodes' transactions come with several nodes.
read_decisions() ->
    [decision(D) || {decision_list, List} <- read_log(file(?DECISION_TAB)), D <- List],
    ok.

decision({trans_tid, serial, N}) -> ets:update_counter(mnesia_decision, serial, {3, N});
decision({master_nodes, Tab, Nodes}) -> masters(Tab, Nodes);
decision(_) -> true.

% DECISION_TAB.LOG anew, as OTP writes it after each dump that found entries in the log.
write_decisions() ->
    Masters = [{master_nodes, Tab, Nodes} || {master_nodes, {tab, Tab}, Nodes} <- ets:tab2list(mnesia_decision)],
    Decisions = {decision_list, ets:lookup(mnesia_decision, serial) ++ Masters},
    Tmp = file(?DECISION_TMP),
    write_log(Tmp, [header(decision_tab, "1.0"), Decisions]),
    ok = file:rename(Tmp, file(?DECISION_TAB)).

% An update as OTP's log item, and back.
item(Tab, Key, {write, Val}) -> {{Tab, Key}, Val, write};
item(Tab, Key, delete) -> {{Tab, Key}, {Tab, Key}, delete};
item(Tab, Key, {delete_object, Val}) -> {{Tab, Key}, Val, delete_object};
item(Tab, Key, {clear_table, Pattern}) -> {{Tab, Key}, Pattern, clear_table};
item(Tab, Key, {update_counter, RecordName, Incr}) -> {{Tab, Key}, {RecordName, Incr}, update_counter}.

item_update({{Tab, Key}, Val, write}) -> update(Tab, Key, {write, Val});
item_update({{Tab, Key}, _, delete}) -> update(Tab, Key, delete);
item_update({{Tab, Key}, Val, delete_object}) -> update(Tab, Key, {delete_object, Val});
item_update({{Tab, Key}, Pattern, clear_table}) -> update(Tab, Key, {clear_table, Pattern});
item_update({{Tab, Key}, {RecordName, Incr}, update_counter}) -> update(Tab, Key, {update_counter, RecordName, Incr});
item_update(_) -> ok.

% The log replays at start, before anyone can subscribe to its tables' events.
update(Tab, Key, Update) ->
    _ = clause_mnesia_access:update({dirty, self()}, Tab, Key, Update),
    ok.

% dump_log/0 and the dump thresholds: the log so far goes into the DCL files, or a table's DCD when its DCL has
% grown past the dc_dump_limit share of it; then the decisions (MIM1H1).
dump_log(_InitBy) ->
    case use_dir() of
        true ->
            Previous = file(?PREVIOUS),
            ok = disk_log:reopen(latest_log, Previous, header(trans_log, "4.3")),
            Entries = [E || E <- read_log(Previous), not is_record(E, log_header)],
            Items = committed_items(Entries),
            [dump_table(T, TabItems) || {T, TabItems} <- by_table(Items), storage(T) =:= disc_copies],
            [dets:sync(T) || {T, _} <- by_table(Items), storage(T) =:= disc_only_copies],
            Entries =/= [] andalso write_decisions(),
            _ = file:delete(Previous),
            dumped;
        false ->
            dumped
    end.

dump_table(Tab, Items) ->
    case needs_dcd(Tab) of
        true -> ets2dcd(Tab);
        false -> append_dcl(Tab, Items)
    end.

% A DCL file at least the dc_dump_limit share of the DCD's size is merged into a new DCD.
needs_dcd(Tab) ->
    case {file:read_file_info(tab_file(Tab, ".DCL")), file:read_file_info(tab_file(Tab, ".DCD"))} of
        {{error, _}, _} -> false;
        {_, {error, _}} -> true;
        {{ok, Dcl}, {ok, Dcd}} -> element(2, Dcd) =< element(2, Dcl) * clause_mnesia_monitor:get_env(dc_dump_limit)
    end.

append_dcl(Tab, Items) ->
    File = tab_file(Tab, ".DCL"),
    New = not exists(File),
    Name = make_ref(),
    {ok, Name} = disk_log:open([{name, Name}, {file, File}]),
    New andalso disk_log:log(Name, header(dcl_log, "1.0")),
    ok = disk_log:log_terms(Name, Items),
    ok = disk_log:close(Name).

sync_log() ->
    case use_dir() of
        true -> disk_log:sync(latest_log);
        false -> {error, no_such_log}
    end.

%% Log files.

% A log file's terms; none when it is missing or unreadable.
read_log(File) ->
    Name = make_ref(),
    case exists(File) andalso disk_log:open([{name, Name}, {file, File}, {mode, read_only}, {repair, true}]) of
        {ok, Name} -> read_chunks(Name, start, []);
        {repaired, Name, _, _} -> read_chunks(Name, start, []);
        _ -> []
    end.

read_chunks(Name, Cont, Acc) ->
    case disk_log:chunk(Name, Cont) of
        {Cont2, Terms} ->
            read_chunks(Name, Cont2, [Terms | Acc]);
        {Cont2, Terms, _BadBytes} ->
            read_chunks(Name, Cont2, [Terms | Acc]);
        _ ->
            _ = disk_log:close(Name),
            lists:append(lists:reverse(Acc))
    end.

% A new log file of the terms.
write_log(File, Terms) ->
    _ = file:delete(File),
    Name = make_ref(),
    {ok, Name} = disk_log:open([{name, Name}, {file, File}, {repair, false}]),
    ok = disk_log:log_terms(Name, Terms),
    ok = disk_log:sync(Name),
    ok = disk_log:close(Name).

header(Kind, Version) ->
    {ok, Vsn} = application:get_key(mnesia, vsn),
    #log_header{log_kind = Kind, log_version = Version, mnesia_version = Vsn, node = node(), now = erlang:timestamp()}.

val(Name) -> clause_mnesia_monitor:val(Name).
