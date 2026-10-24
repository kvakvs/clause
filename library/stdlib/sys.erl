%% Clause's sys (plan 21 step LIB20, docs/library.md): the system message protocol of special processes and its
%% debug options. Requests are gen:call/3,4 calls labelled `system`; a process hands them to handle_system_msg/6,
%% which answers and continues in the module's system_continue/3 or system_terminate/4. The debug list keeps OTP's
%% shape ({trace, true}, {log, NLog}, {statistics, ...}, {log_to_file, Fd}, installed funs), which get_status/1,2
%% shows.
-module(sys).
-export([
    change_code/4,
    change_code/5,
    debug_options/1,
    get_debug/3,
    get_log/1,
    get_state/1,
    get_state/2,
    get_status/1,
    get_status/2,
    handle_debug/4,
    handle_system_msg/6,
    handle_system_msg/7,
    install/2,
    install/3,
    log/2,
    log/3,
    log_to_file/2,
    log_to_file/3,
    no_debug/1,
    no_debug/2,
    print_log/1,
    remove/2,
    remove/3,
    replace_state/2,
    replace_state/3,
    resume/1,
    resume/2,
    statistics/2,
    statistics/3,
    suspend/1,
    suspend/2,
    suspend_loop_hib/6,
    terminate/2,
    terminate/3,
    trace/2,
    trace/3
]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/library.md#runtime-module-replacements).
-include("sys.types.hrl").

-callback system_code_change(Misc, Module, OldVsn, Extra) -> {ok, NMisc} when
    Misc :: term(),
    OldVsn :: undefined | term(),
    Module :: atom(),
    Extra :: term(),
    NMisc :: term().
-callback system_continue(Parent, Debug, Misc) -> no_return() when
    Parent :: pid(), Debug :: [dbg_opt()], Misc :: term().
-callback system_get_state(Misc) -> {ok, State} when Misc :: term(), State :: term().
-callback system_replace_state(StateFun, Misc) -> {ok, NState, NMisc} when
    Misc :: term(), NState :: term(), NMisc :: term(), StateFun :: fun((State :: term()) -> NState).
-callback system_terminate(Reason, Parent, Debug, Misc) -> no_return() when
    Reason :: term(), Parent :: pid(), Debug :: [dbg_opt()], Misc :: term().

%% Requests to a process.

suspend(Name) -> request(Name, suspend).
suspend(Name, Timeout) -> request(Name, suspend, Timeout).

resume(Name) -> request(Name, resume).
resume(Name, Timeout) -> request(Name, resume, Timeout).

get_status(Name) -> request(Name, get_status).
get_status(Name, Timeout) -> request(Name, get_status, Timeout).

get_state(Name) -> result(request(Name, get_state)).
get_state(Name, Timeout) -> result(request(Name, get_state, Timeout)).

replace_state(Name, StateFun) -> result(request(Name, {replace_state, StateFun})).
replace_state(Name, StateFun, Timeout) -> result(request(Name, {replace_state, StateFun}, Timeout)).

change_code(Name, Module, Vsn, Extra) -> request(Name, {change_code, Module, Vsn, Extra}).
change_code(Name, Module, Vsn, Extra, Timeout) ->
    request(Name, {change_code, Module, Vsn, Extra}, Timeout).

terminate(Name, Reason) -> request(Name, {terminate, Reason}).
terminate(Name, Reason, Timeout) -> request(Name, {terminate, Reason}, Timeout).

log(Name, Flag) -> request(Name, {debug, {log, Flag}}).
log(Name, Flag, Timeout) -> request(Name, {debug, {log, Flag}}, Timeout).

trace(Name, Flag) -> request(Name, {debug, {trace, Flag}}).
trace(Name, Flag, Timeout) -> request(Name, {debug, {trace, Flag}}, Timeout).

log_to_file(Name, File) -> request(Name, {debug, {log_to_file, File}}).
log_to_file(Name, File, Timeout) -> request(Name, {debug, {log_to_file, File}}, Timeout).

statistics(Name, Flag) -> request(Name, {debug, {statistics, Flag}}).
statistics(Name, Flag, Timeout) -> request(Name, {debug, {statistics, Flag}}, Timeout).

no_debug(Name) -> request(Name, {debug, no_debug}).
no_debug(Name, Timeout) -> request(Name, {debug, no_debug}, Timeout).

install(Name, {Func, FuncState}) ->
    request(Name, {debug, {install, {Func, FuncState}}});
install(Name, {FuncId, Func, FuncState}) ->
    request(Name, {debug, {install, {FuncId, Func, FuncState}}}).

install(Name, {Func, FuncState}, Timeout) ->
    request(Name, {debug, {install, {Func, FuncState}}}, Timeout);
install(Name, {FuncId, Func, FuncState}, Timeout) ->
    request(Name, {debug, {install, {FuncId, Func, FuncState}}}, Timeout).

remove(Name, FuncOrId) -> request(Name, {debug, {remove, FuncOrId}}).
remove(Name, FuncOrId, Timeout) -> request(Name, {debug, {remove, FuncOrId}}, Timeout).

result({ok, Value}) -> Value;
result({error, Reason}) -> error(Reason).

%% A system request; a failed call exits with {Reason, {sys, F, Args}} naming the sys function.
request(Name, Request) ->
    try gen:call(Name, system, Request) of
        {ok, Result} -> Result
    catch
        exit:Reason -> exit({Reason, request_mfa(Name, Request, [])})
    end.

request(Name, Request, Timeout) ->
    try gen:call(Name, system, Request, Timeout) of
        {ok, Result} -> Result
    catch
        exit:Reason -> exit({Reason, request_mfa(Name, Request, [Timeout])})
    end.

request_mfa(Name, {debug, {Function, Argument}}, Extra) ->
    {sys, Function, [Name, Argument | Extra]};
request_mfa(Name, {change_code, M, Vsn, X}, Extra) ->
    {sys, change_code, [Name, M, Vsn, X | Extra]};
request_mfa(Name, {terminate, Reason}, Extra) ->
    {sys, terminate, [Name, Reason | Extra]};
request_mfa(Name, Request, Extra) ->
    {sys, Request, [Name | Extra]}.

%% The process side.

handle_system_msg(Msg, From, Parent, Module, Debug, Misc) ->
    handle_request(running, Msg, From, {Parent, Module, false}, Debug, Misc).

handle_system_msg(Msg, From, Parent, Module, Debug, Misc, Hibernate) ->
    handle_request(running, Msg, From, {Parent, Module, Hibernate}, Debug, Misc).

%% Answers one request, then continues, stays suspended or terminates. Owner is {Parent, Module, Hibernate}.
handle_request(SysState, Msg, From, {Parent, Module, Hibernate} = Owner, Debug, Misc) ->
    {Next, Reply, NewDebug, NewMisc} = command(SysState, Msg, Owner, Debug, Misc),
    _ = gen:reply(From, Reply),
    case Next of
        running -> Module:system_continue(Parent, NewDebug, NewMisc);
        suspended -> suspended(Parent, Module, NewDebug, NewMisc, Hibernate);
        {terminating, Reason} -> Module:system_terminate(Reason, Parent, NewDebug, NewMisc)
    end.

%% A suspended process answers only system requests and its parent's exit.
suspended(Parent, Module, Debug, Misc, true) ->
    suspend_loop_hib(suspended, Parent, Module, Debug, Misc, true);
suspended(Parent, Module, Debug, Misc, Hibernate) ->
    receive
        {system, From, Msg} ->
            handle_request(suspended, Msg, From, {Parent, Module, Hibernate}, Debug, Misc);
        {'EXIT', Parent, Reason} ->
            Module:system_terminate(Reason, Parent, Debug, Misc)
    end.

suspend_loop_hib(SysState, Parent, Module, Debug, Misc, Hibernate) ->
    receive
        {system, From, Msg} ->
            handle_request(SysState, Msg, From, {Parent, Module, Hibernate}, Debug, Misc);
        {'EXIT', Parent, Reason} ->
            Module:system_terminate(Reason, Parent, Debug, Misc)
    after 0 ->
        proc_lib:hibernate(?MODULE, suspend_loop_hib, [
            SysState, Parent, Module, Debug, Misc, Hibernate
        ])
    end.

%% {NextState, Reply, Debug, Misc} of one request.
command(_, suspend, _, Debug, Misc) ->
    {suspended, ok, Debug, Misc};
command(_, resume, _, Debug, Misc) ->
    {running, ok, Debug, Misc};
command(SysState, get_state, {_, Module, _}, Debug, Misc) ->
    {SysState, state_of(Module, Misc), Debug, Misc};
command(SysState, {replace_state, StateFun}, {_, Module, _}, Debug, Misc) ->
    {Reply, NewMisc} = replaced_state(StateFun, Module, Misc),
    {SysState, Reply, Debug, NewMisc};
command(SysState, get_status, {Parent, Module, _}, Debug, Misc) ->
    {SysState, status(SysState, Parent, Module, Debug, Misc), Debug, Misc};
command(SysState, {debug, What}, _, Debug, Misc) ->
    {Reply, NewDebug} = debug_command(What, Debug),
    {SysState, Reply, NewDebug, Misc};
command(_, {terminate, Reason}, _, Debug, Misc) ->
    {{terminating, Reason}, ok, Debug, Misc};
command(suspended, {change_code, Changed, Vsn, Extra}, {_, Module, _}, Debug, Misc) ->
    {Reply, NewMisc} = changed_code(Module, Changed, Vsn, Extra, Misc),
    {suspended, Reply, Debug, NewMisc};
command(SysState, Other, _, Debug, Misc) ->
    {SysState, {error, {unknown_system_msg, Other}}, Debug, Misc}.

state_of(Module, Misc) ->
    case erlang:function_exported(Module, system_get_state, 1) of
        true ->
            try Module:system_get_state(Misc) of
                {ok, _} = Result -> Result;
                Other -> {error, {callback_failed, {Module, system_get_state}, {bad_return, Other}}}
            catch
                Class:Reason ->
                    {error, {callback_failed, {Module, system_get_state}, {Class, Reason}}}
            end;
        false ->
            {ok, Misc}
    end.

replaced_state(StateFun, Module, Misc) ->
    case erlang:function_exported(Module, system_replace_state, 2) of
        true ->
            try Module:system_replace_state(StateFun, Misc) of
                {ok, State, NewMisc} ->
                    {{ok, State}, NewMisc};
                Other ->
                    {
                        {error,
                            {callback_failed, {Module, system_replace_state}, {bad_return, Other}}},
                        Misc
                    }
            catch
                Class:Reason ->
                    {
                        {error, {callback_failed, {Module, system_replace_state}, {Class, Reason}}},
                        Misc
                    }
            end;
        false ->
            try StateFun(Misc) of
                NewMisc -> {{ok, NewMisc}, NewMisc}
            catch
                Class:Reason -> {{error, {callback_failed, StateFun, {Class, Reason}}}, Misc}
            end
    end.

status(SysState, Parent, Module, Debug, Misc) ->
    Dictionary = get(),
    Shown =
        case erlang:function_exported(Module, format_status, 2) of
            true -> Module:format_status(normal, [Dictionary, SysState, Parent, Debug, Misc]);
            false -> Misc
        end,
    {status, self(), {module, Module}, [Dictionary, SysState, Parent, Debug, Shown]}.

changed_code(Module, Changed, Vsn, Extra, Misc) ->
    try Module:system_code_change(Misc, Changed, Vsn, Extra) of
        {ok, _} = Result -> Result;
        Other -> {{error, Other}, Misc}
    catch
        throw:{ok, _} = Result -> Result;
        throw:Other -> {{error, Other}, Misc};
        error:Reason:Stack -> {{error, {'EXIT', {Reason, Stack}}}, Misc};
        exit:Reason -> {{error, {'EXIT', Reason}}, Misc}
    end.

%% Debug options.

debug_command({trace, true}, Debug) ->
    {ok, install_debug(trace, true, Debug)};
debug_command({trace, false}, Debug) ->
    {ok, remove_debug(trace, Debug)};
debug_command({log, true}, Debug) ->
    {ok, install_debug(log, resized_log(10, get_debug(log, Debug, new_log(10))), Debug)};
debug_command({log, {true, N}}, Debug) when is_integer(N), N >= 1 ->
    {ok, install_debug(log, resized_log(N, get_debug(log, Debug, new_log(N))), Debug)};
debug_command({log, false}, Debug) ->
    {ok, remove_debug(log, Debug)};
debug_command({log, print}, Debug) ->
    print_log(Debug),
    {ok, Debug};
debug_command({log, get}, Debug) ->
    {{ok, get_log(Debug)}, Debug};
debug_command({log_to_file, false}, Debug) ->
    {ok, close_log_file(Debug)};
debug_command({log_to_file, File}, Debug) ->
    Closed = close_log_file(Debug),
    case file:open(File, [write, {encoding, utf8}]) of
        {ok, Fd} -> {ok, install_debug(log_to_file, Fd, Closed)};
        _ -> {{error, open_file}, Closed}
    end;
debug_command({statistics, true}, Debug) ->
    {ok, install_debug(statistics, new_statistics(), Debug)};
debug_command({statistics, false}, Debug) ->
    {ok, remove_debug(statistics, Debug)};
debug_command({statistics, get}, Debug) ->
    {{ok, statistics_of(get_debug(statistics, Debug, []))}, Debug};
debug_command(no_debug, Debug) ->
    _ = close_log_file(Debug),
    {ok, []};
debug_command({install, {Func, FuncState}}, Debug) ->
    {ok, install_debug(Func, FuncState, Debug)};
debug_command({install, {FuncId, Func, FuncState}}, Debug) ->
    {ok, install_debug(FuncId, {Func, FuncState}, Debug)};
debug_command({remove, FuncOrId}, Debug) ->
    {ok, remove_debug(FuncOrId, Debug)};
debug_command(_, Debug) ->
    {unknown_debug, Debug}.

debug_options(Options) -> lists:foldl(fun debug_option/2, [], Options).

debug_option(trace, Debug) ->
    install_debug(trace, true, Debug);
debug_option(log, Debug) ->
    install_debug(log, new_log(10), Debug);
debug_option({log, N}, Debug) when is_integer(N), N > 0 ->
    install_debug(log, new_log(N), Debug);
debug_option(statistics, Debug) ->
    install_debug(statistics, new_statistics(), Debug);
debug_option({log_to_file, File}, Debug) ->
    case file:open(File, [write, {encoding, utf8}]) of
        {ok, Fd} -> install_debug(log_to_file, Fd, Debug);
        _ -> Debug
    end;
debug_option({install, {Func, FuncState}}, Debug) ->
    install_debug(Func, FuncState, Debug);
debug_option({install, {FuncId, Func, FuncState}}, Debug) ->
    install_debug(FuncId, {Func, FuncState}, Debug);
debug_option(_, Debug) ->
    Debug.

%% An item is installed once: a second install keeps the first.
install_debug(Item, Data, Debug) ->
    case lists:keymember(Item, 1, Debug) of
        false -> [{Item, Data} | Debug];
        true -> Debug
    end.

remove_debug(Item, Debug) -> lists:keydelete(Item, 1, Debug).

get_debug(Item, Debug, Default) ->
    case lists:keyfind(Item, 1, Debug) of
        {Item, Data} -> Data;
        false -> Default
    end.

close_log_file(Debug) ->
    case get_debug(log_to_file, Debug, []) of
        [] ->
            Debug;
        Fd ->
            ok = file:close(Fd),
            remove_debug(log_to_file, Debug)
    end.

%% Feeds an event to every debug option; an installed fun returning done (or failing) is removed.
handle_debug([{trace, true} = Option | Rest], FormFunc, State, Event) ->
    FormFunc(standard_io, Event, State),
    [Option | handle_debug(Rest, FormFunc, State, Event)];
handle_debug([{log, Log} | Rest], FormFunc, State, Event) ->
    [{log, log_put({Event, State, FormFunc}, Log)} | handle_debug(Rest, FormFunc, State, Event)];
handle_debug([{log_to_file, Fd} = Option | Rest], FormFunc, State, Event) ->
    FormFunc(Fd, Event, State),
    [Option | handle_debug(Rest, FormFunc, State, Event)];
handle_debug([{statistics, Data} | Rest], FormFunc, State, Event) ->
    [{statistics, count(Event, Data)} | handle_debug(Rest, FormFunc, State, Event)];
handle_debug([{FuncId, {Func, FuncState}} | Rest], FormFunc, State, Event) ->
    Kept = installed(
        fun(NewFuncState) -> {FuncId, {Func, NewFuncState}} end, Func, FuncState, Event, State
    ),
    Kept ++ handle_debug(Rest, FormFunc, State, Event);
handle_debug([{Func, FuncState} | Rest], FormFunc, State, Event) ->
    Kept = installed(fun(NewFuncState) -> {Func, NewFuncState} end, Func, FuncState, Event, State),
    Kept ++ handle_debug(Rest, FormFunc, State, Event);
handle_debug([], _, _, _) ->
    [].

%% The option an installed fun leaves after one event: [] once it is done or fails; a thrown value counts as returned.
installed(Option, Func, FuncState, Event, State) ->
    try Func(FuncState, Event, State) of
        done -> [];
        NewFuncState -> [Option(NewFuncState)]
    catch
        throw:done -> [];
        throw:NewFuncState -> [Option(NewFuncState)];
        _:_ -> []
    end.

print_log(Debug) ->
    lists:foreach(
        fun({Event, State, FormFunc}) -> FormFunc(standard_io, Event, State) end,
        log_items(get_debug(log, Debug, new_log(10)))
    ).

get_log(Debug) -> [Event || {Event, _, _} <- log_items(get_debug(log, Debug, new_log(10)))].

%% The event log of N entries, kept as OTP keeps it: [Free | Reversed] while filling up, then [Reversed | Forward]
%% once full, so a log compares and prints as OTP's.
new_log(N) -> [N].

resized_log(N, Log) -> lists:foldl(fun log_put/2, new_log(N), log_items(Log)).

log_put(Item, [Reversed | Forward]) when is_list(Reversed) ->
    case Forward of
        [_ | Rest] ->
            [[Item | Reversed] | Rest];
        [] ->
            [_ | Rest] = lists:reverse(Reversed, [Item]),
            [[] | Rest]
    end;
log_put(Item, [1 | Reversed]) ->
    [[Item | Reversed]];
log_put(Item, [Free | Reversed]) ->
    [Free - 1, Item | Reversed].

log_items([[] | Forward]) -> Forward;
log_items([[_ | _] = Reversed | Forward]) -> Forward ++ lists:reverse(Reversed);
log_items([_ | Reversed]) -> lists:reverse(Reversed).

new_statistics() -> {erlang:localtime(), process_info(self(), reductions), 0, 0}.

statistics_of({Started, {reductions, Reductions}, In, Out}) ->
    {reductions, Now} = process_info(self(), reductions),
    [
        {start_time, Started},
        {current_time, erlang:localtime()},
        {reductions, Now - Reductions},
        {messages_in, In},
        {messages_out, Out}
    ];
statistics_of(_) ->
    no_statistics.

count({in, _}, {Time, Reductions, In, Out}) -> {Time, Reductions, In + 1, Out};
count({in, _, _}, {Time, Reductions, In, Out}) -> {Time, Reductions, In + 1, Out};
count({out, _, _}, {Time, Reductions, In, Out}) -> {Time, Reductions, In, Out + 1};
count({out, _, _, _}, {Time, Reductions, In, Out}) -> {Time, Reductions, In, Out + 1};
count(_, Data) -> Data.
