%% Clause's logger_std_h (plan 21 step LIB23, docs/library.md): the standard logger handler, writing formatted events
%% on standard_io, standard_error, an io device or a file, with OTP's configuration (overload options kept, file
%% modes, rotation by size with numbered and optionally gzip-compressed archives, file checks and repeated
%% filesync). Each handler has a process registered as logger_std_h_<Id> that owns the device or file; events are
%% formatted in the logging process and written synchronously, so they appear in order and before the program ends.
-module(logger_std_h).
-behaviour(logger_handler).
-export([
    adding_handler/1,
    changing_config/3,
    check_config/4,
    config_changed/3,
    filesync/1,
    filesync/3,
    filter_config/1,
    handle_info/3,
    init/2,
    log/2,
    removing_handler/1,
    reset_state/2,
    terminate/3,
    write/4
]).

-include("include/file.hrl").

-define(OLP_KEYS, [
    sync_mode_qlen,
    drop_mode_qlen,
    flush_qlen,
    burst_limit_enable,
    burst_limit_max_count,
    burst_limit_window_time,
    overload_kill_enable,
    overload_kill_qlen,
    overload_kill_mem_size,
    overload_kill_restart_after
]).
-define(COMMON_KEYS, [filesync_repeat_interval]).
-define(READ_ONLY_KEYS, [olp]).
-define(CALL_TIMEOUT, 5000).

%% The logger_handler callbacks.

