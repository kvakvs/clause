%% Clause's proc_lib (plan 21 step LIB20, docs/library.md): processes that know their ancestors and initial call,
%% synchronous starts acknowledged with init_ack/init_fail, and crash reports when they end abnormally. Reports go
%% through clause_logger in OTP's report shape and text.
-module(proc_lib).
-export([
    format/1,
    format/2,
    format/3,
    get_label/1,
    hibernate/3,
    init_ack/1,
    init_ack/2,
    init_fail/2,
    init_fail/3,
    init_p/3,
    init_p/5,
    initial_call/1,
    report_cb/2,
    set_label/1,
    spawn/1,
    spawn/2,
    spawn/3,
    spawn/4,
    spawn_link/1,
    spawn_link/2,
    spawn_link/3,
    spawn_link/4,
    spawn_opt/2,
    spawn_opt/3,
    spawn_opt/4,
    spawn_opt/5,
    start/3,
    start/4,
    start/5,
    start_link/3,
    start_link/4,
    start_link/5,
    start_monitor/3,
    start_monitor/4,
    start_monitor/5,
    stop/1,
    stop/3,
    translate_initial_call/1,
    wake_up/3
]).
-include("proc_lib.types.hrl").

%% The dictionary keys proc_lib keeps for a process.
-define(ANCESTORS, '$ancestors').
-define(INITIAL_CALL, '$initial_call').
-define(LABEL, '$process_label').

%% Spawning: each new process starts in init_p, which records its ancestors and initial call.

spawn(F) when is_function(F) -> erlang:spawn(?MODULE, init_p, fun_start(F)).

spawn(Node, F) when is_function(F) -> erlang:spawn(Node, ?MODULE, init_p, fun_start(F)).

spawn(M, F, A) when is_atom(M), is_atom(F), is_list(A) ->
    erlang:spawn(?MODULE, init_p, mfa_start(M, F, A)).

spawn(Node, M, F, A) when is_atom(M), is_atom(F), is_list(A) ->
    erlang:spawn(Node, ?MODULE, init_p, mfa_start(M, F, A)).

spawn_link(F) when is_function(F) -> erlang:spawn_link(?MODULE, init_p, fun_start(F)).

spawn_link(Node, F) when is_function(F) -> erlang:spawn_link(Node, ?MODULE, init_p, fun_start(F)).

spawn_link(M, F, A) when is_atom(M), is_atom(F), is_list(A) ->
    erlang:spawn_link(?MODULE, init_p, mfa_start(M, F, A)).

spawn_link(Node, M, F, A) when is_atom(M), is_atom(F), is_list(A) ->
    erlang:spawn_link(Node, ?MODULE, init_p, mfa_start(M, F, A)).

spawn_opt(F, Options) when is_function(F) ->
    erlang:spawn_opt(?MODULE, init_p, fun_start(F), Options).

spawn_opt(Node, F, Options) when is_function(F) ->
    erlang:spawn_opt(Node, ?MODULE, init_p, fun_start(F), Options).

spawn_opt(M, F, A, Options) when is_atom(M), is_atom(F), is_list(A) ->
    erlang:spawn_opt(?MODULE, init_p, mfa_start(M, F, A), Options).

spawn_opt(Node, M, F, A, Options) when is_atom(M), is_atom(F), is_list(A) ->
    erlang:spawn_opt(Node, ?MODULE, init_p, mfa_start(M, F, A), Options).

fun_start(F) -> [parent_name(), ancestors(), F].

mfa_start(M, F, A) -> [parent_name(), ancestors(), M, F, A].

%% The initial call of every process proc_lib spawns.
init_p(Parent, Ancestors, F) when is_function(F) ->
    put(?ANCESTORS, [Parent | Ancestors]),
    put(?INITIAL_CALL, fun_mfa(F)),
    try
        F()
    catch
        Class:Reason:Stack -> exit_p(Class, Reason, Stack)
    end.

init_p(Parent, Ancestors, M, F, A) when is_atom(M), is_atom(F), is_list(A) ->
    put(?ANCESTORS, [Parent | Ancestors]),
    put(?INITIAL_CALL, translated_call(M, F, A)),
    run(M, F, A).

%% What a hibernated proc_lib process wakes up in.
wake_up(M, F, A) when is_atom(M), is_atom(F), is_list(A) -> run(M, F, A).

hibernate(M, F, A) when is_atom(M), is_atom(F), is_list(A) ->
    erlang:hibernate(?MODULE, wake_up, [M, F, A]).

