%% mnesia's monitor, registered as mnesia_monitor as in OTP (plan 21 step MIM1A, docs/libraries/mnesia.md#processes):
%% it owns mnesia_gvar, the schema and every table, so they live as long as mnesia runs, reads the configuration
%% parameters, and runs schema changes one at a time, counting each as a committed or failed transaction.
-module(clause_mnesia_monitor).
-behaviour(gen_server).
-export([start_link/0, schema_operation/1, val/1, is_running/0, get_env/1, check_env/2, increment/1]).
-export([init/1, handle_call/3, handle_cast/2, terminate/2]).

-include("clause_mnesia.hrl").

start_link() ->
    gen_server:start_link({local, mnesia_monitor}, ?MODULE, [], [{timeout, infinity}]).

% Run a schema change (create_table, delete_table, add_index or del_index) as a schema transaction.
schema_operation(Operation) ->
    case is_running() of
        yes -> gen_server:call(mnesia_monitor, {schema, Operation}, infinity);
        _ -> {aborted, {node_not_running, node()}}
    end.

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

% The global variables, the configuration and an empty RAM schema; two schema transactions counted, as OTP's
% start commits them.
init([]) ->
    process_flag(trap_exit, true),
    ets:new(?GVAR, [set, public, named_table]),
    set(mnesia_status, starting),
    [set(Name, get_env(Name)) || Name <- parameters()],
    {ok, Version} = application:get_key(mnesia, vsn),
    [set(Name, Value) || {Name, Value} <- [{version, Version}, {use_dir, false}, {{current, db_nodes}, [node()]}]],
    [set(Counter, N) || {Counter, N} <- [{trans_commits, 2}, {trans_failures, 0}, {trans_restarts, 0}]],
    clause_mnesia_schema:init_schema(),
    set(mnesia_status, running),
    {ok, []}.

% A schema change: its result as {atomic, ok} or {aborted, Reason}, counted.
handle_call({schema, Operation}, _From, State) ->
    Result =
        try run(Operation) of
            ok -> {atomic, ok}
        catch
            exit:{aborted, Reason} -> {aborted, Reason};
            throw:{not_implemented, _} = NotImplemented -> NotImplemented;
            Class:Reason:Stack when Class =/= throw -> {aborted, {Reason, Stack}}
        end,
    case Result of
        {atomic, _} -> increment(trans_commits);
        _ -> increment(trans_failures)
    end,
    {reply, Result, State}.

run({create_table, Def}) -> clause_mnesia_schema:create_table(Def);
run({delete_table, Tab}) -> clause_mnesia_schema:delete_table(Tab);
run({add_index, Tab, Attribute}) -> clause_mnesia_schema:add_index(Tab, Attribute);
run({del_index, Tab, Attribute}) -> clause_mnesia_schema:del_index(Tab, Attribute).

handle_cast(_, State) ->
    {noreply, State}.

terminate(_, _) ->
    set(mnesia_status, stopping).

set(Name, Value) -> ets:insert(?GVAR, {Name, Value}).
