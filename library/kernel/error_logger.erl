%% Clause's error_logger (plan 21 step LIB23, docs/libraries/library.md): the old error logging API over logger. Its messages
%% and reports become log events with OTP's error_logger metadata; report handlers are gen_event handlers of the
%% error_logger event manager, which the logger handler `error_logger` feeds while any is installed.
-module(error_logger).
-export([
    add_report_handler/1,
    add_report_handler/2,
    adding_handler/1,
    delete_report_handler/1,
    error_info/1,
    error_msg/1,
    error_msg/2,
    error_report/1,
    error_report/2,
    format/2,
    get_format_depth/0,
    info_msg/1,
    info_msg/2,
    info_report/1,
    info_report/2,
    limit_term/1,
    log/2,
    logfile/1,
    removing_handler/1,
    start/0,
    start_link/0,
    stop/0,
    tty/1,
    warning_map/0,
    warning_msg/1,
    warning_msg/2,
    warning_report/1,
    warning_report/2,
    which_report_handlers/0
]).

%% The event manager.

start() ->
    case whereis(?MODULE) of
        undefined ->
            case gen_event:start({local, ?MODULE}) of
                {ok, _} -> ok;
                {error, {already_started, _}} -> ok;
                Error -> Error
            end;
        _ ->
            ok
    end.

start_link() -> gen_event:start_link({local, ?MODULE}, [{spawn_opt, [{message_queue_data, off_heap}]}]).

stop() ->
    case whereis(?MODULE) of
        undefined ->
            ok;
        _ ->
            _ = gen_event:stop(?MODULE, {shutdown, stopped}, infinity),
            ok
    end.

%% The logger handler that passes events to the event manager.

