-module(spawning).
-export([main/1, worker/1, wake/1]).

%% Spawn options, spawn requests, aliases, monitor/3 and hibernation (plan step 41G2).
main(_) ->
    spawn_opts(),
    requests(),
    aliases(),
    monitors(),
    hibernation(),
    io:format("spawning: ok~n").

% A process that waits for stop, then ends normally.
worker(Parent) ->
    receive
        stop -> Parent ! {stopped, self()}
    end.

spawn_opts() ->
    Self = self(),
    Linked = spawn_opt(fun() -> worker(Self) end, [link]),
    {links, Links} = process_info(Self, links),
    show(lists:member(Linked, Links)),
    {Pid, Ref} = spawn_opt(fun() -> worker(Self) end, [monitor]),
    Pid ! stop,
    receive
        {'DOWN', Ref, process, Pid, normal} -> show(down)
    end,
    receive
        {stopped, Pid} -> ok
    end,
    Tuned = [
        {priority, high},
        {min_heap_size, 1000},
        {fullsweep_after, 10},
        {message_queue_data, off_heap}
    ],
    Called = spawn_opt(?MODULE, worker, [Self], Tuned),
    Called ! stop,
    show(
        receive
            {stopped, Called} -> stopped
        end
    ),
    {Tagged, TagRef} = spawn_opt(fun() -> ok end, [{monitor, [{tag, my_tag}]}]),
    receive
        {my_tag, TagRef, process, Tagged, normal} -> show(tagged_down)
    end,
    Local = spawn_opt(nonode@nohost, fun() -> worker(Self) end, []),
    Local ! stop,
    show(
        receive
            {stopped, Local} -> stopped
        end
    ),
    Linked ! stop,
    receive
        {stopped, Linked} -> ok
    end,
    show(run(fun() -> spawn_opt(fun() -> ok end, [bogus]) end)),
    show(run(fun() -> spawn_opt(fun() -> ok end, [{priority, bogus}]) end)).

requests() ->
    Self = self(),
    Plain = spawn_request(fun() -> ok end),
    receive
        {spawn_reply, Plain, ok, P1} -> show(is_pid(P1))
    end,
    Tagged = spawn_request(fun() -> worker(Self) end, [{reply_tag, mytag}, monitor]),
    receive
        {mytag, Tagged, ok, P2} ->
            P2 ! stop,
            receive
                {'DOWN', Tagged, process, P2, normal} -> show(request_down)
            end
    end,
    receive
        {stopped, _} -> ok
    end,
    Quiet = spawn_request(fun() -> ok end, [{reply, no}]),
    show(
        receive
            {spawn_reply, Quiet, _, _} -> unexpected
        after 20 -> no_reply
        end
    ),
    Call = spawn_request(?MODULE, worker, [Self]),
    receive
        {spawn_reply, Call, ok, P3} -> P3 ! stop
    end,
    receive
        {stopped, _} -> show(call_stopped)
    end,
    WithOptions = spawn_request(?MODULE, worker, [Self], [{reply_tag, t}]),
    receive
        {t, WithOptions, ok, P4} -> P4 ! stop
    end,
    receive
        {stopped, _} -> show(options_stopped)
    end,
    OnNode = spawn_request(nonode@nohost, ?MODULE, worker, [Self], []),
    receive
        {spawn_reply, OnNode, ok, P5} -> P5 ! stop
    end,
    receive
        {stopped, _} -> show(node_stopped)
    end,
    show({spawn_request_abandon(Plain), spawn_request_abandon(make_ref())}),
    show(run(fun() -> spawn_request_abandon(not_a_ref) end)),
    BadOption = spawn_request(fun() -> ok end, [bogus]),
    receive
        {spawn_reply, BadOption, error, Why} -> show(Why)
    end.

aliases() ->
    Alias = alias(),
    Alias ! hello,
    show(
        receive
            hello -> got_hello
        after 2000 -> lost
        end
    ),
    show(unalias(Alias)),
    Alias ! again,
    show(
        receive
            again -> unexpected
        after 20 -> dropped
        end
    ),
    show(unalias(Alias)),
    Reply = alias([reply]),
    Reply ! first,
    Reply ! second,
    show(collect([])),
    make_ref() ! nowhere,
    show(run(fun() -> unalias(not_a_ref) end)),
    show(run(fun() -> alias([bogus]) end)).

monitors() ->
    Self = self(),
    Child = spawn(fun() -> answer(Self) end),
    Ref = monitor(process, Child, [{alias, demonitor}]),
    Child ! {ask, Ref},
    show(
        receive
            {answer, Ref} -> answered
        after 2000 -> lost
        end
    ),
    Child ! stop,
    receive
        {'DOWN', Ref, process, Child, normal} -> show(demonitor_down)
    end,
    Replier = spawn(fun() -> answer(Self) end),
    ReplyRef = monitor(process, Replier, [{alias, reply_demonitor}]),
    Replier ! {ask, ReplyRef},
    show(
        receive
            {answer, ReplyRef} -> answered
        after 2000 -> lost
        end
    ),
    Replier ! stop,
    show(
        receive
            {'DOWN', ReplyRef, _, _, _} -> unexpected
        after 50 -> monitor_ended
        end
    ),
    Tagged = spawn(fun() -> worker(Self) end),
    TagRef = monitor(process, Tagged, [{tag, {my, tag}}]),
    Tagged ! stop,
    receive
        {{my, tag}, TagRef, process, Tagged, Reason} -> show({tagged, Reason})
    end,
    receive
        {stopped, Tagged} -> ok
    end,
    show(run(fun() -> monitor(process, Self, [bogus]) end)).

% Answer {ask, Alias} through the alias until told to stop.
answer(Parent) ->
    receive
        {ask, Alias} ->
            Alias ! {answer, Alias},
            answer(Parent);
        stop ->
            ok
    end.

hibernation() ->
    Self = self(),
    Sleeper = spawn(fun() -> erlang:hibernate(?MODULE, wake, [Self]) end),
    receive
    after 20 -> ok
    end,
    Sleeper ! ping,
    show(
        receive
            {woke, Message} -> Message
        after 2000 -> lost
        end
    ).

% Called by hibernate/3 once a message arrived.
wake(Parent) ->
    receive
        Message -> Parent ! {woke, Message}
    end.

% The messages in the mailbox, oldest first.
collect(Acc) ->
    receive
        Message -> collect([Message | Acc])
    after 20 -> lists:reverse(Acc)
    end.

show(Value) -> io:format("~p~n", [Value]).

% The value of Fun, or {error, Reason}.
run(Fun) ->
    try
        Fun()
    catch
        error:Reason -> {error, Reason}
    end.
