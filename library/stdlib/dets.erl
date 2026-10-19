%% Clause's dets (plan 21 step LIB53, docs/builtins.md#dets): disk tables in OTP's version 9 file format, so files
%% pass both ways between Clause and OTP. The file engine is C++ (clause_dets): the header, the buddy system, the
%% linear hash with its growth, the collections, scans, bulk loads and repairs. This module keeps OTP's processes
%% and protocol: a registered `dets` server with OTP's dets_registry and dets_owners tables, which opens and closes
%% tables for their users, and one process per open table that holds the file's handle, OTP's write cache, the
%% fixes and the autosave and delayed write timers. Funs run where OTP runs them: init_table/2,3's in the table
%% process, foldl/3, traverse/2 and the chunked matches' in the caller.
-module(dets).
-export([
    add_user/3,
    all/0,
    bchunk/2,
    close/1,
    delete/2,
    delete_all_objects/1,
    delete_object/2,
    file_info/1,
    first/1,
    foldl/3,
    foldr/3,
    from_ets/2,
    fsck/1,
    fsck/2,
    get_head_field/2,
    info/1,
    info/2,
    init/2,
    init_table/2,
    init_table/3,
    insert/2,
    insert_new/2,
    internal_close/1,
    internal_open/3,
    is_compatible_bchunk_format/2,
    is_dets_file/1,
    istart_link/1,
    lookup/2,
    lookup_keys/2,
    match/1,
    match/2,
    match/3,
    match_delete/2,
    match_object/1,
    match_object/2,
    match_object/3,
    member/2,
    next/2,
    open_file/1,
    open_file/2,
    pid2name/1,
    remove_user/2,
    repair_continuation/2,
    safe_fixtable/2,
    select/1,
    select/2,
    select/3,
    select_delete/2,
    slot/2,
    start/0,
    stop/0,
    sync/1,
    system_code_change/4,
    system_continue/3,
    system_terminate/4,
    table/1,
    table/2,
    to_ets/2,
    traverse/2,
    update_counter/3,
    verbose/0,
    verbose/1,
    view/1,
    where/2
]).
-include("dets.types.hrl").

