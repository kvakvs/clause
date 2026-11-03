%% mnesia's events, text files, query tables and information (plan 21 step MIM1E): system, activity and table
%% subscriptions with their messages and errors, report_event/1, the event handler's printouts at each debug level,
%% load_textfile/1 and dump_to_textfile/1, qlc over mnesia:table/1,2, and the info/0 and system_info/0 printouts, each
%% with OTP's results.
-module(mnesia_events).
-export([main/1]).

-include_lib("stdlib/include/qlc.hrl").

-define(OTHER, 'other@nohost').

main(_) ->
    put(names, [{self(), me}]),
    not_running(),
    ok = mnesia:start(),
    name(whereis(mnesia_event), mnesia_event),
    system_events(),
    activity_events(),
    simple_events(),
    detailed_events(),
    work_order(),
    schema_events(),
    subscription_errors(),
    subscriber_exit(),
    debug_levels(),
    stop_events(),
    ok = mnesia:start(),
    qlc_tables(),
    text_files(),
    information(),
    stopped = mnesia:stop(),
    not_running_info(),
    kills(),
    text_file_start(),
    io:format("mnesia_events: ok~n").

% Events before mnesia starts: report_event/1 still reaches the event handler.
not_running() ->
    t(not_running, fun() ->
        [
            mnesia:subscribe(system),
            mnesia:unsubscribe(system),
            mnesia:report_event(early),
            catch_all(fun() -> mnesia:system_info(subscribers) end),
            mnesia:dump_to_textfile("events_early.txt")
        ]
    end).

system_events() ->
    t(system_events, fun() ->
        [
            mnesia:subscribe(system),
            mnesia:subscribe(system),
            norm(mnesia:system_info(subscribers)),
            mnesia:report_event({hello, [1, 2]}),
            events(),
            mnesia:unsubscribe(system),
            mnesia:unsubscribe(system),
            mnesia:report_event(quiet),
            events(),
            norm(mnesia:system_info(subscribers))
        ]
    end).

% A transaction with writes completes an activity; reads, dirty writes and aborts do not.
activity_events() ->
    {atomic, ok} = mnesia:create_table(act, []),
    t(activity_events, fun() ->
        Own = fun(F) ->
            fun() ->
                F(),
                element(2, mnesia:get_activity_id())
            end
        end,
        Write = fun() -> mnesia:write({act, 1, a}) end,
        {atomic, Tid} = mnesia:transaction(Own(Write)),
        [
            mnesia:subscribe(activity),
            mnesia:subscribe(activity),
            begin
                {atomic, Tid2} = mnesia:transaction(Own(Write)),
                [{mnesia_activity_event, {complete, Got}}] = flush(),
                {Tid2 =:= Got, element(3, Got) =:= self(), element(2, Got) > element(2, Tid)}
            end,
            {mnesia:transaction(fun() -> mnesia:read(act, 1) end), events()},
            {mnesia:dirty_write({act, 2, b}), events()},
            {
                mnesia:transaction(fun() ->
                    Write(),
                    mnesia:abort(no)
                end),
                events()
            },
            {mnesia:transaction(fun() -> mnesia:transaction(Write) end), events()},
            {mnesia:sync_transaction(Write), events()},
            {mnesia:async_dirty(Write), events()},
            {mnesia:ets(Write), events()},
            {mnesia:activity(transaction, Write), events()},
            {mnesia:create_table(act2, []), events()},
            {mnesia:clear_table(act), events()},
            mnesia:unsubscribe(activity),
            {mnesia:transaction(Write), events()}
        ]
    end).

