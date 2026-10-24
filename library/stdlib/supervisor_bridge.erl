%% Clause's supervisor_bridge (plan 21 step LIB21, docs/library.md): a gen_server standing in a supervision tree
%% for a process the callback module starts itself. It links to that process, ends with it, and calls the module's
%% terminate/2 when it is told to stop first.
-module(supervisor_bridge).
-behaviour(gen_server).
-export([
    code_change/3,
    format_log/1,
    format_log/2,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    init/1,
    start_link/2,
    start_link/3,
    terminate/2
]).

-include_lib("kernel/include/logger.hrl").

-callback init(Args :: term()) ->
    {ok, Pid :: pid(), State :: term()} | ignore | {error, Error :: term()}.
-callback terminate(Reason :: shutdown | term(), State :: term()) -> Ignored :: term().

%% Created for LIB21: the bridge's state: the callback module, the process it started (undefined once it ended),
%% the module's state and the bridge's name for reports.
-record(state, {mod, pid, child_state, name}).

start_link(Module, Args) -> gen_server:start_link(supervisor_bridge, [Module, Args, self], []).

start_link(Name, Module, Args) ->
    gen_server:start_link(Name, supervisor_bridge, [Module, Args, Name], []).

init([Module, Args, Name0]) ->
    process_flag(trap_exit, true),
    Name =
        case Name0 of
            self -> {self(), Module};
            _ -> Name0
        end,
    case Module:init(Args) of
        {ok, Pid, ChildState} when is_pid(Pid) ->
            link(Pid),
            log(info, progress, [
                {supervisor, Name}, {started, [{pid, Pid}, {mfa, {Module, init, [Args]}}]}
            ]),
            {ok, #state{mod = Module, pid = Pid, child_state = ChildState, name = Name}};
        ignore ->
            ignore;
        {error, Reason} ->
            {stop, Reason}
    end.

handle_call(which_children, _, State) -> {reply, [], State};
handle_call(_, _, State) -> {reply, {error, badcall}, State}.

handle_cast(_, State) -> {noreply, State}.

handle_info({'EXIT', Pid, Reason}, #state{pid = Pid} = State) ->
    case Reason of
        normal -> ok;
        shutdown -> ok;
        {shutdown, _} -> ok;
        _ -> report_error(child_terminated, Reason, State)
    end,
    {stop, Reason, State#state{pid = undefined}};
handle_info(_, State) ->
    {noreply, State}.

terminate(_, #state{pid = undefined}) ->
    ok;
terminate(Reason, #state{mod = Module, child_state = ChildState}) ->
    Module:terminate(Reason, ChildState).

code_change(_, State, _) -> {ok, State}.

report_error(Context, Reason, #state{name = Name, pid = Pid, mod = Module}) ->
    log(error, error, [
        {supervisor, Name},
        {errorContext, Context},
        {reason, Reason},
        {offender, [{pid, Pid}, {mod, Module}]}
    ]).

log(Level, Kind, Report) ->
    {Title, Tag, Type} =
        case Kind of
            progress -> {"PROGRESS REPORT", info_report, progress};
            error -> {"SUPERVISOR REPORT", error_report, supervisor_report}
        end,
    ?LOG(Level, #{label => {supervisor, Kind}, report => Report}, #{
        domain => [otp, sasl],
        report_cb => fun supervisor_bridge:format_log/2,
        logger_formatter => #{title => Title},
        error_logger => #{tag => Tag, type => Type, report_cb => fun supervisor_bridge:format_log/1}
    }).

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
    #{label := {supervisor, progress}, report := [{supervisor, Name}, {started, Child}]},
    #{single_line := true, depth := Depth} = Options
) ->
    {pid, Pid} = lists:keyfind(pid, 1, Child),
    {mfa, MFA} = lists:keyfind(mfa, 1, Child),
    P = p(Options),
    {
        "Supervisor: " ++ P ++ ". ~s pid=~w,mfa=" ++ P ++ ".",
        depth_args(Depth, [Name]) ++ ["Started:", Pid] ++ depth_args(Depth, [MFA])
    };
format_log_single(
    #{
        label := {supervisor, _},
        report := [
            {supervisor, Name},
            {errorContext, Context},
            {reason, Reason},
            {offender, Child}
        ]
    },
    #{single_line := true, depth := Depth} = Options
) ->
    {pid, Pid} = lists:keyfind(pid, 1, Child),
    {mod, Module} = lists:keyfind(mod, 1, Child),
    P = p(Options),
    {
        "Supervisor: " ++ P ++ ". Context: " ++ P ++ ". Reason: " ++ P ++ ". ~s pid=~w,mod=~w.",
        depth_args(Depth, [Name, Context, Reason]) ++ ["Offender:", Pid, Module]
    };
format_log_single(Report, Options) ->
    format_log_multi(Report, Options).

format_log_multi(
    #{label := {supervisor, progress}, report := [{supervisor, Name}, {started, Child}]},
    #{depth := Depth} = Options
) ->
    P = p(Options),
    {"    supervisor: " ++ P ++ "~n    started: " ++ P ++ "~n", depth_args(Depth, [Name, Child])};
format_log_multi(
    #{
        label := {supervisor, _},
        report := [
            {supervisor, Name},
            {errorContext, Context},
            {reason, Reason},
            {offender, Child}
        ]
    },
    #{depth := Depth} = Options
) ->
    P = p(Options),
    {
        "    supervisor: " ++ P ++ "~n    errorContext: " ++ P ++ "~n    reason: " ++ P ++
            "~n    offender: " ++ P ++
            "~n",
        depth_args(Depth, [Name, Context, Reason, Child])
    }.

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
