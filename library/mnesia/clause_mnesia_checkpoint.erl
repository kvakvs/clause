%% mnesia's checkpoints (plan 21 step MIM1F, docs/libraries/mnesia.md#checkpoints): a checkpoint keeps its tables as
%% they were at activation. Each checkpoint is a process under mnesia_checkpoint_sup that owns one retainer per table,
%% an ets table of {Tab, Key, OldRecords} for each key changed since activation; updates retain the records they
%% replace before they change a table. The bookkeeping in mnesia_gvar is OTP's: checkpoints, {checkpoint, Name},
%% {Tab, checkpoints}, {Tab, {retainer, Name}} and a {checkpoints, Names} entry first in the table's commit_work.
-module(clause_mnesia_checkpoint).
-behaviour(gen_server).
-export([activate/1, deactivate/1, tables_and_cookie/1, retainer/2, retain/3, table_gone/1, start_link/1]).
-export([init/1, handle_call/3, handle_cast/2]).

-include("clause_mnesia.hrl").

%% A checkpoint's arguments and state, as OTP's: its name, the tables kept with most and least redundancy, those whose
%% RAM records override their dump, the retainers and the nodes holding them, and the backup cookie.
-record(checkpoint_args, {
    name = {erlang:unique_integer([positive]), node()},
    allow_remote = true,
    ram_overrides_dump = false,
    nodes = [],
    min = [],
    max = [],
    retainers = [],
    cookie
}).

%% A table's retainer, as OTP's gvar row {Tab, {retainer, Name}} holds it: the store of retained records, the nodes
%% that write it, and whether it retains updates (false for a RAM table read from its dump).
-record(retainer, {cp_name, tab_name, store, writers = [], really_retain = true}).

%% Activation and deactivation, in the caller.