% Simple table events: the record or oid with the table's name and the activity.
simple_events() ->
    {atomic, ok} = mnesia:create_table(s, [{index, [3]}]),
    {atomic, ok} = mnesia:create_table(b, [{type, bag}]),
    {atomic, ok} = mnesia:create_table(r, [{record_name, rec}]),
    {atomic, ok} = mnesia:create_table(c, []),
    t(simple_subscribe, fun() ->
        [
            [mnesia:subscribe({table, T}) || T <- [s, b, r]],
            mnesia:subscribe({table, c, simple}),
            mnesia:subscribe({table, s, simple}),
            mnesia:subscribe({table, s, detailed}),
            norm(mnesia:table_info(s, subscribers)),
            work(s),
            work(b)
        ]
    end),
    t(simple_dirty, fun() ->
        [
            {mnesia:dirty_write({s, 1, a}), events()},
            {mnesia:dirty_write({s, 1, a}), events()},
            {mnesia:dirty_delete(s, 1), events()},
            {mnesia:dirty_delete({s, 9}), events()},
            {mnesia:dirty_delete_object({s, 2, b}), events()},
            {[mnesia:dirty_write({b, 1, V}) || V <- [x, y]], events()},
            {mnesia:dirty_delete_object({b, 1, x}), events()},
            {mnesia:dirty_write(r, {rec, 1, x}), events()},
            {mnesia:dirty_delete(r, 1), events()},
            {mnesia:dirty_update_counter(c, k, 5), events()},
            {mnesia:dirty_update_counter(c, k, -10), events()},
            {mnesia:sync_dirty(fun() -> mnesia:write({s, 3, c}) end), events()},
            {mnesia:ets(fun() -> mnesia:write({s, 4, d}) end), events()}
        ]
    end),
    t(simple_transactions, fun() ->
        [
            {tx(fun() -> mnesia:write({s, 5, e}) end), events()},
            {tx(fun() -> mnesia:delete({s, 5}) end), events()},
            {
                tx(fun() ->
                    mnesia:write({s, 6, f}),
                    mnesia:delete({s, 6})
                end),
                events()
            },
            {tx(fun() -> mnesia:delete_object({b, 1, y}) end), events()},
            {tx(fun() -> mnesia:write(r, {rec, 2, y}, write) end), events()},
            {
                tx(fun() ->
                    [mnesia:write({s, K, k}) || K <- [7, 8, 9]],
                    ok
                end),
                lists:sort(events())
            },
            {
                tx(fun() ->
                    mnesia:write({s, 10, g}),
                    mnesia:abort(no)
                end),
                events()
            },
            {mnesia:clear_table(s), norm_schema(events())},
            {[mnesia:unsubscribe({table, T}) || T <- [s, b, r, c]], work(s)},
            {mnesia:dirty_write({s, 11, h}), events()}
        ]
    end).

% Detailed table events carry the table and the records they replace; a simple subscriber elsewhere gets its own.
detailed_events() ->
    {atomic, ok} = mnesia:create_table(d, [{index, [3]}]),
    {atomic, ok} = mnesia:create_table(db, [{type, bag}]),
    {atomic, ok} = mnesia:create_table(dr, [{record_name, drec}]),
    {atomic, ok} = mnesia:create_table(dc, []),
    Me = self(),
    Helper = spawn_link(fun() ->
        Me ! {ready, mnesia:subscribe({table, d, simple})},
        forward(Me)
    end),
    name(Helper, helper),
    receive
        {ready, _} -> ok
    end,
    t(detailed_subscribe, fun() ->
        [
            [mnesia:subscribe({table, T, detailed}) || T <- [d, db, dr, dc]],
            mnesia:subscribe({table, d, detailed}),
            mnesia:subscribe({table, d, simple}),
            norm(mnesia:table_info(d, subscribers)),
            work(d)
        ]
    end),
    t(detailed_events, fun() ->
        [
            {mnesia:dirty_write({d, 1, a}), events()},
            {mnesia:dirty_write({d, 1, b}), events()},
            {mnesia:dirty_delete(d, 1), events()},
            {mnesia:dirty_delete(d, 9), events()},
            {mnesia:dirty_delete_object({d, 2, x}), events()},
            {[mnesia:dirty_write({db, 1, V}) || V <- [x, y]], events()},
            {mnesia:dirty_delete(db, 1), events()},
            {mnesia:dirty_write(dr, {drec, 1, x}), events()},
            {mnesia:dirty_delete(dr, 1), events()},
            {[mnesia:dirty_update_counter(dc, k, I) || I <- [5, 2, -10]], events()},
            {tx(fun() -> mnesia:write({d, 3, c}) end), events()},
            {tx(fun() -> mnesia:delete({d, 3}) end), events()},
            {
                tx(fun() ->
                    mnesia:write({db, 2, x}),
                    mnesia:write({db, 2, y})
                end),
                events()
            },
            {tx(fun() -> mnesia:delete_object({db, 2, x}) end), events()},
            {mnesia:clear_table(d), norm_schema(events())},
            {mnesia:unsubscribe({table, d, detailed}), norm(mnesia:table_info(d, subscribers)), work(d)},
            {mnesia:unsubscribe({table, d, simple}), mnesia:dirty_write({d, 4, d}), events()}
        ]
    end),
    unlink(Helper),
    exit(Helper, kill).