run(M, F, A) ->
    try
        apply(M, F, A)
    catch
        Class:Reason:Stack -> exit_p(Class, Reason, Stack)
    end.

%% {Module, Name, Arity} of a fun, as erlang:fun_info_mfa/1 gives it.
fun_mfa(F) ->
    {module, M} = erlang:fun_info(F, module),
    {name, Name} = erlang:fun_info(F, name),
    {arity, Arity} = erlang:fun_info(F, arity),
    {M, Name, Arity}.

%% Reports the crash, then ends with the exit reason the exception would have given.
exit_p(Class, Reason, Stack) ->
    Start =
        case get(?INITIAL_CALL) of
            {M, F, A} when is_atom(M), is_atom(F), is_integer(A) -> {M, F, dummy_args(A)};
            _ -> []
        end,
    crash_report(Class, Reason, Start, Stack),
    erlang:raise(exit, exit_reason(Class, Reason, Stack), Stack).

exit_reason(error, Reason, Stack) -> {Reason, Stack};
exit_reason(exit, Reason, _) -> Reason;
exit_reason(throw, Reason, Stack) -> {{nocatch, Reason}, Stack}.

%% Synchronous starts: the new process answers with init_ack/1,2 or init_fail/2,3, or ends first.

start(M, F, A) when is_atom(M), is_atom(F), is_list(A) -> start(M, F, A, infinity).

start(M, F, A, Timeout) when is_atom(M), is_atom(F), is_list(A) ->
    await_start(erlang:spawn_monitor(?MODULE, init_p, mfa_start(M, F, A)), Timeout).

start(M, F, A, Timeout, Options) when is_atom(M), is_atom(F), is_list(A) ->
    no_monitor_option(M, F, A, Timeout, Options),
    await_start(?MODULE:spawn_opt(M, F, A, [monitor | Options]), Timeout).

start_link(M, F, A) when is_atom(M), is_atom(F), is_list(A) -> start_link(M, F, A, infinity).

start_link(M, F, A, Timeout) when is_atom(M), is_atom(F), is_list(A) ->
    await_start(?MODULE:spawn_opt(M, F, A, [link, monitor]), Timeout).

start_link(M, F, A, Timeout, Options) when is_atom(M), is_atom(F), is_list(A) ->
    no_monitor_option(M, F, A, Timeout, Options),
    await_start(?MODULE:spawn_opt(M, F, A, [link, monitor | Options]), Timeout).

start_monitor(M, F, A) when is_atom(M), is_atom(F), is_list(A) -> start_monitor(M, F, A, infinity).

start_monitor(M, F, A, Timeout) when is_atom(M), is_atom(F), is_list(A) ->
    await_monitored_start(erlang:spawn_monitor(?MODULE, init_p, mfa_start(M, F, A)), Timeout).

start_monitor(M, F, A, Timeout, Options) when is_atom(M), is_atom(F), is_list(A) ->
    no_monitor_option(M, F, A, Timeout, Options),
    await_monitored_start(?MODULE:spawn_opt(M, F, A, [monitor | Options]), Timeout).

%% The start functions monitor the new process themselves: a monitor option of the caller is badarg.
no_monitor_option(M, F, A, Timeout, Options) ->
    case lists:member(monitor, Options) orelse lists:keymember(monitor, 1, Options) of
        true -> erlang:error(badarg, [M, F, A, Timeout, Options]);
        false -> ok
    end.

await_start({Pid, Ref}, Timeout) ->
    receive
        {ack, Pid, Return} ->
            erlang:demonitor(Ref, [flush]),
            Return;
        {nack, Pid, Return} ->
            drop_exit(Pid),
            _ = await_down(Pid, Ref),
            Return;
        {'DOWN', Ref, process, Pid, Reason} ->
            drop_exit(Pid),
            {error, Reason}
    after Timeout ->
        kill_and_drop_exit(Pid),
        _ = await_down(Pid, Ref),
        {error, timeout}
    end.

%% As await_start, keeping the monitor: a 'DOWN' the start consumed is put back in the mailbox.
await_monitored_start({Pid, Ref}, Timeout) ->
    receive
        {ack, Pid, Return} ->
            {Return, Ref};
        {nack, Pid, Return} ->
            drop_exit(Pid),
            self() ! await_down(Pid, Ref),
            {Return, Ref};
        {'DOWN', Ref, process, Pid, Reason} = Down ->
            drop_exit(Pid),
            self() ! Down,
            {{error, Reason}, Ref}
    after Timeout ->
        kill_and_drop_exit(Pid),
        self() ! await_down(Pid, Ref),
        {{error, timeout}, Ref}
    end.

