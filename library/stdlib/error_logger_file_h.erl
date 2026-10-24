%% Clause's error_logger_file_h (plan 21 step LIB23, docs/library.md): the error_logger report handler behind
%% error_logger:logfile/1, writing error, warning and info messages and standard reports to a file in the old
%% error_logger text (=ERROR REPORT==== 24-Oct-2026::18:00:09 ===). Other events are ignored.
-module(error_logger_file_h).
-behaviour(gen_event).
-export([init/1, handle_event/2, handle_call/2, handle_info/2, terminate/2, code_change/3]).

%% Created for LIB23: the handler's state: the open file, its name, the handler to restore when the file closes, and
%% the format depth.
-record(st, {fd, filename, prev_handler, depth = unlimited}).

init({File, {error_logger, Buffered}}) ->
    case open(File, error_logger) of
        {ok, State} ->
            lists:foreach(fun(Event) -> write_event(State, Event) end, lists:reverse(Buffered)),
            {ok, State};
        Error ->
            Error
    end;
init(File) ->
    open(File, []).

open(File, Previous) ->
    process_flag(trap_exit, true),
    case file:open(File, [write, {encoding, utf8}]) of
        {ok, Fd} ->
            {ok, #st{fd = Fd, filename = File, prev_handler = Previous, depth = error_logger:get_format_depth()}};
        Error ->
            Error
    end.

handle_event({_, GroupLeader, _}, State) when node(GroupLeader) =/= node() ->
    {ok, State};
handle_event(Event, State) ->
    write_event(State, Event),
    {ok, State}.

handle_info({'EXIT', Fd, _}, #st{fd = Fd, prev_handler = []}) ->
    remove_handler;
handle_info({'EXIT', Fd, _}, #st{fd = Fd, prev_handler = Previous}) ->
    {swap_handler, install_prev, [], Previous, go_back};
handle_info(_, State) ->
    {ok, State}.

handle_call(filename, #st{filename = File} = State) -> {ok, File, State};
handle_call(_, State) -> {ok, {error, bad_query}, State}.

terminate(_, #st{fd = Fd}) -> file:close(Fd).

code_change(_, State, _) -> {ok, State}.

write_event(#st{fd = Fd} = State, Event) ->
    case parse(Event) of
        ignore ->
            ok;
        {Title, Pid, Parts} ->
            AtNode =
                case node(Pid) =/= node() of
                    true -> ["** at node ", atom_to_list(node(Pid)), " **\n"];
                    false -> []
                end,
            io:put_chars(Fd, [header(erlang:universaltime(), Title), AtNode, [body(State, P) || P <- Parts]])
    end.

body(State, {Format, Args}) ->
    try
        format(State, Format, Args)
    catch
        _:_ -> format(State, "ERROR: ~tp - ~tp\n", [Format, Args])
    end.

format(#st{depth = unlimited}, Format, Args) ->
    io_lib:format(Format, Args);
format(#st{depth = Depth}, Format, Args) ->
    io_lib:build_text([limited(Item, Depth) || Item <- io_lib:scan_format(Format, Args)]).

limited(#{control_char := C, args := Args} = M, Depth) when C =:= $p; C =:= $w ->
    M#{control_char := C - ($a - $A), args := Args ++ [Depth]};
limited(Item, _) ->
    Item.

parse({error, _, {Pid, Format, Args}}) -> {"ERROR REPORT", Pid, [{Format, Args}]};
parse({info_msg, _, {Pid, Format, Args}}) -> {"INFO REPORT", Pid, [{Format, Args}]};
parse({warning_msg, _, {Pid, Format, Args}}) -> {"WARNING REPORT", Pid, [{Format, Args}]};
parse({error_report, _, {Pid, std_error, Report}}) -> {"ERROR REPORT", Pid, report(Report)};
parse({info_report, _, {Pid, std_info, Report}}) -> {"INFO REPORT", Pid, report(Report)};
parse({warning_report, _, {Pid, std_warning, Report}}) -> {"WARNING REPORT", Pid, report(Report)};
parse(_) -> ignore.

report(Report) when is_list(Report) ->
    case lists:flatten(Report) of
        [] ->
            lines(Report);
        Flat ->
            case io_lib:printable_list(Flat) of
                true -> [{"~ts\n", [Report]}];
                false -> lines(Report)
            end
    end;
report(Report) ->
    [{"~tp\n", [Report]}].

lines(Report) ->
    [
        case Item of
            {Tag, Data} -> {"    ~tp: ~tp\n", [Tag, Data]};
            Data -> {"    ~tp\n", [Data]}
        end
     || Item <- Report
    ].

header(Universal, Title) ->
    {{Y, Mo, D}, {H, Mi, S}, Utc} =
        case utc_log() of
            true ->
                {Date, Clock} = Universal,
                {Date, Clock, "UTC "};
            _ ->
                {Date, Clock} = calendar:universal_time_to_local_time(Universal),
                {Date, Clock, ""}
        end,
    Month = element(Mo, {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}),
    io_lib:format("~n=~ts==== ~p-~s-~p::~2..0w:~2..0w:~2..0w ~s===~n", [Title, D, Month, Y, H, Mi, S, Utc]).

utc_log() ->
    case clause_logger:app_env(sasl, utc_log) of
        {ok, Value} ->
            Value;
        _ ->
            case clause_logger:app_env(stdlib, utc_log) of
                {ok, Value} -> Value;
                _ -> false
            end
    end.