%% The options of open_file/2, in OTP's #open_args{} order (clause_dets:open/1 reads the record).
-record(open_args, {
    file,
    type,
    keypos,
    repair,
    min_no_slots,
    max_no_slots,
    ram_file,
    delayed_write,
    auto_save,
    access,
    debug
}).
%% OTP's write cache: {Key, {Seq, Command}} newest first, its size (external sizes of what was added), an upper
%% bound of the keys inserted, the time of the last write, the threshold and the delay of a delayed write.
-record(cache, {cache = [], csize = 0, inserts = 0, wrtime, tsize, delay}).
%% A table process's state: the file's handle and what OTP's #head{} keeps outside the file.
-record(tab, {
    handle,
    name,
    file,
    type,
    keypos,
    access,
    ram,
    auto_save,
    update_mode,
    cache,
    fixed = false,
    server,
    % Writes since the last save or autosave check (open_file_loop/2's N).
    ops = 0
}).

-define(DEFAULT_MIN_NO_SLOTS, 256).
-define(DEFAULT_CACHE, {3000, 14000}).
-define(DEFAULT_AUTOSAVE, 3).
-define(MAX_SLOTS_LIMIT, (1 bsl 31)).
-define(CALL(From, Request), {'$dets_call', From, Request}).
-define(SERVER, dets).
-define(REGISTRY, dets_registry).
-define(OWNERS, dets_owners).
-define(OBJECT_SPEC(Pattern), [{Pattern, [], ['$_']}]).
-define(BINDINGS_SPEC(Pattern), [{Pattern, [], ['$$']}]).
-define(TRUE_SPEC(Pattern), [{Pattern, [], [true]}]).

%%% --- The interface ---

add_user(Pid, Tab, Args) ->
    req(Pid, {add_user, Tab, Args}).

all() ->
    server_call(all).

bchunk(Tab, start) ->
    badarg(treq(Tab, {bchunk_init, Tab}), [Tab, start]);
bchunk(Tab, #dets_cont{what = bchunk, tab = Tab} = State) ->
    badarg(treq(Tab, {bchunk, State}), [Tab, State]);
bchunk(Tab, Term) ->
    erlang:error(badarg, [Tab, Term]).

close(Tab) ->
    case server_call({close, Tab}) of
        badarg -> {error, not_owner};
        Reply -> Reply
    end.

delete(Tab, Key) ->
    badarg(treq(Tab, {delete_key, [Key]}), [Tab, Key]).

delete_all_objects(Tab) ->
    case treq(Tab, delete_all_objects) of
        badarg -> erlang:error(badarg, [Tab]);
        fixed -> match_delete(Tab, '_');
        Reply -> Reply
    end.

delete_object(Tab, Object) ->
    badarg(treq(Tab, {delete_object, [Object]}), [Tab, Object]).

file_info(_) -> erlang:error({not_implemented, file_info, 1}).
fsck(_) -> erlang:error({not_implemented, fsck, 1}).
fsck(_, _) -> erlang:error({not_implemented, fsck, 2}).
get_head_field(_, _) -> erlang:error({not_implemented, get_head_field, 2}).
istart_link(_) -> erlang:error({not_implemented, istart_link, 1}).
system_code_change(_, _, _, _) -> erlang:error({not_implemented, system_code_change, 4}).
system_continue(_, _, _) -> erlang:error({not_implemented, system_continue, 3}).
system_terminate(_, _, _, _) -> erlang:error({not_implemented, system_terminate, 4}).
view(_) -> erlang:error({not_implemented, view, 1}).

first(Tab) ->
    badarg_exit(treq(Tab, first), [Tab]).

foldr(Fun, Acc, Tab) ->
    foldl(Fun, Acc, Tab).

foldl(Fun, Acc, Tab) ->
    Ref = make_ref(),
    badarg(traverse_all(Fun, Acc, Tab, Ref), [Fun, Acc, Tab]).

from_ets(DetsTab, EtsArgument) ->
    EtsTab =
        case ets_id(EtsArgument) of
            undefined -> erlang:error(badarg, [DetsTab, EtsArgument]);
            Id -> Id
        end,
    ets:safe_fixtable(EtsTab, true),
    First = ets:select(EtsTab, ?OBJECT_SPEC('_'), 100),
    Reply = treq(DetsTab, {initialize, ets_reader(First), term, default}),
    ets:safe_fixtable(EtsTab, false),
    case Reply of
        {thrown, Thrown} -> throw(Thrown);
        Other -> badarg(Other, [DetsTab, EtsTab])
    end.

%% The init_table fun of from_ets/2: the ETS table's objects 100 at a time.
ets_reader(Selected) ->
    fun
        (close) ->
            ok;
        (read) when Selected =:= '$end_of_table' -> end_of_input;
        (read) ->
            {Objects, Continuation} = Selected,
            {Objects, ets_reader(ets:select(Continuation))}
    end.

info(Tab) ->
    case table_pid(Tab) of
        undefined -> undefined;
        Pid -> undefined(req(Pid, info))
    end.

info(Tab, owner) ->
    table_pid(Tab);
info(Tab, users) ->
    case server_call({users, Tab}) of
        [] -> undefined;
        Users -> Users
    end;
info(Tab, Tag) ->
    case table_pid(Tab) of
        undefined -> undefined;
        Pid -> undefined(req(Pid, {info, Tag}))
    end.

init_table(Tab, InitFun) ->
    init_table(Tab, InitFun, []).

init_table(Tab, InitFun, Options) when is_function(InitFun) ->
    case options(Options, [format, min_no_slots]) of
        {badarg, _} ->
            erlang:error(badarg, [Tab, InitFun, Options]);
        [Format, MinNoSlots] ->
            case treq(Tab, {initialize, InitFun, Format, MinNoSlots}) of
                {thrown, Thrown} -> throw(Thrown);
                Other -> badarg(Other, [Tab, InitFun, Options])
            end
    end;
init_table(Tab, InitFun, Options) ->
    erlang:error(badarg, [Tab, InitFun, Options]).

insert(Tab, Objects) when is_list(Objects) ->
    badarg(treq(Tab, {insert, Objects}), [Tab, Objects]);
insert(Tab, Object) ->
    badarg(treq(Tab, {insert, [Object]}), [Tab, Object]).

insert_new(Tab, Objects) when is_list(Objects) ->
    badarg(treq(Tab, {insert_new, Objects}), [Tab, Objects]);
insert_new(Tab, Object) ->
    badarg(treq(Tab, {insert_new, [Object]}), [Tab, Object]).

internal_close(Pid) ->
    req(Pid, close).

internal_open(Pid, Ref, Args) ->
    req(Pid, {internal_open, Ref, Args}).

is_compatible_bchunk_format(Tab, Term) ->
    badarg(treq(Tab, {is_compatible_bchunk_format, Term}), [Tab, Term]).

is_dets_file(FileName) ->
    case clause_dets:is_dets_file(to_list(FileName)) of
        {error, {tooshort, _}} -> false;
        {error, {not_a_dets_file, _}} -> false;
        Other -> Other
    end.

lookup(Tab, Key) ->
    badarg(treq(Tab, {lookup_keys, [Key]}), [Tab, Key]).

lookup_keys(Tab, Keys) ->
    case catch lists:usort(Keys) of
        UniqueKeys when is_list(UniqueKeys), UniqueKeys =/= [] ->
            badarg(treq(Tab, {lookup_keys, UniqueKeys}), [Tab, Keys]);
        _ ->
            erlang:error(badarg, [Tab, Keys])
    end.

match(Tab, Pattern) ->
    badarg(safe_match(Tab, Pattern, bindings), [Tab, Pattern]).

match(Tab, Pattern, N) ->
    badarg(init_chunk_match(Tab, Pattern, bindings, N, no_safe), [Tab, Pattern, N]).

match(#dets_cont{what = bindings} = State) ->
    badarg(chunk_match(State, no_safe), [State]);
match(Term) ->
    erlang:error(badarg, [Term]).

match_delete(Tab, Pattern) ->
    badarg(match_delete(Tab, Pattern, delete), [Tab, Pattern]).

match_delete(Tab, Pattern, What) ->
    case compile_match_spec(What, Pattern) of
        {Spec, Program} ->
            case table_pid(Tab) of
                undefined ->
                    badarg;
                Pid ->
                    deleted(Pid, req(Pid, {match_delete_init, Program, Spec}), What, 0)
            end;
        badarg ->
            badarg
    end.

deleted(_Pid, {done, N}, select, Count) ->
    Count + N;
deleted(_Pid, {done, _}, _What, _Count) ->
    ok;
deleted(Pid, {cont, State, N}, What, Count) ->
    deleted(Pid, req(Pid, {match_delete, State}), What, Count + N);
deleted(_Pid, Error, _What, _Count) ->
    Error.

match_object(Tab, Pattern) ->
    badarg(safe_match(Tab, Pattern, object), [Tab, Pattern]).

match_object(Tab, Pattern, N) ->
    badarg(init_chunk_match(Tab, Pattern, object, N, no_safe), [Tab, Pattern, N]).

match_object(#dets_cont{what = object} = State) ->
    badarg(chunk_match(State, no_safe), [State]);
match_object(Term) ->
    erlang:error(badarg, [Term]).

member(Tab, Key) ->
    badarg(treq(Tab, {member, Key}), [Tab, Key]).

next(Tab, Key) ->
    badarg_exit(treq(Tab, {next, Key}), [Tab, Key]).

open_file(File0) ->
    case to_list(File0) of
        File when is_list(File) ->
            case server_call({open, File}) of
                badarg -> erlang:error(dets_process_died, [File]);
                Reply -> einval(Reply, [File])
            end;
        _ ->
            erlang:error(badarg, [File0])
    end.

open_file(Tab, Args) when is_list(Args) ->
    case catch defaults(Tab, Args) of
        #open_args{} = OpenArgs ->
            case server_call({open, Tab, OpenArgs}) of
                badarg -> erlang:error(dets_process_died, [Tab, Args]);
                Reply -> einval(Reply, [Tab, Args])
            end;
        _ ->
            erlang:error(badarg, [Tab, Args])
    end;
open_file(Tab, Arg) ->
    open_file(Tab, [Arg]).

pid2name(Pid) ->
    ensure_started(),
    owner_name(Pid).

remove_user(Pid, From) ->
    req(Pid, {close, From}).

repair_continuation(#dets_cont{match_program = {match_spec, Compiled}} = Cont, Spec) ->
    case ets:is_compiled_ms(Compiled) of
        true -> Cont;
        false -> Cont#dets_cont{match_program = {match_spec, ets:match_spec_compile(Spec)}}
    end;
repair_continuation(#dets_cont{} = Cont, _Spec) ->
    Cont;
repair_continuation(Term, Spec) ->
    erlang:error(badarg, [Term, Spec]).

safe_fixtable(Tab, Bool) when Bool; not Bool ->
    badarg(treq(Tab, {safe_fixtable, Bool}), [Tab, Bool]);
safe_fixtable(Tab, Term) ->
    erlang:error(badarg, [Tab, Term]).

select(Tab, Spec) ->
    badarg(safe_match(Tab, Spec, select), [Tab, Spec]).

select(Tab, Spec, N) ->
    badarg(init_chunk_match(Tab, Spec, select, N, no_safe), [Tab, Spec, N]).

select(#dets_cont{what = select} = State) ->
    badarg(chunk_match(State, no_safe), [State]);
select(Term) ->
    erlang:error(badarg, [Term]).

select_delete(Tab, Spec) ->
    badarg(match_delete(Tab, Spec, select), [Tab, Spec]).

slot(Tab, Slot) when is_integer(Slot), Slot >= 0 ->
    badarg(treq(Tab, {slot, Slot}), [Tab, Slot]);
slot(Tab, Term) ->
    erlang:error(badarg, [Tab, Term]).

start() ->
    ensure_started().

stop() ->
    case whereis(?SERVER) of
        undefined -> stopped;
        _ -> server_call(stop)
    end.

sync(Tab) ->
    badarg(treq(Tab, sync), [Tab]).

table(_) -> erlang:error({not_implemented, table, 1}).
%% table/1,2 wait for qlc (plan 21 step LIB73).

table(_, _) -> erlang:error({not_implemented, table, 2}).

to_ets(DetsTab, EtsArgument) ->
    case ets_id(EtsArgument) of
        undefined ->
            erlang:error(badarg, [DetsTab, EtsArgument]);
        EtsTab ->
            Insert = fun(Object, _) ->
                true = ets:insert(EtsTab, Object),
                EtsArgument
            end,
            foldl(Insert, EtsArgument, DetsTab)
    end.

ets_id(Name) when is_atom(Name) -> ets:whereis(Name);
ets_id(Tab) -> Tab.

traverse(Tab, Fun) ->
    Ref = make_ref(),
    Step = fun(Object, Acc) ->
        case Fun(Object) of
            continue -> Acc;
            {continue, Value} -> [Value | Acc];
            {done, Value} -> throw({Ref, [Value | Acc]});
            Other -> throw({Ref, Other})
        end
    end,
    badarg(traverse_all(Step, [], Tab, Ref), [Tab, Fun]).

update_counter(Tab, Key, Increment) ->
    badarg(treq(Tab, {update_counter, Key, Increment}), [Tab, Key, Increment]).

verbose() ->
    verbose(true).

verbose(What) ->
    ok = server_call({set_verbose, What}),
    All = all(),
    lists:foreach(fun(Tab) -> treq(Tab, {set_verbose, What}) end, All),
    All.

where(Tab, Object) ->
    badarg(treq(Tab, {where, Object}), [Tab, Object]).

%%% --- Traversals and matches in the caller ---

%% Fold Fun over every object, chunk by chunk with the table fixed; a throw of {Ref, Result} ends it with Result.
traverse_all(Fun, Acc, Tab, Ref) ->
    case table_pid(Tab) of
        undefined ->
            badarg;
        Pid ->
            try
                {Spec, Program} = compile_match_spec(object, '_'),
                case req(Pid, {match, Program, Spec, default, safe}) of
                    {cont, State} -> traverse_chunks(State, Pid, Acc, Fun);
                    Error -> Error
                end
            catch
                throw:{Ref, Result} -> Result
            end
    end.

traverse_chunks(State, Pid, Acc, Fun) ->
    case req(Pid, {match_init, State, safe}) of
        '$end_of_table' ->
            Acc;
        {cont, {Objects, NewState}} ->
            traverse_chunks(NewState, Pid, lists:foldl(Fun, Acc, Objects), Fun);
        Error ->
            Error
    end.

safe_match(Tab, Pattern, What) ->
    collect_matches(init_chunk_match(Tab, Pattern, What, default, safe), []).

collect_matches({error, Error}, _Acc) ->
    {error, Error};
collect_matches({Found, State}, Acc) ->
    collect_matches(chunk_match(State, safe), Found ++ Acc);
collect_matches('$end_of_table', Acc) ->
    Acc;
collect_matches(badarg, _Acc) ->
    badarg.

init_chunk_match(Tab, Pattern, What, N, Safe) when is_integer(N), N >= 0; N =:= default ->
    case compile_match_spec(What, Pattern) of
        {Spec, Program} ->
            case table_pid(Tab) of
                undefined ->
                    badarg;
                Pid ->
                    case req(Pid, {match, Program, Spec, N, Safe}) of
                        {done, Found} ->
                            Done = #dets_cont{
                                tab = Tab,
                                proc = Pid,
                                what = What,
                                bin = eof,
                                no_objs = default,
                                alloc = <<>>
                            },
                            {Found, Done};
                        {cont, State} ->
                            chunk_match(State#dets_cont{what = What, tab = Tab, proc = Pid}, Safe);
                        Error ->
                            Error
                    end
            end;
        badarg ->
            badarg
    end;
init_chunk_match(_Tab, _Pattern, _What, _N, _Safe) ->
    badarg.

%% The next chunk with matches: chunks without any are passed over.
chunk_match(#dets_cont{proc = Pid} = State, Safe) ->
    case req(Pid, {match_init, State, Safe}) of
        '$end_of_table' = End -> End;
        {cont, {[], NewState}} -> chunk_match(NewState, Safe);
        {cont, {Found, NewState}} -> {Found, NewState};
        Error -> Error
    end.

compile_match_spec(select, ?OBJECT_SPEC('_') = Spec) ->
    {Spec, true};
compile_match_spec(select, Spec) ->
    try
        {Spec, {match_spec, ets:match_spec_compile(Spec)}}
    catch
        error:_ -> badarg
    end;
compile_match_spec(object, Pattern) ->
    compile_match_spec(select, ?OBJECT_SPEC(Pattern));
compile_match_spec(bindings, Pattern) ->
    compile_match_spec(select, ?BINDINGS_SPEC(Pattern));
compile_match_spec(delete, Pattern) ->
    compile_match_spec(select, ?TRUE_SPEC(Pattern)).

%%% --- Options ---

defaults(Tab, Args) ->
    Defaults = #open_args{
        file = to_list(Tab),
        type = set,
        keypos = 1,
        repair = true,
        min_no_slots = default,
        max_no_slots = default,
        ram_file = false,
        delayed_write = ?DEFAULT_CACHE,
        auto_save = ?DEFAULT_AUTOSAVE * 60000,
        access = read_write,
        debug = false
    },
    Opened = lists:foldl(fun option/2, Defaults, Args),
    true = is_list(Opened#open_args.file),
    min_within_max(Opened).

to_list(Name) when is_atom(Name) -> atom_to_list(Name);
to_list(Name) -> Name.

option({access, Access}, Args) ->
    one_of(Access, [read, read_write]),
    Args#open_args{access = Access};
option({auto_save, Millis}, Args) when is_integer(Millis), Millis >= 0 ->
    Args#open_args{auto_save = Millis};
option({auto_save, infinity}, Args) ->
    Args#open_args{auto_save = infinity};
option({cache_size, Size}, Args) when is_integer(Size), Size >= 0 ->
    Args;
option({cache_size, infinity}, Args) ->
    Args;
option({delayed_write, default}, Args) ->
    Args#open_args{delayed_write = ?DEFAULT_CACHE};
option({delayed_write, {Delay, Size} = Cache}, Args) when
    is_integer(Delay), Delay >= 0, is_integer(Size), Size >= 0
->
    Args#open_args{delayed_write = Cache};
option({estimated_no_objects, Slots}, Args) ->
    option({min_no_slots, Slots}, Args);
option({file, File}, Args) ->
    Args#open_args{file = to_list(File)};
option({keypos, Position}, Args) when is_integer(Position), Position > 0 ->
    Args#open_args{keypos = Position};
option({max_no_slots, Slots}, Args) ->
    Args#open_args{max_no_slots = max_slots(Slots)};
option({min_no_slots, Slots}, Args) ->
    Args#open_args{min_no_slots = min_slots(Slots)};
option({ram_file, Bool}, Args) ->
    one_of(Bool, [true, false]),
    Args#open_args{ram_file = Bool};
option({repair, Repair}, Args) ->
    one_of(Repair, [true, false, force]),
    Args#open_args{repair = Repair};
option({type, Type}, Args) ->
    one_of(Type, [set, bag, duplicate_bag]),
    Args#open_args{type = Type};
option({version, Version}, Args) when Version =:= default; Version =:= 9 ->
    Args;
option({debug, Bool}, Args) ->
    one_of(Bool, [true, false]),
    Args#open_args{debug = Bool};
option(_, _) ->
    exit(badarg).

min_slots(default) -> default;
min_slots(Slots) when is_integer(Slots), Slots >= ?DEFAULT_MIN_NO_SLOTS -> Slots;
min_slots(Slots) when is_integer(Slots), Slots >= 0 -> ?DEFAULT_MIN_NO_SLOTS.

max_slots(default) -> default;
max_slots(Slots) when is_integer(Slots), Slots > 0, Slots < ?MAX_SLOTS_LIMIT -> Slots.

min_within_max(#open_args{min_no_slots = Min, max_no_slots = Max} = Args) ->
    if
        Min =:= default ->
            Args;
        Max =:= default ->
            Args;
        true ->
            true = Min =< Max,
            Args
    end.

one_of(Value, Allowed) ->
    case lists:member(Value, Allowed) of
        true -> true;
        false -> exit(badarg)
    end.

%% The options of init_table/3 in the order of Keys, with their defaults; {badarg, Key} for a bad one.
options(Options, Keys) when is_list(Options) ->
    options(Options, Keys, []);
options(Option, Keys) ->
    options([Option], Keys, []).

options(Options, [Key | Keys], Values) when is_list(Options) ->
    case option_value(lists:keysearch(Key, 1, Options), Key) of
        badarg -> {badarg, Key};
        {ok, Value} -> options(lists:keydelete(Key, 1, Options), Keys, [Value | Values])
    end;
options([], [], Values) ->
    lists:reverse(Values);
options(Options, _, _) ->
    {badarg, Options}.

option_value({value, {format, Format}}, _) when Format =:= term; Format =:= bchunk ->
    {ok, Format};
option_value({value, {min_no_slots, Slots}}, _) ->
    case catch min_slots(Slots) of
        {'EXIT', _} -> badarg;
        Min -> {ok, Min}
    end;
option_value({value, {n_objects, default}}, Key) ->
    {ok, default_option(Key)};
option_value({value, {n_objects, N}}, _) when is_integer(N), N >= 1 ->
    {ok, N};
option_value({value, {traverse, select}}, _) ->
    {ok, select};
option_value({value, {traverse, {select, Spec}}}, _) ->
    {ok, {select, Spec}};
option_value({value, {traverse, first_next}}, _) ->
    {ok, first_next};
option_value({value, {_, _}}, _) ->
    badarg;
option_value(false, Key) ->
    {ok, default_option(Key)}.

default_option(format) -> term;
default_option(min_no_slots) -> default;
default_option(traverse) -> select;
default_option(n_objects) -> default.

%%% --- Requests ---

table_pid(Tab) ->
    case catch ets:lookup_element(?REGISTRY, Tab, 3) of
        Pid when is_pid(Pid) -> Pid;
        _ -> undefined
    end.

treq(Tab, Request) ->
    case table_pid(Tab) of
        undefined -> badarg;
        Pid -> req(Pid, Request)
    end.

req(Pid, Request) ->
    Ref = erlang:monitor(process, Pid),
    Pid ! ?CALL({self(), Ref}, Request),
    receive
        {'DOWN', Ref, process, Pid, _} ->
            badarg;
        {Ref, Reply} ->
            erlang:demonitor(Ref, [flush]),
            Reply
    end.

reply({Pid, Ref}, Message) ->
    Pid ! {Ref, Message},
    ok.

einval({error, {file_error, _, einval}}, Arguments) -> erlang:error(badarg, Arguments);
einval({error, {file_error, _, badarg}}, Arguments) -> erlang:error(badarg, Arguments);
einval(Reply, _) -> Reply.

badarg(badarg, Arguments) -> erlang:error(badarg, Arguments);
badarg(Reply, _) -> Reply.

undefined(badarg) -> undefined;
undefined(Reply) -> Reply.

badarg_exit(badarg, Arguments) -> erlang:error(badarg, Arguments);
badarg_exit({ok, Reply}, _) -> Reply;
badarg_exit(Reply, _) -> exit(Reply).

%%% --- The server: users and their tables ---

ensure_started() ->
    case whereis(?SERVER) of
        undefined ->
            Self = self(),
            Pid = spawn(fun() -> server_start(Self) end),
            Ref = erlang:monitor(process, Pid),
            receive
                {Pid, started} -> erlang:demonitor(Ref, [flush]);
                {'DOWN', Ref, process, Pid, _} -> ok
            end,
            ok;
        _ ->
            ok
    end.

%% A second server that loses the race to register leaves at once.
server_start(Starter) ->
    case catch register(?SERVER, self()) of
        true ->
            process_flag(trap_exit, true),
            ?REGISTRY = ets:new(?REGISTRY, [set, named_table]),
            ?OWNERS = ets:new(?OWNERS, [set, named_table]),
            Store = ets:new(?SERVER, [duplicate_bag]),
            Starter ! {self(), started},
            server_loop(Store);
        _ ->
            ok
    end.

server_call(Request) ->
    ensure_started(),
    Pid = whereis(?SERVER),
    Ref = erlang:monitor(process, Pid),
    Pid ! {dets_server, {self(), Ref}, Request},
    receive
        {Ref, Reply} ->
            erlang:demonitor(Ref, [flush]),
            Reply;
        {'DOWN', Ref, process, Pid, Reason} ->
            exit({Reason, {dets_server, call, [Request]}})
    end.

server_loop(Store) ->
    receive
        {dets_server, From, stop} ->
            reply(From, stopped);
        {dets_server, {Pid, _} = From, Request} ->
            reply(From, server_request(Store, Pid, Request)),
            server_loop(Store);
        {'EXIT', Pid, _Reason} ->
            exited(Store, Pid),
            server_loop(Store);
        _ ->
            server_loop(Store)
    end.

server_request(_Store, _Pid, all) ->
    ets:foldl(fun(Entry, Names) -> [element(1, Entry) | Names] end, [], ?REGISTRY);
server_request(Store, _Pid, {users, Tab}) ->
    ets:select(Store, [{{'$1', Tab}, [], ['$1']}]);
server_request(_Store, _Pid, {set_verbose, _}) ->
    ok;
server_request(Store, Pid, {close, Tab}) ->
    close_for(Store, Pid, Tab);
server_request(Store, Pid, {open, File}) ->
    open_new(Store, Pid, make_ref(), [File, undefined]);
server_request(Store, Pid, {open, Tab, OpenArgs}) ->
    case ets:lookup(?REGISTRY, Tab) of
        [] ->
            open_new(Store, Pid, Tab, [Tab, OpenArgs, undefined]);
        [{Tab, _Users, Table}] ->
            case add_user(Table, Tab, OpenArgs) of
                ok ->
                    link_user(Store, Pid),
                    true = ets:insert(Store, {Pid, Tab}),
                    _ = ets:update_counter(?REGISTRY, Tab, 1),
                    {ok, Tab};
                Error ->
                    Error
            end
    end.

open_new(Store, Pid, Tab, Args) ->
    Server = self(),
    Table = spawn_link(fun() -> table_start(Server) end),
    true = ets:insert(?REGISTRY, {Tab, 1, Table}),
    true = ets:insert(?OWNERS, {Table, Tab}),
    case internal_open(Table, Tab, Args) of
        ok ->
            link_user(Store, Pid),
            true = ets:insert(Store, {Pid, Tab}),
            {ok, Tab};
        Error ->
            true = ets:delete(?REGISTRY, Tab),
            true = ets:delete(?OWNERS, Table),
            Error
    end.

close_for(Store, Pid, Tab) ->
    case ets:match_object(Store, {Pid, Tab}) of
        [] ->
            {error, not_owner};
        [_ | Keep] ->
            unlink_user(Store, Pid),
            true = ets:match_delete(Store, {Pid, Tab}),
            case ets:lookup(?REGISTRY, Tab) of
                [{Tab, 1, Table}] ->
                    true = ets:delete(?REGISTRY, Tab),
                    true = ets:delete(?OWNERS, Table),
                    unlink(Table),
                    internal_close(Table);
                [{Tab, _, Table}] ->
                    true = ets:insert(Store, Keep),
                    _ = ets:update_counter(?REGISTRY, Tab, -1),
                    remove_user(Table, Pid)
            end
    end.

%% A table process that ended takes its table with it; a user that ended closes its tables.
exited(Store, Pid) ->
    case owner_name(Pid) of
        {ok, Tab} ->
            true = ets:delete(?REGISTRY, Tab),
            true = ets:delete(?OWNERS, Pid),
            Users = ets:select(Store, [{{'$1', Tab}, [], ['$1']}]),
            true = ets:match_delete(Store, {'_', Tab}),
            lists:foreach(fun(User) -> unlink_user(Store, User) end, Users);
        undefined ->
            [close_for(Store, Pid, Tab) || {_, Tab} <- ets:lookup(Store, Pid)],
            ok
    end.

owner_name(Pid) ->
    case catch ets:lookup(?OWNERS, Pid) of
        [{_, Tab}] -> {ok, Tab};
        _ -> undefined
    end.

%% A user is linked once however many tables it opened.
link_user(Store, Pid) ->
    Key = {links, Pid},
    case ets:lookup(Store, Key) of
        [] ->
            true = ets:insert(Store, {Key, 1}),
            link(Pid);
        [{_, Count}] ->
            true = ets:delete(Store, Key),
            true = ets:insert(Store, {Key, Count + 1})
    end.

unlink_user(Store, Pid) ->
    Key = {links, Pid},
    case ets:lookup(Store, Key) of
        [{_, Count}] when Count > 1 ->
            true = ets:delete(Store, Key),
            true = ets:insert(Store, {Key, Count - 1});
        _ ->
            true = ets:delete(Store, Key),
            unlink(Pid)
    end.

%%% --- The table process ---

%% init/2 is OTP's entry of a table process under dets_sup; Clause's server starts table_start/1.
init(_, _) -> erlang:error({not_implemented, init, 2}).

table_start(Server) ->
    process_flag(trap_exit, true),
    receive
        ?CALL(From, {internal_open, Tab, Args}) ->
            case open_table(Server, Tab, Args) of
                {ok, T} ->
                    reply(From, ok),
                    table_loop(T);
                Error ->
                    reply(From, Error),
                    exit(normal)
            end
    end.

open_table(Server, Ref, [File, _Verbose]) ->
    case clause_dets:open_file(File) of
        {ok, Handle} ->
            T = #tab{
                handle = Handle,
                name = Ref,
                file = File,
                access = read_write,
                ram = false,
                auto_save = infinity,
                update_mode = saved,
                cache = new_cache(?DEFAULT_CACHE),
                server = Server
            },
            {ok, opened(T)};
        Error ->
            Error
    end;
open_table(Server, Tab, [Tab, #open_args{} = Args, _Verbose]) ->
    case clause_dets:open(Args) of
        {ok, Handle, Created} ->
            T = #tab{
                handle = Handle,
                name = Tab,
                file = Args#open_args.file,
                access = Args#open_args.access,
                ram = Args#open_args.ram_file,
                auto_save = Args#open_args.auto_save,
                update_mode = saved,
                cache = new_cache(Args#open_args.delayed_write),
                server = Server
            },
            {ok, created(opened(T), Created)};
        Error ->
            Error
    end.

opened(T) ->
    {Type, Keypos, _, _, _, _, _, _, _, _, _, _} = clause_dets:info(T#tab.handle),
    T1 = T#tab{type = Type, keypos = Keypos},
    check_growth(T1),
    T1.

%% A new file is written before its first update, so nothing marks it dirty: OTP's new_dirty.
created(T, true) ->
    start_auto_save_timer(T),
    T#tab{update_mode = new_dirty};
created(T, false) ->
    T.

new_cache({Delay, Size}) ->
    #cache{tsize = Size, delay = Delay}.

table_loop(T) ->
    receive
        ?CALL(From, Request) ->
            table_loop(serve(Request, From, T));
        {'EXIT', Pid, Reason} when Pid =:= T#tab.server ->
            _ = stop_table(T),
            exit(Reason);
        {'EXIT', Pid, _Reason} ->
            table_loop(remove_fix(T, Pid, close));
        _ ->
            table_loop(T)
    end.

stop_table(T) ->
    fclose(unlink_fixers(T)).

serve({add_user, Tab, Args}, From, T) ->
    #open_args{file = File, type = Type, keypos = Keypos, ram_file = Ram, access = Access} = Args,
    Same =
        Tab =:= T#tab.name andalso Keypos =:= T#tab.keypos andalso Type =:= T#tab.type andalso
            Ram =:= T#tab.ram andalso Access =:= T#tab.access andalso File =:= T#tab.file,
    reply(
        From,
        case Same of
            true -> ok;
            false -> {error, incompatible_arguments}
        end
    ),
    T;
serve(auto_save, _From, T) ->
    auto_save(T);
serve(close, From, T) ->
    reply(From, fclose(T)),
    _ = unlink_fixers(T),
    exit(normal);
serve({close, Pid}, From, T) ->
    T1 = remove_fix(T, Pid, close),
    reply(From, status(T1)),
    T1;
serve({delayed_write, WriteTime}, _From, T) ->
    delayed_write(T, WriteTime);
serve(info, From, T) ->
    answer(From, info_list(T));
serve({info, Tag}, From, T) ->
    answer(From, info_item(T, Tag));
serve({is_compatible_bchunk_format, Term}, From, T) ->
    reply(From, compatible_bchunk(T, Term)),
    T;
serve(may_grow, _From, #tab{update_mode = dirty} = T) ->
    case clause_dets:may_grow(T#tab.handle) of
        {ok, true} -> self() ! ?CALL(self(), may_grow);
        _ -> ok
    end,
    T#tab{ops = T#tab.ops + 1};
serve(may_grow, _From, T) ->
    T;
serve({set_verbose, _}, From, T) ->
    reply(From, ok),
    T;
serve({where, Object}, From, T) ->
    answer(From, where_is(T, Object));
serve(_Request, From, #tab{update_mode = {error, _} = Error} = T) ->
    reply(From, Error),
    T;
serve(Request, From, T) ->
    serve_ok(Request, From, T).

%% The requests of a table whose file is well.
serve_ok({bchunk_init, Tab}, From, T) ->
    answer(From, bchunk_start(T, Tab));
serve_ok({bchunk, State}, From, T) ->
    answer(From, bchunk_next(T, State, From));
serve_ok(delete_all_objects, From, T) ->
    {T1, Reply} = delete_all(T),
    reply(From, Reply),
    T1#tab{ops = 0};
serve_ok(first, From, T) ->
    answer(From, first_key(T));
serve_ok({initialize, InitFun, Format, MinNoSlots}, From, T) ->
    answer(From, initialize(T, InitFun, Format, MinNoSlots));
serve_ok({lookup_keys, Keys}, From, T) ->
    lookup_reply(From, update_cache(T, [{{lookup, From}, Keys}]));
serve_ok({member, Key}, From, T) ->
    lookup_reply(From, update_cache(T, [{{lookup, [From]}, [Key]}]));
serve_ok({match_init, State, Safe}, From, T) ->
    {T1, Reply} = match_next(T, State),
    T2 =
        case Reply of
            {cont, _} -> T1;
            _ when Safe =:= no_safe -> T1;
            _ -> fixtable(T1, pid_of(From), false)
        end,
    reply(From, Reply),
    T2;
serve_ok({match, Program, Spec, N, Safe}, From, T) ->
    answer(From, match_start(T, Program, Spec, N, Safe, From));
serve_ok({next, Key}, From, T) ->
    answer(From, next_key(T, Key));
serve_ok({safe_fixtable, Bool}, From, T) ->
    T1 = fixtable(T, pid_of(From), Bool),
    reply(From, ok),
    T1;
serve_ok({slot, Slot}, From, T) ->
    answer(From, slot_objects(T, Slot));
serve_ok(sync, From, T) ->
    {T1, Reply} = perform_save(T, true),
    reply(From, Reply),
    T1#tab{ops = 0};
serve_ok(Request, From, #tab{update_mode = dirty} = T) ->
    serve_write(Request, From, T);
serve_ok(Request, From, #tab{update_mode = new_dirty} = T) ->
    serve_write(Request, From, T#tab{update_mode = dirty, ops = 0});
serve_ok(Request, From, #tab{access = read_write, update_mode = saved} = T) ->
    case clause_dets:mark_dirty(T#tab.handle) of
        ok ->
            start_auto_save_timer(T),
            serve_write(Request, From, T#tab{update_mode = dirty, ops = 0});
        Error ->
            {T1, _} = corrupt(T, Error),
            reply(From, Error),
            T1
    end;
serve_ok(_Request, From, T) ->
    reply(From, {error, {access_mode, T#tab.file}}),
    T.

%% The updates, on a table marked dirty.
serve_write({delete_key, Keys}, From, T) ->
    written(From, update_cache(T, [{delete_key, Keys}]));
serve_write({delete_object, Objects}, From, T) ->
    checked_write(From, T, Objects, {delete_object, Objects});
serve_write({insert, Objects}, From, T) ->
    checked_write(From, T, Objects, {insert, Objects});
serve_write({insert_new, Objects}, From, T) ->
    {T1, Reply} = insert_new_objects(T, Objects),
    reply(From, Reply),
    T1#tab{ops = T1#tab.ops + 1};
serve_write({match_delete, State}, From, T) ->
    {T1, Reply} = match_delete_next(T, State),
    T2 =
        case Reply of
            {cont, _, _} -> T1;
            _ -> fixtable(T1, pid_of(From), false)
        end,
    reply(From, Reply),
    T2#tab{ops = T2#tab.ops + 1};
serve_write({match_delete_init, Program, Spec}, From, T) ->
    {T1, Reply} = match_delete_start(T, Program, Spec, From),
    reply(From, Reply),
    T1#tab{ops = T1#tab.ops + 1};
serve_write({update_counter, Key, Increment}, From, T) ->
    {T1, Reply} = update_counter_of(T, Key, Increment),
    reply(From, Reply),
    T1#tab{ops = T1#tab.ops + 1}.

checked_write(From, T, Objects, Op) ->
    case valid_objects(Objects, T#tab.keypos) of
        true ->
            written(From, update_cache(T, [Op]));
        false ->
            reply(From, badarg),
            T
    end.

%% A write's reply once the cache has it.
written(From, {T, Found}) when is_list(Found) ->
    reply(From, ok),
    T#tab{ops = T#tab.ops + 1};
written(From, {T, Error}) ->
    reply(From, Error),
    T#tab{ops = T#tab.ops + 1}.

%% A lookup's or member's reply: the objects of every key, the lists sorted and appended as OTP's
%% lookup_replies/1 gives them, or whether any was found.
lookup_reply(From, {T, Found}) when is_list(Found) ->
    reply(From, lookup_result(Found)),
    T;
lookup_reply(From, {T, Error}) ->
    reply(From, Error),
    T.

lookup_result([{[_], Objects}]) ->
    Objects =/= [];
lookup_result([{_, Objects}]) ->
    Objects;
lookup_result([{Asker, _} | _] = Found) ->
    Lists = msort([Objects || {_, Objects} <- Found]),
    case Asker of
        [_] -> lists:append(Lists) =/= [];
        _ -> lists:append(Lists)
    end.

answer(From, {T, Reply}) ->
    reply(From, Reply),
    T.

pid_of({Pid, _}) -> Pid.

valid_objects([Object | Objects], Keypos) when tuple_size(Object) >= Keypos ->
    valid_objects(Objects, Keypos);
valid_objects(Objects, _) ->
    Objects =:= [].

status(#tab{update_mode = saved}) -> ok;
status(#tab{update_mode = dirty}) -> ok;
status(#tab{update_mode = new_dirty}) -> ok;
status(#tab{update_mode = Error}) -> Error.

%% A table whose file failed answers every later request with the error.
corrupt(#tab{update_mode = {error, _}} = T, Error) ->
    {T, Error};
corrupt(T, Error) ->
    {T#tab{update_mode = Error}, Error}.

start_auto_save_timer(#tab{auto_save = infinity}) ->
    ok;
start_auto_save_timer(#tab{auto_save = Millis}) ->
    _ = erlang:send_after(Millis, self(), ?CALL(self(), auto_save)),
    ok.

auto_save(#tab{update_mode = saved} = T) ->
    T;
auto_save(#tab{update_mode = {error, _}} = T) ->
    T;
auto_save(#tab{ops = 0} = T) ->
    {T1, _} = perform_save(T, true),
    erlang:garbage_collect(),
    T1#tab{ops = 0};
auto_save(#tab{update_mode = dirty} = T) ->
    start_auto_save_timer(T),
    T#tab{ops = 0}.

%% Grow after 200 ms when there are more keys than slots (after an unfix or an init_table).
check_growth(#tab{access = read}) ->
    ok;
check_growth(T) ->
    {_, _, _, Keys, _, _, Next, _, _, _, _, _} = clause_dets:info(T#tab.handle),
    case Keys > Next of
        true -> _ = erlang:send_after(200, self(), ?CALL(self(), may_grow));
        false -> ok
    end,
    ok.

%%% --- The write cache ---

time_now() ->
    erlang:monotonic_time(1000000).

%% Add commands to the cache: written at once with a lookup that needs the file, or when the cache grew past its
%% threshold, else after the delay. The lookups' objects, or an error.
update_cache(T, ToAdd) ->
    #cache{cache = C, csize = Size0, inserts = Inserts0} = Cache = T#tab.cache,
    NewSize = Size0 + erlang:external_size(ToAdd),
    {NewC, Inserts, Lookup, Found} = cache_commands(T, ToAdd, C, Size0, Inserts0, false, []),
    Cache1 = Cache#cache{cache = NewC, csize = NewSize, inserts = Inserts},
    T1 = T#tab{cache = Cache1},
    if
        Lookup; NewSize >= Cache#cache.tsize ->
            case flush(T1) of
                {T2, LU} when is_list(LU) -> {T2, Found ++ LU};
                Failed -> Failed
            end;
        NewC =:= [] ->
            {T1, Found};
        Cache#cache.wrtime =:= undefined ->
            Now = time_now(),
            _ = erlang:send_after(Cache#cache.delay, self(), ?CALL(self(), {delayed_write, Now})),
            {T1#tab{cache = Cache1#cache{wrtime = Now}}, Found};
        Size0 =:= 0 ->
            {T1#tab{cache = Cache1#cache{wrtime = time_now()}}, Found};
        true ->
            {T1, Found}
    end.

cache_commands(T, [{delete_object, Objects} | Rest], C, Seq, Inserts, Lookup, Found) ->
    {C1, Seq1} = cache_objects(Objects, T#tab.keypos, delete_object, C, Seq),
    cache_commands(T, Rest, C1, Seq1, Inserts, Lookup, Found);
cache_commands(T, [{insert, Objects} | Rest], C, Seq, Inserts, Lookup, Found) ->
    {C1, Seq1} = cache_objects(Objects, T#tab.keypos, insert, C, Seq),
    cache_commands(T, Rest, C1, Seq1, Inserts + length(Objects), Lookup, Found);
cache_commands(T, [{delete_key, Keys} | Rest], C, Seq, Inserts, Lookup, Found) ->
    {C1, Seq1} = cache_keys(Keys, delete_key, C, Seq),
    cache_commands(T, Rest, C1, Seq1, Inserts, Lookup, Found);
cache_commands(T, [{Lookup, Keys} | Rest], [], Seq, Inserts, _Lookup, Found) ->
    {C1, Seq1} = cache_keys(Keys, Lookup, [], Seq),
    cache_commands(T, Rest, C1, Seq1, Inserts, true, Found);
cache_commands(T, [{{lookup, Asker} = Lookup, Keys} | Rest], C, Seq, Inserts, Lookups, Found) ->
    case cached_objects(T#tab.type, Keys, C, []) of
        false ->
            {C1, Seq1} = cache_keys(Keys, Lookup, C, Seq),
            cache_commands(T, Rest, C1, Seq1, Inserts, true, Found);
        Objects ->
            cache_commands(T, Rest, C, Seq, Inserts, Lookups, [{Asker, Objects} | Found])
    end;
cache_commands(_T, [], C, _Seq, Inserts, Lookup, Found) ->
    {C, Inserts, Lookup, Found}.

cache_objects([Object | Objects], Keypos, Command, C, Seq) ->
    cache_objects(
        Objects, Keypos, Command, [{element(Keypos, Object), {Seq, {Command, Object}}} | C], Seq + 1
    );
cache_objects([], _Keypos, _Command, C, Seq) ->
    {C, Seq}.

cache_keys([Key | Keys], Command, C, Seq) ->
    cache_keys(Keys, Command, [{Key, {Seq, Command}} | C], Seq + 1);
cache_keys([], _Command, C, Seq) ->
    {C, Seq}.

%% The objects of keys the cache alone answers (a set's inserted or deleted keys); false otherwise.
cached_objects(Type, [Key | Keys], C, Found) ->
    case newest(Key, C) of
        {Key, {_, {insert, Object}}} when Type =:= set ->
            cached_objects(Type, Keys, C, [Object | Found]);
        {Key, {_, delete_key}} ->
            cached_objects(Type, Keys, C, Found);
        _ ->
            false
    end;
cached_objects(_Type, [], _C, Found) ->
    Found.

newest(Key, [{K, _} = Entry | _]) when K =:= Key -> Entry;
newest(Key, [_ | C]) -> newest(Key, C);
newest(_Key, []) -> false.

%% Write the cache to the file; the lookups' objects, or the error that corrupts the table.
flush(#tab{cache = #cache{cache = []}} = T) ->
    {T, []};
flush(#tab{cache = Cache} = T) ->
    WriteTime =
        case Cache#cache.wrtime of
            undefined -> undefined;
            _ -> time_now()
        end,
    T1 = T#tab{cache = Cache#cache{cache = [], csize = 0, inserts = 0, wrtime = WriteTime}},
    case clause_dets:write_cache(T#tab.handle, Cache#cache.cache, Cache#cache.inserts) of
        {ok, Found} -> {T1, Found};
        Error -> corrupt(T1, Error)
    end.

%% Run Fun on a flushed table; the table and the error when the flush failed.
flushed(T, Fun) ->
    case flush(T) of
        {T1, Found} when is_list(Found) -> Fun(T1);
        Failed -> Failed
    end.

delayed_write(#tab{cache = #cache{wrtime = WriteTime}} = T, WriteTime) ->
    case flush(T) of
        {#tab{cache = Cache} = T1, Found} when is_list(Found) ->
            T1#tab{cache = Cache#cache{wrtime = undefined}};
        {T1, _Error} ->
            T1
    end;
delayed_write(#tab{cache = #cache{csize = 0} = Cache} = T, _WriteTime) ->
    T#tab{cache = Cache#cache{wrtime = undefined}};
delayed_write(#tab{cache = #cache{wrtime = Last} = Cache} = T, WriteTime) ->
    When = round((Last - WriteTime) / 1000),
    _ = erlang:send_after(When, self(), ?CALL(self(), {delayed_write, Last})),
    T#tab{cache = Cache}.

%% The objects of keys, flushing the cache: OTP's flookup_keys/2.
lookup_objects(T, Keys) ->
    case update_cache(T, [{{lookup, nopid}, Keys}]) of
        {T1, [{_, Objects}]} ->
            {T1, Objects};
        {T1, Found} when is_list(Found) ->
            {T1, lists:flatmap(fun({_, Objects}) -> Objects end, Found)};
        Failed ->
            Failed
    end.

%%% --- Saving and closing ---

perform_save(#tab{update_mode = Mode} = T, Sync) when Mode =:= dirty; Mode =:= new_dirty ->
    flushed(T, fun(T1) ->
        case clause_dets:save(T1#tab.handle, Sync) of
            ok -> {T1#tab{update_mode = saved}, ok};
            Error -> corrupt(T1, Error)
        end
    end);
perform_save(T, _Sync) ->
    {T, status(T)}.

fclose(T) ->
    {T1, Reply} = perform_save(T, false),
    ok = clause_dets:close(T1#tab.handle),
    Reply.

%%% --- Fixing ---

fixtable(#tab{fixed = false} = T, Pid, true) ->
    link(Pid),
    ok = clause_dets:fix(T#tab.handle, true),
    T#tab{fixed = {{erlang:monotonic_time(), erlang:time_offset()}, [{Pid, 1}]}};
fixtable(#tab{fixed = {Time, Counters}} = T, Pid, true) ->
    case lists:keyfind(Pid, 1, Counters) of
        {Pid, Count} ->
            T#tab{fixed = {Time, lists:keyreplace(Pid, 1, Counters, {Pid, Count + 1})}};
        false ->
            link(Pid),
            T#tab{fixed = {Time, [{Pid, 1} | Counters]}}
    end;
fixtable(T, Pid, false) ->
    remove_fix(T, Pid, false).

%% Drop a process's fixes (all of them when it closed the table or ended); the last one unfixes the table.
remove_fix(#tab{fixed = false} = T, _Pid, _How) ->
    T;
remove_fix(#tab{fixed = {Time, Counters}} = T, Pid, How) ->
    case lists:keyfind(Pid, 1, Counters) of
        {Pid, Count} when Count =:= 1; How =:= close ->
            unlink(Pid),
            case lists:keydelete(Pid, 1, Counters) of
                [] ->
                    check_growth(T),
                    erlang:garbage_collect(),
                    ok = clause_dets:fix(T#tab.handle, false),
                    T#tab{fixed = false};
                Rest ->
                    T#tab{fixed = {Time, Rest}}
            end;
        {Pid, Count} ->
            T#tab{fixed = {Time, lists:keyreplace(Pid, 1, Counters, {Pid, Count - 1})}};
        false ->
            T
    end.

unlink_fixers(#tab{fixed = false} = T) ->
    T;
unlink_fixers(#tab{fixed = {_, Counters}} = T) ->
    lists:foreach(fun({Pid, _}) -> unlink(Pid) end, Counters),
    ok = clause_dets:fix(T#tab.handle, false),
    T#tab{fixed = false}.

%%% --- Reads and updates ---

info_list(T) ->
    flushed(T, fun(T1) ->
        {Type, Keypos, Objects, _, FileSize, _, _, _, _, _, _, _} = clause_dets:info(T1#tab.handle),
        {T1, [
            {type, Type},
            {keypos, Keypos},
            {size, Objects},
            {file_size, FileSize},
            {filename, T1#tab.file}
        ]}
    end).

info_item(T, access) ->
    {T, T#tab.access};
info_item(T, auto_save) ->
    {T, T#tab.auto_save};
info_item(T, bchunk_format) ->
    flushed(T, fun(T1) ->
        case parameters(T1) of
            undefined -> {T1, undefined};
            Parameters -> {T1, term_to_binary(Parameters)}
        end
    end);
info_item(T, delayed_write) ->
    {T, {(T#tab.cache)#cache.delay, (T#tab.cache)#cache.tsize}};
info_item(T, filename) ->
    {T, T#tab.file};
info_item(T, file_size) ->
    flushed(T, fun(T1) -> {T1, element(5, clause_dets:info(T1#tab.handle))} end);
info_item(T, fixed) ->
    {T, T#tab.fixed =/= false};
info_item(T, hash) ->
    {T, element(9, clause_dets:info(T#tab.handle))};
info_item(T, keypos) ->
    {T, T#tab.keypos};
info_item(T, memory) ->
    info_item(T, file_size);
info_item(T, no_objects) ->
    info_item(T, size);
info_item(T, no_keys) ->
    flushed(T, fun(T1) -> {T1, element(4, clause_dets:info(T1#tab.handle))} end);
info_item(T, no_slots) ->
    {_, _, _, _, _, Min, Next, Max, _, _, _, _} = clause_dets:info(T#tab.handle),
    {T, {Min, Next, Max}};
info_item(T, pid) ->
    {T, self()};
info_item(T, ram_file) ->
    {T, T#tab.ram};
info_item(T, safe_fixed) ->
    {T,
        case T#tab.fixed of
            false -> false;
            {{MonTime, Offset}, Counters} -> {timestamp(MonTime, Offset), Counters}
        end};
info_item(T, safe_fixed_monotonic_time) ->
    {T,
        case T#tab.fixed of
            false -> false;
            {{MonTime, _}, Counters} -> {MonTime, Counters}
        end};
info_item(T, size) ->
    flushed(T, fun(T1) -> {T1, element(3, clause_dets:info(T1#tab.handle))} end);
info_item(T, type) ->
    {T, T#tab.type};
info_item(T, version) ->
    {T, 9};
info_item(T, _) ->
    {T, undefined}.

timestamp(MonTime, Offset) ->
    Micro = erlang:convert_time_unit(MonTime + Offset, native, microsecond),
    Mega = Micro div 1000000000000,
    {Mega, Micro div 1000000 - Mega * 1000000, Micro rem 1000000}.

%% OTP's '$hash2' record of the table parameters, which bchunk/2 and init_table/3 exchange.
parameters(T) ->
    case clause_dets:info(T#tab.handle) of
        {_, _, _, _, _, _, _, _, _, _, _, undefined} ->
            undefined;
        {Type, Keypos, Objects, Keys, _, Min, Next, Max, Hash, M, N, Collections} ->
            Method =
                case Hash of
                    phash2 -> 1;
                    phash -> 0
                end,
            {'$hash2', 9, 1, filename:basename(T#tab.file), Type, Keypos, Method, N, M, Next, Min,
                Max, Objects, Keys, Collections}
    end.

compatible_bchunk(T, Term) ->
    case parameters(T) of
        undefined ->
            false;
        {'$hash2', _, _, _, Type, Keypos, Method, _, _, _, _, _, _, _, _} ->
            case catch binary_to_term(Term) of
                {'$hash2', _, 1, _, Type, Keypos, Method, _, _, _, _, _, _, _, _} -> true;
                _ -> false
            end
    end.

where_is(T, Object) ->
    case valid_objects([Object], T#tab.keypos) of
        true -> flushed(T, fun(T1) -> {T1, clause_dets:where(T1#tab.handle, Object)} end);
        false -> {T, badarg}
    end.

first_key(T) ->
    flushed(T, fun(T1) ->
        case clause_dets:first(T1#tab.handle) of
            {ok, _} = Reply -> {T1, Reply};
            {error, _} = Error -> corrupt(T1, Error)
        end
    end).

next_key(T, Key) ->
    flushed(T, fun(T1) ->
        case clause_dets:next(T1#tab.handle, Key) of
            {ok, _} = Reply -> {T1, Reply};
            {error, _} = Error -> corrupt(T1, Error)
        end
    end).

slot_objects(T, Slot) ->
    flushed(T, fun(T1) ->
        case clause_dets:slot(T1#tab.handle, Slot) of
            {error, _} = Error -> corrupt(T1, Error);
            Objects -> {T1, Objects}
        end
    end).

delete_all(#tab{fixed = false} = T) ->
    case clause_dets:delete_all(T#tab.handle) of
        ok ->
            T1 = T#tab{
                cache = new_cache({(T#tab.cache)#cache.delay, (T#tab.cache)#cache.tsize}),
                update_mode = dirty
            },
            start_auto_save_timer(T1),
            {T1, ok};
        Error ->
            corrupt(T, Error)
    end;
delete_all(T) ->
    {T, fixed}.

insert_new_objects(T, Objects) ->
    Keypos = T#tab.keypos,
    case catch [element(Keypos, Object) || Object <- Objects] of
        Keys when is_list(Keys) ->
            case update_cache(T, [{{lookup, nopid}, Keys}]) of
                {T1, Found} when is_list(Found) ->
                    case lists:all(fun({_, Present}) -> Present =:= [] end, Found) of
                        true ->
                            case update_cache(T1, [{insert, Objects}]) of
                                {T2, Inserted} when is_list(Inserted) -> {T2, true};
                                Failed -> Failed
                            end;
                        false ->
                            {T1, false}
                    end;
                Failed ->
                    Failed
            end;
        _ ->
            {T, badarg}
    end.

update_counter_of(#tab{type = set} = T, Key, Increment) ->
    case lookup_objects(T, [Key]) of
        {T1, [Object]} ->
            case catch counted(Object, T1#tab.keypos, Increment) of
                {'EXIT', _} ->
                    {T1, badarg};
                {Value, Updated} ->
                    case update_cache(T1, [{insert, [Updated]}]) of
                        {T2, Found} when is_list(Found) -> {T2, Value};
                        Failed -> Failed
                    end
            end;
        {T1, []} ->
            {T1, badarg};
        Failed ->
            Failed
    end;
update_counter_of(T, _Key, _Increment) ->
    {T, badarg}.

counted(Object, _Keypos, {Position, Increment}) ->
    counted_at(Object, Position, Increment);
counted(Object, Keypos, Increment) ->
    counted_at(Object, Keypos + 1, Increment).

counted_at(Object, Position, Increment) ->
    Value = element(Position, Object) + Increment,
    {Value, setelement(Position, Object, Value)}.

%%% --- Matches and selects in the table process ---

%% Start a match: the keys every clause's head names are looked up, any other pattern scans the file.
match_start(T, Program, Spec, N, Safe, From) ->
    case spec_keys(Spec, T#tab.keypos, []) of
        [] ->
            flushed(T, fun(T1) ->
                T2 =
                    case Safe of
                        safe -> fixtable(T1, pid_of(From), true);
                        no_safe -> T1
                    end,
                {T2, {cont, (scan_start(T2, N))#dets_cont{match_program = Program}}}
            end);
        Keys ->
            case lookup_objects(T, lists:usort(Keys)) of
                {T1, Objects} when is_list(Objects) ->
                    {match_spec, Compiled} = Program,
                    {T1, {done, ets:match_spec_run(Objects, Compiled)}};
                Failed ->
                    Failed
            end
    end.

scan_start(T, N) ->
    {Bin, Alloc} = clause_dets:scan_init(T#tab.handle),
    #dets_cont{no_objs = N, bin = Bin, alloc = Alloc}.

match_next(T, #dets_cont{bin = eof}) ->
    {T, '$end_of_table'};
match_next(T, #dets_cont{} = State) ->
    case scanned(T, State, select) of
        {ok, Found, _, NewState} -> {T, {cont, {Found, NewState}}};
        Error -> corrupt(T, Error)
    end.

%% One chunk of the scan: {ok, Results, ObjectsScanned, Continuation}, or an error.
scanned(
    T, #dets_cont{bin = Bin, alloc = Alloc, no_objs = N, match_program = Program} = State, Mode
) ->
    case clause_dets:scan(T#tab.handle, Bin, Alloc, N, Program, Mode) of
        {Results, Scanned, Bin1, Alloc1} ->
            {ok, Results, Scanned, State#dets_cont{bin = Bin1, alloc = Alloc1}};
        Error ->
            Error
    end.

%% The keys of a match specification whose every head is a tuple with a constant key; [] otherwise.
spec_keys([], _Keypos, Keys) ->
    Keys;
spec_keys([{Head, _, _} | Spec], Keypos, Keys) when is_tuple(Head) ->
    case tuple_size(Head) >= Keypos of
        true ->
            Key = element(Keypos, Head),
            case has_variable(Key) of
                true -> [];
                false -> spec_keys(Spec, Keypos, [Key | Keys])
            end;
        false ->
            spec_keys(Spec, Keypos, Keys)
    end;
spec_keys(_, _, _) ->
    [].

has_variable('_') ->
    true;
has_variable(Atom) when is_atom(Atom) ->
    case atom_to_list(Atom) of
        [$$ | Digits] ->
            case catch list_to_integer(Digits) of
                {'EXIT', _} -> false;
                _ -> true
            end;
        _ ->
            false
    end;
has_variable(Tuple) when is_tuple(Tuple) ->
    has_variable(tuple_to_list(Tuple));
has_variable([Head | Tail]) ->
    has_variable(Head) orelse has_variable(Tail);
has_variable(_) ->
    false.

match_delete_start(T, Program, Spec, From) ->
    case spec_keys(Spec, T#tab.keypos, []) of
        [] -> delete_matching(T, Program, Spec, From);
        Keys -> delete_keys_matching(T, lists:usort(Keys), Program)
    end.

delete_matching(#tab{fixed = false} = T, _Program, ?TRUE_SPEC('_'), _From) ->
    flushed(T, fun(T1) ->
        Objects = element(3, clause_dets:info(T1#tab.handle)),
        case delete_all(T1) of
            {T2, ok} -> {T2, {done, Objects}};
            Other -> Other
        end
    end);
delete_matching(T, Program, _Spec, From) ->
    T1 = fixtable(T, pid_of(From), true),
    flushed(T1, fun(T2) ->
        {T2, {cont, (scan_start(T2, default))#dets_cont{match_program = Program}, 0}}
    end).

delete_keys_matching(T, Keys, {match_spec, Compiled}) ->
    case lookup_objects(T, Keys) of
        {T1, Objects} when is_list(Objects) ->
            Matching = [
                Object
             || Object <- Objects, ets:match_spec_run([Object], Compiled) =:= [true]
            ],
            delete_found(T1, lists:reverse(Matching), fixed);
        Failed ->
            Failed
    end.

match_delete_next(T, State) ->
    case scanned(T, State, matching) of
        {ok, _, 0, _} -> {T, {done, 0}};
        {ok, Matching, _, NewState} -> delete_found(T, Matching, NewState);
        Error -> corrupt(T, Error)
    end.

delete_found(T, Objects, What) ->
    N = length(Objects),
    case update_cache(T, [{delete_object, Objects}]) of
        {T1, Found} when is_list(Found), What =:= fixed -> {T1, {done, N}};
        {T1, Found} when is_list(Found) -> {T1, {cont, What, N}};
        Failed -> Failed
    end.

%%% --- bchunk/2 ---

bchunk_start(T, Tab) ->
    flushed(T, fun(T1) ->
        case parameters(T1) of
            undefined ->
                {T1, {error, old_version}};
            Parameters ->
                {Bin, Alloc} = clause_dets:bchunk_init(T1#tab.handle),
                State = #dets_cont{
                    no_objs = default,
                    bin = Bin,
                    alloc = Alloc,
                    tab = Tab,
                    proc = self(),
                    what = bchunk
                },
                {T1, {State, [term_to_binary(Parameters)]}}
        end
    end).

bchunk_next(T, #dets_cont{proc = Proc}, _From) when Proc =/= self() ->
    {T, badarg};
bchunk_next(T, #dets_cont{bin = eof}, _From) ->
    {T, '$end_of_table'};
bchunk_next(T, #dets_cont{alloc = Alloc} = State, _From) ->
    case clause_dets:bchunk(T#tab.handle, Alloc) of
        {eof, _, Data} -> {T, {State#dets_cont{bin = eof}, Data}};
        {_, Alloc1, Data} -> {T, {State#dets_cont{alloc = Alloc1}, Data}};
        Error -> corrupt(T, Error)
    end.

%%% --- init_table/2,3 ---

initialize(#tab{access = read} = T, InitFun, _Format, _MinSlots) ->
    _ = (catch InitFun(close)),
    {T, {error, {access_mode, T#tab.file}}};
initialize(#tab{fixed = false} = T, InitFun, term, MinSlots) ->
    {_, _, _, _, _, Min, _, Max, _, _, _, _} = clause_dets:info(T#tab.handle),
    if
        is_integer(MinSlots), MinSlots > Max ->
            {T, badarg};
        true ->
            Chosen =
                case MinSlots of
                    default -> Min;
                    _ -> MinSlots
                end,
            Keep = T#tab.update_mode =:= new_dirty andalso Chosen =:= Min,
            case clause_dets:bulk_start(T#tab.handle, Chosen, Keep) of
                ok -> bulk_load(T, InitFun, make_ref());
                Error -> corrupt(T, Error)
            end
    end;
initialize(#tab{fixed = false} = T, InitFun, bchunk, _MinSlots) ->
    ok = clause_dets:truncate(T#tab.handle),
    bchunk_load(T, InitFun, make_ref());
initialize(T, InitFun, _Format, _MinSlots) ->
    _ = (catch InitFun(close)),
    {T, {error, {fixed_table, T#tab.name}}}.

%% Read every object from the init fun into the file, then write them out at once. As file_sorter does, the fun is
%% closed only when its objects are invalid.
bulk_load(T, InitFun, Ref) ->
    case catch {Ref, InitFun(read)} of
        {Ref, end_of_input} ->
            case clause_dets:bulk_end(T#tab.handle) of
                ok -> loaded(T);
                Error -> bulk_failed(T, Error)
            end;
        {Ref, {Objects, Next}} when is_list(Objects), is_function(Next) ->
            case clause_dets:bulk_add(T#tab.handle, Objects) of
                ok ->
                    bulk_load(T, Next, Ref);
                Error ->
                    _ = (catch Next(close)),
                    bulk_failed(T, Error)
            end;
        {Ref, Value} ->
            bulk_failed(T, {error, {init_fun, Value}});
        Thrown ->
            thrown(T, Thrown)
    end.

loaded(T) ->
    T1 = T#tab{update_mode = dirty, cache = new_cache(cache_size(T))},
    check_growth(T1),
    start_auto_save_timer(T1),
    {T1, ok}.

bulk_failed(T, Error) ->
    ok = clause_dets:bulk_abort(T#tab.handle),
    corrupt(T, Error).

%% A throw (or crash) of the init fun is thrown again by init_table/3 in the caller.
thrown(T, Thrown) ->
    {T1, _} = bulk_failed(T, {thrown, Thrown}),
    {T1, {thrown, Thrown}}.

%% init_table/3 of bchunk/2's data: the table parameters first, then the collections of a table with the same
%% slots, written at once (dets_v9:bchunk_init/2).
bchunk_load(T, InitFun, Ref) ->
    case catch {Ref, InitFun(read)} of
        {Ref, end_of_input} ->
            bulk_failed(T, {error, {init_fun, end_of_input}});
        {Ref, {[], Next}} when is_function(Next) ->
            bchunk_load(T, Next, Ref);
        {Ref, {[Parameters | Data], Next}} when is_list(Data), is_function(Next) ->
            case bchunk_parameters(T, Parameters) of
                {ok, Record} -> bchunk_started(T, Record, Data, Next, Ref);
                not_ok -> bulk_failed(T, {error, {init_fun, Parameters}})
            end;
        {Ref, Value} ->
            bulk_failed(T, {error, {init_fun, Value}});
        Thrown ->
            thrown(T, Thrown)
    end.

bchunk_started(T, Record, Data, Next, Ref) ->
    case clause_dets:bchunk_start(T#tab.handle, Record) of
        ok -> bchunk_data(T, Data, Next, Ref, element(15, Record));
        badarg -> {T, badarg}
    end.

bchunk_data(T, Data, Fun, Ref, Collections) ->
    case clause_dets:bchunk_add(T#tab.handle, Data) of
        ok ->
            case catch {Ref, Fun(read)} of
                {Ref, end_of_input} ->
                    case clause_dets:bchunk_end(T#tab.handle, Collections) of
                        ok -> loaded(T);
                        Error -> bulk_failed(T, Error)
                    end;
                {Ref, {More, Next}} when is_list(More), is_function(Next) ->
                    bchunk_data(T, More, Next, Ref, Collections);
                {Ref, Value} ->
                    bulk_failed(T, {error, {init_fun, Value}});
                Thrown ->
                    thrown(T, Thrown)
            end;
        Error ->
            _ = (catch Fun(close)),
            bulk_failed(T, Error)
    end.

%% The '$hash2' record of bchunk data made by a table of the same type, key position and hash method; not_ok for
%% anything else.
bchunk_parameters(T, Binary) ->
    {Type, Keypos, _, _, _, _, _, _, Hash, _, _, _} = clause_dets:info(T#tab.handle),
    Method =
        case Hash of
            phash2 -> 1;
            phash -> 0
        end,
    case catch binary_to_term(Binary) of
        {'$hash2', _, 1, _, Type, Keypos, Method, _, _, _, _, _, _, _, _} = Record -> {ok, Record};
        _ -> not_ok
    end.

cache_size(T) ->
    {(T#tab.cache)#cache.delay, (T#tab.cache)#cache.tsize}.

%%% --- dets_utils:cmp/2 and msort/1 ---

cmp(T, T) ->
    0;
cmp([E1 | T1], [E2 | T2]) ->
    case cmp(E1, E2) of
        0 -> cmp(T1, T2);
        R -> R
    end;
cmp(T1, T2) when tuple_size(T1) =:= tuple_size(T2) ->
    cmp_elements(T1, T2, 1, tuple_size(T1));
cmp(I, F) when is_integer(I), is_float(F) ->
    -1;
cmp(F, I) when is_float(F), is_integer(I) ->
    1;
cmp(T1, T2) when T1 < T2 ->
    -1;
cmp(_, _) ->
    1.

cmp_elements(T1, T2, I, I) ->
    cmp(element(I, T1), element(I, T2));
cmp_elements(T1, T2, I, N) ->
    case cmp(element(I, T1), element(I, T2)) of
        0 -> cmp_elements(T1, T2, I + 1, N);
        R -> R
    end.

msort(List) ->
    lists:sort(fun(X, Y) -> cmp(X, Y) =< 0 end, lists:sort(List)).
