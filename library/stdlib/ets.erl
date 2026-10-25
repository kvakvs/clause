%% Clause's ets (plan 21 step LIB10, docs/builtins.md#ets): OTP 29's ETS tables over the runtime's tables
%% (docs/runtime.md#ets-tables). Table, read, write, walk and update functions are runtime builtins with
%% clause_builtin_placeholder bodies; the folds and the i/0..3 table viewers, which call funs or the io server,
%% are Erlang over them. Match specifications (plan 21 step LIB11) run in the runtime too: the select, match and
%% match_spec builtins, with match_spec_run/2, test_ms/2, repair_continuation/2, match_delete/2 and init_table/2
%% Erlang over them. Table files (tab2file/2,3, file2tab/1,2, tabfile_info/1) are Erlang over file.
-module(ets).
-export([
    all/0,
    delete/1,
    delete/2,
    delete_all_objects/1,
    delete_object/2,
    file2tab/1,
    file2tab/2,
    first/1,
    first_lookup/1,
    foldl/3,
    foldr/3,
    from_dets/2,
    fun2ms/1,
    give_away/3,
    i/0,
    i/1,
    i/2,
    i/3,
    info/1,
    info/2,
    init_table/2,
    insert/2,
    insert_new/2,
    internal_delete_all/2,
    internal_request_all/0,
    internal_select_delete/2,
    is_compiled_ms/1,
    last/1,
    last_lookup/1,
    lookup/2,
    lookup_element/3,
    lookup_element/4,
    match/1,
    match/2,
    match/3,
    match_delete/2,
    match_object/1,
    match_object/2,
    match_object/3,
    match_spec_compile/1,
    match_spec_run/2,
    match_spec_run_r/3,
    member/2,
    new/2,
    next/2,
    next_lookup/2,
    prev/2,
    prev_lookup/2,
    rename/2,
    repair_continuation/2,
    safe_fixtable/2,
    select/1,
    select/2,
    select/3,
    select_count/2,
    select_delete/2,
    select_replace/2,
    select_reverse/1,
    select_reverse/2,
    select_reverse/3,
    setopts/2,
    slot/2,
    tab2file/2,
    tab2file/3,
    tab2list/1,
    tabfile_info/1,
    table/1,
    table/2,
    take/2,
    test_ms/2,
    to_dets/2,
    update_counter/3,
    update_counter/4,
    update_element/3,
    update_element/4,
    whereis/1
]).
-include("ets.types.hrl").

all() -> clause_builtin_placeholder.
delete(_) -> clause_builtin_placeholder.
delete(_, _) -> clause_builtin_placeholder.
delete_all_objects(_) -> clause_builtin_placeholder.
delete_object(_, _) -> clause_builtin_placeholder.
first(_) -> clause_builtin_placeholder.
first_lookup(_) -> clause_builtin_placeholder.
give_away(_, _, _) -> clause_builtin_placeholder.
info(_) -> clause_builtin_placeholder.
info(_, _) -> clause_builtin_placeholder.
insert(_, _) -> clause_builtin_placeholder.
insert_new(_, _) -> clause_builtin_placeholder.
internal_delete_all(_, _) -> erlang:error({not_implemented, internal_delete_all, 2}).
internal_request_all() -> erlang:error({not_implemented, internal_request_all, 0}).
internal_select_delete(_, _) -> clause_builtin_placeholder.
is_compiled_ms(_) -> clause_builtin_placeholder.
last(_) -> clause_builtin_placeholder.
last_lookup(_) -> clause_builtin_placeholder.
lookup(_, _) -> clause_builtin_placeholder.
lookup_element(_, _, _) -> clause_builtin_placeholder.
lookup_element(_, _, _, _) -> clause_builtin_placeholder.
match(_) -> clause_builtin_placeholder.
match(_, _) -> clause_builtin_placeholder.
match(_, _, _) -> clause_builtin_placeholder.
match_object(_) -> clause_builtin_placeholder.
match_object(_, _) -> clause_builtin_placeholder.
match_object(_, _, _) -> clause_builtin_placeholder.
match_spec_compile(_) -> clause_builtin_placeholder.
match_spec_run_r(_, _, _) -> clause_builtin_placeholder.
member(_, _) -> clause_builtin_placeholder.
new(_, _) -> clause_builtin_placeholder.
next(_, _) -> clause_builtin_placeholder.
next_lookup(_, _) -> clause_builtin_placeholder.
prev(_, _) -> clause_builtin_placeholder.
prev_lookup(_, _) -> clause_builtin_placeholder.
rename(_, _) -> clause_builtin_placeholder.
safe_fixtable(_, _) -> clause_builtin_placeholder.
select(_) -> clause_builtin_placeholder.
select(_, _) -> clause_builtin_placeholder.
select(_, _, _) -> clause_builtin_placeholder.
select_count(_, _) -> clause_builtin_placeholder.
select_delete(_, _) -> clause_builtin_placeholder.
select_replace(_, _) -> clause_builtin_placeholder.
select_reverse(_) -> clause_builtin_placeholder.
select_reverse(_, _) -> clause_builtin_placeholder.
select_reverse(_, _, _) -> clause_builtin_placeholder.
setopts(_, _) -> clause_builtin_placeholder.
slot(_, _) -> clause_builtin_placeholder.
tab2list(_) -> clause_builtin_placeholder.
take(_, _) -> clause_builtin_placeholder.
update_counter(_, _, _) -> clause_builtin_placeholder.
update_counter(_, _, _, _) -> clause_builtin_placeholder.
update_element(_, _, _) -> clause_builtin_placeholder.
update_element(_, _, _, _) -> clause_builtin_placeholder.
whereis(_) -> clause_builtin_placeholder.

%% match_spec_run(List, CompiledMatchSpec): the results for the list's terms in order (plan 21 step LIB11).
match_spec_run(List, CompiledMatchSpec) ->
    lists:reverse(ets:match_spec_run_r(List, CompiledMatchSpec, [])).

%% test_ms(Tuple, MatchSpec): the result of the specification on the tuple, or its errors.
test_ms(Term, MatchSpec) ->
    case erlang:match_spec_test(Term, MatchSpec, table) of
        {ok, Result, _Flags, _Messages} -> {ok, Result};
        {error, _Errors} = Error -> Error
    end.

%% repair_continuation(Continuation, MatchSpec): a continuation whose compiled specification no longer lives gets it
%% again; ordered_set and hash continuations have OTP's shapes.
repair_continuation('$end_of_table', _MatchSpec) ->
    '$end_of_table';
repair_continuation(
    {Table, LastKey, EndCondition, Limit, Compiled, Rest, Reverse, Count} = Continuation, MatchSpec
) when
    is_integer(Limit), is_list(Rest), is_integer(Reverse), is_integer(Count)
->
    case ets:is_compiled_ms(Compiled) of
        true ->
            Continuation;
        false ->
            {Table, LastKey, EndCondition, Limit, ets:match_spec_compile(MatchSpec), Rest, Reverse, Count}
    end;
repair_continuation({Table, Slot, Limit, Compiled, Rest, Count} = Continuation, MatchSpec) when
    is_integer(Slot), is_integer(Limit), is_list(Rest), is_integer(Count)
->
    case ets:is_compiled_ms(Compiled) of
        true -> Continuation;
        false -> {Table, Slot, Limit, ets:match_spec_compile(MatchSpec), Rest, Count}
    end.

%% match_delete(Table, Pattern): every object matching the pattern goes.
match_delete(Table, Pattern) ->
    ets:select_delete(Table, [{Pattern, [], [true]}]),
    true.

%% init_table(Table, InitFun): the table emptied, then filled with the objects InitFun gives until end_of_input; a
%% failing insert calls InitFun(close) and exits, any other answer of InitFun exits.
init_table(Table, InitFun) ->
    Tid = soft_whereis(Table),
    ets:delete_all_objects(Tid),
    init_continue(Tid, InitFun(read)).

init_continue(_Tid, end_of_input) ->
    true;
init_continue(Tid, {Objects, InitFun}) when is_list(Objects), is_function(InitFun) ->
    case catch insert_each(Tid, Objects) of
        {'EXIT', Reason} ->
            catch InitFun(close),
            exit(Reason);
        true ->
            init_continue(Tid, InitFun(read))
    end;
init_continue(_Tid, Other) ->
    exit(Other).

table(Table) ->
    table(Table, []).

%% Created for qlc (plan 21 step LIB73): a query handle over the table, traversed by select/3 (the default), first/next
%% or last/prev, with key lookups unless a match specification is given; other options go to qlc:table/2.
table(TableArg, Options) ->
    Table = table_id(TableArg),
    case table_options(Options, [traverse, n_objects], []) of
        badarg ->
            erlang:error(badarg, [Table, Options]);
        {[Traverse, NObjs], QlcOptions} ->
            KeyEquality =
                case ets:info(Table, type) of
                    ordered_set -> '==';
                    _ -> '=:='
                end,
            TableOptions = [
                {pre_fun, fun(_) -> ets:safe_fixtable(Table, true) end},
                {post_fun, fun() -> ets:safe_fixtable(Table, false) end},
                {info_fun, fun(Tag) -> table_info(Table, Tag) end},
                {format_fun, table_format_fun(Table, Options)},
                {key_equality, KeyEquality},
                {lookup_fun, table_lookup_fun(Table, Traverse)}
            ],
            (qlc_module()):table(table_traverse_fun(Table, Traverse, NObjs), TableOptions ++ QlcOptions)
    end.

%% Created for qlc: reached by a runtime name so that only programs evaluating queries link it.
qlc_module() -> qlc.

%% The values of table/2's own options with their defaults, and the options left for qlc:table/2.
table_options(Options, Keys, Values) when not is_list(Options) ->
    table_options([Options], Keys, Values);
table_options(Options, [Key | Keys], Values) ->
    case table_option(lists:keyfind(Key, 1, Options), Key) of
        badarg -> badarg;
        {ok, Value} -> table_options(lists:keydelete(Key, 1, Options), Keys, [Value | Values])
    end;
table_options(Options, [], Values) ->
    {lists:reverse(Values), Options}.

table_option({n_objects, default}, n_objects) -> {ok, 100};
table_option({n_objects, N}, n_objects) when is_integer(N), N >= 1 -> {ok, N};
table_option({traverse, select}, traverse) -> {ok, select};
table_option({traverse, {select, _MS} = Select}, traverse) -> {ok, Select};
table_option({traverse, first_next}, traverse) -> {ok, first_next};
table_option({traverse, last_prev}, traverse) -> {ok, last_prev};
table_option({Key, _}, Key) -> badarg;
table_option(false, n_objects) -> {ok, 100};
table_option(false, traverse) -> {ok, select}.

table_traverse_fun(Table, first_next, _NObjs) ->
    fun() -> qlc_next(Table, ets:first(Table)) end;
table_traverse_fun(Table, last_prev, _NObjs) ->
    fun() -> qlc_prev(Table, ets:last(Table)) end;
table_traverse_fun(Table, select, NObjs) ->
    fun(MS) -> qlc_select(ets:select(Table, MS, NObjs)) end;
table_traverse_fun(Table, {select, MS}, NObjs) ->
    fun() -> qlc_select(ets:select(Table, MS, NObjs)) end.

table_lookup_fun(_Table, {select, _MS}) ->
    undefined;
table_lookup_fun(Table, _Traverse) ->
    fun
        (_Pos, [K]) -> ets:lookup(Table, K);
        (_Pos, Ks) -> lists:flatmap(fun(K) -> ets:lookup(Table, K) end, Ks)
    end.

%% How info/1,2 of qlc shows the table: the call that made it, or the lookups it does.
table_format_fun(Table, Options) ->
    fun
        ({all, _NElements, _ElementFun}) when Options =:= [] ->
            {ets, table, [Table]};
        ({all, _NElements, _ElementFun}) ->
            {ets, table, [Table, Options]};
        ({match_spec, MS}) ->
            {ets, table, [Table, [{traverse, {select, MS}} | table_listify(Options)]]};
        ({lookup, _KeyPos, [Value], _NElements, ElementFun}) ->
            io_lib:format("~w:lookup(~w, ~w)", [ets, Table, ElementFun(Value)]);
        ({lookup, _KeyPos, Values, _NElements, ElementFun}) ->
            Vals = [ElementFun(V) || V <- Values],
            io_lib:format("lists:flatmap(fun(V) -> ~w:lookup(~w, V) end, ~w)", [ets, Table, Vals])
    end.

table_listify(L) when is_list(L) -> L;
table_listify(T) -> [T].

table_info(Table, num_of_objects) -> ets:info(Table, size);
table_info(Table, keypos) -> ets:info(Table, keypos);
table_info(Table, is_unique_objects) -> ets:info(Table, type) =/= duplicate_bag;
table_info(Table, is_sorted_key) -> ets:info(Table, type) =:= ordered_set;
table_info(_Table, _) -> undefined.

qlc_next(_Table, '$end_of_table') -> [];
qlc_next(Table, Key) -> ets:lookup(Table, Key) ++ fun() -> qlc_next(Table, ets:next(Table, Key)) end.

qlc_prev(_Table, '$end_of_table') -> [];
qlc_prev(Table, Key) -> ets:lookup(Table, Key) ++ fun() -> qlc_prev(Table, ets:prev(Table, Key)) end.

qlc_select('$end_of_table') -> [];
qlc_select({Objects, Cont}) -> Objects ++ fun() -> qlc_select(ets:select(Cont)) end.

insert_each(_Tid, []) ->
    true;
insert_each(Tid, [Object | Objects]) ->
    ets:insert(Tid, Object),
    insert_each(Tid, Objects).

%% The identifier of a named table, or the argument as it is (an identifier, or a name of no table).
soft_whereis(Table) when is_atom(Table) ->
    case ets:whereis(Table) of
        undefined -> Table;
        Tid -> Tid
    end;
soft_whereis(Table) ->
    Table.

%% Table files (plan 21 step LIB12): disk_log halt logs in OTP's internal format, written here with file and read
%% with clause_disk_log:items/1, so files are exchangeable with OTP. The first item is the table's information as a
%% tuple (ets:info/1 and the file format's version and extended information), then the objects, then, with
%% extended information, ['$end_of_table', [{count, N}, {md5, Digest}]].
-define(LOG_OPENED, <<1, 2, 3, 4, 6, 7, 8, 9>>).
-define(LOG_CLOSED, <<99, 88, 77, 11>>).
-define(MIN_MD5_TERM, 65528).

%% tab2file(Table, File [, Options]): options {extended_info, [object_count | md5sum]} and {sync, boolean()}.
tab2file(Table, File) ->
    tab2file(Table, File, []).

tab2file(Table, File, Options) ->
    try
        Info =
            case ets:info(Table) of
                undefined -> throw(badtab);
                TableInfo -> TableInfo
            end,
        {id, Tid} = lists:keyfind(id, 1, Info),
        Extended = file_options(Options),
        _ = file:delete(File),
        case file:read_file_info(File) of
            {error, enoent} -> ok;
            _ -> throw(eaccess)
        end,
        Fd = open_log(File),
        try
            write_table(Fd, Tid, Info, Extended),
            close_log(Fd, sync_option(Options))
        catch
            Class:Reason:Stack ->
                _ = file:close(Fd),
                _ = file:delete(File),
                erlang:raise(Class, Reason, Stack)
        end
    catch
        throw:Error -> {error, Error};
        exit:Error -> {error, Error}
    end.

%% The extended information to write: {ObjectCount, Md5sum}.
file_options(Options) when is_list(Options) ->
    file_options(Options, {false, false}, false).

file_options([], Extended, _) ->
    Extended;
file_options([{sync, Sync} | Rest], Extended, Seen) when is_boolean(Sync) ->
    file_options(Rest, Extended, Seen);
file_options([{extended_info, List} | Rest], Extended, false) ->
    file_options(Rest, extended_options(List, Extended), true);
file_options([Other | _], _, _) ->
    throw({unknown_option, Other});
file_options(Malformed, _, _) ->
    throw({malformed_option, Malformed}).

extended_options([], Extended) ->
    Extended;
extended_options([object_count | Rest], {_, Md5}) ->
    extended_options(Rest, {true, Md5});
extended_options([md5sum | Rest], {Count, _}) ->
    extended_options(Rest, {Count, true});
extended_options([Unexpected | _], _) ->
    throw({unknown_option, [{extended_info, [Unexpected]}]});
extended_options(Malformed, _) ->
    throw({malformed_option, Malformed}).

extended_list({Count, Md5}) ->
    [object_count || Count] ++ [md5sum || Md5].

%% A new log file, its head marked opened until it is closed; disk_log's errors for bad names and files.
open_log(File) when is_list(File); is_atom(File) ->
    case file:open(File, [write, binary]) of
        {ok, Fd} ->
            write_log(Fd, ?LOG_OPENED),
            Fd;
        {error, Reason} ->
            throw({file_error, File, Reason})
    end;
open_log(File) ->
    throw({badarg, [{file, File}, {repair, truncate}]}).

write_log(Fd, Data) ->
    case file:write(Fd, Data) of
        ok -> ok;
        {error, Reason} -> throw(Reason)
    end.

%% The information tuple, the objects 100 at a time with the table fixed, the end information, then the closed mark.
write_table(Fd, Tid, Info, {Count, Md5} = Extended) ->
    Header = list_to_tuple(
        Info ++ [{major_version, 1}, {minor_version, 0}, {extended_info, extended_list(Extended)}]
    ),
    Digest0 =
        case Md5 of
            true -> erlang:md5_init();
            false -> none
        end,
    Digest1 = log_terms(Fd, [Header], Digest0),
    try
        ets:safe_fixtable(Tid, true)
    catch
        error:badarg -> throw(badtab)
    end,
    {Digest2, Objects} =
        try
            dump_objects(Fd, ets:select(Tid, [{'_', [], ['$_']}], 100), Digest1, 0)
        after
            unfix(Tid)
        end,
    End = [{count, Objects} || Count] ++ [{md5, erlang:md5_final(Digest2)} || Md5],
    case End of
        [] -> ok;
        _ -> log_terms(Fd, [['$end_of_table', End]], Digest2)
    end,
    ok.

%% Mark the log closed, after flushing it to disk for {sync, true} as disk_log:sync/1 does.
close_log(Fd, Sync) ->
    case Sync of
        true -> ok = file:sync(Fd);
        false -> ok
    end,
    {ok, _} = file:position(Fd, 4),
    write_log(Fd, ?LOG_CLOSED),
    file:close(Fd).

%% The last {sync, Bool} option given, false without one.
sync_option(Options) ->
    lists:foldl(
        fun
            ({sync, Sync}, _) -> Sync;
            (_, Sync) -> Sync
        end,
        false,
        Options
    ).

unfix(Tid) ->
    try
        ets:safe_fixtable(Tid, false)
    catch
        _:_ -> ok
    end.

dump_objects(_Fd, '$end_of_table', Digest, Count) ->
    {Digest, Count};
dump_objects(Fd, {Objects, Continuation}, Digest, Count) ->
    Next = log_terms(Fd, Objects, Digest),
    dump_objects(Fd, ets:select(Continuation), Next, Count + length(Objects)).

%% Write terms as log items, adding each one's external format to the digest (none without md5sum).
log_terms(Fd, Terms, Digest) ->
    Binaries = [term_to_binary(Term) || Term <- Terms],
    write_log(Fd, [log_item(Binary) || Binary <- Binaries]),
    case Digest of
        none ->
            none;
        _ ->
            lists:foldl(
                fun(Binary, State) -> erlang:md5_update(State, Binary) end, Digest, Binaries
            )
    end.

log_item(Binary) when byte_size(Binary) < ?MIN_MD5_TERM ->
    Size = byte_size(Binary),
    [<<Size:32, 98, 87, 76, 65>>, Binary];
log_item(Binary) ->
    Size = byte_size(Binary),
    [<<Size:32, 98, 87, 76, 65>>, erlang:md5(<<Size:32>>), Binary].

%% file2tab(File [, Options]): a new table of a file's information and objects; options {verify, boolean()} and
%% {table, Table} (load into an existing table).
file2tab(File) ->
    file2tab(File, []).

file2tab(File, Options) ->
    try
        {Verify, Target} = load_options(Options, false, []),
        {Items, Bad} = read_log(File),
        {{Major, Minor}, _, _, Header} = Head = log_head(Items, Bad, Verify),
        Major > 1 andalso throw({unsupported_file_version, {Major, Minor}}),
        {Table, HeadCount} = create_table(Header, Target),
        try
            load_table(Table, Head, tl(Items), HeadCount, Verify)
        catch
            Class:Reason:Stack ->
                ets:delete(Table),
                erlang:raise(Class, Reason, Stack)
        end
    catch
        throw:Error -> {error, Error};
        exit:Error -> {error, Error}
    end.

load_options([], Verify, Target) ->
    {Verify, Target};
load_options([{verify, true} | Rest], _, Target) ->
    load_options(Rest, true, Target);
load_options([{verify, false} | Rest], Verify, Target) ->
    load_options(Rest, Verify, Target);
load_options([{table, Table} | Rest], Verify, []) ->
    load_options(Rest, Verify, Table);
load_options([Unexpected | _], _, _) ->
    throw({unknown_option, Unexpected});
load_options(Malformed, _, _) ->
    throw({malformed_option, Malformed}).

%% The items of a log file and the bytes that framed none; disk_log's read errors otherwise.
read_log(File) when is_list(File); is_atom(File) ->
    case file:read_file(File) of
        {ok, Bytes} -> log_items(clause_disk_log:items(Bytes), File);
        {error, Reason} -> throw({read_error, {file_error, File, Reason}})
    end;
read_log(File) ->
    throw({read_error, {badarg, [{file, File}, {mode, read_only}]}}).

log_items({_Status, Items, Bad}, _File) -> {Items, Bad};
log_items(not_a_log_file, File) -> throw({read_error, {not_a_log_file, File}}).

%% The head of a table file: {{Major, Minor}, {ObjectCount, Md5sum}, Digest, Information}; badfile unless the first
%% item is a tuple of the six mandatory items (or, verifying, when any byte frames no item). Verifying reads the
%% version under the names major and minor, as OTP does.
log_head([First | _], Bad, Verify) when not Verify; Bad =:= 0 ->
    Header = header_items(First, Verify),
    mandatory(Header) =:= 6 orelse throw(badfile),
    Extended =
        case lists:keyfind(extended_info, 1, Header) of
            {extended_info, List} when is_list(List) ->
                {lists:member(object_count, List), lists:member(md5sum, List)};
            _ ->
                {false, false}
        end,
    Digest =
        case Verify andalso element(2, Extended) of
            true -> erlang:md5_update(erlang:md5_init(), First);
            false -> none
        end,
    {version(Header, Verify), Extended, Digest, Header};
log_head(_, _, _) ->
    throw(badfile).

%% The information items; a damaged item is badfile, as disk_log's read_only chunks skip it, but raises from
%% binary_to_term/1 when verifying, as OTP reads the binary then.
header_items(Binary, true) ->
    header_tuple(binary_to_term(Binary));
header_items(Binary, false) ->
    try binary_to_term(Binary) of
        Term -> header_tuple(Term)
    catch
        error:_ -> throw(badfile)
    end.

header_tuple(Tuple) when is_tuple(Tuple) -> tuple_to_list(Tuple);
header_tuple(_) -> throw(badfile).

mandatory(Header) ->
    length([
        Tag
     || {Tag, _} <- Header, lists:member(Tag, [name, type, protection, named_table, keypos, size])
    ]).

version(Header, true) -> {version_of(major, Header), version_of(minor, Header)};
version(Header, false) -> {version_of(major_version, Header), version_of(minor_version, Header)}.

version_of(Key, Header) ->
    case lists:keyfind(Key, 1, Header) of
        {Key, Version} -> Version;
        false -> 0
    end.

%% The table to load into: a new one of the file's name, type, protection, key position and concurrency, or the one
%% given; and the size the file's information records.
create_table(Header, Target) ->
    {name, Name} = lists:keyfind(name, 1, Header),
    {type, Type} = lists:keyfind(type, 1, Header),
    {protection, Protection} = lists:keyfind(protection, 1, Header),
    {keypos, _} = Keypos = lists:keyfind(keypos, 1, Header),
    {size, Size} = lists:keyfind(size, 1, Header),
    Options =
        [Option || {read_concurrency, _} = Option <- [lists:keyfind(read_concurrency, 1, Header)]] ++
            [
                Option
             || {write_concurrency, _} = Option <- [lists:keyfind(write_concurrency, 1, Header)]
            ] ++
            [compressed || lists:keyfind(compressed, 1, Header) =:= {compressed, true}] ++
            [named_table || named(Header)] ++ [Type, Protection, Keypos],
    case Target of
        [] -> {new_table(Name, Options), Size};
        _ -> {Target, Size}
    end.

new_table(Name, Options) ->
    try
        ets:new(Name, Options)
    catch
        _:_ -> throw(cannot_create_table)
    end.

named(Header) ->
    case lists:keyfind(named_table, 1, Header) of
        {named_table, true} -> true;
        {named_table, false} -> false
    end.

%% Insert the objects 100 at a time and check what the file's end information and the options ask for; without
%% {verify, true} the file's extended information is not checked.
load_table(Table, {_, Extended, Digest, _}, Items, HeadCount, Verify) ->
    {Count, Md5} =
        case Verify of
            true -> Extended;
            false -> {false, false}
        end,
    {Objects, End, Final} =
        case Md5 of
            true -> digested(Items, Digest, []);
            false -> decoded(Items, Verify)
        end,
    insert_chunks(Table, Objects),
    check_load(End, Final, length(Objects), HeadCount, {Verify, Count, Md5}),
    {ok, Table}.

%% The objects of the items and the end information, if the last item is it; items that do not decode are skipped,
%% as disk_log's read_only chunks skip them, or badfile when verifying.
decoded(Items, Verify) ->
    Terms = lists:filtermap(fun(Item) -> decode_item(Item, Verify) end, Items),
    case lists:reverse(Terms) of
        [['$end_of_table', End] | Objects] -> {lists:reverse(Objects), End, none};
        _ -> {Terms, none, none}
    end.

decode_item(Item, Verify) ->
    try
        {true, binary_to_term(Item)}
    catch
        error:_ when Verify -> throw(badfile);
        error:_ -> false
    end.

%% With md5sum verified: items that do not decode are skipped, and the first end information ends the objects.
digested([Item | Items], Digest, Objects) ->
    try binary_to_term(Item) of
        ['$end_of_table', End] -> {lists:reverse(Objects), End, Digest};
        Term -> digested(Items, erlang:md5_update(Digest, Item), [Term | Objects])
    catch
        error:_ -> digested(Items, Digest, Objects)
    end;
digested([], Digest, Objects) ->
    {lists:reverse(Objects), none, Digest}.

check_load(none, _Digest, Loaded, HeadCount, {Verify, Count, Md5}) ->
    (Count orelse Md5) andalso throw(badfile),
    Verify andalso Loaded =/= HeadCount andalso throw(invalid_object_count),
    ok;
check_load(End, Digest, Loaded, HeadCount, {Verify, Count, Md5}) ->
    Md5 andalso erlang:md5_final(Digest) =/= end_value(md5, End) andalso throw(checksum_error),
    case Count of
        true ->
            Loaded =/= end_value(count, End) andalso throw(invalid_object_count);
        false ->
            Verify andalso not Md5 andalso Loaded =/= HeadCount andalso throw(invalid_object_count)
    end,
    ok.

end_value(Key, End) ->
    case lists:keyfind(Key, 1, End) of
        {Key, Value} -> Value;
        _ -> false
    end.

insert_chunks(Table, Objects) when length(Objects) > 100 ->
    {Chunk, Rest} = lists:split(100, Objects),
    ets:insert(Table, Chunk),
    insert_chunks(Table, Rest);
insert_chunks(Table, Objects) ->
    ets:insert(Table, Objects).

%% tabfile_info(File): what a table file's information records.
tabfile_info(File) when is_list(File); is_atom(File) ->
    try
        {Items, Bad} = read_log(File),
        {{Major, Minor}, _, _, Header} = log_head(Items, Bad, false),
        Extended =
            case lists:keyfind(extended_info, 1, Header) of
                false -> {extended_info, []};
                Found -> Found
            end,
        Fields = [
            lists:keyfind(Key, 1, Header)
         || Key <- [name, type, protection, named_table, keypos, size]
        ],
        {ok, Fields ++ [Extended, {version, {Major, Minor}}]}
    catch
        throw:Error -> {error, Error};
        exit:Error -> {error, Error}
    end.

%% fun2ms(LiteralFun): without the ms_transform parse transform, only a fun of the shell's evaluator could be
%% translated at run time; Clause has no such funs yet, so every fun exits as a compiled fun does in OTP.
fun2ms(Fun) when is_function(Fun) ->
    exit(
        {badarg,
            {ets, fun2ms, [
                function,
                called,
                with,
                real,
                'fun',
                should,
                be,
                transformed,
                with,
                parse_transform,
                'or',
                called,
                with,
                a,
                'fun',
                generated,
                in,
                the,
                shell
            ]}}
    ).

%% foldl(Function, Acc0, Table): Function over every object, first to last, with the table fixed meanwhile.
foldl(Function, Acc, Table) ->
    Tid = table_id(Table),
    ets:safe_fixtable(Tid, true),
    try
        fold_forward(Function, Acc, Tid, ets:first_lookup(Tid))
    after
        ets:safe_fixtable(Tid, false)
    end.

fold_forward(_Function, Acc, _Tid, '$end_of_table') ->
    Acc;
fold_forward(Function, Acc, Tid, {Key, Objects}) ->
    fold_forward(Function, lists:foldl(Function, Acc, Objects), Tid, ets:next_lookup(Tid, Key)).

%% foldr(Function, Acc0, Table): Function over every object, last to first.
foldr(Function, Acc, Table) ->
    Tid = table_id(Table),
    ets:safe_fixtable(Tid, true),
    try
        fold_backward(Function, Acc, Tid, ets:last_lookup(Tid))
    after
        ets:safe_fixtable(Tid, false)
    end.

fold_backward(_Function, Acc, _Tid, '$end_of_table') ->
    Acc;
fold_backward(Function, Acc, Tid, {Key, Objects}) ->
    fold_backward(Function, lists:foldr(Function, Acc, Objects), Tid, ets:prev_lookup(Tid, Key)).

%% The identifier of a table argument, so a rename during a fold or a browse does not lose the table.
table_id(Table) when is_atom(Table) ->
    case ets:whereis(Table) of
        undefined -> erlang:error(badarg, [Table]);
        Tid -> Tid
    end;
table_id(Table) ->
    Table.

%% i(): one line per table, sorted: id, name, type, size, memory and owner (its registered name if any).
i() ->
    columns([id, name, type, size, mem, owner]),
    io:format(
        " -------------------------------------"
        "---------------------------------------\n"
    ),
    lists:foreach(fun table_line/1, lists:sort(ets:all())).

table_line(Table) ->
    try
        Owner = ets:info(Table, owner),
        columns([
            Table,
            ets:info(Table, name),
            ets:info(Table, type),
            ets:info(Table, size),
            ets:info(Table, memory),
            owner_name(Owner)
        ])
    catch
        _:_ -> io:format("~-10s ... unreadable \n", [printed(Table)])
    end.

owner_name(Owner) ->
    case process_info(Owner, registered_name) of
        {registered_name, Name} -> Name;
        _ -> Owner
    end.

%% Columns of 15, 17, 5, 6 and 8 characters and a last one as long as it is; a longer value pushes the rest right.
columns(Values) ->
    Texts = [printed(Value) || Value <- Values],
    Padded = lists:zipwith(fun pad/2, lists:droplast(Texts), [15, 17, 5, 6, 8]),
    io:format(" ~s ~s ~s ~s ~s ~s\n", Padded ++ [lists:last(Texts)]).

pad(Text, Width) ->
    Text ++ lists:duplicate(max(0, Width - length(Text)), $\s).

printed(Term) ->
    lists:flatten(io_lib:format("~p", [Term])).

%% i(Table): browse a table on the terminal, 40 lines a page, 80 columns wide.
i(Table) ->
    i(Table, 40).

i(Table, Height) ->
    i(Table, Height, 80).

i(Table, Height, Width) ->
    Tid = table_id(Table),
    browse(#{table => Tid, height => Height, width => Width}, ets:first(Tid), 1, 1).

-define(MORE_PROMPT, '(c)ontinue (q)uit (p)Digits (k)ill /Regexp -->').
-define(END_PROMPT, 'EOT  (q)uit (p)Digits (k)ill /Regexp -->').

%% Show the objects from Key on until the page is full (Line is the page's line, Number the object's number).
browse(View, '$end_of_table', Line, Number) ->
    command(View, ?END_PROMPT, eot, '$end_of_table', Line, Number);
browse(#{height := Height} = View, Key, Line, Number) when Line < Height ->
    show_key(View, Key, Line, Number);
browse(View, Key, Line, Number) ->
    command(View, ?MORE_PROMPT, normal, Key, Line, Number).

show_key(#{table := Tid} = View, Key, Line, Number) ->
    Objects = ets:lookup(Tid, Key),
    show_objects(View, Objects, Number),
    Count = length(Objects),
    browse(View, ets:next(Tid, Key), Line + Count, Number + Count).

show_objects(View, Objects, Number) ->
    lists:foldl(
        fun(Object, At) ->
            show_object(View, Object, At),
            At + 1
        end,
        Number,
        Objects
    ).

show_object(#{width := Width}, Object, Number) ->
    Text = printed(Object),
    Shown =
        case length(Text) > Width - 8 of
            true -> string:slice(Text, 0, Width - 13) ++ "  ...";
            false -> Text
        end,
    io:format("<~-4w> ~s~n", [Number, Shown]).

%% Read a command at a page end (Mode normal), the table's end (eot) or in a search ({re, Re}).
command(View, Prompt, Mode, Key, Line, Number) ->
    Again = fun() -> command(View, Prompt, Mode, Key, Line, Number) end,
    case read_command(Prompt) of
        "c\n" when Mode =:= normal ->
            show_key(View, Key, 1, Number);
        "c\n" when is_tuple(Mode) ->
            search(View, Key, element(2, Mode), 1, Number);
        "q\n" ->
            ok;
        "k\n" ->
            kill(View);
        [$p | Digits] ->
            print_numbered(View, Mode, Digits),
            Again();
        [$/ | Pattern] ->
            start_search(View, Pattern, Again);
        eof ->
            ok;
        _ ->
            Again()
    end.

kill(#{table := Tid}) ->
    ets:delete(Tid),
    ok.

read_command(Prompt) ->
    case io:get_line(Prompt) of
        "\n" -> "c\n";
        <<"\n">> -> "c\n";
        Line when is_binary(Line) -> unicode:characters_to_list(Line);
        Line -> Line
    end.

%% The command text without its trailing newlines and dollar signs.
command_text(Text) ->
    string:trim(Text, trailing, "$\n").

print_numbered(#{table := Tid}, Mode, Digits) ->
    case catch list_to_integer(command_text(Digits)) of
        {'EXIT', _} ->
            io:put_chars("Bad digits\n");
        Wanted when is_tuple(Mode) ->
            catch print_match(Tid, ets:first(Tid), Wanted, element(2, Mode));
        Wanted ->
            catch print_object(Tid, ets:first(Tid), Wanted)
    end.

print_object(Tid, Key, Wanted) ->
    Objects = ets:lookup(Tid, Key),
    case Wanted =< length(Objects) of
        true -> io:format("~p~n", [lists:nth(Wanted, Objects)]);
        false -> print_object(Tid, ets:next(Tid, Key), Wanted - length(Objects))
    end.

print_match(_Tid, '$end_of_table', _Wanted, _Re) ->
    ok;
print_match(Tid, Key, Wanted, Re) ->
    Objects = [Object || Object <- ets:lookup(Tid, Key), matches(Object, Re)],
    case Wanted =< length(Objects) of
        true -> io:format("~p~n", [lists:nth(Wanted, Objects)]);
        false -> print_match(Tid, ets:next(Tid, Key), Wanted - length(Objects), Re)
    end.

start_search(#{table := Tid} = View, Pattern, Again) ->
    case re:compile(command_text(Pattern), [unicode]) of
        {ok, Re} ->
            search(View, ets:first(Tid), Re, 1, 1);
        {error, {Reason, _At}} ->
            io:format("~ts\n", [Reason]),
            Again()
    end.

%% Show the objects matching Re from Key on until the page is full.
search(View, '$end_of_table', Re, Line, Number) ->
    command(View, ?END_PROMPT, {re, Re}, '$end_of_table', Line, Number);
search(#{height := Height, table := Tid} = View, Key, Re, Line, Number) when Line < Height ->
    search_objects(View, Key, ets:lookup(Tid, Key), Re, Line, Number);
search(View, Key, Re, Line, Number) ->
    command(View, ?MORE_PROMPT, {re, Re}, Key, Line, Number).

search_objects(#{table := Tid} = View, Key, [], Re, Line, Number) ->
    search(View, ets:next(Tid, Key), Re, Line, Number);
search_objects(View, Key, [Object | Rest], Re, Line, Number) ->
    case matches(Object, Re) of
        true ->
            show_object(View, Object, Number),
            search_objects(View, Key, Rest, Re, Line + 1, Number + 1);
        false ->
            search_objects(View, Key, Rest, Re, Line, Number)
    end.

matches(Object, Re) ->
    re:run(printed(Object), Re, [{capture, none}]) =:= match.

%% from_dets(EtsTable, DetsTable) and to_dets(EtsTable, DetsTable) (plan 21 step LIB12C): dets:to_ets/2 and
%% dets:from_ets/2, their errors raised as OTP's catch of them raises them.
from_dets(EtsTable, DetsTable) ->
    case dets_result(fun() -> dets:to_ets(DetsTable, EtsTable) end) of
        {value, EtsTable} -> true;
        Other -> dets_error(Other, [EtsTable, DetsTable])
    end.

to_dets(EtsTable, DetsTable) ->
    case dets_result(fun() -> dets:from_ets(DetsTable, EtsTable) end) of
        {value, ok} -> DetsTable;
        Other -> dets_error(Other, [EtsTable, DetsTable])
    end.

%% A dets call's value or its exception's reason, as `catch` gives them: a throw is a value.
dets_result(Call) ->
    try Call() of
        Value -> {value, Value}
    catch
        throw:Thrown -> {value, Thrown};
        error:Reason -> {reason, Reason};
        exit:{Reason, _} -> {reason, Reason};
        exit:Reason -> {reason, Reason}
    end.

dets_error({value, {error, Reason}}, Arguments) -> erlang:error(Reason, Arguments);
dets_error({value, Unexpected}, Arguments) -> erlang:error(Unexpected, Arguments);
dets_error({reason, Reason}, Arguments) -> erlang:error(Reason, Arguments).