% The commit work's order as indexes and subscriptions come and go.
work_order() ->
    {atomic, ok} = mnesia:create_table(w, [{attributes, [k, a, b]}]),
    t(work_order, fun() ->
        [
            {mnesia:subscribe({table, w}), work(w)},
            {mnesia:add_table_index(w, a), work(w)},
            {mnesia:add_table_index(w, b), work(w)},
            {mnesia:del_table_index(w, a), work(w)},
            {mnesia:transform_table(w, ignore, [k, a, b, c]), work(w)},
            {mnesia:transform_table(w, fun(R) -> erlang:append_element(R, x) end, [k, a, b, c, d]), work(w)},
            {mnesia:add_table_index(w, a), work(w)},
            {mnesia:unsubscribe({table, w}), work(w)},
            {mnesia:subscribe({table, w, detailed}), work(w)},
            {mnesia:del_table_index(w, a), mnesia:del_table_index(w, b), work(w)},
            {mnesia:unsubscribe({table, w, detailed}), work(w)},
            events()
        ]
    end).

% Schema events: each change's schema records, simple and detailed.
schema_events() ->
    t(schema_events, fun() ->
        [
            mnesia:subscribe({table, schema}),
            {mnesia:create_table(sc, [{attributes, [k, v, w]}]), full_schema(events())},
            {mnesia:add_table_index(sc, w), norm_schema(events())},
            {mnesia:write_table_property(sc, {p, 1}), norm_schema(events())},
            {mnesia:add_table_copy(sc, ?OTHER, ram_copies), norm_schema(events())},
            {mnesia:del_table_copy(sc, ?OTHER), norm_schema(events())},
            {mnesia:change_table_access_mode(sc, read_only), norm_schema(events())},
            {mnesia:change_table_copy_type(sc, node(), ram_copies), norm_schema(events())},
            {mnesia:dump_tables([sc]), norm_schema(events())},
            {mnesia:delete_table(sc), norm_schema(events())},
            mnesia:unsubscribe({table, schema}),
            mnesia:subscribe({table, schema, detailed}),
            {mnesia:create_table(sd, []), norm_schema(events())},
            {mnesia:transform_table(sd, ignore, [k, v, w]), norm_schema(events())},
            {mnesia:clear_table(sd), norm_schema(events())},
            {mnesia:delete_table(sd), norm_schema(events())},
            mnesia:unsubscribe({table, schema, detailed}),
            {mnesia:create_table(se, []), events()}
        ]
    end).

subscription_errors() ->
    {atomic, ok} = mnesia:create_table(far, [{ram_copies, [?OTHER]}]),
    t(subscription_errors, fun() ->
        [
            mnesia:subscribe(bogus),
            mnesia:subscribe({table, nope}),
            mnesia:subscribe({table, nope, detailed}),
            mnesia:subscribe({table, far}),
            mnesia:subscribe({table, s, bogus}),
            mnesia:subscribe({table, s, simple, extra}),
            mnesia:unsubscribe(bogus),
            mnesia:unsubscribe({table, nope}),
            mnesia:unsubscribe({table, far}),
            mnesia:unsubscribe({table, s}),
            mnesia:unsubscribe({table, s, detailed}),
            mnesia:unsubscribe(activity),
            catch_all(fun() -> tx(fun() -> mnesia:subscribe(system) end) end),
            mnesia:unsubscribe(system),
            norm(mnesia:table_info(far, subscribers)),
            norm(mnesia:table_info(far, commit_work))
        ]
    end).