drop_exit(Pid) ->
    unlink(Pid),
    receive
        {'EXIT', Pid, _} -> ok
    after 0 -> ok
    end.

%% Unlinks before the kill, so the starter is not taken down with the process.
kill_and_drop_exit(Pid) ->
    unlink(Pid),
    exit(Pid, kill),
    receive
        {'EXIT', Pid, _} -> ok
    after 0 -> ok
    end.

await_down(Pid, Ref) ->
    receive
        {'DOWN', Ref, process, Pid, _} = Down -> Down
    end.

init_ack(Parent, Return) ->
    Parent ! {ack, self(), Return},
    ok.

init_ack(Return) -> init_ack(starter(), Return).

init_fail(Parent, Return, Exception) ->
    _ = Parent ! {nack, self(), Return},
    case Exception of
        {error, Reason} ->
            erlang:error(Reason);
        {exit, Reason} ->
            erlang:exit(Reason);
        {throw, Reason} ->
            erlang:throw(Reason);
        {Class, Reason, Stack} ->
            erlang:error(erlang:raise(Class, Reason, Stack), [Parent, Return, Exception])
    end.

init_fail(Return, Exception) -> init_fail(starter(), Return, Exception).

starter() ->
    [Parent | _] = get(?ANCESTORS),
    Parent.

%% Process information.

initial_call(Process) ->
    case raw_initial_call(Process) of
        {M, F, A} -> {M, F, dummy_args(A)};
        false -> false
    end.

translate_initial_call(Process) ->
    case raw_initial_call(Process) of
        {_, _, _} = MFA -> MFA;
        false -> {?MODULE, init_p, 5}
    end.

set_label(Label) ->
    put(?LABEL, Label),
    ok.

get_label(Pid) when Pid =:= self() ->
    get(?LABEL);
get_label(Pid) ->
    case dictionary_value(Pid, ?LABEL) of
        {ok, Label} -> Label;
        error -> undefined
    end.

%% 'Argument__1' ... 'Argument__N', standing for arguments proc_lib does not keep.
dummy_args(N) -> [list_to_atom("Argument__" ++ integer_to_list(I)) || I <- lists:seq(1, N)].

raw_initial_call({X, Y, Z}) when is_integer(X), is_integer(Y), is_integer(Z) ->
    raw_initial_call(c_pid(X, Y, Z));
raw_initial_call(Pid) when is_pid(Pid) ->
    case dictionary_value(Pid, ?INITIAL_CALL) of
        {ok, {_, _, _} = MFA} -> MFA;
        _ -> false
    end;
raw_initial_call(Info) when is_list(Info) ->
    case lists:keyfind({dictionary, ?INITIAL_CALL}, 1, Info) of
        {_, {_, _, _} = MFA} ->
            MFA;
        {_, _} ->
            false;
        false ->
            case lists:keyfind(dictionary, 1, Info) of
                {dictionary, Dictionary} -> initial_call_in(Dictionary);
                false -> false
            end
    end.

initial_call_in(Dictionary) ->
    case lists:keyfind(?INITIAL_CALL, 1, Dictionary) of
        {_, {_, _, _} = MFA} -> MFA;
        _ -> false
    end.

%% c:pid/3's pid.
c_pid(X, Y, Z) ->
    list_to_pid("<" ++ lists:join(".", [integer_to_list(I) || I <- [X, Y, Z]]) ++ ">").

%% A key of a process's dictionary: {ok, Value}, or error for a missing key or process.
dictionary_value(Pid, Key) ->
    case process_info(Pid, dictionary) of
        {dictionary, Dictionary} ->
            case lists:keyfind(Key, 1, Dictionary) of
                {Key, Value} -> {ok, Value};
                false -> error
            end;
        _ ->
            error
    end.

%% The initial call recorded for gen processes: the callback module's init/1, as OTP's proc_lib translates them.
translated_call(gen, init_it, [gen_server, _, _, supervisor, {_, Module, _}, _]) ->
    {supervisor, Module, 1};
translated_call(gen, init_it, [gen_server, _, _, _, supervisor, {_, Module, _}, _]) ->
    {supervisor, Module, 1};
translated_call(gen, init_it, [gen_server, _, _, supervisor_bridge, [Module | _], _]) ->
    {supervisor_bridge, Module, 1};
