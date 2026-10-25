%% Clause's logger_disk_log_h (plan 21 step LIB52, docs/library.md): a logger handler writing formatted events to an
%% external halt or wrap disk_log, with OTP's configuration and checks (file, type, max_no_bytes and max_no_files set
%% once). Each handler has a process registered as logger_disk_log_h_<Id> that owns the log (opened with
%% linkto, notify and quiet); events are formatted in the logging process and logged synchronously, the log is synced
%% when the process goes idle and on the filesync_repeat_interval timer, and disk_log notifications and failed
%% writes are reported once per change, as OTP's.
-module(logger_disk_log_h).
-behaviour(logger_handler).
-export([filesync/1]).
-export([check_config/4, filesync/3, handle_info/3, init/2, reset_state/2, terminate/3, write/4]).
-export([adding_handler/1, changing_config/3, filter_config/1, log/2, removing_handler/1]).

-define(CALL_TIMEOUT, 5000).
-define(WRITE_ONCE, [type, file, max_no_files, max_no_bytes]).

% --- The logger_handler callbacks ---

adding_handler(Config) -> clause_logger_h:adding_handler(?MODULE, Config, fun start/2).

changing_config(SetOrUpdate, Old, New) -> clause_logger_h:changing_config(?MODULE, SetOrUpdate, Old, New).