% A subscriber that exits leaves no subscriptions behind.
subscriber_exit() ->
    Me = self(),
    {Pid, Ref} = spawn_monitor(fun() ->
        Result = [mnesia:subscribe(W) || W <- [system, activity, {table, s}, {table, d, detailed}]],
        Me ! {subscribed, self(), Result}
    end),
    name(Pid, gone),
    Subscribed =
        receive
            {subscribed, Pid, Result} -> Result
        end,
    receive
        {'DOWN', Ref, process, Pid, _} -> ok
    end,
    t(subscriber_exit, fun() ->
        [
            Subscribed,
            gone(fun() -> mnesia:system_info(subscribers) end, Pid),
            gone(fun() -> mnesia:table_info(s, subscribers) end, Pid),
            gone(fun() -> mnesia:table_info(d, subscribers) end, Pid),
            work(s),
            work(d)
        ]
    end).

% The debug level subscribes the event handler to the schema (debug) or every local table (trace), whose events it
% prints.
debug_levels() ->
    Subscribers = fun() -> [{T, norm(mnesia:table_info(T, subscribers))} || T <- [schema, s, d]] end,
    t(debug_trace, fun() -> [mnesia:set_debug_level(trace), Subscribers()] end),
    ok = mnesia:dirty_write({s, 12, i}),
    timer:sleep(100),
    t(debug_debug, fun() -> [mnesia:set_debug_level(debug), Subscribers()] end),
    t(debug_none, fun() -> [mnesia:set_debug_level(none), Subscribers()] end),
    t(debug_verbose, fun() -> [mnesia:set_debug_level(verbose), Subscribers(), mnesia:set_debug_level(none)] end).

% Stopping mnesia reports mnesia_down to the system subscribers.
stop_events() ->
    {ok, _} = mnesia:subscribe(system),
    stopped = mnesia:stop(),
    t(stop_events, fun() -> [events(), mnesia:subscribe(system)] end).

% mnesia:table/1,2 as a qlc source in activities: traversals, key and index lookups, explicit selects and cursors.
qlc_tables() ->
    {atomic, ok} = mnesia:create_table(q, [{type, ordered_set}, {index, [3]}]),
    [ok = mnesia:dirty_write({q, K, V}) || {K, V} <- [{1, a}, {2, b}, {3, a}, {4, c}]],
    Ms = [{{q, '$1', '_'}, [{'>', '$1', 2}], ['$_']}],
    t(qlc_tables, fun() ->
        [
            tx(fun() -> qlc:e(mnesia:table(q)) end),
            tx(fun() -> qlc:e(qlc:q([X || X <- mnesia:table(q), element(2, X) =:= 2])) end),
            tx(fun() -> qlc:e(qlc:q([K || {q, K, V} <- mnesia:table(q), V =:= a])) end),
            tx(fun() -> qlc:e(mnesia:table(q, [{traverse, {select, Ms}}])) end),
            tx(fun() -> qlc:e(mnesia:table(q, {n_objects, 1})) end),
            tx(fun() -> qlc:e(qlc:q([K || {q, K, _} <- mnesia:table(q, [{n_objects, 1}]), K > 1])) end),
            mnesia:async_dirty(fun() -> qlc:e(mnesia:table(q)) end),
            mnesia:sync_dirty(fun() -> qlc:e(mnesia:table(q, [{lock, write}])) end),
            mnesia:ets(fun() -> qlc:e(qlc:q([V || {q, _, V} <- mnesia:table(q)], unique)) end),
            tx(fun() ->
                _ = qlc:e(mnesia:table(q, [{lock, write}])),
                norm(mnesia:system_info(held_locks))
            end),
            tx(fun() ->
                C = qlc:cursor(mnesia:table(q, [{n_objects, 1}])),
                First = qlc:next_answers(C, 2),
                Rest = qlc:next_answers(C, all_remaining),
                ok = qlc:delete_cursor(C),
                {First, Rest}
            end),
            tx(fun() -> qlc:fold(fun({q, K, _}, Acc) -> K + Acc end, 0, mnesia:table(q)) end),
            catch_all(fun() -> qlc:e(mnesia:table(q)) end),
            catch_all(fun() -> mnesia:table(q, [{traverse, bogus}]) end),
            tx(fun() -> qlc:e(mnesia:table(nope)) end)
        ]
    end),
    t(qlc_info, fun() ->
        [
            qlc:info(mnesia:table(q)),
            qlc:info(mnesia:table(q, [{lock, write}, {n_objects, 5}])),
            qlc:info(qlc:q([X || X <- mnesia:table(q), element(2, X) =:= 2])),
            qlc:info(qlc:q([K || {q, K, V} <- mnesia:table(q), V =:= a])),
            qlc:info(mnesia:table(q, [{traverse, {select, Ms}}]))
        ]
    end).