translated_call(gen, init_it, [gen_server, _, _, _, supervisor_bridge, [Module | _], _]) ->
    {supervisor_bridge, Module, 1};
translated_call(gen, init_it, [gen_event | _]) ->
    {gen_event, init_it, 6};
translated_call(gen, init_it, [_, _, _, Module, _, _]) when is_atom(Module) -> {Module, init, 1};
translated_call(gen, init_it, [_, _, _, _, Module | _]) when is_atom(Module) -> {Module, init, 1};
translated_call(M, F, A) when is_atom(M), is_atom(F) -> {M, F, length(A)}.

%% The registered name of the caller, else its pid: what a child records as its parent.
parent_name() ->
    case process_info(self(), registered_name) of
        {registered_name, Name} -> Name;
        _ -> self()
    end.

ancestors() ->
    case get(?ANCESTORS) of
        A when is_list(A) -> A;
        _ -> []
    end.

%% Stopping: sys:terminate/3, then the 'DOWN' of the process within what is left of Timeout.

stop(Process) -> stop(Process, normal, infinity).

stop(Process, Reason, Timeout) ->
    Ref = erlang:monitor(process, Process),
    Started = erlang:monotonic_time(millisecond),
    Left =
        try sys:terminate(Process, Reason, Timeout) of
            ok -> remaining(Timeout, Started)
        catch
            exit:{noproc, {sys, terminate, _}} ->
                demonitor(Ref, [flush]),
                exit(noproc);
            exit:{timeout, {sys, terminate, _}} ->
                demonitor(Ref, [flush]),
                exit(timeout);
            exit:{Reason, {sys, terminate, _}} ->
                remaining(Timeout, Started);
            exit:Other ->
                demonitor(Ref, [flush]),
                exit(Other)
        end,
    receive
        {'DOWN', Ref, _, _, Reason} -> ok;
        {'DOWN', Ref, _, _, Other2} -> exit(Other2)
    after Left ->
        demonitor(Ref, [flush]),
        exit(timeout)
    end.

remaining(infinity, _) ->
    infinity;
remaining(Timeout, Started) ->
    Timeout - ((erlang:monotonic_time(microsecond) + 999) div 1000 - Started).

%% Crash reports.

crash_report(exit, normal, _, _) ->
    ok;
crash_report(exit, shutdown, _, _) ->
    ok;
crash_report(exit, {shutdown, _}, _, _) ->
    ok;
crash_report(Class, Reason, Start, Stack) ->
    clause_logger:log(
        error,
        {report, #{
            label => {proc_lib, crash},
            report => [own_report(Class, Reason, Start, Stack), neighbours()]
        }},
        #{
            domain => [otp, sasl],
            report_cb => fun proc_lib:report_cb/2,
            logger_formatter => #{title => "CRASH REPORT"},
            error_logger => #{tag => error_report, type => crash_report}
        }
    ).

own_report(Class, Reason, [], Stack) ->
    process_report(Class, Reason, Stack);
own_report(Class, Reason, Start, Stack) ->
    [{initial_call, Start} | process_report(Class, Reason, Stack)].

process_report(Class, Reason, Stack) ->
    Info = process_info(self(), [
        registered_name,
        dictionary,
        message_queue_len,
        messages,
        links,
        trap_exit,
        status,
        heap_size,
        stack_size,
        reductions
    ]),
    {dictionary, Dictionary} = lists:keyfind(dictionary, 1, Info),
    [
        {pid, self()},
        lists:keyfind(registered_name, 1, Info),
        {process_label, get(?LABEL)},
        {error_info, {Class, Reason, Stack}},
        {ancestors, ancestors()},
        lists:keyfind(message_queue_len, 1, Info),
        lists:keyfind(messages, 1, Info),
        lists:keyfind(links, 1, Info),
        {dictionary, [Entry || {Key, _} = Entry <- Dictionary, not internal_key(Key)]},
        lists:keyfind(trap_exit, 1, Info),
        lists:keyfind(status, 1, Info),
        lists:keyfind(heap_size, 1, Info),
        lists:keyfind(stack_size, 1, Info),
        lists:keyfind(reductions, 1, Info)
    ].

internal_key(Key) -> Key =:= ?ANCESTORS orelse Key =:= ?INITIAL_CALL orelse Key =:= ?LABEL.