removing_handler(#{config := #{olp := Pid}}) ->
    clause_logger_h:stop(Pid),
    ok.

filter_config(Config) -> clause_logger_h:filter_config(Config).

% Formats in the logging process, then logs through the handler's process.
log(Event, #{config := #{olp := Pid}} = Config) ->
    Bin = clause_logger_h:to_binary(Event, Config),
    case clause_logger_h:request(Pid, {log, Bin}) of
        ok -> ok;
        {error, Reason} -> erlang:error(Reason)
    end.

% Write buffered data to disk.
filesync(Name) when is_atom(Name) ->
    RegName = clause_logger_h:reg_name(?MODULE, Name),
    case whereis(RegName) of
        undefined -> exit({noproc, {gen_server, call, [RegName, filesync]}});
        Pid -> clause_logger_h:request(Pid, filesync)
    end;
filesync(Name) ->
    {error, {badarg, {filesync, [Name]}}}.

% The handler's own options: a new handler gets the defaults (a wrap log of 10 files of 1 MiB, or an unlimited halt
% log, named after the handler in the working directory); type, file and the sizes cannot change.
check_config(Name, set, undefined, HConfig0) ->
    checked(with_defaults(Name, HConfig0));
check_config(_Name, SetOrUpdate, OldHConfig, NewHConfig0) ->
    WriteOnce = maps:with(?WRITE_ONCE, OldHConfig),
    Default =
        case SetOrUpdate of
            set -> WriteOnce;
            update -> OldHConfig
        end,
    NewHConfig = maps:merge(Default, NewHConfig0),
    case maps:with(?WRITE_ONCE, NewHConfig) of
        WriteOnce ->
            checked(NewHConfig);
        Other ->
            {Old, New} = clause_logger_h:diff_maps(WriteOnce, Other),
            {error, {illegal_config_change, ?MODULE, Old, New}}
    end.

checked(HConfig) ->
    case [Option || Option <- maps:to_list(HConfig), not valid(Option)] of
        [] -> {ok, HConfig};
        [{Key, Value} | _] -> {error, {invalid_config, ?MODULE, #{Key => Value}}}
    end.

valid({file, File}) -> is_list(File);
valid({max_no_files, undefined}) -> true;
valid({max_no_files, N}) -> is_integer(N) andalso N > 0;
valid({max_no_bytes, infinity}) -> true;
valid({max_no_bytes, N}) -> is_integer(N) andalso N > 0;
valid({type, Type}) -> Type =:= wrap orelse Type =:= halt;
valid(_) -> false.

with_defaults(Name, HConfig) ->
    Type = maps:get(type, HConfig, wrap),
    {NoFiles, NoBytes} =
        case Type of
            halt -> {undefined, infinity};
            _ -> {10, 1048576}
        end,
    {ok, Dir} = file:get_cwd(),
    Defaults = #{file => filename:join(Dir, Name), max_no_files => NoFiles, max_no_bytes => NoBytes, type => Type},
    maps:merge(Defaults, HConfig).

% --- The handler process ---

start(Name, HConfig) ->
    Starter = self(),
    Pid = spawn_link(fun() -> handler_init(Name, HConfig, Starter) end),
    receive
        {Pid, ok} -> {ok, Pid};
        {Pid, Error} -> Error
    after ?CALL_TIMEOUT -> {error, handler_process_not_started}
    end.

handler_init(Name, HConfig, Starter) ->
    case catch register(clause_logger_h:reg_name(?MODULE, Name), self()) of
        true ->
            case init(Name, HConfig) of
                {ok, HState} ->
                    Starter ! {self(), ok},
                    State = #{
                        name => Name,
                        handler_state => HState,
                        last_op => sync,
                        filesync_repeat_interval => maps:get(filesync_repeat_interval, HConfig)
                    },
                    handler_loop(repeated_filesync(State));
                Error ->
                    Starter ! {self(), Error}
            end;
        _ ->
            Starter ! {self(), {error, {already_started, whereis(clause_logger_h:reg_name(?MODULE, Name))}}}
    end.

handler_loop(#{name := Name, handler_state := HState} = State) ->
    receive
        {{log, Bin}, {From, Ref}} ->
            {_, HState1} = write(Name, sync, Bin, HState),
            From ! {Ref, ok},
            handler_loop(idle(State#{handler_state => HState1, last_op => write}));
        {filesync, {From, Ref}} ->
            {Result, HState1} = filesync(Name, sync, HState),
            From ! {Ref, Result},
            handler_loop(State#{handler_state => HState1, last_op => sync});
        {timeout, TRef, repeated_filesync} when TRef =:= map_get(sync_timer, State) ->
            handler_loop(repeated_filesync(synced(State)));
        {config_changed, Common, _Own} ->
            Interval = maps:get(filesync_repeat_interval, Common),
            handler_loop(repeated_filesync(cancel_filesync(State#{filesync_repeat_interval => Interval})));
        stop ->
            terminate(Name, normal, HState);
        Info ->
            handler_loop(State#{handler_state => handle_info(Name, Info, HState)})
    end.

% With no more requests waiting, the log is synced, as OTP's handler does when it goes idle.
idle(State) ->
    case erlang:process_info(self(), message_queue_len) of
        {message_queue_len, 0} -> synced(State);
        _ -> State
    end.

synced(#{last_op := sync} = State) ->
    State;
synced(#{name := Name, handler_state := HState} = State) ->
    {_, HState1} = filesync(Name, async, HState),
    State#{handler_state => HState1, last_op => sync}.

repeated_filesync(#{filesync_repeat_interval := Interval} = State) when is_integer(Interval) ->
    State#{sync_timer => erlang:start_timer(Interval, self(), repeated_filesync)};
repeated_filesync(State) ->
    State#{sync_timer => undefined}.

cancel_filesync(#{sync_timer := TRef} = State) when is_reference(TRef) ->
    _ = erlang:cancel_timer(TRef),
    State#{sync_timer => undefined};
cancel_filesync(State) ->
    State.

% --- OTP's handler callbacks over the disk log ---

init(Name, #{file := File, type := Type, max_no_bytes := MNB, max_no_files := MNF}) ->
    case open_disk_log(Name, File, Type, MNB, MNF) of
        ok ->
            {ok, #{
                log_opts => #{file => File, type => Type, max_no_bytes => MNB, max_no_files => MNF},
                prev_log_result => ok,
                prev_sync_result => ok,
                prev_disk_log_info => undefined
            }};
        Error ->
            Error
    end.

filesync(Name, _Mode, State) -> maybe_notify_error(Name, filesync, disk_log:sync(Name), prev_sync_result, State).

write(Name, Mode, Bin, State) -> maybe_notify_error(Name, log, disk_log_write(Name, Mode, Bin), prev_log_result, State).

reset_state(_Name, State) -> State#{prev_log_result => ok, prev_sync_result => ok, prev_disk_log_info => undefined}.

handle_info(Name, {disk_log, _Node, Log, {truncated, _NoLostItems} = Info}, State) ->
    maybe_notify_status(Name, Log, Info, State);
handle_info(Name, {disk_log, _Node, Log, {blocked_log, _Items} = Info}, State) ->
    maybe_notify_status(Name, Log, Info, State);
handle_info(Name, {disk_log, _Node, Log, full = Info}, State) ->
    maybe_notify_status(Name, Log, Info, State);
handle_info(Name, {disk_log, _Node, Log, {error_status, _Status} = Info}, State) ->
    maybe_notify_status(Name, Log, Info, State);
handle_info(_, _, State) ->
    State.

terminate(Name, _Reason, _State) ->
    _ = disk_log:sync(Name),
    _ = disk_log:close(Name),
    ok.

open_disk_log(Name, File, Type, MaxNoBytes, MaxNoFiles) ->
    case filelib:ensure_dir(File) of
        ok ->
            Size =
                case Type of
                    halt -> MaxNoBytes;
                    wrap -> {MaxNoBytes, MaxNoFiles}
                end,
            Opts = [
                {name, Name},
                {file, File},
                {size, Size},
                {type, Type},
                {linkto, self()},
                {repair, false},
                {format, external},
                {notify, true},
                {quiet, true},
                {mode, read_write}
            ],
            case disk_log:open(Opts) of
                {ok, Name} -> ok;
                {error, _Reason} = Error -> Error
            end;
        Error ->
            Error
    end.

disk_log_write(Name, sync, Bin) -> disk_log:blog(Name, Bin);
disk_log_write(Name, async, Bin) -> disk_log:balog(Name, Bin).

maybe_notify_error(Name, Op, Result, Key, #{log_opts := LogOpts} = State) ->
    {Result, error_notify_new({Name, Op, LogOpts, Result}, Result, Key, State)}.

maybe_notify_status(Name, Log, Info, State) ->
    error_notify_new({disk_log, Name, Log, Info}, Info, prev_disk_log_info, State).

% A result other than ok is reported when it differs from the last one.
error_notify_new(Term, What, Key, State) ->
    case What =:= ok orelse What =:= maps:get(Key, State) of
        true -> ok;
        false -> logger:internal_log(error, Term)
    end,
    State#{Key => What}.
