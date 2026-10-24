%% Clause's gen_fsm (plan 21 step LIB21, docs/library.md): the deprecated finite state machine behaviour, with OTP's
%% messages ('$gen_event', '$gen_sync_event', '$gen_all_state_event', '$gen_sync_all_state_event'), timers, results
%% (catch-based, as OTP's) and reports. It keeps a loop of its own, as OTP's does, rather than adapting gen_statem,
%% whose time-outs and messages differ.
-module(gen_fsm).
-export([
    cancel_timer/1,
    enter_loop/4,
    enter_loop/5,
    enter_loop/6,
    format_log/1,
    format_log/2,
    format_status/2,
    init_it/6,
    reply/2,
    send_all_state_event/2,
    send_event/2,
    send_event_after/2,
    start/3,
    start/4,
    start_link/3,
    start_link/4,
    start_timer/2,
    stop/1,
    stop/3,
    sync_send_all_state_event/2,
    sync_send_all_state_event/3,
    sync_send_event/2,
    sync_send_event/3,
    system_code_change/4,
    system_continue/3,
    system_get_state/1,
    system_replace_state/2,
    system_terminate/4,
    wake_hib/7
]).

-include_lib("kernel/include/logger.hrl").

-behaviour(sys).

-deprecated({'_', '_', "use the 'gen_statem' module instead"}).

-callback init(Args) -> Result when
    Args :: term(),
    Result ::
        {ok, StateName :: atom(), StateData :: term()}
        | {ok, StateName :: atom(), StateData :: term(), timeout() | hibernate}
        | {stop, Reason :: term()}
        | ignore.
-callback 'StateName'(Event, StateData) -> Result when
    Event :: timeout | term(),
    StateData :: term(),
    Result ::
        {next_state, NextStateName :: atom(), NewStateData :: term()}
        | {next_state, NextStateName :: atom(), NewStateData :: term(), timeout() | hibernate}
        | {stop, Reason :: term(), NewStateData :: term()}.
-callback 'StateName'(Event, From, StateData) -> Result when
    Event :: term(),
    From :: {pid(), Tag :: term()},
    StateData :: term(),
    Result ::
        {reply, Reply, NextStateName, NewStateData}
        | {reply, Reply, NextStateName, NewStateData, timeout() | hibernate}
        | {next_state, NextStateName, NewStateData}
        | {next_state, NextStateName, NewStateData, timeout() | hibernate}
        | {stop, Reason, Reply, NewStateData}
        | {stop, Reason, NewStateData},
    Reply :: term(),
    NextStateName :: atom(),
    NewStateData :: term(),
    Reason :: term().
-callback handle_event(Event :: term(), StateName :: atom(), StateData :: term()) ->
    {next_state, NextStateName :: atom(), NewStateData :: term()}
    | {next_state, NextStateName :: atom(), NewStateData :: term(), timeout() | hibernate}
    | {stop, Reason :: term(), NewStateData :: term()}.
-callback handle_sync_event(
    Event :: term(),
    From :: {pid(), Tag :: term()},
    StateName :: atom(),
    StateData :: term()
) ->
    {reply, Reply :: term(), NextStateName :: atom(), NewStateData :: term()}
    | {reply, Reply :: term(), NextStateName :: atom(), NewStateData :: term(), timeout() | hibernate}
    | {next_state, NextStateName :: atom(), NewStateData :: term()}
    | {next_state, NextStateName :: atom(), NewStateData :: term(), timeout() | hibernate}
    | {stop, Reason :: term(), Reply :: term(), NewStateData :: term()}
    | {stop, Reason :: term(), NewStateData :: term()}.
-callback handle_info(Info :: term(), StateName :: atom(), StateData :: term()) ->
    {next_state, NextStateName :: atom(), NewStateData :: term()}
    | {next_state, NextStateName :: atom(), NewStateData :: term(), timeout() | hibernate}
    | {stop, Reason :: normal | term(), NewStateData :: term()}.
-callback terminate(
    Reason :: normal | shutdown | {shutdown, term()} | term(),
    StateName :: atom(),
    StateData :: term()
) -> term().
-callback code_change(
    OldVsn :: term() | {down, term()}, StateName :: atom(), StateData :: term(), Extra :: term()
) ->
    {ok, NextStateName :: atom(), NewStateData :: term()}.
-callback format_status(Opt, nonempty_improper_list(PDict, [StateData])) -> Status when
    Opt :: normal | terminate,
    PDict :: [{Key :: term(), Value :: term()}],
    StateData :: term(),
    Status :: term().
-optional_callbacks([
    'StateName'/2, 'StateName'/3, handle_info/3, terminate/3, code_change/4, format_status/2
]).

-compile(nowarn_deprecated_catch).

%% Created for LIB21: what the loop keeps besides the state: the parent, the name, the callback module and the
%% idle time before hibernation.
-record(fsm, {parent, name, module, hibernate_after}).

start(Module, Args, Options) -> gen:start(?MODULE, nolink, Module, Args, Options).

start(Name, Module, Args, Options) -> gen:start(?MODULE, nolink, Name, Module, Args, Options).

start_link(Module, Args, Options) -> gen:start(?MODULE, link, Module, Args, Options).

start_link(Name, Module, Args, Options) -> gen:start(?MODULE, link, Name, Module, Args, Options).

stop(Name) -> gen:stop(Name).

stop(Name, Reason, Timeout) -> gen:stop(Name, Reason, Timeout).

send_event(Name, Event) -> send(Name, {'$gen_event', Event}).

send_all_state_event(Name, Event) -> send(Name, {'$gen_all_state_event', Event}).

send({global, Name}, Msg) ->
    catch (global_registry()):send(Name, Msg),
    ok;
send({via, Module, Name}, Msg) ->
    catch Module:send(Name, Msg),
    ok;
send(Name, Msg) ->
    Name ! Msg,
    ok.

%% Created for LIB21: global until LIB25, reached by a runtime name (gen.erl does the same).
global_registry() -> global.

sync_send_event(Name, Event) -> sync(Name, '$gen_sync_event', Event, [], sync_send_event).

sync_send_event(Name, Event, Timeout) ->
    sync(Name, '$gen_sync_event', Event, [Timeout], sync_send_event).

sync_send_all_state_event(Name, Event) ->
    sync(Name, '$gen_sync_all_state_event', Event, [], sync_send_all_state_event).

sync_send_all_state_event(Name, Event, Timeout) ->
    sync(Name, '$gen_sync_all_state_event', Event, [Timeout], sync_send_all_state_event).

sync(Name, Label, Event, Timeout, Function) ->
    case catch apply(gen, call, [Name, Label, Event | Timeout]) of
        {ok, Reply} -> Reply;
        {'EXIT', Reason} -> exit({Reason, {?MODULE, Function, [Name, Event | Timeout]}})
    end.

start_timer(Time, Msg) -> erlang:start_timer(Time, self(), {'$gen_timer', Msg}).

send_event_after(Time, Event) -> erlang:start_timer(Time, self(), {'$gen_event', Event}).

%% The time left, or 0 when the timer fired already (its message is removed), or false for no timer.
cancel_timer(Ref) ->
    case erlang:cancel_timer(Ref) of
        false ->
            receive
                {timeout, Ref, _} -> 0
            after 0 -> false
            end;
        Left ->
            Left
    end.

reply(From, Reply) -> gen:reply(From, Reply).

enter_loop(Module, Options, StateName, StateData) ->
    enter_loop(Module, Options, StateName, StateData, self(), infinity).

enter_loop(Module, Options, StateName, StateData, {Scope, _} = Name) when
    Scope =:= local; Scope =:= global
->
    enter_loop(Module, Options, StateName, StateData, Name, infinity);
enter_loop(Module, Options, StateName, StateData, {via, _, _} = Name) ->
    enter_loop(Module, Options, StateName, StateData, Name, infinity);
enter_loop(Module, Options, StateName, StateData, Timeout) ->
    enter_loop(Module, Options, StateName, StateData, self(), Timeout).

enter_loop(Module, Options, StateName, StateData, Server, Timeout) ->
    Name = gen:get_proc_name(Server),
    Fsm = #fsm{
        parent = gen:get_parent(),
        name = Name,
        module = Module,
        hibernate_after = gen:hibernate_after(Options)
    },
    loop(Fsm, StateName, StateData, Timeout, gen:debug_options(Name, Options)).