%% The linked processes that do not trap exits, and theirs, up to 15: the processes the crash may take along.
neighbours() ->
    {_, Visited} = visit(adjacent(self()), {15, [self()]}),
    Reports = [neighbour_report(Pid) || Pid <- lists:delete(self(), Visited)],
    [{neighbour, Report} || Report <- Reports, Report =/= undefined].

visit([Pid | Pids], {N, Visited}) when N > 0 ->
    case lists:member(Pid, Visited) of
        false -> visit(adjacent(Pid), visit(Pids, {N - 1, [Pid | Visited]}));
        true -> visit(Pids, {N, Visited})
    end;
visit(_, Acc) ->
    Acc.

adjacent(Pid) ->
    case is_pid(Pid) andalso node(Pid) =:= node() andalso process_info(Pid, links) of
        {links, Links} -> [P || P <- Links, is_pid(P), not traps_exits(P)];
        _ -> []
    end.

traps_exits(Pid) -> node(Pid) =/= node() orelse process_info(Pid, trap_exit) =/= {trap_exit, false}.

neighbour_report(Pid) ->
    Info = process_info(Pid, [
        registered_name,
        dictionary,
        message_queue_len,
        links,
        trap_exit,
        status,
        heap_size,
        stack_size,
        reductions
    ]),
    case Info of
        undefined ->
            undefined;
        _ ->
            {dictionary, Dictionary} = lists:keyfind(dictionary, 1, Info),
            [
                {pid, Pid},
                lists:keyfind(registered_name, 1, Info),
                {process_label, proplists:get_value(?LABEL, Dictionary)},
                {initial_call, neighbour_call(Dictionary)},
                {ancestors, proplists:get_value(?ANCESTORS, Dictionary, [])}
                | [
                    lists:keyfind(Key, 1, Info)
                 || Key <- [
                        message_queue_len,
                        links,
                        trap_exit,
                        status,
                        heap_size,
                        stack_size,
                        reductions
                    ]
                ]
            ]
    end.

neighbour_call(Dictionary) ->
    case initial_call_in(Dictionary) of
        {M, F, A} -> {M, F, dummy_args(A)};
        false -> undefined
    end.

%% Crash report text.

report_cb(#{label := {proc_lib, crash}, report := Report}, Options) ->
    format_crash(
        Report,
        maps:merge(
            #{
                chars_limit => unlimited,
                depth => unlimited,
                single_line => false,
                encoding => utf8
            },
            Options
        )
    ).

format(Report) -> format(Report, latin1).

format(Report, Encoding) -> format(Report, Encoding, unlimited).

format(Report, Encoding, Depth) ->
    format_crash(Report, #{
        chars_limit => unlimited, depth => Depth, encoding => Encoding, single_line => false
    }).

