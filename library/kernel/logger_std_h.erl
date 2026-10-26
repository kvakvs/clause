%% Clause's logger_std_h (plan 21 step LIB23, docs/libraries/library.md): the standard logger handler, writing formatted events
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

-define(CALL_TIMEOUT, 5000).

%% The logger_handler callbacks.

% The common and overload options are clause_logger_h's, as OTP's logger_h_common's.
adding_handler(Config) -> clause_logger_h:adding_handler(?MODULE, Config, fun start/2).

changing_config(SetOrUpdate, Old, New) -> clause_logger_h:changing_config(?MODULE, SetOrUpdate, Old, New).

removing_handler(#{config := #{olp := Pid}}) ->
    clause_logger_h:stop(Pid),
    ok.

filter_config(Config) -> clause_logger_h:filter_config(Config).

%% Formats in the logging process, then writes through the handler's process.
log(Event, #{config := #{olp := Pid}} = Config) ->
    Bin = clause_logger_h:to_binary(Event, Config),
    case clause_logger_h:request(Pid, {log, Bin}) of
        ok -> ok;
        {error, Reason} -> erlang:error(Reason)
    end.

filesync(Name) when is_atom(Name) ->
    RegName = reg_name(Name),
    case whereis(RegName) of
        undefined -> exit({noproc, {gen_server, call, [RegName, filesync]}});
        Pid -> clause_logger_h:request(Pid, filesync)
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

%% The handler process.

reg_name(Name) -> clause_logger_h:reg_name(?MODULE, Name).

start(Name, HConfig) ->
    Starter = self(),
    Pid = spawn_link(fun() -> handler_init(Name, HConfig, Starter) end),
    receive
        {Pid, ok} -> {ok, Pid};
        {Pid, Error} -> Error
    after ?CALL_TIMEOUT -> {error, handler_process_not_started}
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