% {ok, Name, Nodes} or {error, Reason}, with OTP's checks.
activate(Args) when is_list(Args) ->
    try lists:foldl(fun check_arg/2, #checkpoint_args{}, Args) of
        Cp ->
            case check_tables(Cp) of
                {ok, Overriders, Tabs} -> arrange(Cp#checkpoint_args{ram_overrides_dump = Overriders}, Tabs);
                Error -> Error
            end
    catch
        exit:Reason -> {error, Reason};
        error:Reason -> {error, Reason}
    end;
activate(Args) ->
    {error, {badarg, Args}}.

check_arg({name, Name}, Cp) ->
    lists:member(Name, checkpoints()) andalso exit({already_exists, Name}),
    Cp#checkpoint_args{name = Name};
check_arg({allow_remote, Bool}, Cp) when is_boolean(Bool) ->
    Cp#checkpoint_args{allow_remote = Bool};
check_arg({ram_overrides_dump, Overrides}, Cp) when is_boolean(Overrides); is_list(Overrides) ->
    Cp#checkpoint_args{ram_overrides_dump = Overrides};
check_arg({min, Tabs}, Cp) when is_list(Tabs) ->
    Cp#checkpoint_args{min = Tabs};
check_arg({max, Tabs}, Cp) when is_list(Tabs) ->
    Cp#checkpoint_args{max = Tabs};
check_arg({ignore_new, Tids}, Cp) when is_list(Tids) ->
    Cp;
check_arg(Arg, _) ->
    exit({badarg, Arg}).

% The tables, none of them both min and max; ram_overrides_dump tables must be in both lists, which no table can be.
check_tables(#checkpoint_args{name = Name, min = Min, max = Max, ram_overrides_dump = Overriders}) ->
    case {[T || T <- Min, lists:member(T, Max)], Overriders} of
        {[_ | _] = Both, _} -> {error, {combine_error, Name, [{min, Both}, {max, Both}]}};
        _ when Min =:= [], Max =:= [] -> {error, {combine_error, Name, [{min, Min}, {max, Max}]}};
        {[], false} -> {ok, [], Min ++ Max};
        {[], true} -> {ok, Min ++ Max, Min ++ Max};
        {[], _} -> overriders(Name, Overriders, Min, Max)
    end.

overriders(Name, Overriders, Min, Max) ->
    case {[T || T <- Overriders, not lists:member(T, Min)], [T || T <- Overriders, not lists:member(T, Max)]} of
        {[], []} -> {ok, Overriders, Min ++ Max};
        {[], Outside} -> {error, {combine_error, Name, [{ram_overrides_dump, Outside}, {max, Outside}]}};
        {Outside, _} -> {error, {combine_error, Name, [{ram_overrides_dump, Outside}, {min, Outside}]}}
    end.

% A retainer for each table where a replica is active; the checkpoint's process holds them.
arrange(#checkpoint_args{name = Name} = Cp, Tabs) ->
    try [#retainer{cp_name = Name, tab_name = T, writers = writers(Cp, T)} || T <- Tabs] of
        Retainers ->
            Nodes = lists:usort(lists:append([W || #retainer{writers = W} <- Retainers])),
            start(Cp#checkpoint_args{retainers = Retainers, nodes = Nodes, cookie = cookie()})
    catch
        throw:Reason -> {error, Reason}
    end.

writers(#checkpoint_args{name = Name, allow_remote = Remote}, Tab) ->
    Active = clause_mnesia_monitor:val({Tab, active_replicas}),
    case {Remote, lists:member(node(), Active)} of
        {_, true} -> [node()];
        {true, false} when Active =/= [] -> [hd(Active)];
        _ -> throw({"Cannot prepare checkpoint (replica not available)", [Tab, Name]})
    end.

cookie() ->
    {{erlang:monotonic_time() + erlang:time_offset(), erlang:unique_integer(), 1}, node()}.

start(#checkpoint_args{name = Name, nodes = Nodes} = Cp) ->
    try supervisor:start_child(mnesia_checkpoint_sup, [Cp]) of
        {ok, _} -> {ok, Name, Nodes};
        {error, Reason} -> {error, {"Cannot create checkpoint retainer", Name, node(), Reason}}
    catch
        exit:_ -> {error, {node_not_running, node()}}
    end.

deactivate(Name) ->
    call(Name, deactivate).

% The checkpoint's tables and cookie, for its backup.
tables_and_cookie(Name) ->
    case call(Name, get_checkpoint) of
        #checkpoint_args{min = Min, max = Max, cookie = Cookie} -> {ok, Min ++ Max, Cookie};
        Error -> Error
    end.

% A table's retainer in a checkpoint: {ok, Store, ReallyRetain}, or an error when the checkpoint does not hold it.
retainer(Name, Tab) ->
    case ets:lookup(?GVAR, {Tab, {retainer, Name}}) of
        [{_, #retainer{store = {ets, Store}, really_retain = Really}}] -> {ok, Store, Really};
        _ -> {error, {"No retainer attached to table", [Tab, Name]}}
    end.

call(Name, Request) ->
    case ets:lookup(?GVAR, {checkpoint, Name}) of
        [{_, Pid}] ->
            try
                gen_server:call(Pid, Request, infinity)
            catch
                exit:{Reason, _} -> {error, {"Got exit", [Name, Reason]}}
            end;
        [] ->
            {error, {no_exists, Name}}
    end.

checkpoints() ->
    try
        ets:lookup_element(?GVAR, checkpoints, 2)
    catch
        error:_ -> []
    end.

%% Retaining, where an update happens.

% The records of Key that an update replaces, kept by each checkpoint of the table that has not kept the key yet; a
% cleared table keeps each of its keys.
retain(Tab, Key, Update) ->
    case ets:lookup(?GVAR, {Tab, commit_work}) of
        [{_, [{checkpoints, Names} | _]}] -> retain_keys(Tab, Key, Update, Names);
        _ -> ok
    end.

retain_keys(Tab, _, {clear_table, _}, Names) ->
    Records = stored(Tab, '_'),
    [
        retain_key(Tab, K, [R || R <- Records, element(2, R) =:= K], Names)
     || K <- lists:usort([element(2, R) || R <- Records])
    ],
    ok;
retain_keys(Tab, Key, _, Names) ->
    retain_key(Tab, Key, stored(Tab, Key), Names).

retain_key(Tab, Key, Old, Names) ->
    [ets:insert_new(Store, {Tab, Key, Old}) || Name <- Names, {ok, Store, true} <- [retainer(Name, Tab)]],
    ok.

stored(schema, '_') -> ets:tab2list(schema);
stored(schema, Key) -> ets:lookup(schema, Key);
stored(Tab, '_') -> clause_mnesia_db:match_object(Tab, '_');
stored(Tab, Key) -> clause_mnesia_db:lookup(Tab, Key).

% This node's replica of a table goes: each of its checkpoints loses its last retainer and ends, reported as OTP's.
table_gone(Tab) ->
    Names =
        try
            ets:lookup_element(?GVAR, {Tab, checkpoints}, 2)
        catch
            error:_ -> []
        end,
    [
        begin
            clause_mnesia_subscr:report_system_event({mnesia_checkpoint_deactivated, Name}),
            deactivate(Name)
        end
     || Name <- Names
    ],
    ok.

%% The checkpoint's process.

start_link(Cp) ->
    gen_server:start_link(?MODULE, Cp, []).

init(#checkpoint_args{name = Name, retainers = Retainers} = Cp) ->
    Prepared = [prepare(Cp, R) || R <- Retainers],
    ets:insert(?GVAR, {{checkpoint, Name}, self()}),
    add(checkpoints, Name),
    {ok, Cp#checkpoint_args{retainers = Prepared}}.

% A retainer's store, its gvar rows and the checkpoints entry of its table's commit work. A RAM table whose RAM records
% do not override its dump keeps its dumped records and retains nothing.
prepare(#checkpoint_args{ram_overrides_dump = Overriders}, #retainer{cp_name = Name, tab_name = Tab} = R) ->
    Store = ets:new(mnesia_retainer, [set, public, {keypos, 2}]),
    Really =
        case clause_mnesia_monitor:val({Tab, storage_type}) =:= ram_copies andalso not lists:member(Tab, Overriders) of
            true -> dumped(Tab, Store);
            false -> true
        end,
    R2 = R#retainer{store = {ets, Store}, really_retain = Really},
    ets:insert(?GVAR, {{Tab, {retainer, Name}}, R2}),
    add({Tab, checkpoints}, Name),
    add_to_work(Tab, Name),
    R2.

dumped(Tab, Store) ->
    [
        ets:insert(Store, {Tab, element(2, Rec), [Rec | old(Store, element(2, Rec))]})
     || Rec <- clause_mnesia_disc:dumped_records(Tab)
    ],
    false.

old(Store, Key) ->
    case ets:lookup(Store, Key) of
        [] -> [];
        [{_, _, Old}] -> Old
    end.

add_to_work(Tab, Name) ->
    case clause_mnesia_monitor:val({Tab, commit_work}) of
        [{checkpoints, Names} | Rest] ->
            set({Tab, commit_work}, [{checkpoints, [Name | lists:delete(Name, Names)]} | Rest]);
        Work ->
            set({Tab, commit_work}, [{checkpoints, [Name]} | Work])
    end.

handle_call(get_checkpoint, _From, Cp) ->
    {reply, Cp, Cp};
handle_call(deactivate, _From, #checkpoint_args{name = Name, retainers = Retainers} = Cp) ->
    ets:delete(?GVAR, {checkpoint, Name}),
    del(checkpoints, Name),
    [remove(R) || R <- Retainers],
    {stop, normal, ok, Cp}.

handle_cast(_, Cp) ->
    {noreply, Cp}.

% A retainer goes with its rows; a table that is gone has none left.
remove(#retainer{cp_name = Name, tab_name = Tab, store = {ets, Store}}) ->
    ets:delete(Store),
    try
        ets:delete(?GVAR, {Tab, {retainer, Name}}),
        del({Tab, checkpoints}, Name),
        case clause_mnesia_monitor:val({Tab, commit_work}) of
            [{checkpoints, [Name]} | Rest] -> set({Tab, commit_work}, Rest);
            [{checkpoints, Names} | Rest] -> set({Tab, commit_work}, [{checkpoints, lists:delete(Name, Names)} | Rest]);
            _ -> ok
        end
    catch
        _:_ -> ok
    end.

add(Var, Value) ->
    set(Var, [Value | lists:delete(Value, clause_mnesia_monitor:val(Var))]).

del(Var, Value) ->
    set(Var, lists:delete(Value, clause_mnesia_monitor:val(Var))).

set(Name, Value) -> ets:insert(?GVAR, {Name, Value}).