format_crash([Own, Links], #{single_line := Single} = Options) ->
    Indent = indent(Single),
    Separator = newline(Single, "; "),
    io_lib:format("~scrasher:" ++ newline(Single, " ") ++ "~ts" ++ Separator ++ "~ts", [
        Indent, format_own(Own, Indent ++ Indent, Options), format_links(Links, Indent, Options)
    ]).

format_own(Own, Indent, #{single_line := Single} = Options) ->
    case lists:splitwith(fun(Item) -> element(1, Item) =/= error_info end, Own) of
        {Before, [{error_info, {Class, Reason, Stack}} | After]} ->
            lists:join(newline(Single, ", "), [
                format_items(Before, Indent, Options),
                format_exception(Class, Reason, Stack, Options),
                format_items(After, Indent, Options)
            ]);
        _ ->
            format_items(Own, Indent, Options)
    end.

format_links(Links, Indent, #{single_line := Single} = Options) ->
    Reports = [
        [
            Indent ++ Indent,
            "neighbour:",
            newline(Single, " "),
            format_items(Report, ["  ", Indent ++ Indent], Options)
        ]
     || {neighbour, Report} <- Links
    ],
    [Indent, "neighbours:", newline(Single, " "), lists:join(newline(Single, "; "), Reports)].

format_items(Items, Indent, #{single_line := Single} = Options) ->
    lists:join(newline(Single, ", "), [
        format_item(Item, Indent, Options)
     || Item <- Items, shown(Item)
    ]).

shown({process_label, undefined}) -> false;
shown({_, _}) -> true;
shown(_) -> false.

format_item({initial_call, {M, F, Args}}, Indent, #{single_line := Single}) when is_list(Args) ->
    [
        line_indent(Single, Indent),
        "initial call: ",
        io_lib:write_atom(M),
        $:,
        io_lib:write_atom(F),
        $/,
        integer_to_list(length(Args))
    ];
format_item({Tag, Data}, Indent, #{single_line := Single} = Options) ->
    {P, Depth} = directive(Options),
    io_lib:format("~s~" ++ width(Single) ++ "p: ~" ++ width(Single) ++ ".18" ++ P, [
        line_indent(Single, Indent),
        Tag,
        Data
        | Depth
    ]).

%% The exception of a crash report, in the layout of erl_error's exception texts.
format_exception(Class, Reason, Stack, #{single_line := true} = Options) ->
    {P, Depth} = directive(Options),
    [atom_to_list(Class), ": ", io_lib:format("~0" ++ P, [{Reason, Stack} | Depth])];
format_exception(Class, Reason, Stack, Options) ->
    {P, Depth} = directive(Options),
    Explained = io_lib:format("~ts", [explain(Class, Reason, Stack, P, Depth)]),
    [
        "    exception ",
        atom_to_list(Class),
        ": ",
        Explained
        | [frame(Frame, First) || {Frame, First} <- numbered(Stack)]
    ].

%% The stack frames, the first marked true.
numbered([First | Rest]) -> [{First, true} | [{Frame, false} || Frame <- Rest]];
numbered(_) -> [].

explain(error, badarg, _, _, _) ->
    "bad argument";
explain(error, badarith, _, _, _) ->
    "an error occurred when evaluating an arithmetic expression";
explain(error, {badmatch, V}, _, P, D) ->
    io_lib:format("no match of right hand side value ~" ++ P, [V | D]);
explain(error, function_clause, [{M, F, Args, _} | _], P, D) when is_list(Args) ->
    io_lib:format("no function clause matching ~tw:~tw(~ts)", [
        M,
        F,
        lists:join(",", [
            io_lib:format(
                "~" ++ P,
                [A | D]
            )
         || A <- Args
        ])
    ]);
explain(error, function_clause, _, _, _) ->
    "no function clause matching";
explain(error, {case_clause, V}, _, P, D) ->
    io_lib:format("no case clause matching ~" ++ P, [V | D]);
explain(error, if_clause, _, _, _) ->
    "no true branch found when evaluating an if expression";
explain(error, {try_clause, V}, _, P, D) ->
    io_lib:format("no try clause matching ~" ++ P, [V | D]);
explain(error, undef, [{M, F, A, _} | _], _, _) when is_list(A) ->
    io_lib:format("undefined function ~tw:~tw/~w", [M, F, length(A)]);
explain(error, undef, [{M, F, A, _} | _], _, _) ->
    io_lib:format("undefined function ~tw:~tw/~w", [M, F, A]);
explain(error, {badfun, F}, _, P, D) ->
    io_lib:format("bad function ~" ++ P, [F | D]);
explain(error, {badarity, {F, Args}}, _, P, D) when is_list(Args) ->
    io_lib:format("~" ++ P ++ " called with ~w arguments", [F | D] ++ [length(Args)]);
explain(error, timeout_value, _, _, _) ->
    "bad receive timeout value";
explain(error, noproc, _, _, _) ->
    "no such process or port";
explain(error, system_limit, _, _, _) ->
    "a system limit has been reached";
explain(_, Reason, _, P, D) ->
    io_lib:format("~" ++ P, [Reason | D]).

frame({M, F, A, Location}, First) ->
    Arity =
        case A of
            Args when is_list(Args) -> length(Args);
            N -> N
        end,
    Where =
        case {proplists:get_value(file, Location), proplists:get_value(line, Location)} of
            {undefined, _} -> "";
            {File, undefined} -> io_lib:format(" (~ts)", [File]);
            {File, Line} -> io_lib:format(" (~ts, line ~w)", [File, Line])
        end,
    Lead =
        case First of
            true -> "in function  ";
            false -> "in call from "
        end,
    io_lib:format("~n      ~s~tw:~tw/~w~ts", [Lead, M, F, Arity, Where]);
frame(_, _) ->
    "".

directive(#{encoding := Encoding, depth := unlimited}) -> {modifier(Encoding) ++ "p", []};
directive(#{encoding := Encoding, depth := Depth}) -> {modifier(Encoding) ++ "P", [Depth]}.

modifier(latin1) -> "";
modifier(_) -> "t".

indent(true) -> "";
indent(false) -> "  ".

line_indent(true, _) -> "";
line_indent(false, Indent) -> Indent.

width(true) -> "0";
width(false) -> "".

newline(true, Else) -> Else;
newline(false, _) -> "\n".
