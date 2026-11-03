%% mnesia's event subscriptions, registered as mnesia_subscr as in OTP (plan 21 step MIM1E,
%% docs/libraries/mnesia.md#events). The server links each subscriber and drops its subscriptions when it exits; the
%% subscriber lists live in mnesia_gvar as OTP keeps them: subscribers and activity_subscribers, and a
%% {subscribers, Simple, Detailed} entry in each subscribed table's commit_work. The reports are client functions run
%% where the event happens: system events, completed transactions, and table events of commits and schema changes.
-module(clause_mnesia_subscr).
-behaviour(gen_server).
-export([start_link/0, subscribe/2, unsubscribe/2, subscribers/0, set_debug_level/1]).
-export([report_system_event/1, report_activity/1, report_table_event/5, report_schema_event/3, sort_commit/1]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2, terminate/2]).

-include("clause_mnesia.hrl").

start_link() ->
    gen_server:start_link({local, mnesia_subscr}, ?MODULE, [], [{timeout, infinity}]).

%% Subscriptions.

% A subscription of Pid: system, activity, or {table, Tab} with simple or detailed events.
subscribe(Pid, What) ->
    case category(What) of
        badarg -> {error, {badarg, What}};
        Category -> call({activate, Pid, Category})
    end.

unsubscribe(Pid, What) ->
    case category(What) of
        badarg -> {error, {badarg, What}};
        Category -> call({deactivate, Pid, Category})
    end.

category(system) -> system;
category(activity) -> activity;
category({table, Tab}) -> {table, Tab, simple};
category({table, Tab, simple}) -> {table, Tab, simple};
category({table, Tab, detailed}) -> {table, Tab, detailed};
category(_) -> badarg.

% system_info(subscribers): the event manager first, which always takes system events.
subscribers() ->
    [whereis(mnesia_event) | val(subscribers)].

call(Request) ->
    case whereis(mnesia_subscr) of
        undefined -> {error, {node_not_running, node()}};
        Pid -> gen_server:call(Pid, Request, infinity)
    end.

% The debug level when mnesia runs: the event manager subscribes to the schema's events (debug) or every local table's
% (trace), and to none below; the previous level back.
set_debug_level(Level) ->
    Old = val(debug),
    Event = whereis(mnesia_event),
    Local = val({schema, local_tables}),
    Subscribed =
        case Level of
            debug -> [schema];
            trace -> Local;
            _ -> []
        end,
    [unsubscribe(Event, {table, Tab}) || Tab <- Local, not lists:member(Tab, Subscribed)],
    [subscribe(Event, {table, Tab}) || Tab <- Subscribed],
    ets:insert(?GVAR, {debug, Level}),
    Old.

%% Reports, run where the event happens.

% A system event to the event manager and the system subscribers; without a running manager, a manager started for
% this event alone prints it, as OTP's does.
report_system_event(Event) ->
    Message = {mnesia_system_event, Event},
    case whereis(mnesia_event) of
        undefined -> once(Message);
        Manager -> gen_event:notify(Manager, Message)
    end,
    deliver(catch_val(subscribers, []), Message).

once(Message) ->
    case clause_mnesia_app:start_event() of
        {ok, Manager} ->
            unlink(Manager),
            gen_event:call(mnesia_event, clause_mnesia_event:handler(), Message, infinity),
            gen_event:stop(Manager);
        _ ->
            ok
    end.

% A transaction that wrote completed: {mnesia_activity_event, {complete, Tid}} to the activity subscribers.
report_activity(Tid) ->
    deliver(catch_val(activity_subscribers, []), {mnesia_activity_event, {complete, Tid}}).

% A table update's events before it changes the table: Obj is the written record, the deleted oid or record, or the
% cleared pattern; Old the records it replaces, or undefined to read them first.
report_table_event(Tab, Tid, Obj, Op, Old) ->
    case lists:keyfind(subscribers, 1, catch_val({Tab, commit_work}, [])) of
        false -> ok;
        {subscribers, Simple, Detailed} -> table_event(Simple, Detailed, Tab, Tid, Obj, Op, Old)
    end.

