%% mnesia's monitor, registered as mnesia_monitor as in OTP (plan 21 step MIM1A, docs/libraries/mnesia.md#processes):
%% it owns mnesia_gvar, the schema and every table, so they live as long as mnesia runs, reads the configuration
%% parameters, and applies schema changes one at a time. Each change runs in a schema transaction (MIM1B), which takes
%% OTP's locks and makes OTP's checks before the monitor applies it (MIM1C). On a disc schema it also loads the
%% directory at start and dumps the transaction log (MIM1D). Other nodes' monitors negotiate a protocol with it and
%% stay linked to it, so that each sees the other's mnesia go down (MIM1H4, clause_mnesia_connect).
-module(clause_mnesia_monitor).
-behaviour(gen_server).
-export([start_link/0, schema_transaction/1, important/2, val/1, is_running/0, get_env/1, check_env/2, increment/1]).
-export([negotiate_protocol/1]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2, terminate/2]).

-include("clause_mnesia.hrl").

start_link() ->
    gen_server:start_link({local, mnesia_monitor}, ?MODULE, [], [{timeout, infinity}]).

% A schema change (clause_mnesia_change:prepare/1's operations) as a schema transaction, run by a process of
% its own as OTP's schema coordinator does: {atomic, ok} or {aborted, Reason}.
schema_transaction(Operation) ->
    case is_running() of
        yes ->
            {Pid, Ref} = spawn_monitor(fun() ->
                exit({done, mnesia:transaction(fun() -> schema_change(Operation) end)})
            end),
            receive
                {'DOWN', Ref, process, Pid, {done, Result}} -> Result;
                {'DOWN', Ref, process, Pid, Reason} -> {aborted, {transaction_crashed, Reason}}
            end;
        _ ->
            {aborted, {node_not_running, node()}}
    end.

% The change's locks and checks in the schema transaction, then the monitor applies it with OTP's operations, which
% it logs (MIM1H2).
schema_change(Operation) ->
    case clause_mnesia_change:prepare(Operation) of
        {not_implemented, _} = NotImplemented ->
            NotImplemented;
        {value, Value, Changes} ->
            applied(Changes, clause_mnesia_ops:ops(Operation, Changes)),
            Value;
        Changes ->
            applied(Changes, clause_mnesia_ops:ops(Operation, Changes))
    end.

% A change's result is ok, or the value its preparation gave, as restore/2's tables (MIM1F). The other nodes running
% mnesia commit the operations too (MIM1H8).
applied(Changes, Ops) ->
    {_, Tid, #tidstore{store = Store}} = get(mnesia_activity_state),
    Local = fun() ->
        case gen_server:call(mnesia_monitor, {schema, Changes, Ops, Tid}, infinity) of
            ok -> ok;
            {aborted, Reason} -> mnesia:abort(Reason)
        end
    end,
    _ = clause_mnesia_commit:schema(Tid, [node() | lists:delete(node(), val({schema, active_replicas}))], Ops, Local),
    % A schema change counts as a write: its transaction completes an activity (MIM1E).
    true = ets:insert(Store, {op, schema_change}),
    ok.

% The nodes of Nodes whose monitors agreed on a protocol with this one, linked to it (MIM1H4).
negotiate_protocol([]) ->
    [];
negotiate_protocol(Nodes) ->
    gen_server:call(mnesia_monitor, {negotiate_protocol, Nodes}, infinity).

% A report to mnesia_event's handler, which prints it, and to the system subscribers, as OTP's mnesia_lib:important/2
% does (MIM1C).
important(Format, Args) ->
    clause_mnesia_subscr:report_system_event({mnesia_info, Format, Args}).

% A global variable; a missing table's where_to_read is nowhere, its writers and replicas none, and any other
% missing variable aborts with node_not_running or no_exists.
val(Name) ->
    try
        ets:lookup_element(?GVAR, Name, 2)
    catch
        error:_ -> missing(Name)
    end.

missing({_, where_to_read}) ->
    nowhere;
missing({_, where_to_write}) ->
    [];
missing({_, active_replicas}) ->
    [];
missing(Name) ->
    case is_running() of
        no -> mnesia:abort({node_not_running, node()});
        _ -> mnesia:abort({no_exists, Name})
    end.

% yes, no, starting or stopping.
is_running() ->
    try ets:lookup_element(?GVAR, mnesia_status, 2) of
        running -> yes;
        Status -> Status
    catch
        error:_ -> no
    end.

increment(Counter) ->
    ets:update_counter(?GVAR, Counter, 1).

% A configuration parameter: the running value, the application environment's or the default.
get_env(Name) ->
    try
        ets:lookup_element(?GVAR, Name, 2)
    catch
        error:_ ->
            case application:get_env(mnesia, Name) of
                {ok, Value} -> checked(Name, Value);
                undefined -> checked(Name, default(Name))
            end
    end.

checked(Name, Value) ->
    try
        check_env(Name, Value)
    catch
        error:_ -> exit({bad_config, Name, Value})
    end.

% The configuration parameters mnesia reads at start.
parameters() ->
    [
        access_module,
        allow_index_on_key,
        auto_repair,
        backup_module,
        debug,
        dir,
        dump_disc_copies_at_startup,
        dump_log_load_regulation,
        dump_log_time_threshold,
        dump_log_update_in_place,
        dump_log_write_threshold,
        event_module,
        extra_db_nodes,
        ignore_fallback_at_startup,
        fallback_error_function,
        fold_chunk_size,
        max_wait_for_decision,
        schema_location,
        core_dir,
        pid_sort_order,
        no_table_loaders,
        dc_dump_limit,
        send_compressed,
        max_transfer_size,
        schema
    ].

% The documented defaults of the configuration parameters.
default(access_module) -> mnesia;
default(auto_repair) -> true;
default(allow_index_on_key) -> false;
default(backup_module) -> mnesia_backup;
default(debug) -> none;
default(dir) -> filename:absname(lists:concat(["Mnesia.", node()]));
default(dump_disc_copies_at_startup) -> true;
default(dump_log_load_regulation) -> false;
default(dump_log_time_threshold) -> timer:minutes(3);
default(dump_log_update_in_place) -> true;
default(dump_log_write_threshold) -> 1000;
default(event_module) -> mnesia_event;
default(extra_db_nodes) -> [];
default(ignore_fallback_at_startup) -> false;
default(fallback_error_function) -> {mnesia, lkill};
default(fold_chunk_size) -> 100;
default(max_wait_for_decision) -> infinity;
default(schema_location) -> opt_disc;
default(core_dir) -> false;
default(pid_sort_order) -> false;
default(no_table_loaders) -> 2;
default(dc_dump_limit) -> 4;
default(send_compressed) -> 0;
default(max_transfer_size) -> 64000;
default(schema) -> [].

% A configuration value as mnesia keeps it; an error for a value of the wrong type.
check_env(Name, Value) when Name =:= access_module; Name =:= backup_module; Name =:= event_module ->
    true = is_atom(Value),
    Value;
check_env(debug, Level) ->
    case Level of
        false -> none;
        true -> debug;
        _ when Level =:= none; Level =:= verbose; Level =:= debug; Level =:= trace -> Level
    end;
check_env(dir, Dir) ->
    filename:absname(Dir);
check_env(fallback_error_function, {M, F}) when is_atom(M), is_atom(F) ->
    {M, F};
check_env(extra_db_nodes, Nodes) when is_list(Nodes) ->
    [N || N <- Nodes, N =/= node(), is_atom(N) orelse error(badarg)];
check_env(fold_chunk_size, infinity) ->
    infinity;
check_env(max_wait_for_decision, infinity) ->
    infinity;
check_env(schema_location, Media) when Media =:= disc; Media =:= opt_disc; Media =:= ram ->
    Media;
check_env(core_dir, Dir) when Dir =:= false; Dir =:= "false" ->
    false;
check_env(core_dir, Dir) when is_list(Dir) ->
    Dir;
check_env(pid_sort_order, Order) ->
    case Order of
        _ when Order =:= r9b_plain; Order =:= "r9b_plain" -> r9b_plain;
        _ when Order =:= standard; Order =:= "standard" -> standard;
        _ -> false
    end;
check_env(dc_dump_limit, N) when is_number(N), N > 0 ->
    N;
check_env(send_compressed, L) when is_integer(L), L >= 0, L =< 9 ->
    L;
check_env(schema, L) when is_list(L) ->
    L;
check_env(Name, B) when is_boolean(B) ->
    true = lists:member(Name, booleans()),
    B;
check_env(Name, I) when is_integer(I), I > 0 ->
    true = lists:member(Name, positives()),
    I.

booleans() ->
    [
        allow_index_on_key,
        auto_repair,
        dump_disc_copies_at_startup,
        dump_log_load_regulation,
        dump_log_update_in_place,
        ignore_fallback_at_startup
    ].

positives() ->
    [
        dump_log_time_threshold,
        dump_log_write_threshold,
        fold_chunk_size,
        max_wait_for_decision,
        no_table_loaders,
        max_transfer_size
    ].

% The global variables, the configuration and the schema: an empty RAM schema, or the directory's (MIM1D); two schema
% transactions counted, as OTP's start commits them; they take two transaction serials of mnesia_decision, the first
% before a disc schema's log is dumped (MIM1H1).
init([]) ->
    process_flag(trap_exit, true),
    ets:new(?GVAR, [set, public, named_table]),
    ets:new(mnesia_decision, [set, public, named_table, {keypos, 2}]),
    ets:insert(mnesia_decision, {trans_tid, serial, 0}),
    set(mnesia_status, starting),
    [set(Name, get_env(Name)) || Name <- parameters()],
    {ok, Version} = application:get_key(mnesia, vsn),
    UseDir = clause_mnesia_disc:use_dir(),
    [set(Name, Value) || {Name, Value} <- [{version, Version}, {use_dir, UseDir}, {{current, db_nodes}, [node()]}]],
    Counters = [{trans_commits, 2}, {trans_failures, 0}, {trans_restarts, 0}, {trans_log_writes, 0}],
    [set(Counter, N) || {Counter, N} <- Counters],
    [set(List, []) || List <- [subscribers, activity_subscribers, checkpoints]],
    ets:update_counter(mnesia_decision, serial, {3, 1}),
    case UseDir of
        true -> clause_mnesia_disc:start();
        false -> clause_mnesia_schema:init_schema()
    end,
    ets:update_counter(mnesia_decision, serial, {3, 1}),
    set(mnesia_status, running),
    % Nodes coming up are checked for a partitioned network (MIM1H4).
    ok = net_kernel:monitor_nodes(true),
    {ok, dump_timer()}.

% The next dump of the log after dump_log_time_threshold (MIM1D).
dump_timer() ->
    erlang:send_after(val(dump_log_time_threshold), self(), {dump_log, time_threshold}).

% A schema transaction's changes, applied in order: ok or {aborted, Reason}. The changed tables' records reach
% schema.DAT, the transaction's operations and the records it wrote reach the log as OTP's commit, and it counts two
% log writes, as OTP's (MIM1D, MIM1H2). Around the operations OTP dumps the log after, a table rewritten or deleted,
% the log is dumped before and after, so that no logged write replays over the table.
handle_call({schema, Changes, Ops, Tid}, _From, State) ->
    Result =
        try
            Dumped = clause_mnesia_ops:dumped(Ops),
            Dumped andalso clause_mnesia_disc:dump_log(schema_prepare),
            Results = [clause_mnesia_schema:apply_change(Change, Tid) || Change <- Changes],
            [clause_mnesia_disc:save_schema(Tab) || Tab <- lists:usort([changed_table(C) || C <- Changes])],
            clause_mnesia_disc:log_schema(Tid, Ops, [U || {logged, Updates} <- Results, U <- Updates]),
            Dumped andalso clause_mnesia_disc:dump_log(schema_commit),
            clause_mnesia_disc:use_dir() andalso [increment(trans_log_writes) || _ <- [prepare, commit]],
            ok
        catch
            exit:{aborted, Reason} -> {aborted, Reason};
            _:Reason:Stack -> {aborted, {Reason, Stack}}
        end,
    {reply, Result, State};
% A replica's table that a transfer fills, and a replica loaded from another node at start (MIM1H9).
handle_call({replica_table, Tab}, _From, State) ->
    {reply, clause_mnesia_schema:replica_table(Tab), State};
handle_call({replica_loaded, Tab, Node, Reason}, _From, State) ->
    {reply, clause_mnesia_schema:replica_loaded(Tab, Node, Reason), State};
handle_call({dump_log, InitBy}, _From, State) ->
    {reply, clause_mnesia_disc:dump_log(InitBy), State};
% Another node's monitor asks for a protocol (MIM1H4).
handle_call({negotiate_protocol, Mon, _Version, Protocols}, _From, State) when node(Mon) =/= node() ->
    {reply, clause_mnesia_connect:answer(Mon, Protocols), State};
% This node asks other nodes' monitors (MIM1H4).
handle_call({negotiate_protocol, Nodes}, From, State) ->
    ok = clause_mnesia_connect:negotiate(Nodes, From),
    {noreply, State}.

changed_table({restore, _, _}) -> schema;
changed_table({announce, _, _}) -> schema;
changed_table({records, _}) -> schema;
changed_table({_, #cstruct{name = Tab}}) -> Tab;
changed_table({_, #cstruct{name = Tab}, _}) -> Tab;
changed_table({_, Tab}) -> Tab.

handle_cast({dump_log, InitBy}, State) ->
    clause_mnesia_disc:dump_log(InitBy),
    {noreply, State}.

handle_info({dump_log, time_threshold}, _Timer) ->
    clause_mnesia_disc:dump_log(time_threshold),
    {noreply, dump_timer()};
% A linked monitor of another node ended: its mnesia went down (MIM1H4).
handle_info({'EXIT', Pid, _}, State) when node(Pid) =/= node() ->
    clause_mnesia_recover:mnesia_down(node(Pid)),
    {noreply, State};
handle_info({nodeup, Node}, State) ->
    clause_mnesia_connect:node_up(Node),
    {noreply, State};
handle_info(_, State) ->
    {noreply, State}.

terminate(_, _) ->
    set(mnesia_status, stopping),
    clause_mnesia_disc:stop().

set(Name, Value) -> ets:insert(?GVAR, {Name, Value}).