adding_handler(#{id := ?MODULE} = Config) ->
    case start() of
        ok -> {ok, Config};
        Error -> Error
    end.

removing_handler(#{id := ?MODULE}) ->
    stop(),
    ok.

log(#{level := Level, msg := Msg, meta := Meta}, _) -> forward(Level, Msg, Meta).

forward(Level, {report, Msg}, #{?MODULE := #{tag := Tag, type := Type}} = Meta) ->
    Report =
        case Msg of
            #{label := _, report := R} -> R;
            _ -> Msg
        end,
    notify(Level, Tag, Type, Report, Meta);
forward(Level, {report, Msg}, #{?MODULE := #{tag := Tag}} = Meta) ->
    {Format, Args} =
        case Msg of
            #{label := _, format := F, args := A} -> {F, A};
            _ -> report_format(Msg, Meta)
        end,
    notify(Level, Tag, Format, Args, Meta);
forward(Level, {Format, Args}, #{?MODULE := #{tag := Tag}} = Meta) when is_list(Format), is_list(Args) ->
    notify(Level, Tag, Format, Args, Meta);
forward(_, _, _) ->
    ok.

%% A report without its own format through the report callback of the metadata.
report_format(Msg, Meta) ->
    case report_cb(Meta) of
        Callback when is_function(Callback, 1) ->
            try Callback(Msg) of
                {F, A} when is_list(F), is_list(A) -> {F, A};
                Other -> {"REPORT_CB ERROR: ~tp; Returned: ~tp", [Msg, Other]}
            catch
                C:R:S -> {"REPORT_CB CRASH: ~tp; Reason: ~tp", [Msg, {C, R, S}]}
            end;
        Callback when is_function(Callback, 2) ->
            try Callback(Msg, #{depth => get_format_depth(), chars_limit => unlimited, single_line => false}) of
                Text when is_list(Text); is_binary(Text) -> {"~ts", [Text]};
                Other -> {"REPORT_CB ERROR: ~tp; Returned: ~tp", [Msg, Other]}
            catch
                C:R -> {"REPORT_CB CRASH: ~tp; Reason: ~tp", [Msg, {C, R}]}
            end
    end.

report_cb(#{?MODULE := #{report_cb := Callback}}) -> Callback;
report_cb(#{report_cb := Callback}) -> Callback;
report_cb(_) -> fun logger:format_report/1.

%% Warnings may be mapped to errors or infos (warning_map/0).
notify(Level, Tag0, FormatOrType0, ArgsOrReport, #{pid := Pid0, gl := GroupLeader, ?MODULE := Mine}) ->
    {Tag, FormatOrType} = mapped(Level, Tag0, FormatOrType0),
    Pid =
        case maps:get(emulator, Mine, false) of
            true -> emulator;
            _ -> Pid0
        end,
    gen_event:notify(?MODULE, {Tag, GroupLeader, {Pid, FormatOrType, ArgsOrReport}}).

mapped(warning, Tag, FormatOrType) ->
    case warning_map() of
        warning -> {Tag, FormatOrType};
        Level -> {mapped_tag(Level, Tag), mapped_type(Level, FormatOrType)}
    end;
mapped(_, Tag, FormatOrType) ->
    {Tag, FormatOrType}.

mapped_tag(error, warning_msg) -> error;
mapped_tag(error, warning_report) -> error_report;
mapped_tag(info, warning_msg) -> info_msg;
mapped_tag(info, warning_report) -> info_report;
mapped_tag(_, Tag) -> Tag.

mapped_type(error, std_warning) -> std_error;
mapped_type(info, std_warning) -> std_info;
mapped_type(_, Type) -> Type.

%% What warnings are reported as: warning, as the emulator's +W option sets by default.
warning_map() -> warning.

%% Messages and reports.

error_msg(Format) -> error_msg(Format, []).

error_msg(Format, Args) ->
    logger:log(error, #{label => {?MODULE, error_msg}, format => Format, args => Args}, meta(error)).

format(Format, Args) -> error_msg(Format, Args).

error_report(Report) -> error_report(std_error, Report).

error_report(Type, Report) ->
    logger:log(error, #{label => {?MODULE, error_report}, report => Report}, meta(error_report, Type)).

warning_report(Report) -> warning_report(std_warning, Report).

warning_report(Type, Report) ->
    logger:log(warning, #{label => {?MODULE, warning_report}, report => Report}, meta(warning_report, Type)).

warning_msg(Format) -> warning_msg(Format, []).

warning_msg(Format, Args) ->
    logger:log(warning, #{label => {?MODULE, warning_msg}, format => Format, args => Args}, meta(warning_msg)).

info_report(Report) -> info_report(std_info, Report).

info_report(Type, Report) ->
    logger:log(notice, #{label => {?MODULE, info_report}, report => Report}, meta(info_report, Type)).

info_msg(Format) -> info_msg(Format, []).

info_msg(Format, Args) ->
    logger:log(notice, #{label => {?MODULE, info_msg}, format => Format, args => Args}, meta(info_msg)).

error_info(Error) ->
    {Format, Args} =
        case is_text(Error) of
            true -> {Error, []};
            false -> {"~p", [Error]}
        end,
    logger:log(notice, Format, Args, #{?MODULE => #{tag => info, type => Error}, domain => [Error]}).

is_text(List) when is_list(List) ->
    case lists:flatten(List) of
        [] -> false;
        Flat -> io_lib:printable_list(Flat)
    end;
is_text(_) ->
    false.

meta(Tag) -> (report_meta())#{?MODULE => #{tag => Tag}}.

meta(Tag, Type) ->
    Meta = (report_meta())#{?MODULE => #{tag => Tag, type => Type}},
    case {Tag, Type} of
        {error_report, std_error} -> Meta;
        {info_report, std_info} -> Meta;
        {warning_report, std_warning} -> Meta;
        _ -> Meta#{domain => [Type]}
    end.

report_meta() -> #{report_cb => fun report_to_format/1}.

report_to_format(#{label := {?MODULE, _}, report := Report}) when is_map(Report) ->
    {"~tp\n", [Report]};
report_to_format(#{label := {?MODULE, _}, format := Format, args := Args}) ->
    try io_lib:scan_format(Format, Args) of
        _ -> {Format, Args}
    catch
        _:_ -> {"ERROR: ~tp - ~tp", [Format, Args]}
    end;
report_to_format(Term) ->
    logger:format_otp_report(Term).

%% Report handlers.

add_report_handler(Module) when is_atom(Module) -> add_report_handler(Module, []).

add_report_handler(Module, Args) when is_atom(Module) ->
    _ = logger:add_handler(?MODULE, ?MODULE, #{level => info, filter_default => log}),
    gen_event:add_handler(?MODULE, Module, Args).

delete_report_handler(Module) when is_atom(Module) ->
    case whereis(?MODULE) of
        Pid when is_pid(Pid) ->
            Return = gen_event:delete_handler(?MODULE, Module, []),
            case gen_event:which_handlers(?MODULE) of
                [] ->
                    _ = logger:remove_handler(?MODULE),
                    ok;
                _ ->
                    ok
            end,
            Return;
        _ ->
            ok
    end.

which_report_handlers() ->
    case whereis(?MODULE) of
        Pid when is_pid(Pid) -> gen_event:which_handlers(?MODULE);
        undefined -> []
    end.

logfile({open, File}) ->
    case lists:member(error_logger_file_h, which_report_handlers()) of
        true -> {error, allready_have_logfile};
        false -> add_report_handler(file_handler(), File)
    end;
logfile(close) ->
    case whereis(?MODULE) of
        Pid when is_pid(Pid) ->
            case gen_event:delete_handler(?MODULE, error_logger_file_h, normal) of
                {error, Reason} -> {error, Reason};
                _ -> ok
            end;
        _ ->
            {error, module_not_found}
    end;
logfile(filename) ->
    case whereis(?MODULE) of
        Pid when is_pid(Pid) ->
            case gen_event:call(?MODULE, error_logger_file_h, filename) of
                {error, _} -> {error, no_log_file};
                File -> File
            end;
        _ ->
            {error, no_log_file}
    end.

%% The file handler, named by a literal reference so programs that open a log file link it.
file_handler() ->
    _ = fun error_logger_file_h:init/1,
    error_logger_file_h.

%% Standard output reports: through the default handler when it writes there, else a handler of its own.
tty(true) ->
    _ =
        case logger:get_handler_config(default) of
            {ok, #{module := logger_std_h, config := #{type := standard_io}}} ->
                logger:remove_handler_filter(default, error_logger_tty_false);
            _ ->
                logger:add_handler(error_logger_tty_true, logger_std_h, #{
                    filter_default => stop,
                    filters => [
                        {remote_gl, {fun logger_filters:remote_gl/2, stop}},
                        {domain, {fun logger_filters:domain/2, {log, super, [otp]}}},
                        {no_domain, {fun logger_filters:domain/2, {log, undefined, []}}}
                    ],
                    formatter => {logger_formatter, #{legacy_header => true, single_line => false}},
                    config => #{type => standard_io}
                })
        end,
    ok;
tty(false) ->
    delete_report_handler(error_logger_tty_h),
    _ = logger:remove_handler(error_logger_tty_true),
    _ =
        case logger:get_handler_config(default) of
            {ok, #{module := logger_std_h, config := #{type := standard_io}}} ->
                logger:add_handler_filter(default, error_logger_tty_false, {fun(_, _) -> stop end, ok});
            _ ->
                ok
        end,
    ok.

limit_term(Term) ->
    case get_format_depth() of
        unlimited -> Term;
        Depth -> io_lib:limit_term(Term, Depth)
    end.

%% The kernel's error_logger_format_depth: at least 10, or unlimited.
get_format_depth() ->
    case clause_logger:app_env(kernel, error_logger_format_depth) of
        {ok, Depth} when is_integer(Depth) -> max(10, Depth);
        _ -> unlimited
    end.