% A cleared table reports its schema record deleted and written again.
table_event(Simple, Detailed, Tab, Tid, _, clear_table, _) ->
    Record = {schema, Tab, clause_mnesia_schema:cs2list(val({Tab, cstruct}))},
    deliver(Simple, {mnesia_table_event, {delete, {schema, Tab}, Tid}}),
    deliver(Simple, {mnesia_table_event, {write, Record, Tid}}),
    deliver(Detailed, {mnesia_table_event, {delete, schema, {schema, Tab}, [Record], Tid}}),
    deliver(Detailed, {mnesia_table_event, {write, schema, Record, [], Tid}});
table_event(Simple, Detailed, Tab, Tid, Obj, Op, Old) ->
    deliver(Simple, {mnesia_table_event, {Op, setelement(1, Obj, Tab), Tid}}),
    Detailed =/= [] andalso deliver_detailed(Detailed, Tab, Tid, Obj, Op, Old).

% A detailed event reads the replaced records; none when the table has gone meanwhile, as in OTP.
deliver_detailed(Detailed, Tab, Tid, Obj, Op, Old) ->
    try detailed(Tab, Tid, Obj, Op, Old) of
        Event -> deliver(Detailed, {mnesia_table_event, Event})
    catch
        error:_ -> ok
    end.

detailed(Tab, Tid, {_, Key} = Oid, delete, undefined) -> {delete, Tab, Oid, stored(Tab, Key), Tid};
detailed(Tab, Tid, Obj, delete_object, _) -> {delete, Tab, Obj, [Obj], Tid};
detailed(Tab, Tid, Obj, delete, Old) -> {delete, Tab, Obj, Old, Tid};
detailed(Tab, Tid, Obj, write, undefined) -> {write, Tab, Obj, stored(Tab, element(2, Obj)), Tid};
detailed(Tab, Tid, Obj, write, Old) -> {write, Tab, Obj, Old, Tid}.

stored(schema, Key) -> ets:lookup(schema, Key);
stored(Tab, Key) -> clause_mnesia_db:lookup(Tab, Key).

% A schema record written or deleted by a schema change, as the schema table's events.
report_schema_event(Tid, Record, Op) ->
    report_table_event(schema, Tid, Record, Op, undefined).

deliver(Pids, Message) ->
    [Pid ! Message || Pid <- Pids],
    ok.

% A table's commit work as OTP orders it: checkpoints first, the rest reversed at each change.
sort_commit(Work) ->
    reorder(Work, []).

reorder([{checkpoints, _} | _] = Rest, Acc) -> Rest ++ Acc;
reorder([Entry | Rest], Acc) -> reorder(Rest, [Entry | Acc]);
reorder([], Acc) -> Acc.

%% Server side: the subscriptions of each linked process.

init([]) ->
    process_flag(trap_exit, true),
    Event = whereis(mnesia_event),
    link(Event),
    Subscriptions = ets:new(mnesia_subscr, [duplicate_bag, private]),
    ets:insert(Subscriptions, {Event, system}),
    debug_subscriptions(Event, Subscriptions),
    {ok, Subscriptions}.

% At the debug levels debug and trace the event manager takes the schema's events from the start.
debug_subscriptions(Event, Subscriptions) ->
    case val(debug) of
        Level when Level =:= debug; Level =:= trace ->
            change({activate, Event, {table, schema, simple}}, Subscriptions);
        _ ->
            ok
    end.

handle_call({Kind, Pid, What}, _From, Subscriptions) when Kind =:= activate; Kind =:= deactivate ->
    {reply, change({Kind, Pid, What}, Subscriptions), Subscriptions}.

handle_cast(_, Subscriptions) ->
    {noreply, Subscriptions}.

handle_info({'EXIT', Pid, _}, Subscriptions) ->
    drop(Pid, Subscriptions),
    {noreply, Subscriptions};
handle_info(_, Subscriptions) ->
    {noreply, Subscriptions}.

% Stopping reports mnesia_down to the system subscribers, then forgets every subscription.
terminate(_, Subscriptions) ->
    report_system_event({mnesia_down, node()}),
    [
        begin
            drop(Pid, Subscriptions),
            unlink(Pid)
        end
     || Pid <- lists:usort([P || {P, _} <- ets:tab2list(Subscriptions)])
    ],
    ok.

change({activate, Pid, {table, Tab, _} = What}, Subscriptions) ->
    Node = node(),
    case catch_val({Tab, where_to_read}, missing) of
        Node -> activate(Pid, What, table_subscribers(Tab), Subscriptions);
        missing -> {error, {no_exists, Tab}};
        _ -> {error, {not_active_local, Tab}}
    end;