% load_textfile/1 creates the tables a file lists and writes its records; dump_to_textfile/1 writes them back.
text_files() ->
    File = "events_text.txt",
    write(File, [
        "{tables, [{tt, [{type, ordered_set}, {attributes, [k, v]}]},\n",
        "          {tu, [{record_name, rec}, {attributes, [k, v]}]},\n",
        "          {bad, [{attributes, [k]}]}]}.\n",
        "{tt, 1, a}.\n{tt, 2, b}.\n{tu, 1, x}.\n{bad, 1}.\n"
    ]),
    t(load_textfile, fun() ->
        [
            mnesia:load_textfile(File),
            mnesia:dirty_match_object({tt, '_', '_'}),
            mnesia:dirty_match_object(tu, {rec, '_', '_'}),
            catch_all(fun() -> mnesia:table_info(bad, type) end),
            mnesia:load_textfile(File)
        ]
    end),
    t(load_textfile_errors, fun() ->
        [
            mnesia:load_textfile("events_missing.txt"),
            text("events_empty.txt", []),
            text("events_header.txt", ["{tt, 3, c}.\n"]),
            text("events_unknown.txt", ["{tables, [{tt, []}]}.\n", "{tt, 3, c}.\n", "{zz, 1}.\n"]),
            text("events_atom.txt", ["{tables, [{tt, []}]}.\n", "foo.\n"]),
            text("events_syntax.txt", ["{tables, [{tt, []}]}.\n", "{tt, 3.\n"]),
            text("events_badtab.txt", ["{tables, [foo]}.\n"]),
            text("events_tab3.txt", ["{tables, [{tv, rec, []}]}.\n"]),
            mnesia:dirty_match_object({tt, '_', '_'})
        ]
    end),
    Tabs = [T || T <- mnesia:system_info(local_tables), T =/= schema, T =/= tt, T =/= tu],
    [{atomic, ok} = mnesia:delete_table(T) || T <- Tabs],
    t(dump_to_textfile, fun() ->
        [
            mnesia:dump_to_textfile("events_dump.txt"),
            file:consult("events_dump.txt"),
            mnesia:delete_table(tt),
            mnesia:delete_table(tu),
            mnesia:load_textfile("events_dump.txt"),
            lists:sort(mnesia:dirty_match_object({tt, '_', '_'})),
            mnesia:table_info(tu, record_name)
        ]
    end).

text(File, Lines) ->
    write(File, Lines),
    catch_all(fun() -> mnesia:load_textfile(File) end).

write(File, Lines) ->
    ok = file:write_file(File, Lines).

% info/0 and system_info/0 print the same report; system_info/0 returns whether mnesia runs.
information() ->
    {atomic, ok} = mnesia:create_table(far2, [{ram_copies, [?OTHER]}]),
    {atomic, ok} = mnesia:create_table(ro, [{access_mode, read_only}]),
    ok = mnesia:set_master_nodes(tt, [node()]),
    io:format("info:~n"),
    ok = mnesia:info(),
    io:format("system_info:~n"),
    t(system_info, fun() -> mnesia:system_info() end).