adding_handler(#{id := Name} = Config) ->
    HConfig0 = maps:get(config, Config, #{}),
    case check_config(Name, set, undefined, maps:without(?OLP_KEYS ++ ?COMMON_KEYS, HConfig0)) of
        {ok, Own} ->
            Common = maps:merge(
                maps:merge(#{filesync_repeat_interval => 5000}, maps:with(?COMMON_KEYS, HConfig0)),
                maps:with(?COMMON_KEYS, Own)
            ),
            Olp = maps:merge(olp_defaults(), maps:with(?OLP_KEYS, HConfig0)),
            case {check_common(Common), check_olp(Olp)} of
                {ok, ok} ->
                    HConfig = maps:merge(maps:merge(Common, Own), Olp),
                    case start(Name, HConfig) of
                        {ok, Pid} -> {ok, Config#{config => HConfig#{olp => Pid}}};
                        Error -> Error
                    end;
                {{error, Faulty}, _} ->
                    {error, {invalid_config, ?MODULE, Faulty}};
                {_, Error} ->
                    Error
            end;
        Error ->
            Error
    end.

changing_config(SetOrUpdate, #{id := Name, config := OldHConfig}, New) ->
    NewHConfig0 = maps:get(config, New, #{}),
    Excluded = ?OLP_KEYS ++ ?COMMON_KEYS ++ ?READ_ONLY_KEYS,
    case check_config(Name, SetOrUpdate, maps:without(Excluded, OldHConfig), maps:without(Excluded, NewHConfig0)) of
        {ok, Own} ->
            CommonBase =
                case SetOrUpdate of
                    set -> #{filesync_repeat_interval => 5000};
                    update -> maps:with(?COMMON_KEYS, OldHConfig)
                end,
            Common = maps:merge(
                maps:merge(CommonBase, maps:with(?COMMON_KEYS, NewHConfig0)),
                maps:with(?COMMON_KEYS, Own)
            ),
            OlpBase =
                case SetOrUpdate of
                    set -> olp_defaults();
                    update -> maps:with(?OLP_KEYS, OldHConfig)
                end,
            Olp = maps:merge(OlpBase, maps:with(?OLP_KEYS, NewHConfig0)),
            case {check_common(Common), check_olp(Olp)} of
                {ok, ok} ->
                    Pid = maps:get(olp, OldHConfig),
                    Pid ! {config_changed, Common, Own},
                    HConfig = maps:merge(
                        maps:merge(maps:merge(Common, Own), maps:with(?READ_ONLY_KEYS, OldHConfig)),
                        Olp
                    ),
                    {ok, New#{config => HConfig}};
                {{error, Faulty}, _} ->
                    {error, {invalid_config, ?MODULE, Faulty}};
                {_, Error} ->
                    Error
            end;
        Error ->
            Error
    end.

removing_handler(#{config := #{olp := Pid}}) ->
    stop(Pid),
    ok.

filter_config(#{config := HConfig} = Config) -> Config#{config => maps:without(?READ_ONLY_KEYS, HConfig)}.

%% Formats in the logging process, then writes through the handler's process.
log(Event, #{config := #{olp := Pid}} = Config) ->
    Bin = to_binary(Event, Config),
    case request(Pid, {log, Bin}) of
        ok -> ok;
        {error, Reason} -> erlang:error(Reason)
    end.

filesync(Name) when is_atom(Name) ->
    RegName = reg_name(Name),
    case whereis(RegName) of
        undefined -> exit({noproc, {gen_server, call, [RegName, filesync]}});
        Pid -> request(Pid, filesync)
    end;
filesync(Name) ->
    {error, {badarg, {filesync, [Name]}}}.

%% Checks a handler configuration, its type, file and mode fixed once set.
check_config(Name, set, undefined, New) ->
    checked(with_defaults(Name, normalize(New)));
check_config(Name, SetOrUpdate, Old, New0) ->
    Fixed = maps:with([type, file, modes], Old),
    Base =
        case SetOrUpdate of
            set -> with_defaults(Name, Fixed);
            update -> Old
        end,
    New = maps:merge(Base, normalize(New0)),
    case maps:with([type, file, modes], New) of
        Fixed -> checked(New);
        Other -> {error, {illegal_config_change, ?MODULE, Fixed, Other}}
    end.

checked(HConfig) ->
    case first_invalid(maps:get(type, HConfig), maps:to_list(HConfig)) of
        ok -> {ok, fix_modes(HConfig)};
        {Key, Value} -> {error, {invalid_config, ?MODULE, #{Key => Value}}}
    end.

first_invalid(Type, [Item | Rest]) ->
    case valid(Type, Item) of
        true -> first_invalid(Type, Rest);
        false -> Item
    end;
first_invalid(_, []) ->
    ok.

valid(Type, {type, Type}) ->
    lists:member(Type, [standard_io, standard_error, file]) orelse
        is_tuple(Type) andalso tuple_size(Type) =:= 2 andalso element(1, Type) =:= device;
valid(file, {file, File}) ->
    is_list(File);
valid(file, {modes, Modes}) ->
    is_list(Modes);
valid(file, {max_no_bytes, Size}) ->
    is_integer(Size) andalso Size > 0 orelse Size =:= infinity;
valid(file, {max_no_files, Count}) ->
    is_integer(Count) andalso Count >= 0;
valid(file, {compress_on_rotate, Compress}) ->
    is_boolean(Compress);
valid(file, {file_check, Check}) ->
    is_integer(Check) andalso Check >= 0;
valid(_, _) ->
    false.

normalize(#{type := {file, File}} = HConfig) -> normalize(HConfig#{type => file, file => File});
normalize(#{type := {file, File, Modes}} = HConfig) -> normalize(HConfig#{type => file, file => File, modes => Modes});
normalize(#{file := File} = HConfig) -> HConfig#{file => filename:absname(File)};
normalize(HConfig) -> HConfig.

with_defaults(Name, #{type := Type} = HConfig) -> maps:merge(defaults(Name, Type), HConfig);
with_defaults(Name, #{file := _} = HConfig) -> maps:merge(defaults(Name, file), HConfig);
with_defaults(Name, HConfig) -> maps:merge(defaults(Name, standard_io), HConfig).

defaults(Name, file) ->
    #{
        type => file,
        file => filename:absname(atom_to_list(Name)),
        modes => [raw, append],
        file_check => 0,
        max_no_bytes => infinity,
        max_no_files => 0,
        compress_on_rotate => false
    };
defaults(_, Type) ->
    #{type => Type}.

%% File modes always write or append, raw and with delayed writes; other devices never sync on a timer.
fix_modes(#{modes := Modes} = HConfig) ->
    Writing =
        case [M || M <- Modes, lists:member(M, [write, append, exclusive])] of
            [] -> [append | Modes];
            _ -> Modes
        end,
    Raw =
        case lists:member(raw, Writing) of
            false -> [raw | Writing];
            true -> Writing
        end,
    Delayed =
        case
            lists:any(fun(M) -> M =:= delayed_write orelse is_tuple(M) andalso element(1, M) =:= delayed_write end, Raw)
        of
            true -> Raw;
            false -> [delayed_write | Raw]
        end,
    HConfig#{modes => Delayed};
fix_modes(HConfig) ->
    HConfig#{filesync_repeat_interval => no_repeat}.

check_common(Common) ->
    case
        [
            {K, V}
         || K := V <- Common, not (K =:= filesync_repeat_interval andalso (is_integer(V) orelse V =:= no_repeat))
        ]
    of
        [] -> ok;
        [{K, V} | _] -> {error, #{K => V}}
    end.

olp_defaults() ->
    #{
        sync_mode_qlen => 10,
        drop_mode_qlen => 200,
        flush_qlen => 1000,
        burst_limit_enable => true,
        burst_limit_max_count => 500,
        burst_limit_window_time => 1000,
        overload_kill_enable => false,
        overload_kill_qlen => 20000,
        overload_kill_mem_size => 3000000,
        overload_kill_restart_after => 5000
    }.

check_olp(Options) ->
    Bad = [
        {K, V}
     || K := V <- Options,
        not (is_integer(V) orelse
            (K =:= burst_limit_enable orelse K =:= overload_kill_enable) andalso is_boolean(V) orelse
            K =:= overload_kill_restart_after andalso V =:= infinity)
    ],
    case Bad of
        [{K, V} | _] ->
            {error, {invalid_olp_config, #{K => V}}};
        [] ->
            #{sync_mode_qlen := Sync, drop_mode_qlen := Drop, flush_qlen := Flush} = Options,
            case Drop > 1 andalso Sync =< Drop andalso Drop =< Flush of
                true -> ok;
                false -> {error, {invalid_olp_levels, maps:with([sync_mode_qlen, drop_mode_qlen, flush_qlen], Options)}}
            end
    end.

%% Formatting: the formatter's text as a binary; a failing formatter falls back to the default one.
to_binary(#{msg := {report, _}, meta := Meta} = Event, Config) when not is_map_key(report_cb, Meta) ->
    binary_text(Event#{meta => Meta#{report_cb => fun logger:format_otp_report/1}}, Config);
to_binary(Event, Config) ->
    binary_text(Event, Config).

binary_text(Event, Config) ->
    {Formatter, FConfig} = maps:get(
        formatter,
        Config,
        {logger_formatter, #{
            legacy_header => true,
            single_line => false
        }}
    ),
    Text = format(Event, Formatter, FConfig),
    case unicode:characters_to_binary(Text) of
        Bin when is_binary(Bin) -> Bin;
        _ -> <<"FORMATTER ERROR: bad return value\n">>
    end.

format(Event, Formatter, FConfig) ->
    try
        formatted(Formatter, Event, FConfig)
    catch
        _:_ ->
            case {Formatter, FConfig} of
                {logger_formatter, #{}} when map_size(FConfig) =:= 0 -> "DEFAULT FORMATTER CRASHED\n";
                _ -> format(Event#{msg => {"FORMATTER CRASH: ~tp", [maps:get(msg, Event)]}}, logger_formatter, #{})
            end
    end.

%% Names the default formatter literally, so programs that log link it.
formatted(logger_formatter, Event, FConfig) -> logger_formatter:format(Event, FConfig);
formatted(Formatter, Event, FConfig) -> Formatter:format(Event, FConfig).

%% The handler process.

reg_name(Name) -> list_to_atom(lists:concat([?MODULE, "_", Name])).

start(Name, HConfig) ->
    Starter = self(),
    Pid = spawn_link(fun() -> handler_init(Name, HConfig, Starter) end),
    receive
        {Pid, ok} -> {ok, Pid};
        {Pid, Error} -> Error
    after ?CALL_TIMEOUT -> {error, handler_process_not_started}
    end.

stop(Pid) ->
    Ref = monitor(process, Pid),
    unlink(Pid),
    Pid ! stop,
    receive
        {'DOWN', Ref, _, _, _} -> ok
    after ?CALL_TIMEOUT ->
        exit(Pid, kill),
        ok
    end.

%% A synchronous request to the handler process: its reply, or {error, Reason} when it ended.
request(Pid, Request) when Pid =:= self() ->
    {error, {attempting_syncronous_call_to_self, Request}};
request(Pid, Request) ->
    Ref = monitor(process, Pid),
    Pid ! {Request, {self(), Ref}},
    receive
        {Ref, Result} ->
            demonitor(Ref, [flush]),
            Result;
        {'DOWN', Ref, _, _, Reason} ->
            {error, Reason}
    end.

handler_init(Name, HConfig, Starter) ->
    case catch register(reg_name(Name), self()) of
        true ->
            case open_device(Name, HConfig) of
                {ok, State} ->
                    Starter ! {self(), ok},
                    handler_loop(repeat_filesync(rotation(State)));
                {error, Reason} ->
                    Starter ! {self(), {error, Reason}}
            end;
        _ ->
            Starter ! {self(), {error, {already_started, whereis(reg_name(Name))}}}
    end.

open_device(Name, #{type := file, file := File} = HConfig) ->
    case open_file(File, maps:get(modes, HConfig)) of
        {ok, Fd} ->
            {ok, HConfig#{
                name => Name, fd => Fd, inode => inode(File), synced => true, written => false, last_check => now_ms()
            }};
        {error, Reason} ->
            {error, {open_failed, File, Reason}}
    end;
open_device(Name, #{type := {device, Device}} = HConfig) ->
    {ok, HConfig#{name => Name, dev => Device, written => false}};
open_device(Name, #{type := Device} = HConfig) ->
    {ok, HConfig#{name => Name, dev => Device, written => false}}.

handler_loop(State) ->
    receive
        {{log, Bin}, {From, Ref}} ->
            Next = write_bin(Bin, check_file(State)),
            From ! {Ref, ok},
            handler_loop(Next);
        {filesync, {From, Ref}} ->
            Next = sync(State),
            From ! {Ref, ok},
            handler_loop(Next);
        {timeout, TRef, repeated_filesync} when TRef =:= map_get(sync_timer, State) ->
            handler_loop(repeat_filesync(sync(State)));
        {config_changed, Common, Own} ->
            Changed = maps:merge(
                State,
                maps:merge(
                    Common,
                    maps:with(
                        [
                            file_check,
                            max_no_bytes,
                            max_no_files,
                            compress_on_rotate
                        ],
                        Own
                    )
                )
            ),
            handler_loop(repeat_filesync(rotation(cancel_filesync(Changed))));
        stop ->
            close_device(State);
        _ ->
            handler_loop(State)
    end.

write_bin(Bin, #{dev := standard_io} = State) ->
    io:put_chars(standard_io, Bin),
    State;
write_bin(Bin, #{dev := Device} = State) ->
    io:put_chars(Device, Bin),
    State;
write_bin(Bin, #{fd := Fd} = State) ->
    _ = file:write(Fd, Bin),
    rotate_if_full(byte_size(Bin), State#{synced => false, written => true}).

sync(#{fd := Fd, synced := false} = State) ->
    _ = file:datasync(Fd),
    State#{synced => true};
sync(State) ->
    State.

close_device(#{fd := Fd}) ->
    _ = file:datasync(Fd),
    _ = file:close(Fd),
    ok;
close_device(_) ->
    ok.

repeat_filesync(#{filesync_repeat_interval := Interval} = State) when is_integer(Interval), is_map_key(fd, State) ->
    State#{sync_timer => erlang:start_timer(Interval, self(), repeated_filesync)};
repeat_filesync(State) ->
    State#{sync_timer => undefined}.

cancel_filesync(#{sync_timer := TRef} = State) when is_reference(TRef) ->
    _ = erlang:cancel_timer(TRef),
    State#{sync_timer => undefined};
cancel_filesync(State) ->
    State.

%% Reopens the file when it was moved or removed (file_check milliseconds apart, 0 before every write).
check_file(#{fd := _, file_check := Check, last_check := Last} = State) ->
    case Check =:= 0 orelse now_ms() - Last > Check of
        true -> ensure_file(State);
        false -> State
    end;
check_file(State) ->
    State.

ensure_file(#{file := File, inode := INode, modes := Modes} = State) ->
    case file:read_file_info(File) of
        {ok, #file_info{inode = INode}} ->
            State#{last_check => now_ms()};
        _ ->
            close_device(State),
            {ok, Fd} = open_file(File, Modes),
            State#{fd => Fd, inode => inode(File), last_check => now_ms(), synced => true}
    end.

open_file(File, Modes) ->
    ok = ensure_dir(filename:dirname(File)),
    file:open(File, Modes).

ensure_dir(Dir) ->
    case file:read_file_info(Dir) of
        {ok, #file_info{type = directory}} ->
            ok;
        _ ->
            Parent = filename:dirname(Dir),
            Parent =/= Dir andalso ensure_dir(Parent),
            case file:make_dir(Dir) of
                ok -> ok;
                {error, eexist} -> ok;
                Error -> Error
            end
    end.

inode(File) ->
    case file:read_file_info(File) of
        {ok, #file_info{inode = INode}} -> INode;
        _ -> undefined
    end.

now_ms() -> erlang:monotonic_time(millisecond).

%% Rotation: once the file grows past max_no_bytes it becomes File.0 (compressed File.0.gz), older archives move
%% up to max_no_files - 1, and a new file starts.
rotation(#{fd := _, max_no_bytes := infinity} = State) ->
    remove_archives(0, State),
    maps:remove(rotation, State);
rotation(#{fd := _, file := File, max_no_bytes := Size, max_no_files := Count, compress_on_rotate := Compress} = State) ->
    remove_archives(Count, State),
    Current =
        case file:read_file_info(File) of
            {ok, #file_info{size = S}} -> S;
            _ -> 0
        end,
    Rotating = State#{rotation => #{size => Size, count => Count, compress => Compress, current => Current}},
    recompress(0, Rotating),
    rotate_if_full(0, Rotating);
rotation(State) ->
    State.

remove_archives(Count, #{file := File} = State) ->
    Plain = archive(File, Count, false),
    Compressed = archive(File, Count, true),
    case {file:read_file_info(Plain), file:read_file_info(Compressed)} of
        {{error, enoent}, {error, enoent}} ->
            ok;
        _ ->
            _ = file:delete(Plain),
            _ = file:delete(Compressed),
            remove_archives(Count + 1, State)
    end.

%% Archives left by the other compress setting are converted.
recompress(Count, #{rotation := #{count := Count}}) ->
    ok;
recompress(N, #{file := File, rotation := #{compress := Compress}} = State) ->
    Archive = archive(File, N, not Compress),
    case file:read_file_info(Archive) of
        {ok, _} when Compress -> compress_file(Archive);
        {ok, _} -> decompress_file(Archive);
        _ -> ok
    end,
    recompress(N + 1, State).

rotate_if_full(Added, #{rotation := #{size := Limit, current := Current} = Rotation} = State) ->
    case Current + Added > Limit of
        true -> rotate(State#{rotation => Rotation#{current => Current + Added}});
        false -> State#{rotation => Rotation#{current => Current + Added}}
    end;
rotate_if_full(_, State) ->
    State.

rotate(#{file := File, modes := Modes, rotation := #{count := Count, compress := Compress} = Rotation} = State) ->
    Synced = sync(State),
    _ = file:close(maps:get(fd, Synced)),
    rotate_files(File, Count, Compress),
    {ok, Fd} = open_file(File, Modes),
    Synced#{fd => Fd, inode => inode(File), rotation => Rotation#{current => 0}}.

rotate_files(File, 0, _) ->
    _ = file:delete(File),
    ok;
rotate_files(File, 1, Compress) ->
    First = File ++ ".0",
    case file:rename(File, First) of
        ok when Compress -> compress_file(First);
        _ -> ok
    end;
rotate_files(File, Count, Compress) ->
    _ = file:rename(archive(File, Count - 2, Compress), archive(File, Count - 1, Compress)),
    rotate_files(File, Count - 1, Compress).

archive(File, N, false) -> File ++ "." ++ integer_to_list(N);
archive(File, N, true) -> archive(File, N, false) ++ ".gz".

compress_file(File) ->
    {ok, Data} = file:read_file(File),
    ok = file:write_file(File ++ ".gz", zlib:gzip(Data)),
    _ = file:delete(File),
    ok.

decompress_file(File) ->
    {ok, Data} = file:read_file(File),
    ok = file:write_file(filename:rootname(File, ".gz"), zlib:gunzip(Data)),
    _ = file:delete(File),
    ok.

%% logger_h_common's callbacks of OTP's handler process, which Clause's handler process does not use.
init(_, _) -> erlang:error({not_implemented, init, 2}).
config_changed(_, _, _) -> erlang:error({not_implemented, config_changed, 3}).
reset_state(_, _) -> erlang:error({not_implemented, reset_state, 2}).
filesync(_, _, _) -> erlang:error({not_implemented, filesync, 3}).
write(_, _, _, _) -> erlang:error({not_implemented, write, 4}).
handle_info(_, _, _) -> erlang:error({not_implemented, handle_info, 3}).
terminate(_, _, _) -> erlang:error({not_implemented, terminate, 3}).