change({activate, Pid, system}, Subscriptions) ->
    activate(Pid, system, subscribers(), Subscriptions);
change({activate, Pid, activity}, Subscriptions) ->
    activate(Pid, activity, val(activity_subscribers), Subscriptions);
change({deactivate, Pid, What}, Subscriptions) ->
    ets:match_delete(Subscriptions, {Pid, What}),
    ets:member(Subscriptions, Pid) orelse unlink(Pid),
    try
        del_subscriber(What, Pid),
        {ok, node()}
    catch
        _:_ -> {error, badarg}
    end.

% A new subscription: refused when Pid has one of the kind already, and for a process that does not exist.
activate(Pid, What, Old, Subscriptions) ->
    case lists:member(Pid, Old) of
        true ->
            {error, {already_exists, What}};
        false ->
            try link(Pid) of
                true ->
                    ets:insert(Subscriptions, {Pid, What}),
                    add_subscriber(What, Pid),
                    {ok, node()}
            catch
                error:_ -> {error, {no_exists, Pid}}
            end
    end.

table_subscribers(Tab) ->
    case lists:keyfind(subscribers, 1, val({Tab, commit_work})) of
        false -> [];
        {subscribers, Simple, Detailed} -> Simple ++ Detailed
    end.

add_subscriber(system, Pid) ->
    set(subscribers, [Pid | lists:delete(Pid, val(subscribers))]);
add_subscriber(activity, Pid) ->
    set(activity_subscribers, [Pid | lists:delete(Pid, val(activity_subscribers))]);
add_subscriber({table, Tab, How}, Pid) ->
    Work = val({Tab, commit_work}),
    {Simple, Detailed} = table_lists(Work),
    Entry =
        case How of
            simple -> {subscribers, [Pid | Simple], Detailed};
            detailed -> {subscribers, Simple, [Pid | Detailed]}
        end,
    set({Tab, commit_work}, sort_commit(with_entry(Entry, Work))),
    set({Tab, subscribers}, [Pid | lists:delete(Pid, val({Tab, subscribers}))]).

% The new entry keeps the place of the old one, as OTP replaces it in its list.
with_entry(Entry, Work) ->
    case lists:keymember(subscribers, 1, Work) of
        true -> lists:keyreplace(subscribers, 1, Work, Entry);
        false -> [Entry | Work]
    end.

table_lists(Work) ->
    case lists:keyfind(subscribers, 1, Work) of
        false -> {[], []};
        {subscribers, Simple, Detailed} -> {Simple, Detailed}
    end.

% Pid leaves a subscriber list; a table's subscribers entry goes with its last subscriber.
del_subscriber(system, Pid) ->
    set(subscribers, lists:delete(Pid, val(subscribers)));
del_subscriber(activity, Pid) ->
    set(activity_subscribers, lists:delete(Pid, val(activity_subscribers)));
del_subscriber({table, Tab, _}, Pid) ->
    Work = val({Tab, commit_work}),
    case table_lists(Work) of
        {[], []} ->
            false;
        {Simple, Detailed} ->
            Remaining =
                case {lists:delete(Pid, Simple), lists:delete(Pid, Detailed)} of
                    {[], []} -> lists:keydelete(subscribers, 1, Work);
                    {S, D} -> lists:keyreplace(subscribers, 1, Work, {subscribers, S, D})
                end,
            set({Tab, subscribers}, lists:delete(Pid, val({Tab, subscribers}))),
            set({Tab, commit_work}, sort_commit(Remaining))
    end.

% A subscriber gone: each of its subscriptions with it.
drop(Pid, Subscriptions) ->
    [catch_drop(What, Pid) || {_, What} <- ets:lookup(Subscriptions, Pid)],
    ets:delete(Subscriptions, Pid).

catch_drop(What, Pid) ->
    try
        del_subscriber(What, Pid)
    catch
        _:_ -> ok
    end.

val(Name) -> clause_mnesia_monitor:val(Name).

catch_val(Name, Default) ->
    try
        ets:lookup_element(?GVAR, Name, 2)
    catch
        error:_ -> Default
    end.

set(Name, Value) -> ets:insert(?GVAR, {Name, Value}).