not_running_info() ->
    io:format("info_not_running:~n"),
    ok = mnesia:info(),
    t(system_info_not_running, fun() -> mnesia:system_info() end).

% lkill/0 and kill/0 end mnesia's processes at once; mnesia can start again.
kills() ->
    ok = mnesia:start(),
    {atomic, ok} = mnesia:create_table(k, []),
    t(lkill, fun() ->
        [mnesia:lkill(), mnesia:system_info(is_running), whereis(mnesia_sup), running_applications()]
    end),
    ok = mnesia:start(),
    t(kill, fun() -> [mnesia:system_info(tables), mnesia:kill(), running_applications(), length(mnesia:ms())] end).

running_applications() ->
    lists:keymember(mnesia, 1, application:which_applications()).

% load_textfile/1 starts mnesia, on a new disc schema when there is none.
text_file_start() ->
    ok = mnesia:delete_schema([node()]),
    t(text_file_start, fun() ->
        [
            mnesia:load_textfile("events_dump.txt"),
            mnesia:system_info(is_running),
            mnesia:system_info(use_dir),
            mnesia:table_info(tt, storage_type),
            lists:sort(mnesia:dirty_match_object({tt, '_', '_'}))
        ]
    end),
    stopped = mnesia:stop(),
    ok = mnesia:delete_schema([node()]).

%% Helpers.

t(Label, F) ->
    io:format("~p: ~p~n", [Label, catch_all(F)]).

catch_all(F) ->
    try
        F()
    catch
        Class:Reason -> {Class, norm(Reason)}
    end.

tx(F) ->
    norm(mnesia:transaction(F)).

% The messages received so far, with pids and transaction ids named.
events() ->
    norm(flush()).

flush() ->
    receive
        Msg -> [Msg | flush()]
    after 100 -> []
    end.

forward(To) ->
    receive
        Msg ->
            To ! {forwarded, Msg},
            forward(To)
    end.

name(Pid, Name) ->
    put(names, [{Pid, Name} | get(names)]).

% Pids by name, and transaction ids without their serial numbers, which other transactions change.
norm(Pid) when is_pid(Pid) ->
    case lists:keyfind(Pid, 1, get(names)) of
        {_, Name} -> Name;
        false -> pid
    end;
norm({tid, N, Pid}) when is_integer(N) ->
    {tid, norm(Pid)};
norm(T) when is_tuple(T) ->
    list_to_tuple(norm(tuple_to_list(T)));
norm([H | T]) ->
    [norm(H) | norm(T)];
norm(Term) ->
    Term.

% Schema records with their cookies and versions hidden, which hold times.
full_schema(Events) ->
    hide(Events).

% Schema records with the fields the changes here touch.
norm_schema(Events) ->
    compact(hide(Events)).

compact({schema, Tab, [{name, Tab} | _] = Def}) ->
    {schema, Tab, [
        F
     || {K, _} = F <- Def, lists:member(K, [ram_copies, access_mode, index, attributes, user_properties])
    ]};
compact(T) when is_tuple(T) -> list_to_tuple(compact(tuple_to_list(T)));
compact([H | T]) ->
    [compact(H) | compact(T)];
compact(Term) ->
    Term.

hide({cookie, _}) -> {cookie, hidden};
hide({version, _}) -> {version, hidden};
hide(T) when is_tuple(T) -> list_to_tuple(hide(tuple_to_list(T)));
hide([H | T]) -> [hide(H) | hide(T)];
hide(Term) -> Term.

% A table's commit work: the kind of each entry, and the subscribers.
work(Tab) ->
    [
        case W of
            {subscribers, _, _} -> norm(W);
            _ -> element(1, W)
        end
     || W <- mnesia:table_info(Tab, commit_work)
    ].

% A subscriber list once Pid has left it (the subscription server learns of the exit on its own).
gone(F, Pid) ->
    gone(F, Pid, 50).

gone(F, _Pid, 0) ->
    {still_there, norm(F())};
gone(F, Pid, N) ->
    List = F(),
    case lists:member(Pid, List) of
        true ->
            timer:sleep(20),
            gone(F, Pid, N - 1);
        false ->
            norm(List)
    end.