init_it(Starter, self, Name, Module, Args, Options) ->
    init_it(Starter, self(), Name, Module, Args, Options);
init_it(Starter, Parent, RegName, Module, Args, Options) ->
    Name = gen:name(RegName),
    Debug = gen:debug_options(Name, Options),
    Fsm = #fsm{
        parent = Parent,
        name = Name,
        module = Module,
        hibernate_after = gen:hibernate_after(Options)
    },
    case catch Module:init(Args) of
        {ok, StateName, StateData} ->
            proc_lib:init_ack(Starter, {ok, self()}),
            loop(Fsm, StateName, StateData, infinity, Debug);
        {ok, StateName, StateData, Timeout} ->
            proc_lib:init_ack(Starter, {ok, self()}),
            loop(Fsm, StateName, StateData, Timeout, Debug);
        {stop, Reason} ->
            gen:unregister_name(RegName),
            exit(Reason);
        ignore ->
            gen:unregister_name(RegName),
            proc_lib:init_fail(Starter, ignore, {exit, normal});
        {'EXIT', Reason} ->
            gen:unregister_name(RegName),
            exit(Reason);
        Else ->
            exit({bad_return_value, Else})
    end.

%% The loop; Timeout is infinity, a time (a timeout event when nothing arrives) or hibernate.
loop(#fsm{} = Fsm, StateName, StateData, hibernate, Debug) ->
    proc_lib:hibernate(?MODULE, wake_hib, [
        Fsm#fsm.parent,
        Fsm#fsm.name,
        StateName,
        StateData,
        Fsm#fsm.module,
        Fsm#fsm.hibernate_after,
        Debug
    ]);
loop(#fsm{hibernate_after = Idle} = Fsm, StateName, StateData, infinity, Debug) ->
    receive
        Msg -> decode(Msg, Fsm, StateName, StateData, infinity, Debug, false)
    after Idle -> loop(Fsm, StateName, StateData, hibernate, Debug)
    end;
loop(Fsm, StateName, StateData, Time, Debug) ->
    Msg =
        receive
            Input -> Input
        after Time -> {'$gen_event', timeout}
        end,
    decode(Msg, Fsm, StateName, StateData, Time, Debug, false).

wake_hib(Parent, Name, StateName, StateData, Module, Idle, Debug) ->
    Fsm = #fsm{parent = Parent, name = Name, module = Module, hibernate_after = Idle},
    receive
        Msg -> decode(Msg, Fsm, StateName, StateData, hibernate, Debug, true)
    end.

decode(
    Msg,
    #fsm{parent = Parent, name = Name, module = Module} = Fsm,
    StateName,
    StateData,
    Time,
    Debug,
    Hibernated
) ->
    case Msg of
        {system, From, Request} ->
            sys:handle_system_msg(
                Request,
                From,
                Parent,
                ?MODULE,
                Debug,
                [Name, StateName, StateData, Module, Time, Fsm#fsm.hibernate_after],
                Hibernated
            );
        {'EXIT', Parent, Reason} ->
            terminate(Reason, Fsm, undefined, Msg, StateName, StateData, Debug);
        _ when Debug =:= [] ->
            handle(Msg, Fsm, StateName, StateData, []);
        _ ->
            handle(
                Msg,
                Fsm,
                StateName,
                StateData,
                sys:handle_debug(
                    Debug,
                    fun print_event/3,
                    Name,
                    {in, Msg, StateName}
                )
            )
    end.

handle(Msg, #fsm{name = Name} = Fsm, StateName, StateData, Debug) ->
    From = from(Msg),
    case catch dispatch(Msg, Fsm#fsm.module, StateName, StateData) of
        {next_state, Next, NewData} ->
            loop(Fsm, Next, NewData, infinity, debug(Debug, Name, {noreply, Next}));
        {next_state, Next, NewData, Time} ->
            loop(Fsm, Next, NewData, Time, debug(Debug, Name, {noreply, Next}));
        {reply, Reply, Next, NewData} when From =/= undefined ->
            loop(Fsm, Next, NewData, infinity, reply(Name, From, Reply, Debug, Next));
        {reply, Reply, Next, NewData, Time} when From =/= undefined ->
            loop(Fsm, Next, NewData, Time, reply(Name, From, Reply, Debug, Next));
        {stop, Reason, NewData} ->
            terminate(Reason, Fsm, From, Msg, StateName, NewData, Debug);
        {stop, Reason, Reply, NewData} when From =/= undefined ->
            {'EXIT', R} = (catch terminate(Reason, Fsm, From, Msg, StateName, NewData, Debug)),
            _ = reply(Name, From, Reply, Debug, StateName),
            exit(R);
        {'EXIT', What} ->
            terminate(What, Fsm, From, Msg, StateName, StateData, Debug);
        Reply ->
            terminate({bad_return_value, Reply}, Fsm, From, Msg, StateName, StateData, Debug)
    end.

dispatch({'$gen_event', Event}, Module, StateName, StateData) ->
    Module:StateName(Event, StateData);
dispatch({'$gen_all_state_event', Event}, Module, StateName, StateData) ->
    Module:handle_event(Event, StateName, StateData);
dispatch({'$gen_sync_event', From, Event}, Module, StateName, StateData) ->
    Module:StateName(Event, From, StateData);
dispatch({'$gen_sync_all_state_event', From, Event}, Module, StateName, StateData) ->
    Module:handle_sync_event(Event, From, StateName, StateData);
dispatch({timeout, Ref, {'$gen_timer', Msg}}, Module, StateName, StateData) ->
    Module:StateName({timeout, Ref, Msg}, StateData);
dispatch({timeout, _, {'$gen_event', Event}}, Module, StateName, StateData) ->
    Module:StateName(Event, StateData);
dispatch(Info, Module, StateName, StateData) ->
    Module:handle_info(Info, StateName, StateData).

from({'$gen_sync_event', From, _}) -> From;
from({'$gen_sync_all_state_event', From, _}) -> From;
from(_) -> undefined.

reply(Name, From, Reply, Debug, StateName) ->
    reply(From, Reply),
    debug(Debug, Name, {out, Reply, From, StateName}).

debug([], _, _) -> [];
debug(Debug, Name, Event) -> sys:handle_debug(Debug, fun print_event/3, Name, Event).

%% Termination: terminate/3 of the module, then the error report unless the reason is normal, shutdown or
%% {shutdown, _}.
terminate(Reason, #fsm{module = Module} = Fsm, From, Msg, StateName, StateData, Debug) ->
    case erlang:function_exported(Module, terminate, 3) of
        true ->
            case catch Module:terminate(Reason, StateName, StateData) of
                {'EXIT', R} ->
                    error_info(
                        R,
                        Fsm,
                        From,
                        Msg,
                        StateName,
                        shown_data(terminate, Module, get(), StateData),
                        Debug
                    ),
                    exit(R);
                _ ->
                    ok
            end;
        false ->
            ok
    end,
    case Reason of
        normal ->
            exit(normal);
        shutdown ->
            exit(shutdown);
        {shutdown, _} ->
            exit(Reason);
        _ ->
            error_info(
                Reason,
                Fsm,
                From,
                Msg,
                StateName,
                shown_data(terminate, Module, get(), StateData),
                Debug
            ),
            exit(Reason)
    end.

error_info(Reason, #fsm{name = Name}, From, Msg, StateName, StateData, Debug) ->
    ?LOG(
        error,
        #{
            label => {gen_fsm, terminate},
            name => Name,
            last_message => Msg,
            state_name => StateName,
            state_data => StateData,
            log => sys:get_log(Debug),
            reason => Reason,
            client_info => client_info(From),
            process_label => proc_lib:get_label(self())
        },
        #{
            domain => [otp],
            report_cb => fun gen_fsm:format_log/2,
            error_logger => #{tag => error, report_cb => fun gen_fsm:format_log/1}
        }
    ).

%% The caller of a sync event that ended the machine: its name or pid and its stack (Clause keeps none).
client_info(undefined) ->
    undefined;
client_info({Pid, _}) ->
    client_info(Pid);
client_info(Pid) when is_pid(Pid), node(Pid) =:= node() ->
    case process_info(Pid, registered_name) of
        undefined -> {Pid, dead};
        {registered_name, Name} -> {Pid, {Name, []}};
        [] -> {Pid, {Pid, []}}
    end;
client_info(Pid) when is_pid(Pid) ->
    {Pid, remote}.

%% sys callbacks; Misc is [Name, StateName, StateData, Module, Time, HibernateAfter].

system_continue(Parent, Debug, [Name, StateName, StateData, Module, Time, Idle]) ->
    loop(
        #fsm{parent = Parent, name = Name, module = Module, hibernate_after = Idle},
        StateName,
        StateData,
        Time,
        Debug
    ).

system_terminate(Reason, Parent, Debug, [Name, StateName, StateData, Module, _, Idle]) ->
    Fsm = #fsm{parent = Parent, name = Name, module = Module, hibernate_after = Idle},
    terminate(Reason, Fsm, undefined, [], StateName, StateData, Debug).

system_code_change([Name, StateName, StateData, Module, Time, Idle], _, OldVsn, Extra) ->
    case catch Module:code_change(OldVsn, StateName, StateData, Extra) of
        {ok, NewStateName, NewStateData} ->
            {ok, [Name, NewStateName, NewStateData, Module, Time, Idle]};
        Else ->
            Else
    end.

system_get_state([_, StateName, StateData, _, _, _]) -> {ok, {StateName, StateData}}.

system_replace_state(StateFun, [Name, StateName, StateData, Module, Time, Idle]) ->
    {NewStateName, NewStateData} = Result = StateFun({StateName, StateData}),
    {ok, Result, [Name, NewStateName, NewStateData, Module, Time, Idle]}.

print_event(Dev, {in, Msg, StateName}, Name) ->
    case Msg of
        {'$gen_event', Event} ->
            io:format(Dev, "*DBG* ~tp got event ~tp in state ~tw~n", [Name, Event, StateName]);
        {'$gen_all_state_event', Event} ->
            io:format(Dev, "*DBG* ~tp got all_state_event ~tp in state ~tw~n", [
                Name, Event, StateName
            ]);
        {'$gen_sync_event', {From, _}, Event} ->
            io:format(Dev, "*DBG* ~tp got sync_event ~tp from ~tw in state ~tw~n", [
                Name, Event, From, StateName
            ]);
        {'$gen_sync_all_state_event', {From, _}, Event} ->
            io:format(Dev, "*DBG* ~tp got sync_all_state_event ~tp from ~tw in state ~tw~n", [
                Name,
                Event,
                From,
                StateName
            ]);
        {timeout, Ref, {'$gen_timer', Message}} ->
            io:format(Dev, "*DBG* ~tp got timer ~tp in state ~tw~n", [
                Name, {timeout, Ref, Message}, StateName
            ]);
        {timeout, _, {'$gen_event', Event}} ->
            io:format(Dev, "*DBG* ~tp got timer ~tp in state ~tw~n", [Name, Event, StateName]);
        _ ->
            io:format(Dev, "*DBG* ~tp got ~tp in state ~tw~n", [Name, Msg, StateName])
    end;
print_event(Dev, {out, Msg, {To, _}, StateName}, Name) ->
    io:format(Dev, "*DBG* ~tp sent ~tp to ~tw~n      and switched to state ~tw~n", [
        Name, Msg, To, StateName
    ]);
print_event(Dev, {noreply, StateName}, Name) ->
    io:format(Dev, "*DBG* ~tp switched to state ~tw~n", [Name, StateName]).

format_status(Opt, [Dictionary, SysState, Parent, Debug, [Name, StateName, StateData, Module, _, _]]) ->
    Header = gen:format_status_header("Status for state machine", Name),
    Specific =
        case shown_data(Opt, Module, Dictionary, StateData) of
            S when is_list(S) -> S;
            S -> [S]
        end,
    [
        {header, Header},
        {data, [
            {"Status", SysState},
            {"Parent", Parent},
            {"Logged events", sys:get_log(Debug)},
            {"StateName", StateName}
        ]}
        | Specific
    ].

%% The state data through the module's format_status/2; by default the data itself (terminate) or a data item.
shown_data(Opt, Module, Dictionary, StateData) ->
    Default =
        case Opt of
            terminate -> StateData;
            _ -> [{data, [{"StateData", StateData}]}]
        end,
    case erlang:function_exported(Module, format_status, 2) of
        true ->
            case catch Module:format_status(Opt, [Dictionary, StateData]) of
                {'EXIT', _} -> Default;
                Else -> Else
            end;
        false ->
            Default
    end.

%% Report text.

format_log(Report) ->
    format_log_multi(Report, #{
        chars_limit => unlimited, depth => unlimited, single_line => false, encoding => utf8
    }).

format_log(Report, Options0) ->
    Options = maps:merge(
        #{chars_limit => unlimited, depth => unlimited, single_line => false, encoding => utf8},
        Options0
    ),
    IoOptions =
        case Options of
            #{chars_limit := unlimited} -> [];
            #{chars_limit := Limit} -> [{chars_limit, Limit}]
        end,
    {Format, Args} = format_log_single(Report, Options),
    io_lib:format(Format, Args, IoOptions).

format_log_single(
    #{
        label := {gen_fsm, terminate},
        name := Name,
        last_message := Msg,
        state_name := StateName,
        state_data := StateData,
        log := Log,
        reason := Reason,
        client_info := Client,
        process_label := Label
    },
    #{single_line := true, depth := Depth} = Options
) ->
    P = p(Options),
    {ClientFormat, ClientArgs} = client_log_single(Client, P, Depth),
    Format =
        lists:append([
            "State machine ",
            P,
            " terminating",
            labelled(Label, ". Label: " ++ P),
            ". Reason: ",
            P,
            ". Last event: ",
            P,
            ". State: ",
            P,
            ". Data: ",
            P,
            unless_empty(Log, ". Log: " ++ P),
            "."
        ]),
    Args =
        [Name] ++ labelled(Label, [Label]) ++
            [fix_reason(Reason), message_args(Msg), StateName, StateData] ++
            unless_empty(Log, [Log]),
    {Format ++ ClientFormat, depth_args(Depth, Args) ++ ClientArgs};
format_log_single(
    #{label := {gen_fsm, no_handle_info}, module := Module, message := Msg},
    #{single_line := true, depth := Depth} = Options
) ->
    P = p(Options),
    {
        "Undefined handle_info in " ++ P ++ ". Unhandled message: " ++ P ++ ".",
        depth_args(Depth, [Module, Msg])
    };
format_log_single(Report, Options) ->
    format_log_multi(Report, Options).

format_log_multi(
    #{
        label := {gen_fsm, terminate},
        name := Name,
        last_message := Msg,
        state_name := StateName,
        state_data := StateData,
        log := Log,
        reason := Reason,
        client_info := Client,
        process_label := Label
    },
    #{depth := Depth} = Options
) ->
    P = p(Options),
    {ClientFormat, ClientArgs} = client_log(Client, P, Depth),
    Format =
        lists:append([
            "** State machine ",
            P,
            " terminating \n",
            labelled(Label, "** Process label == " ++ P ++ "~n"),
            message_text(Msg, P) ++ "** When State == ",
            P,
            "~n",
            "**      Data  == ",
            P,
            "~n",
            "** Reason for termination ==~n** ",
            P,
            "~n",
            unless_empty(Log, "** Log ==~n**" ++ P ++ "~n")
        ]),
    Args =
        [Name] ++ labelled(Label, [Label]) ++ message_list(Msg) ++
            [StateName, StateData, fix_reason(Reason)] ++
            unless_empty(Log, [Log]),
    {Format ++ ClientFormat, depth_args(Depth, Args) ++ ClientArgs};
format_log_multi(
    #{label := {gen_fsm, no_handle_info}, module := Module, message := Msg},
    #{depth := Depth} = Options
) ->
    Args =
        case Depth of
            unlimited -> [Module, Msg];
            _ -> [Module, Msg, Depth]
        end,
    {"** Undefined handle_info in ~p~n** Unhandled message: " ++ p(Options) ++ "~n", Args}.

message_text({'$gen_event', _}, P) ->
    "** Last event in was " ++ P ++ "~n";
message_text({'$gen_sync_event', _, _}, P) ->
    "** Last sync event in was " ++ P ++ " from ~tw~n";
message_text({'$gen_all_state_event', _}, P) ->
    "** Last event in was " ++ P ++ " (for all states)~n";
message_text({'$gen_sync_all_state_event', _, _}, P) ->
    "** Last sync event in was " ++ P ++ " (for all states) from " ++ P ++ "~n";
message_text({timeout, _, {'$gen_timer', _}}, P) ->
    "** Last timer event in was " ++ P ++ "~n";
message_text({timeout, _, {'$gen_event', _}}, P) ->
    "** Last timer event in was " ++ P ++ "~n";
message_text(_, P) ->
    "** Last message in was " ++ P ++ "~n".

message_list({'$gen_event', Event}) -> [Event];
message_list({'$gen_sync_event', {From, _}, Event}) -> [Event, From];
message_list({'$gen_all_state_event', Event}) -> [Event];
message_list({'$gen_sync_all_state_event', {From, _}, Event}) -> [Event, From];
message_list({timeout, Ref, {'$gen_timer', Msg}}) -> [{timeout, Ref, Msg}];
message_list({timeout, _, {'$gen_event', Event}}) -> [Event];
message_list(Msg) -> [Msg].

%% The single-line report shows the event list as one argument.
message_args(Msg) -> message_list(Msg).

labelled(undefined, _) -> [];
labelled(_, Value) -> Value.

unless_empty([], _) -> [];
unless_empty(_, Value) -> Value.

fix_reason({undef, [{M, F, A, L} | MFAs]} = Reason) ->
    case code:is_loaded(M) of
        false ->
            {'module could not be loaded', [{M, F, A, L} | MFAs]};
        _ ->
            case is_list(A) andalso erlang:function_exported(M, F, length(A)) of
                true -> Reason;
                false -> {'function not exported', [{M, F, A, L} | MFAs]}
            end
    end;
fix_reason(Reason) ->
    Reason.

client_log_single(undefined, _, _) ->
    {"", []};
client_log_single({Pid, dead}, _, _) ->
    {" Client ~0p is dead.", [Pid]};
client_log_single({Pid, remote}, _, _) ->
    {" Client ~0p is remote on node ~0p.", [Pid, node(Pid)]};
client_log_single({_, {Name, Stack}}, P, Depth) ->
    {
        " Client " ++ P ++ " stacktrace: " ++ P ++ ".",
        depth_args(Depth, [Name, lists:sublist(Stack, 4)])
    }.

client_log(undefined, _, _) ->
    {"", []};
client_log({Pid, dead}, _, _) ->
    {"** Client ~p is dead~n", [Pid]};
client_log({Pid, remote}, _, _) ->
    {"** Client ~p is remote on node ~p~n", [Pid, node(Pid)]};
client_log({_, {Name, Stack}}, P, Depth) ->
    {"** Client " ++ P ++ " stacktrace~n** " ++ P ++ "~n", depth_args(Depth, [Name, Stack])}.

depth_args(unlimited, Args) -> Args;
depth_args(Depth, Args) -> lists:append([[Arg, Depth] || Arg <- Args]).

p(#{single_line := Single, depth := Depth, encoding := Encoding}) ->
    "~" ++
        case Single of
            true -> "0";
            false -> ""
        end ++
        case Encoding of
            latin1 -> "";
            _ -> "t"
        end ++
        case Depth of
            unlimited -> "p";
            _ -> "P"
        end.
