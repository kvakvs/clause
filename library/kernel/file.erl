-module(file).
-export([
    open/2,
    read/2,
    write/2,
    read_line/1,
    position/2,
    close/1,
    read_file/1,
    write_file/2,
    delete/1,
    rename/2,
    list_dir/1,
    make_dir/1,
    del_dir/1
]).
%% OTP 29 exports implemented natively or not yet: catalog builtins, whose clause_builtin_placeholder body the runtime's
%% builtin replaces, and stubs raising error({not_implemented, F, A}) (docs/library.md#runtime-module-replacements).
-export([
    advise/4,
    allocate/3,
    altname/1,
    change_group/2,
    change_mode/2,
    change_owner/2,
    change_owner/3,
    change_time/2,
    change_time/3,
    consult/1,
    copy/2,
    copy/3,
    copy_opened/3,
    datasync/1,
    del_dir_r/1,
    delete/2,
    eval/1,
    eval/2,
    format_error/1,
    get_cwd/0,
    get_cwd/1,
    ipread_s32bu_p32bu/3,
    ipread_s32bu_p32bu_int/3,
    list_dir_all/1,
    make_link/2,
    make_symlink/2,
    native_name_encoding/0,
    path_consult/2,
    path_eval/2,
    path_eval/3,
    path_open/3,
    path_script/2,
    path_script/3,
    pread/2,
    pread/3,
    pwrite/2,
    pwrite/3,
    raw_read_file_info/1,
    raw_write_file_info/2,
    read_file/2,
    read_file_info/1,
    read_file_info/2,
    read_link/1,
    read_link_all/1,
    read_link_info/1,
    read_link_info/2,
    script/1,
    script/2,
    sendfile/2,
    sendfile/5,
    set_cwd/1,
    sync/1,
    truncate/1,
    write_file/3,
    write_file_info/2,
    write_file_info/3
]).
-include("file.types.hrl").
%% file's own atoms (error reasons, request tags) never name a called module; the modules io requests name are the
%% callers' atoms.
-compile({clause_liveness, strict}).

% The project-owned file module (docs/library.md, docs/ports.md#standard-io-and-files): files are ports of the
% runtime's file driver ({spawn_driver, "clause_file"}), driven with port_control/3. An open file is an I/O
% server process that owns its port and monitors the opener, as in OTP.

-define(OPEN, 1).
-define(READ, 2).
-define(WRITE, 3).
-define(POSITION, 4).
-define(READ_LINE, 5).
-define(CLOSE, 6).
% Flushing an open file to its device, a flag byte 1 for the data only (plan 21 step LIB23).
-define(SYNC, 7).
% Cutting a file at its position, access advice, path owners, permissions and times, reads and writes at an offset
% and reserving space (plan 21 step LIB50).
-define(TRUNCATE, 8).
-define(ADVISE, 9).
-define(READ_FILE, 10).
-define(WRITE_FILE, 11).
-define(DELETE, 12).
-define(RENAME, 13).
-define(LIST_DIR, 14).
-define(MAKE_DIR, 15).
-define(DEL_DIR, 16).
-define(READ_FILE_INFO, 17).
-define(GET_CWD, 18).
-define(SET_CWD, 19).
-define(READ_LINK, 20).
-define(READ_LINK_INFO, 21).
-define(MAKE_LINK, 22).
-define(MAKE_SYMLINK, 23).
-define(SET_OWNER, 24).
-define(SET_PERMISSIONS, 25).
-define(SET_TIMES, 26).
-define(PREAD, 30).
-define(PWRITE, 31).
-define(ALLOCATE, 32).
% Integers the driver takes as signed 64-bit numbers (plan 21 step LIB50).
-define(IS_INT64(N), (is_integer(N) andalso N >= -16#8000000000000000 andalso N =< 16#7FFFFFFFFFFFFFFF)).

% Open File with Modes (read, write, append, exclusive, binary, raw, compressed, compressed_one; others are ignored,
% as OTP ignores options it does not use): {ok, IoDevice} or {error, Reason}. With ram, File is the contents.
open(File, Modes) when is_list(Modes) ->
    case {lists:member(raw, Modes), lists:member(ram, Modes)} of
        {true, true} -> erlang:error(badarg, [File, Modes]);
        {false, true} -> open_ram(File, Modes);
        _ -> open_file(File, Modes)
    end;
open(_, _) ->
    {error, badarg}.

open_file(File, Modes) ->
    case {name(File), layer(Modes), device_encoding(proplists:get_value(encoding, Modes, latin1))} of
        {error, _, _} ->
            {error, badarg};
        {_, _, error} ->
            {error, badarg};
        {_, einval, _} ->
            {error, einval};
        {Name, _, _} ->
            Self = self(),
            Server = spawn(fun() -> serve(Self, Name, Modes) end),
            receive
                {Server, opened, Result} -> Result
            end
    end.

read(File, Size) when is_integer(Size), Size >= 0 -> call(File, {read, Size});
read(_, _) -> {error, badarg}.

% Bytes are made a binary before the request, so a server only fails a write it attempted (and then stops).
write(File, Bytes) ->
    try iolist_to_binary(Bytes) of
        Data -> call(File, {write, Data})
    catch
        error:badarg -> {error, badarg}
    end.

read_line(File) -> call(File, read_line).

position(File, Location) -> call(File, {position, Location}).

% Close an open file; ok also for one already closed.
close(File) ->
    case call(File, close) of
        {error, terminated} -> ok;
        Result -> Result
    end.

read_file(File) ->
    path_call(File, ?READ_FILE, fun(Name) -> Name end, fun(Content) -> {ok, Content} end).

write_file(File, Bytes) ->
    try iolist_to_binary(Bytes) of
        Data -> path_call(File, ?WRITE_FILE, fun(Name) -> [sized(Name), Data] end, fun(_) -> ok end)
    catch
        error:badarg -> {error, badarg}
    end.

delete(File) -> path_call(File, ?DELETE, fun(Name) -> Name end, fun(_) -> ok end).

rename(From, To) ->
    case name(To) of
        error -> {error, badarg};
        Target -> path_call(From, ?RENAME, fun(Name) -> [sized(Name), Target] end, fun(_) -> ok end)
    end.

list_dir(Dir) ->
    path_call(Dir, ?LIST_DIR, fun(Name) -> Name end, fun(Names) -> {ok, names(Names, [])} end).

make_dir(Dir) -> path_call(Dir, ?MAKE_DIR, fun(Name) -> Name end, fun(_) -> ok end).

del_dir(Dir) -> path_call(Dir, ?DEL_DIR, fun(Name) -> Name end, fun(_) -> ok end).

% Write Bytes to File opened with Modes plus the implicit binary and write (plan step 41Y2): append and exclusive
% apply, as open/2 takes them.
write_file(File, Bytes, Modes) when is_list(Modes) ->
    try iolist_to_binary(Bytes) of
        Data ->
            case open(File, [binary, write | Modes]) of
                {ok, Device} ->
                    Result = write(Device, Data),
                    ok = close(Device),
                    Result;
                Error ->
                    Error
            end
    catch
        error:badarg -> {error, badarg}
    end.

% The #file_info{} of File, times as local date and time (plan step 41Y2).
read_file_info(File) -> read_file_info(File, []).

% The #file_info{} of File; {time, local | universal | posix} picks the time format, raw is accepted.
read_file_info(File, Options) when is_list(Options) ->
    case time_option(Options, local) of
        error ->
            {error, badarg};
        Time ->
            path_call(File, ?READ_FILE_INFO, fun(Name) -> Name end, fun(Status) ->
                {ok, file_info(Status, Time)}
            end)
    end.

time_option([{time, Time} | Rest], _) when Time =:= local; Time =:= universal; Time =:= posix ->
    time_option(Rest, Time);
time_option([raw | Rest], Time) ->
    time_option(Rest, Time);
time_option([], Time) ->
    Time;
time_option(_, _) ->
    error.

% The driver's status record as #file_info{}.
file_info(
    <<Size:64, Type, Access, ATime:64/signed, MTime:64/signed, CTime:64/signed, Mode:32, Links:32, Major:32, Minor:32,
        Inode:64, Uid:32, Gid:32>>,
    Time
) ->
    #file_info{
        size = Size,
        type = element(Type + 1, {regular, directory, symlink, device, other}),
        access = element(Access + 1, {none, read, write, read_write}),
        atime = file_time(ATime, Time),
        mtime = file_time(MTime, Time),
        ctime = file_time(CTime, Time),
        mode = Mode,
        links = Links,
        major_device = Major,
        minor_device = Minor,
        inode = Inode,
        uid = Uid,
        gid = Gid
    }.

file_time(Seconds, posix) ->
    Seconds;
file_time(Seconds, universal) ->
    erlang:posixtime_to_universaltime(Seconds);
file_time(Seconds, local) ->
    erlang:universaltime_to_localtime(erlang:posixtime_to_universaltime(Seconds)).

% --- Directories, links and copies (plan step 41P) ---

% The current directory with forward slashes; on Windows also the current directory of a drive "X:".
get_cwd() -> path_call(<<>>, ?GET_CWD, fun(Name) -> Name end, fun(Dir) -> {ok, decode(Dir)} end).

get_cwd(Drive) ->
    path_call(Drive, ?GET_CWD, fun(Name) -> Name end, fun(Dir) -> {ok, decode(Dir)} end).

% Change the current directory of the whole program, as OTP's file server does.
set_cwd(Dir) -> path_call(Dir, ?SET_CWD, fun(Name) -> Name end, fun(_) -> ok end).

read_link(Link) ->
    path_call(Link, ?READ_LINK, fun(Name) -> Name end, fun(Target) -> {ok, decode(Target)} end).

read_link_all(Link) -> read_link(Link).

read_link_info(File) -> read_link_info(File, []).

% The #file_info{} of File itself when it is a symbolic link; options as read_file_info/2's.
read_link_info(File, Options) when is_list(Options) ->
    case time_option(Options, local) of
        error ->
            {error, badarg};
        Time ->
            path_call(File, ?READ_LINK_INFO, fun(Name) -> Name end, fun(Status) ->
                {ok, file_info(Status, Time)}
            end)
    end.

raw_read_file_info(File) -> read_file_info(File, [raw]).

read_file(File, Options) when is_list(Options) -> read_file(File).

delete(File, Options) when is_list(Options) -> delete(File).

make_link(Existing, New) -> link_call(Existing, New, ?MAKE_LINK).

make_symlink(Existing, New) -> link_call(Existing, New, ?MAKE_SYMLINK).

link_call(Existing, New, Operation) ->
    case name(New) of
        error ->
            {error, badarg};
        Link ->
            path_call(Existing, Operation, fun(Name) -> [sized(Name), Link] end, fun(_) -> ok end)
    end.

% File names are UTF-8 on every platform Clause supports.
native_name_encoding() -> utf8.

list_dir_all(Dir) -> list_dir(Dir).

% Delete a directory and everything below it, or a file.
del_dir_r(File) ->
    case read_link_info(File) of
        {ok, #file_info{type = directory}} ->
            case list_dir_all(File) of
                {ok, Names} ->
                    case delete_all([join(File, Name) || Name <- Names]) of
                        ok -> del_dir(File);
                        Error -> Error
                    end;
                Error ->
                    Error
            end;
        {ok, _} ->
            delete(File);
        Error ->
            Error
    end.

delete_all([File | Rest]) ->
    case del_dir_r(File) of
        ok -> delete_all(Rest);
        Error -> Error
    end;
delete_all([]) ->
    ok.

join(Dir, Name) when is_binary(Dir) ->
    <<Dir/binary, "/", (unicode:characters_to_binary(Name))/binary>>;
join(Dir, Name) when is_atom(Dir) -> join(atom_to_list(Dir), Name);
join(Dir, Name) ->
    lists:flatten([Dir, "/", Name]).

% Copy all of Source to Destination: names, {Name, Modes} or open files; {ok, BytesCopied}.
copy(Source, Destination) -> copy(Source, Destination, infinity).

copy(Source, Destination, Count) when Count =:= infinity; is_integer(Count), Count >= 0 ->
    case open_end(Source, [read]) of
        {ok, In, CloseIn} ->
            Result =
                case open_end(Destination, [write]) of
                    {ok, Out, CloseOut} ->
                        Copied = copy_loop(In, Out, Count, 0),
                        CloseOut(),
                        Copied;
                    Error ->
                        Error
                end,
            CloseIn(),
            Result;
        Error ->
            Error
    end;
copy(_, _, _) ->
    {error, badarg}.

% An open file for one end of a copy and how to close it: a device stays open.
open_end(Device, _) when is_pid(Device) ->
    {ok, Device, fun() -> ok end};
open_end({Name, Modes}, Default) when is_list(Modes) ->
    open_end(Name, Modes ++ Default);
open_end(Name, Modes) ->
    case open(Name, [binary | Modes]) of
        {ok, Device} -> {ok, Device, fun() -> close(Device) end};
        Error -> Error
    end.

copy_loop(_, _, 0, Copied) ->
    {ok, Copied};
copy_loop(In, Out, Count, Copied) ->
    Size =
        case Count of
            infinity -> 65536;
            _ -> min(Count, 65536)
        end,
    case read(In, Size) of
        {ok, Data} ->
            case write(Out, Data) of
                ok when Count =:= infinity -> copy_loop(In, Out, Count, Copied + iolist_size(Data));
                ok -> copy_loop(In, Out, Count - iolist_size(Data), Copied + iolist_size(Data));
                Error -> Error
            end;
        eof ->
            {ok, Copied};
        Error ->
            Error
    end.

% --- Requests to an open file's server ---

% Send Request to the server of File and wait for its reply; {error, terminated} once the server is gone.
call(File, Request) when is_pid(File) ->
    Ref = monitor(process, File),
    File ! {file_request, self(), Ref, Request},
    receive
        {file_reply, Ref, Reply} ->
            demonitor(Ref, [flush]),
            Reply;
        {'DOWN', Ref, process, File, _} ->
            {error, terminated}
    end;
call(_, _) ->
    {error, badarg}.

% The I/O server of one open file. As OTP's, it is not linked to the opener but closes when the opener ends. A raw
% file is served too, and answers as OTP's raw files do where they differ (plan 21 step LIB50).
serve(Opener, Name, Modes) ->
    Port = open_port({spawn_driver, "clause_file"}, [binary]),
    case {reply(port_control(Port, ?OPEN, [mode_byte(Modes), Name])), codec(layer(Modes))} of
        {{ok, _}, {ok, Codec}} ->
            monitor(process, Opener),
            {ok, Encoding} = device_encoding(proplists:get_value(encoding, Modes, latin1)),
            put(io_encoding, Encoding),
            Opener ! {self(), opened, {ok, self()}},
            start(Opener, Port, Modes, Codec);
        {{ok, _}, Error} ->
            port_close(Port),
            Opener ! {self(), opened, Error};
        {Error, _} ->
            Opener ! {self(), opened, Error}
    end.

% Serve a plain file, or a compressed one through its gzip or zstd layer; a file opened compressed (gzip) for reading
% that does not start with gzip's magic bytes is read as it is, as OTP's layer does.
start(Opener, Port, Modes, Codec) ->
    Binary = lists:member(binary, Modes),
    Raw = lists:member(raw, Modes),
    case Codec of
        #{inflate := _, gzip := true} ->
            case reply(port_control(Port, ?READ, <<2:64>>)) of
                {ok, <<16#1F, 16#8B>>} ->
                    {ok, _} = reply(port_control(Port, ?POSITION, <<0, 0:64>>)),
                    compressed_loop(Opener, inflating(Port, Codec, Binary, Raw));
                _ ->
                    {ok, _} = reply(port_control(Port, ?POSITION, <<0, 0:64>>)),
                    (maps:get(close, Codec))(),
                    loop(Opener, Port, Binary, Raw)
            end;
        #{inflate := _} ->
            compressed_loop(Opener, inflating(Port, Codec, Binary, Raw));
        #{deflate := _} ->
            compressed_loop(Opener, deflating(Port, Codec, Binary, Raw));
        none ->
            loop(Opener, Port, Binary, Raw)
    end.

loop(Opener, Port, Binary, Raw) ->
    receive
        {file_request, From, Ref, close} ->
            port_close(Port),
            From ! {file_reply, Ref, ok};
        {file_request, From, Ref, Request} ->
            answer(file_reply, From, Ref, encoded_file_request(Port, Binary, Raw, Request)),
            loop(Opener, Port, Binary, Raw);
        {io_request, From, ReplyAs, {setopts, Options}} ->
            {Reply, NewBinary} = set_io_options(Options, Binary),
            answer(io_reply, From, ReplyAs, Reply),
            loop(Opener, Port, NewBinary, Raw);
        {io_request, From, ReplyAs, Request} ->
            answer(io_reply, From, ReplyAs, io_request(Port, Binary, Request)),
            loop(Opener, Port, Binary, Raw);
        {'DOWN', _, process, Opener, _} ->
            port_close(Port)
    end.

% --- I/O options of a file server (plan 21 step LIB63) ---

% Reads of a file opened with an encoding answer as OTP's latin1 get_chars and get_line requests do: characters,
% and no_translation (which stops the server) for one above 255.
encoded_file_request(Port, Binary, Raw, Request) ->
    case {io_encoding(), Request} of
        {latin1, _} -> file_request(Port, Binary, Raw, Request);
        {_, read_line} -> encoded_read(Port, Binary, {get_line, unicode, ''});
        {_, {read, Size}} when is_integer(Size), Size >= 0 ->
            encoded_read(Port, Binary, {get_chars, unicode, '', Size});
        _ -> file_request(Port, Binary, Raw, Request)
    end.

encoded_read(Port, Binary, Request) ->
    case io_request(Port, true, Request) of
        Bytes when is_binary(Bytes) -> latin1_data(unicode:characters_to_list(Bytes, io_encoding()), Binary);
        Other -> Other
    end.

latin1_data(Chars, Binary) ->
    case lists:all(fun(C) -> C =< 255 end, Chars) of
        true when Binary -> {ok, list_to_binary(Chars)};
        true -> {ok, Chars};
        false -> {stop, no_translation, {error, {no_translation, unicode, latin1}}}
    end.

% The device encoding of this file server: latin1 unless the file was opened or set to another, as OTP's.
io_encoding() ->
    case get(io_encoding) of
        undefined -> latin1;
        Encoding -> Encoding
    end.

% OTP's file I/O server options: binary (or list) and encoding; a bad encoding is badarg, others enotsup.
set_io_options(Options0, Binary) when is_list(Options0) ->
    Expanded = [
        case Option of
            latin1 -> {encoding, latin1};
            unicode -> {encoding, unicode};
            _ -> Option
        end
     || Option <- Options0
    ],
    Options = proplists:unfold(proplists:substitute_negations([{list, binary}], Expanded)),
    Known = fun
        ({binary, _}) -> true;
        ({encoding, _}) -> true;
        (_) -> false
    end,
    case lists:all(Known, Options) of
        true -> apply_io_options(Options, Binary);
        false -> {{error, enotsup}, Binary}
    end;
set_io_options(_, Binary) ->
    {{error, enotsup}, Binary}.

apply_io_options(Options, Binary) ->
    case device_encoding(proplists:get_value(encoding, Options, io_encoding())) of
        {ok, Encoding} ->
            put(io_encoding, Encoding),
            {ok, proplists:get_value(binary, Options, Binary)};
        error ->
            {{error, badarg}, Binary}
    end.

% An encoding as OTP's file I/O server keeps it.
device_encoding(latin1) -> {ok, latin1};
device_encoding(utf8) -> {ok, unicode};
device_encoding(unicode) -> {ok, unicode};
device_encoding(utf16) -> {ok, {utf16, big}};
device_encoding({utf16, Endian} = Encoding) when Endian =:= big; Endian =:= little -> {ok, Encoding};
device_encoding(utf32) -> {ok, {utf32, big}};
device_encoding({utf32, Endian} = Encoding) when Endian =:= big; Endian =:= little -> {ok, Encoding};
device_encoding(_) -> error.

% Bytes read from an encoded file as an I/O reply: characters in list mode, the bytes in binary mode.
encoded(Bytes, false) when is_binary(Bytes) ->
    case io_encoding() of
        latin1 -> binary_to_list(Bytes);
        Encoding -> unicode:characters_to_list(Bytes, Encoding)
    end;
encoded(Data, _) ->
    Data.

% Send a reply; {stop, Reason, Reply} also ends the server with Reason after replying, {exit, Reason} ends it without
% a reply.
answer(_, _, _, {exit, Reason}) ->
    exit(Reason);
answer(Tag, From, Ref, {stop, Reason, Reply}) ->
    From ! {Tag, Ref, Reply},
    exit(Reason);
answer(Tag, From, Ref, Reply) ->
    From ! {Tag, Ref, Reply}.

% Answer a file request as request/3 does, but a failed write, sync, truncate or advice stops the server, as OTP's
% file server stops, while a raw file (OTP serves none) goes on. Added for plan 21 step LIB50.
file_request(Port, Binary, Raw, {pread, At, Size}) ->
    pread_request(Port, Binary, Raw, At, Size);
file_request(Port, _, Raw, {pwrite, At, Bytes}) ->
    pwrite_request(Port, Raw, At, Bytes);
file_request(_, _, Raw, raw) ->
    Raw;
file_request(Port, _, Raw, truncate) ->
    stopping(Raw, ok_reply(reply(port_control(Port, ?TRUNCATE, []))));
file_request(Port, _, Raw, {advise, Offset, Length, Advice}) ->
    stopping(Raw, advise_request(Port, Offset, Length, Advice));
file_request(Port, _, _, {allocate, Offset, Length}) ->
    case range(Offset, Length) of
        {ok, Range} -> ok_reply(reply(port_control(Port, ?ALLOCATE, Range)));
        Error -> Error
    end;
file_request(Port, Binary, Raw, {Operation, _} = Request) when
    Operation =:= write; Operation =:= sync; Operation =:= read
->
    stopping(Raw, request(Port, Binary, Request));
file_request(Port, Binary, Raw, Line) when Line =:= read_line; Line =:= get_line ->
    line_stopping(Raw, request(Port, Binary, Line));
file_request(Port, Binary, _, Request) ->
    request(Port, Binary, Request).

stopping(false, {error, Reason} = Error) -> {stop, Reason, Error};
stopping(_, Reply) -> Reply.

% A failed line read ends a file server without a reply, so its caller sees {error, terminated}, as with OTP's.
line_stopping(false, {error, Reason}) -> {exit, Reason};
line_stopping(_, Reply) -> Reply.

% pread: a file server positions and reads (at cur it reads on); a raw file reads at an integer offset and stays put.
pread_request(Port, Binary, false, At, Size) when At =:= cur; At =:= {cur, 0} ->
    stopping(false, request(Port, Binary, {read, Size}));
pread_request(Port, Binary, false, At, Size) ->
    case request(Port, Binary, {position, At}) of
        {ok, _} -> stopping(false, request(Port, Binary, {read, Size}));
        Error -> Error
    end;
pread_request(Port, Binary, true, At, Size) when ?IS_INT64(At) ->
    data(reply(port_control(Port, ?PREAD, <<At:64/signed, Size:64>>)), Binary);
pread_request(_, _, true, _, _) ->
    {error, badarg}.

% pwrite: as pread; a file server stops when the write fails or Bytes are not iodata.
pwrite_request(Port, false, At, Bytes) ->
    case At =:= cur orelse At =:= {cur, 0} orelse request(Port, false, {position, At}) of
        {error, _} = Error -> Error;
        _ -> stopping(false, written(Port, ?WRITE, <<>>, Bytes))
    end;
pwrite_request(Port, true, At, Bytes) when ?IS_INT64(At) ->
    written(Port, ?PWRITE, <<At:64/signed>>, Bytes);
pwrite_request(_, true, _, _) ->
    {error, badarg}.

written(Port, Operation, Head, Bytes) ->
    try iolist_to_binary(Bytes) of
        Data -> ok_reply(reply(port_control(Port, Operation, [Head, Data])))
    catch
        error:badarg -> {error, badarg}
    end.

% advise: the offset and length as prim_file checks them, then one of OTP's advice atoms (einval for another).
advise_request(Port, Offset, Length, Advice) ->
    case {range(Offset, Length), advice(Advice)} of
        {{ok, Range}, Code} when is_integer(Code) -> ok_reply(reply(port_control(Port, ?ADVISE, [Range, Code])));
        {{ok, _}, error} -> {error, einval};
        {Error, _} -> Error
    end.

advice(normal) -> 0;
advice(random) -> 1;
advice(sequential) -> 2;
advice(will_need) -> 3;
advice(dont_need) -> 4;
advice(no_reuse) -> 5;
advice(_) -> error.

% An offset and length for the driver: badarg for non-numbers, einval for floats, negatives and integers past 64
% bits, as prim_file's NIF answers.
range(Offset, Length) when not is_number(Offset); not is_number(Length) -> {error, badarg};
range(Offset, Length) when ?IS_INT64(Offset), ?IS_INT64(Length), Offset >= 0, Length >= 0 ->
    {ok, <<Offset:64, Length:64>>};
range(_, _) ->
    {error, einval}.

% --- Compressed files (plan 21 step LIB50C) ---

% The gzip layer of a file's modes: inflate when reading (compressed resets at the end of each gzip member, so
% concatenated members read as one; compressed_one stops after the first), deflate when writing, einval for reading
% and writing at once or appending, none without compression.
layer(Modes) ->
    Compressed = lists:member(compressed, Modes),
    One = lists:member(compressed_one, Modes),
    Write = lists:member(write, Modes),
    Read = lists:member(read, Modes) orelse not Write,
    case {lists:keyfind(zstd, 1, Modes), layer(Compressed orelse One, One, Read, Write, lists:member(append, Modes))} of
        {false, Layer} -> Layer;
        {{zstd, _} = Zstd, none} -> zstd_layer(Zstd, layer(true, false, Read, Write, lists:member(append, Modes)));
        {_, _} -> einval
    end.

% {zstd, Parameters} (plan 21 step LIB54E) reads or writes zstd frames as compressed reads or writes gzip.
zstd_layer(Zstd, deflate) -> {deflate, Zstd};
zstd_layer(Zstd, {inflate, _}) -> {inflate, Zstd};
zstd_layer(_, Other) -> Other.

% The funs of a layer's stream, made in the server that uses them: inflate, reset and close when reading, deflate,
% finish and close when writing; {error, badarg} for zstd parameters its context refuses, as OTP's layer answers.
codec({inflate, {zstd, Parameters}}) ->
    try zstd:context(decompress, Parameters) of
        {ok, Z} ->
            {ok, #{
                inflate => fun(Data) -> zstd:decompress(Data, Z) end,
                reset => fun() -> zstd:reset(Z) end,
                close => fun() -> zstd:close(Z) end
            }}
    catch
        _:_ -> {error, badarg}
    end;
codec({deflate, {zstd, Parameters}}) ->
    try zstd:context(compress, Parameters) of
        {ok, Z} ->
            {ok, #{
                deflate => fun(Data) -> zstd:compress(Data, Z) end,
                finish => fun() -> element(2, zstd:finish(Z, [])) end,
                close => fun() -> zstd:close(Z) end
            }}
    catch
        _:_ -> {error, badarg}
    end;
codec({inflate, Mode}) ->
    Z = zlib:open(),
    ok = zlib:inflateInit(Z, 31, Mode),
    {ok, #{
        gzip => true,
        inflate => fun(Data) -> zlib:inflate(Z, Data) end,
        reset => fun() -> zlib:inflateReset(Z) end,
        close => fun() -> zlib:close(Z) end
    }};
codec(deflate) ->
    Z = zlib:open(),
    ok = zlib:deflateInit(Z, default, deflated, 31, 8, default),
    {ok, #{
        deflate => fun(Data) -> zlib:deflate(Z, Data) end,
        finish => fun() -> zlib:deflate(Z, [], finish) end,
        close => fun() -> zlib:close(Z) end
    }};
codec(none) ->
    {ok, none};
codec(einval) ->
    {ok, none}.

layer(false, _, _, _, _) -> none;
layer(true, _, true, true, _) -> einval;
layer(true, _, _, _, true) -> einval;
layer(true, true, false, true, _) -> einval;
layer(true, _, false, true, _) -> deflate;
layer(true, true, true, false, _) -> {inflate, cut};
layer(true, false, true, false, _) -> {inflate, reset}.

% Bytes of compressed data read at a time, as OTP's layer reads them.
-define(INFLATE_CHUNK, 8192).

inflating(Port, Codec, Binary, Raw) ->
    #{port => Port, binary => Binary, raw => Raw, z => Codec, mode => inflate, buffer => <<>>, position => 0}.

deflating(Port, Codec, Binary, Raw) ->
    #{port => Port, binary => Binary, raw => Raw, z => Codec, mode => deflate, position => 0}.

% The server of a compressed file; positions count uncompressed bytes.
compressed_loop(Opener, State) ->
    receive
        {file_request, From, Ref, close} ->
            From ! {file_reply, Ref, compressed_close(State)};
        {file_request, From, Ref, Request} ->
            {Reply, Next} = compressed_request(Request, State),
            closed_on_stop(Reply, Next),
            answer(file_reply, From, Ref, Reply),
            compressed_loop(Opener, Next);
        {io_request, From, ReplyAs, Request} ->
            {Reply, Next} = compressed_io(Request, State),
            closed_on_stop(Reply, Next),
            answer(io_reply, From, ReplyAs, Reply),
            compressed_loop(Opener, Next);
        {'DOWN', _, process, Opener, _} ->
            _ = compressed_close(State),
            ok
    end.

% A server that stops still finishes a written file's gzip stream, as OTP's layer does when its owner ends.
closed_on_stop({stop, _, _}, State) -> compressed_close(State);
closed_on_stop({exit, _}, State) -> compressed_close(State);
closed_on_stop(_, _) -> ok.

% Closing a written file first finishes its gzip stream.
compressed_close(#{mode := deflate, port := Port, z := #{finish := Finish, close := Close}}) ->
    Result = ok_reply(reply(port_control(Port, ?WRITE, Finish()))),
    port_close(Port),
    Close(),
    Result;
compressed_close(#{port := Port, z := #{close := Close}}) ->
    port_close(Port),
    Close(),
    ok.

% A file request on a compressed file: reads and lines when reading, writes when writing, positions both ways
% (backwards only when reading); a file server's positioned reads and writes position first, as on plain files.
% Reading a written file or writing a read one is ebadf, the rest enotsup; a file server then stops where OTP's
% stops (writes, truncate, advise, sync).
compressed_request({read, Size}, #{mode := inflate} = State) ->
    read_stopping(inflated(State, Size));
compressed_request(read_line, #{mode := inflate} = State) ->
    line_read_stopping(inflated_line(State));
compressed_request(get_line, #{mode := inflate} = State) ->
    unwrapped(line_read_stopping(inflated_line(State)));
compressed_request({position, Location}, State) ->
    compressed_position(State, Location);
compressed_request({write, Data}, #{mode := deflate} = State) ->
    deflated(State, Data);
compressed_request({pread, At, Size}, #{raw := false} = State) ->
    case compressed_position(State, At) of
        {{ok, _}, Next} -> compressed_request({read, Size}, Next);
        Other -> Other
    end;
compressed_request({pwrite, At, Data}, #{raw := false} = State) ->
    case compressed_position(State, At) of
        {{ok, _}, Next} -> compressed_request({write, iolist_or_badarg(Data)}, Next);
        Other -> Other
    end;
compressed_request(raw, #{raw := Raw} = State) ->
    {Raw, State};
compressed_request(Request, #{raw := Raw} = State) when Request =:= read_line; Request =:= get_line ->
    {line_stopping(Raw, {error, ebadf}), State};
compressed_request(Request, #{raw := Raw} = State) when element(1, Request) =:= read; element(1, Request) =:= write ->
    {stopping(Raw, {error, ebadf}), State};
compressed_request(Request, #{raw := Raw} = State) when
    Request =:= truncate; element(1, Request) =:= advise; element(1, Request) =:= sync
->
    {stopping(Raw, {error, enotsup}), State};
compressed_request(_, State) ->
    {{error, enotsup}, State}.

% A failed read stops a file server, as OTP's stops.
read_stopping({Reply, #{raw := Raw} = State}) -> {stopping(Raw, Reply), State}.

line_read_stopping({Reply, #{raw := Raw} = State}) -> {line_stopping(Raw, Reply), State}.

iolist_or_badarg(Data) ->
    try
        iolist_to_binary(Data)
    catch
        error:badarg -> badarg
    end.

% An I/O request on a compressed file, as on a plain one but through the layer.
compressed_io(getopts, #{binary := Binary} = State) ->
    {[{binary, Binary}, {encoding, latin1}], State};
compressed_io({setopts, Options}, #{binary := Binary} = State) ->
    {Reply, NewBinary} = set_io_options(Options, Binary),
    {Reply, State#{binary := NewBinary}};
compressed_io({get_line, _, _}, #{mode := inflate} = State) ->
    unwrapped(line_read_stopping(inflated_line(State#{raw := false})));
compressed_io({get_chars, _, _, Count}, #{mode := inflate} = State) when is_integer(Count), Count >= 0 ->
    unwrapped(read_stopping(inflated(State#{raw := false}, Count)));
compressed_io({get_until, Encoding, _, M, F, Args}, #{mode := inflate} = State) ->
    compressed_until(State, {Encoding, M, F, Args}, []);
compressed_io({put_chars, Encoding, M, F, A}, State) ->
    case catch apply(M, F, A) of
        {'EXIT', Reason} -> {{stop, Reason, {error, F}}, State};
        Chars -> compressed_io({put_chars, Encoding, Chars}, State)
    end;
compressed_io({put_chars, Encoding, Chars}, #{mode := deflate} = State) ->
    case unicode:characters_to_binary(Chars, Encoding, latin1) of
        Bytes when is_binary(Bytes) -> deflated(State, Bytes);
        _ -> {{stop, no_translation, {error, {no_translation, Encoding, latin1}}}, State}
    end;
compressed_io({put_chars, _, _}, State) ->
    {{stop, ebadf, {error, ebadf}}, State};
compressed_io({get_line, _, _}, State) ->
    {{exit, ebadf}, State};
compressed_io(Request, State) when element(1, Request) =:= get_chars; element(1, Request) =:= get_until ->
    {{stop, ebadf, {error, ebadf}}, State};
compressed_io(Request, State) ->
    {{error, {request, Request}}, State}.

unwrapped({{ok, Data}, State}) -> {Data, State};
unwrapped(Other) -> Other.

% Feed 4 KiB of uncompressed bytes and then eof to M:F until it is done; what it leaves over goes back to the buffer.
compressed_until(State, Until, Cont) ->
    case taken(State, 4096) of
        {{ok, Bytes}, Next} -> compressed_apply(Next, Until, Cont, binary_to_list(Bytes));
        {eof, Next} -> compressed_apply(Next, Until, Cont, eof);
        {{error, Reason} = Error, Next} -> {{stop, Reason, Error}, Next}
    end.

compressed_apply(#{binary := Binary} = State, {Encoding, M, F, Args} = Until, Cont, Data) ->
    case catch apply(M, F, [Cont, Data | Args]) of
        {done, Result, Rest} -> {until_result(Binary, Encoding, Result), given_back(State, Rest)};
        {more, NewCont} -> compressed_until(State, Until, NewCont);
        {'EXIT', Reason} -> {{stop, Reason, {error, F}}, State}
    end.

given_back(#{buffer := Buffer, position := Position} = State, Rest) when is_list(Rest), Rest =/= [] ->
    Bytes = unicode:characters_to_binary(Rest, latin1),
    State#{buffer := <<Bytes/binary, Buffer/binary>>, position := Position - byte_size(Bytes)};
given_back(State, _) ->
    State.

% Up to Size uncompressed bytes as the file's data; what is left before the end of the data, or eof.
inflated(#{binary := Binary} = State, Size) ->
    {Reply, Next} = taken(State, Size),
    {data(Reply, Binary), Next}.

taken(#{buffer := Buffer, position := Position} = State, Size) when byte_size(Buffer) >= Size ->
    <<Data:Size/binary, Rest/binary>> = Buffer,
    {{ok, Data}, State#{buffer := Rest, position := Position + Size}};
taken(#{buffer := Buffer} = State, Size) ->
    case refill(State) of
        {ok, Next} -> taken(Next, Size);
        eof when Buffer =/= <<>> -> taken(State, byte_size(Buffer));
        Other -> {Other, State}
    end.

% The next line (CRLF read as LF, the position counting the CR); the rest of the data without a newline, which a raw
% file's position does not count, as OTP's layer leaves it (a file server's buffer counts it).
inflated_line(#{buffer := Buffer, position := Position, binary := Binary, raw := Raw} = State) ->
    case binary:match(Buffer, <<"\n">>) of
        {At, 1} ->
            <<Line:(At + 1)/binary, Rest/binary>> = Buffer,
            {data({ok, crlf(Line)}, Binary), State#{buffer := Rest, position := Position + At + 1}};
        nomatch ->
            case refill(State) of
                {ok, Next} ->
                    inflated_line(Next);
                eof when Buffer =/= <<>> ->
                    Moved =
                        Position +
                            (case Raw of
                                true -> 0;
                                false -> byte_size(Buffer)
                            end),
                    {data({ok, Buffer}, Binary), State#{buffer := <<>>, position := Moved}};
                Other ->
                    {Other, State}
            end
    end.

crlf(Line) ->
    case byte_size(Line) - 2 of
        Size when Size >= 0 ->
            case Line of
                <<Text:Size/binary, "\r\n">> -> <<Text/binary, "\n">>;
                _ -> Line
            end;
        _ ->
            Line
    end.

% Read and inflate the next chunk into the buffer; eof, or {error, eio} for data that is not gzip.
refill(#{port := Port, z := #{inflate := Inflate}, buffer := Buffer} = State) ->
    case reply(port_control(Port, ?READ, <<?INFLATE_CHUNK:64>>)) of
        {ok, Compressed} ->
            try Inflate(Compressed) of
                Data -> {ok, State#{buffer := iolist_to_binary([Buffer | Data])}}
            catch
                error:badarg -> {error, badarg};
                error:_ -> {error, eio}
            end;
        Other ->
            Other
    end.

% A position from the start or the current position (eof is einval); reading moves forward by inflating and back by
% starting over, writing moves forward by writing zeros.
compressed_position(State, Mark) when is_atom(Mark) -> compressed_position(State, {Mark, 0});
compressed_position(State, Offset) when is_integer(Offset) -> compressed_position(State, {bof, Offset});
compressed_position(State, {bof, Offset}) when is_integer(Offset) -> moved(State, Offset);
compressed_position(#{position := At} = State, {cur, Offset}) when is_integer(Offset) -> moved(State, At + Offset);
compressed_position(State, {eof, Offset}) when is_integer(Offset) -> {{error, einval}, State};
compressed_position(State, _) -> {{error, badarg}, State}.

moved(#{mode := inflate} = State, Desired) when Desired < 0 ->
    {{error, einval}, State};
moved(#{position := Desired} = State, Desired) ->
    {{ok, Desired}, State};
moved(#{mode := inflate, position := At} = State, Desired) when At < Desired ->
    case taken(State, min(Desired - At, ?INFLATE_CHUNK)) of
        {{ok, _}, Next} -> moved(Next, Desired);
        {eof, _} -> {{ok, At}, State};
        Other -> Other
    end;
moved(#{mode := inflate, port := Port, z := #{reset := Reset}} = State, Desired) ->
    case reply(port_control(Port, ?POSITION, <<0, 0:64>>)) of
        {ok, <<0:64>>} ->
            ok = Reset(),
            moved(State#{buffer := <<>>, position := 0}, Desired);
        Other ->
            {Other, State}
    end;
moved(#{position := At} = State, Desired) when At < Desired ->
    case deflated(State, <<0:(min(Desired - At, 4 bsl 20))/unit:8>>) of
        {ok, Next} -> moved(Next, Desired);
        Other -> Other
    end;
moved(State, _) ->
    {{error, einval}, State}.

% Write Data through the deflate stream; ok and the moved position.
deflated(#{raw := Raw} = State, badarg) ->
    {stopping(Raw, {error, badarg}), State};
deflated(#{port := Port, z := #{deflate := Deflate}, position := Position, raw := Raw} = State, Data) ->
    case reply(port_control(Port, ?WRITE, Deflate(Data))) of
        {ok, _} -> {ok, State#{position := Position + iolist_size(Data)}};
        Error -> {stopping(Raw, Error), State}
    end.

% Answer one I/O request as OTP's file I/O server does: the reply, or {stop, Reason, Reply} when the server must exit
% after replying (a failing get_until function, an untranslatable put_chars).
io_request(_, Binary, getopts) ->
    [{binary, Binary}, {encoding, io_encoding()}];
io_request(Port, Binary, {get_line, _, _}) ->
    encoded(line_stopping(false, request(Port, true, get_line)), Binary);
io_request(Port, Binary, {get_chars, _, _, Count}) when is_integer(Count), Count >= 0 ->
    case io_encoding() of
        latin1 ->
            case stopping(false, request(Port, Binary, {read, Count})) of
                {ok, Data} -> Data;
                Other -> Other
            end;
        unicode ->
            encoded(utf8_chars(Port, Count, <<>>), Binary);
        _ ->
            case stopping(false, request(Port, true, {read, Count})) of
                {ok, Data} -> encoded(Data, Binary);
                Other -> Other
            end
    end;
io_request(Port, Binary, {get_until, Encoding, _, M, F, Args}) ->
    until(Port, Binary, {Encoding, M, F, Args}, []);
io_request(Port, Binary, {put_chars, Encoding, M, F, A}) ->
    case catch apply(M, F, A) of
        {'EXIT', Reason} -> {stop, Reason, {error, F}};
        Chars -> io_request(Port, Binary, {put_chars, Encoding, Chars})
    end;
io_request(Port, _, {put_chars, Encoding, Chars}) ->
    Device = io_encoding(),
    case unicode:characters_to_binary(Chars, Encoding, Device) of
        Bytes when is_binary(Bytes) ->
            ok_reply(reply(port_control(Port, ?WRITE, Bytes)));
        _ ->
            {stop, no_translation, {error, {no_translation, Encoding, Device}}}
    end;
io_request(_, _, Request) ->
    {error, {request, Request}}.

% The bytes of the next Count UTF-8 characters of a unicode file, fewer at its end; eof at the end itself.
utf8_chars(Port, Count, Read) ->
    Chars = utf8_count(Read),
    case Chars >= Count of
        true ->
            Read;
        false ->
            case reply(port_control(Port, ?READ, <<(Count - Chars):64>>)) of
                {ok, Bytes} -> utf8_chars(Port, Count, <<Read/binary, Bytes/binary>>);
                eof when Read =:= <<>> -> eof;
                eof -> Read;
                {error, Reason} = Error -> {stop, Reason, Error}
            end
    end.

% The complete characters of UTF-8 bytes.
utf8_count(Bytes) ->
    case unicode:characters_to_list(Bytes) of
        Chars when is_list(Chars) -> length(Chars);
        {_, Chars, _} -> length(Chars)
    end.

% Feed 4 KiB chunks and then eof to M:F until it is done; the characters it leaves over are read again next.
until(Port, Binary, Until, Cont) ->
    case reply(port_control(Port, ?READ, <<4096:64>>)) of
        {ok, Bytes} -> apply_until(Port, Binary, Until, Cont, chunk_chars(Port, Bytes));
        eof -> apply_until(Port, Binary, Until, Cont, eof);
        {error, Reason} = Error -> {stop, Reason, Error}
    end.

apply_until(Port, Binary, {Encoding, M, F, Args} = Until, Cont, Data) ->
    case catch apply(M, F, [Cont, Data | Args]) of
        {done, Result, Rest} ->
            unread(Port, Rest),
            until_result(Binary, Encoding, Result);
        {more, NewCont} ->
            until(Port, Binary, Until, NewCont);
        {'EXIT', Reason} ->
            {stop, Reason, {error, F}}
    end.

% A list result of a file in binary mode is returned as a binary, as OTP's io_lib:get_until casts it.
until_result(true, unicode, Result) when is_list(Result) -> unicode:characters_to_binary(Result);
until_result(true, _, Result) when is_list(Result) -> iolist_to_binary(Result);
until_result(_, _, Result) -> Result.

% Move the file position back over the bytes of the characters M:F did not use.
unread(Port, Rest) when is_list(Rest), Rest =/= [] ->
    Size = byte_size(unicode:characters_to_binary(Rest, unicode, io_encoding())),
    port_control(Port, ?POSITION, <<1, (-Size):64/signed>>);
unread(_, _) ->
    ok.

% The characters of a chunk in the device encoding; an incomplete character at its end is read again next (plan 21
% step LIB63).
chunk_chars(_, Bytes) when Bytes =:= <<>> ->
    [];
chunk_chars(Port, Bytes) ->
    case io_encoding() of
        latin1 ->
            binary_to_list(Bytes);
        Encoding ->
            case unicode:characters_to_list(Bytes, Encoding) of
                Chars when is_list(Chars) ->
                    Chars;
                {incomplete, Chars, Tail} ->
                    port_control(Port, ?POSITION, <<1, (-byte_size(Tail)):64/signed>>),
                    Chars;
                {error, Chars, Invalid} ->
                    Chars ++ binary_to_list(Invalid)
            end
    end.

% Answer one request of an open file.
request(Port, Binary, {read, Size}) ->
    data(reply(port_control(Port, ?READ, <<Size:64>>)), Binary);
request(Port, Binary, read_line) ->
    data(line(Port), Binary);
request(Port, Binary, get_line) ->
    case data(line(Port), Binary) of
        {ok, Line} -> Line;
        Other -> Other
    end;
request(Port, _, {write, Data}) ->
    ok_reply(reply(port_control(Port, ?WRITE, Data)));
request(Port, _, {sync, DataOnly}) ->
    ok_reply(reply(port_control(Port, ?SYNC, <<DataOnly>>)));
request(Port, _, {position, Location}) ->
    case location(Location) of
        badarg ->
            {error, badarg};
        {Whence, Offset} ->
            case reply(port_control(Port, ?POSITION, <<Whence, Offset:64/signed>>)) of
                {ok, <<Position:64>>} -> {ok, Position};
                Error -> Error
            end
    end.

% The next line of a file, a CRLF ending read as LF, as OTP's file server and raw files read lines (plan 21 step
% LIB50C).
line(Port) ->
    case reply(port_control(Port, ?READ_LINE, [])) of
        {ok, Line} -> {ok, crlf(Line)};
        Other -> Other
    end.

% A position as a whence byte (0 start, 1 current, 2 end) and an offset; badarg for anything else, as OTP.
location(Offset) when is_integer(Offset) -> {0, Offset};
location(bof) -> {0, 0};
location(cur) -> {1, 0};
location(eof) -> {2, 0};
location({bof, Offset}) when is_integer(Offset) -> {0, Offset};
location({cur, Offset}) when is_integer(Offset) -> {1, Offset};
location({eof, Offset}) when is_integer(Offset) -> {2, Offset};
location(_) -> badarg.

% The mode byte of the open operation: read 1, write 2, append 4, exclusive 8.
mode_byte(Modes) ->
    lists:foldl(
        fun
            (read, Byte) -> Byte bor 1;
            (write, Byte) -> Byte bor 2;
            (append, Byte) -> Byte bor 4;
            (exclusive, Byte) -> Byte bor 8;
            (_, Byte) -> Byte
        end,
        0,
        Modes
    ).

% --- RAM files (plan 21 step LIB54C) ---

% A file in memory: a server process like the others, holding the contents, the position and the modes, answering as
% OTP's ram_file driver does (modes ram, read, write and binary only; read when neither is given).
open_ram(Data, Modes) ->
    try {iolist_to_binary(Data), ram_modes(Modes, #{read => false, write => false, binary => false})} of
        {_, error} ->
            {error, badarg};
        {Contents, #{read := false, write := false} = State} ->
            start_ram(Contents, State#{read := true});
        {Contents, State} ->
            start_ram(Contents, State)
    catch
        error:badarg -> {error, badarg}
    end.

ram_modes([ram | Rest], State) ->
    ram_modes(Rest, State);
ram_modes([Mode | Rest], State) when Mode =:= read; Mode =:= write; Mode =:= binary ->
    ram_modes(Rest, State#{Mode := true});
ram_modes([], State) ->
    State;
ram_modes(_, _) ->
    error.

start_ram(Contents, State) ->
    Opener = self(),
    {ok,
        spawn(fun() ->
            monitor(process, Opener),
            ram_loop(Opener, State#{contents => Contents, position => 0})
        end)}.

ram_loop(Opener, State) ->
    receive
        {file_request, From, Ref, close} ->
            From ! {file_reply, Ref, ok};
        {file_request, From, Ref, Request} ->
            {Reply, Next} = ram_request(Request, State),
            From ! {file_reply, Ref, Reply},
            ram_loop(Opener, Next);
        {io_request, From, ReplyAs, _} ->
            From ! {io_reply, ReplyAs, {error, enotsup}},
            ram_loop(Opener, State);
        {'DOWN', _, process, Opener, _} ->
            ok
    end.

% The driver's offsets and sizes are signed 32-bit numbers.
-define(IS_RAM_INT(N), (N >= -16#80000000 andalso N < 16#80000000)).

ram_request({read, _}, #{read := false} = State) ->
    {{error, ebadf}, State};
ram_request({read, Size}, State) when not ?IS_RAM_INT(Size) ->
    {{error, einval}, State};
ram_request({read, Size}, #{position := At} = State) ->
    Reply = ram_read(State, At, Size),
    {Reply, State#{position := At + ram_size(Reply)}};
ram_request({pread, At, _}, State) when not is_integer(At) ->
    {{error, badarg}, State};
ram_request({pread, _, _}, #{read := false} = State) ->
    {{error, ebadf}, State};
ram_request({pread, At, Size}, State) when ?IS_RAM_INT(At), ?IS_RAM_INT(Size), At >= 0 ->
    {ram_read(State, At, Size), State};
ram_request({pread, _, _}, State) ->
    {{error, einval}, State};
ram_request({write, _}, #{write := false} = State) ->
    {{error, ebadf}, State};
ram_request({write, Data}, #{position := At} = State) ->
    {ok, State#{contents := ram_written(State, At, Data), position := At + byte_size(Data)}};
ram_request({pwrite, At, Bytes}, State) ->
    ram_pwrite(At, Bytes, State);
ram_request({position, Location}, State) ->
    ram_position(Location, State);
ram_request(truncate, #{write := false} = State) ->
    {{error, eacces}, State};
ram_request(truncate, #{position := At} = State) ->
    {ok, State#{contents := ram_resized(State, At)}};
ram_request({advise, _, _, Advice}, State) ->
    {
        case advice(Advice) of
            error -> {error, einval};
            _ -> ok
        end,
        State
    };
ram_request({sync, _}, State) ->
    {ok, State};
ram_request({allocate, _, _}, State) ->
    {ok, State};
ram_request(raw, State) ->
    {false, State};
ram_request(_, State) ->
    % OTP's ram files have no line reads.
    {{error, enotsup}, State}.

% Size bytes at At, the shorter rest before the end, eof past it (an empty read is never eof).
ram_read(#{contents := Contents, binary := Binary}, At, Size) ->
    case max(0, min(Size, byte_size(Contents) - At)) of
        0 when Size > 0 -> eof;
        Length -> data({ok, binary_part(Contents, At, Length)}, Binary)
    end.

ram_size({ok, Data}) -> iolist_size(Data);
ram_size(_) -> 0.

ram_pwrite(_, _, #{write := false} = State) ->
    {{error, ebadf}, State};
ram_pwrite(At, _, State) when not is_integer(At) ->
    {{error, badarg}, State};
ram_pwrite(At, _, State) when not ?IS_RAM_INT(At); At < 0 ->
    {{error, einval}, State};
ram_pwrite(At, Bytes, State) ->
    try iolist_to_binary(Bytes) of
        Data -> {ok, State#{contents := ram_written(State, At, Data)}}
    catch
        error:badarg -> {{error, badarg}, State}
    end.

% The contents with Data at At, a gap before it filled with zeros.
ram_written(#{contents := Contents} = State, At, Data) ->
    <<Before:At/binary, After/binary>> = ram_resized(State, max(At, byte_size(Contents))),
    Rest =
        case After of
            <<_:(byte_size(Data))/binary, Kept/binary>> -> Kept;
            _ -> <<>>
        end,
    <<Before/binary, Data/binary, Rest/binary>>.

ram_resized(#{contents := Contents}, Size) when Size =< byte_size(Contents) -> binary_part(Contents, 0, Size);
ram_resized(#{contents := Contents}, Size) -> <<Contents/binary, 0:((Size - byte_size(Contents)) * 8)>>.

ram_position(Location, #{position := At, contents := Contents} = State) ->
    case ram_offset(Location, At, byte_size(Contents)) of
        {ok, Offset, _} when not ?IS_RAM_INT(Offset) -> {{error, einval}, State};
        {ok, Offset, Base} when Base + Offset < 0 -> {{error, einval}, State};
        {ok, Offset, Base} -> {{ok, Base + Offset}, State#{position := Base + Offset}};
        Error -> {Error, State}
    end.

ram_offset(Offset, _, _) when is_integer(Offset) -> {ok, Offset, 0};
ram_offset(bof, _, _) -> {ok, 0, 0};
ram_offset(cur, At, _) -> {ok, 0, At};
ram_offset(eof, _, Size) -> {ok, 0, Size};
ram_offset({bof, Offset}, _, _) when is_integer(Offset) -> {ok, Offset, 0};
ram_offset({cur, Offset}, At, _) when is_integer(Offset) -> {ok, Offset, At};
ram_offset({eof, Offset}, _, Size) when is_integer(Offset) -> {ok, Offset, Size};
ram_offset(_, _, _) -> {error, badarg}.

% --- Operations on paths ---

% Run one path operation on a new driver port: Encode builds its data from the file's name, Decode its result.
path_call(File, Operation, Encode, Decode) ->
    case name(File) of
        error ->
            {error, badarg};
        Name ->
            Port = open_port({spawn_driver, "clause_file"}, [binary]),
            Reply = reply(port_control(Port, Operation, Encode(Name))),
            % Unlinked first: a caller trapping exits gets no {'EXIT', Port, normal}, as OTP's file NIFs send none.
            unlink(Port),
            port_close(Port),
            case Reply of
                {ok, Result} -> Decode(Result);
                Error -> Error
            end
    end.

% A name preceded by its 32-bit byte size.
sized(Name) -> [<<(byte_size(Name)):32>>, Name].

% --- Driver replies and conversions ---

% A driver reply: {ok, Result}, eof or {error, Reason}.
reply(<<0, Result/binary>>) -> {ok, Result};
reply(<<2>>) -> eof;
reply(<<1, Reason/binary>>) -> {error, list_to_atom(binary_to_list(Reason))}.

ok_reply({ok, _}) -> ok;
ok_reply(Other) -> Other.

% Read data as a binary or a list, per the file's mode.
data({ok, Bytes}, true) -> {ok, Bytes};
data({ok, Bytes}, false) -> {ok, binary_to_list(Bytes)};
data(Other, _) -> Other.

% The names of a list_dir reply, each followed by a zero byte.
names(<<>>, Names) ->
    lists:reverse(Names);
names(Bytes, Names) ->
    {Name, Rest} = until_zero(Bytes, 0),
    names(Rest, [decode(Name) | Names]).

until_zero(Bytes, Size) ->
    case Bytes of
        <<Name:Size/binary, 0, Rest/binary>> -> {Name, Rest};
        _ -> until_zero(Bytes, Size + 1)
    end.

% The UTF-8 bytes of a file name: a string, a binary or an atom; error for anything else.
name(Name) when is_binary(Name) -> Name;
name(Name) when is_atom(Name) -> name(atom_to_list(Name));
name(Name) when is_list(Name) ->
    try
        encode(Name)
    catch
        error:_ -> error
    end;
name(_) ->
    error.

% UTF-8 bytes of a deep list of characters.
encode(Chars) -> list_to_binary(encode(Chars, [])).

encode([], Acc) ->
    lists:reverse(Acc);
encode([C | Rest], Acc) when is_list(C) ->
    encode(Rest, lists:reverse(binary_to_list(encode(C)), Acc));
encode([C | Rest], Acc) when is_integer(C), C >= 0, C < 16#80 -> encode(Rest, [C | Acc]);
encode([C | Rest], Acc) when is_integer(C), C < 16#800 ->
    encode(Rest, [16#80 bor (C band 16#3F), 16#C0 bor (C bsr 6) | Acc]);
encode([C | Rest], Acc) when is_integer(C), C < 16#10000 ->
    encode(Rest, [
        16#80 bor (C band 16#3F), 16#80 bor ((C bsr 6) band 16#3F), 16#E0 bor (C bsr 12) | Acc
    ]);
encode([C | Rest], Acc) when is_integer(C), C < 16#110000 ->
    encode(Rest, [
        16#80 bor (C band 16#3F),
        16#80 bor ((C bsr 6) band 16#3F),
        16#80 bor ((C bsr 12) band 16#3F),
        16#F0 bor (C bsr 18)
        | Acc
    ]).

% The characters of UTF-8 bytes; bytes that are not UTF-8 stay as they are.
decode(<<>>) -> [];
decode(<<C/utf8, Rest/binary>>) -> [C | decode(Rest)];
decode(<<C, Rest/binary>>) -> [C | decode(Rest)].

% --- Erlang terms and expressions in files (plan 21 step LIB33) ---

% A file is read whole and decoded as epp:set_encoding/1 sets the encoding (a coding comment in the first two lines,
% UTF-8 by default); its forms are then scanned one by one as io:read/3 and io:parse_erl_exprs/3 read them.

% consult(File): {ok, Terms} of the terms ending with a dot, or the first error.
consult(File) ->
    case read_source(File) of
        {ok, Source} -> consult_forms(Source, 1, []);
        Error -> Error
    end.

% path_consult(Path, File): consult/1 of File in the first directory of Path that has it.
path_consult(Path, File) ->
    case path_source(Path, File) of
        {ok, Source, Full} ->
            case consult_forms(Source, 1, []) of
                {ok, Terms} -> {ok, Terms, Full};
                Error -> Error
            end;
        Error ->
            Error
    end.

% eval(File), eval(File, Bindings): evaluate every expression sequence ending with a dot; ok or the first error.
eval(File) -> eval(File, erl_eval:new_bindings()).

eval(File, Bs) ->
    case read_source(File) of
        {ok, Source} -> eval_forms(Source, ignore, 1, undefined, [], Bs);
        Error -> Error
    end.

path_eval(Path, File) -> path_eval(Path, File, erl_eval:new_bindings()).

path_eval(Path, File, Bs) ->
    case path_source(Path, File) of
        {ok, Source, Full} ->
            case eval_forms(Source, ignore, 1, undefined, [], Bs) of
                ok -> {ok, Full};
                Error -> Error
            end;
        Error ->
            Error
    end.

% script(File), script(File, Bindings): eval/1,2 returning {ok, Value} of the last expression sequence.
script(File) -> script(File, erl_eval:new_bindings()).

script(File, Bs) ->
    case read_source(File) of
        {ok, Source} -> eval_forms(Source, return, 1, undefined, [], Bs);
        Error -> Error
    end.

path_script(Path, File) -> path_script(Path, File, erl_eval:new_bindings()).

path_script(Path, File, Bs) ->
    case path_source(Path, File) of
        {ok, Source, Full} ->
            case eval_forms(Source, return, 1, undefined, [], Bs) of
                {ok, Value} -> {ok, Value, Full};
                Error -> Error
            end;
        Error ->
            Error
    end.

% path_open(Path, File, Modes): open/2 of File in the first directory of Path where it is found; an absolute File
% ignores Path.
path_open(Path, File, Modes) ->
    case name(File) of
        error ->
            {error, badarg};
        _ ->
            case filename:pathtype(File) of
                relative -> path_open_first(Path, File, Modes);
                _ -> opened_as(open(File, Modes), File)
            end
    end.

path_open_first([Dir | Rest], File, Modes) ->
    case name(Dir) of
        error ->
            {error, badarg};
        _ ->
            Full = path_join(Dir, File),
            case open(Full, Modes) of
                {ok, Fd} ->
                    {ok, Fd, Full};
                {error, Reason} when Reason =:= enoent; Reason =:= enotdir ->
                    path_open_first(Rest, File, Modes);
                Error ->
                    Error
            end
    end;
path_open_first([], _File, _Modes) ->
    {error, enoent}.

path_join(".", File) -> File;
path_join(Dir, File) -> filename:join(Dir, File).

opened_as({ok, Fd}, Name) -> {ok, Fd, Name};
opened_as(Error, _) -> Error.

% The decoded characters of a file: {Chars, valid}, or {Chars, invalid} up to a byte that is not UTF-8.
read_source(File) ->
    case open(File, [read, binary]) of
        {ok, Fd} -> source_of(Fd);
        Error -> Error
    end.

path_source(Path, File) ->
    case path_open(Path, File, [read, binary]) of
        {ok, Fd, Full} ->
            case source_of(Fd) of
                {ok, Source} -> {ok, Source, Full};
                Error -> Error
            end;
        Error ->
            Error
    end.

source_of(Fd) ->
    Read = clause_epp:read_all(Fd),
    _ = close(Fd),
    case Read of
        {ok, Bytes} -> {ok, element(2, clause_epp:source(Bytes, utf8))};
        Error -> Error
    end.

consult_forms(Source, Line, Acc) ->
    case next_tokens(Source, Line) of
        {{ok, Tokens, End}, Rest} ->
            case erl_parse:parse_term(Tokens) of
                {ok, Term} -> consult_forms(Rest, End, [Term | Acc]);
                {error, Error} -> {error, Error}
            end;
        {{error, Error, _}, _} ->
            {error, Error};
        {{error, _} = Error, _} ->
            Error;
        {{eof, _}, _} ->
            {ok, lists:reverse(Acc)}
    end.

% The forms of a file evaluated in order with the bindings each leaves; evaluation goes on after an error, which is
% recorded at the line after its form. Handling return gives the last value.
eval_forms(Source, Handling, Line, Last, Errors, Bs) ->
    {Read, Rest} = next_exprs(Source, Line),
    eval_form(Read, Rest, Handling, Last, Errors, Bs).

eval_form({ok, Exprs, End}, Source, Handling, Last, Errors, Bs0) ->
    try erl_eval:exprs(Exprs, Bs0) of
        {value, Value, Bs} -> eval_forms(Source, Handling, End, {Value}, Errors, Bs)
    catch
        Class:Reason:Stack ->
            eval_forms(
                Source, Handling, End, Last, [{End, file, {Class, Reason, Stack}} | Errors], Bs0
            )
    end;
eval_form({error, What, End}, Source, Handling, Last, Errors, Bs) ->
    eval_forms(Source, Handling, End, Last, [What | Errors], Bs);
eval_form({eof, End}, _Source, Handling, Last, Errors, _Bs) ->
    eval_result(Handling, Last, lists:reverse(Errors), End).

eval_result(return, {Value}, [], _End) ->
    {ok, Value};
eval_result(return, undefined, Errors, End) ->
    {error, hd(Errors ++ [{End, file, undefined_script}])};
eval_result(ignore, _, [], _End) ->
    ok;
eval_result(_, _, [First | _], _End) ->
    {error, First}.

next_exprs(Source, Line) ->
    case next_tokens(Source, Line) of
        {{ok, Tokens, End}, Rest} ->
            case erl_parse:parse_exprs(Tokens) of
                {ok, Exprs} -> {{ok, Exprs, End}, Rest};
                {error, Error} -> {{error, Error, End}, Rest}
            end;
        Other ->
            Other
    end.

% The next form's scan as io:read/3 reads it from OTP's file server.
next_tokens(Source, Line) -> clause_epp:scan(Source, Line, []).

% --- Positioned I/O, truncation, advice and space (plan 21 step LIB50) ---

% A file server positions and then reads or writes, so the position moves; a raw file takes integer offsets only
% and keeps its position, as OTP's raw files do.
pread(File, LocNums) when is_pid(File), is_list(LocNums) -> pread_all(File, LocNums, []);
pread(_, _) -> {error, badarg}.

pread_all(_, [], Acc) ->
    {ok, lists:reverse(Acc)};
pread_all(File, [{At, Size} | Rest], Acc) when is_integer(Size), Size >= 0 ->
    case pread(File, At, Size) of
        {ok, Data} -> pread_all(File, Rest, [Data | Acc]);
        eof -> pread_all(File, Rest, [eof | Acc]);
        {error, _} = Error -> Error
    end;
pread_all(_, _, _) ->
    {error, badarg}.

pread(File, At, Size) when is_pid(File), is_integer(Size), Size >= 0 -> call(File, {pread, At, Size});
pread(_, _, _) -> {error, badarg}.

% pwrite(File, LocBytes): ok, or {error, {N, Reason}} after N successful writes.
pwrite(File, LocBytes) when is_pid(File), is_list(LocBytes) -> pwrite_all(File, LocBytes, 0);
pwrite(_, _) -> {error, badarg}.

pwrite_all(_, [], _) ->
    ok;
pwrite_all(File, [{At, Bytes} | Rest], Done) ->
    case pwrite(File, At, Bytes) of
        ok -> pwrite_all(File, Rest, Done + 1);
        {error, Reason} -> {error, {Done, Reason}}
    end;
pwrite_all(_, _, _) ->
    {error, badarg}.

pwrite(File, At, Bytes) when is_pid(File) -> call(File, {pwrite, At, Bytes});
pwrite(_, _, _) -> {error, badarg}.

% Cut the file at its current position.
truncate(File) when is_pid(File) -> call(File, truncate);
truncate(_) -> {error, badarg}.

% Access advice for a range: ok on every platform that takes or ignores it.
advise(File, Offset, Length, Advice) when is_pid(File) -> call(File, {advise, Offset, Length, Advice});
advise(_, _, _, _) -> {error, badarg}.

% Reserve space for a range; {error, enotsup} where the platform cannot (Windows, as OTP).
allocate(File, Offset, Length) when is_pid(File) -> call(File, {allocate, Offset, Length}).

% --- File information changes (plan 21 step LIB50) ---

write_file_info(File, #file_info{} = Info) -> write_file_info(File, Info, []).

% Change owner, permissions and times in prim_file's order, from the fields that are set; an undefined atime is now,
% an undefined mtime the atime and an undefined ctime the mtime. {error, badarg} for bad fields and times.
write_file_info(File, #file_info{} = Info, Options) when is_list(Options) ->
    Time = time_type(Options),
    case name(File) of
        error ->
            {error, badarg};
        Name ->
            try
                change_info(Name, Info, Time)
            catch
                throw:Reason -> {error, Reason};
                error:_ -> {error, badarg}
            end
    end.

raw_write_file_info(File, #file_info{} = Info) -> write_file_info(File, Info, [raw]).

% The time option of a list, as prim_file reads it.
time_type([{time, Time} | _]) -> Time;
time_type([time | _]) -> true;
time_type([_ | Rest]) -> time_type(Rest);
time_type([]) -> local.

change_info(Name, #file_info{mode = Mode, uid = Uid, gid = Gid, atime = A, mtime = M, ctime = C}, Time) ->
    ATime = int64(access_time(A, Time)),
    MTime = int64(later_time(M, ATime, Time)),
    CTime = int64(later_time(C, MTime, Time)),
    changed(path_change(Name, ?SET_OWNER, <<(owner_id(Uid)):32/signed, (owner_id(Gid)):32/signed>>)),
    changed(mode_change(Name, Mode)),
    changed(path_change(Name, ?SET_TIMES, <<ATime:64/signed, MTime:64/signed, CTime:64/signed>>)).

access_time(undefined, local) -> posix_time(erlang:localtime(), local);
access_time(undefined, universal) -> posix_time(erlang:universaltime(), universal);
access_time(undefined, posix) -> erlang:universaltime_to_posixtime(erlang:universaltime());
access_time(Time, Type) -> posix_time(Time, Type).

later_time(undefined, Earlier, _) -> Earlier;
later_time(Time, _, Type) -> posix_time(Time, Type).

posix_time(Seconds, posix) when is_integer(Seconds) -> Seconds;
posix_time({_, _} = Time, universal) -> erlang:universaltime_to_posixtime(Time);
posix_time({_, _} = Time, local) -> erlang:universaltime_to_posixtime(erlang:localtime_to_universaltime(Time)).

int64(N) when ?IS_INT64(N) -> N.

owner_id(undefined) -> -1;
owner_id(Id) when is_integer(Id), Id >= -16#80000000, Id =< 16#7FFFFFFF -> Id.

mode_change(_, undefined) ->
    ok;
mode_change(Name, Mode) when is_integer(Mode), Mode >= 0, Mode =< 16#FFFFFFFF ->
    path_change(Name, ?SET_PERMISSIONS, <<Mode:32>>).

path_change(Name, Operation, Head) -> path_call(Name, Operation, fun(Path) -> [Head, Path] end, fun(_) -> ok end).

% A change that failed ends write_file_info with its reason; enotsup counts as done, as prim_file counts it.
changed(ok) -> ok;
changed({error, enotsup}) -> ok;
changed({error, Reason}) -> throw(Reason).

change_mode(Name, Mode) when is_integer(Mode) -> write_file_info(Name, #file_info{mode = Mode}).

change_owner(Name, Uid) when is_integer(Uid) -> write_file_info(Name, #file_info{uid = Uid}).

change_owner(Name, Uid, Gid) when is_integer(Uid), is_integer(Gid) ->
    write_file_info(Name, #file_info{uid = Uid, gid = Gid}).

change_group(Name, Gid) when is_integer(Gid) -> write_file_info(Name, #file_info{gid = Gid}).

change_time(Name, {{Y, Mo, D}, {H, Mi, S}} = MTime) when
    is_integer(Y), is_integer(Mo), is_integer(D), is_integer(H), is_integer(Mi), is_integer(S)
->
    write_file_info(Name, #file_info{mtime = MTime}).

change_time(Name, {{AY, AMo, AD}, {AH, AMi, AS}} = ATime, {{MY, MMo, MD}, {MH, MMi, MS}} = MTime) when
    is_integer(AY),
    is_integer(AMo),
    is_integer(AD),
    is_integer(AH),
    is_integer(AMi),
    is_integer(AS),
    is_integer(MY),
    is_integer(MMo),
    is_integer(MD),
    is_integer(MH),
    is_integer(MMi),
    is_integer(MS)
->
    write_file_info(Name, #file_info{atime = ATime, mtime = MTime}).

% --- sendfile (plan 21 step LIB50) ---

% Clause has no native sendfile: the data is read in chunks (at most 1 MiB) and sent with gen_tcp:send/2 or the
% given fun, OTP's fallback. Only a raw file may be sent, as OTP requires; its position is kept.
-define(MAX_CHUNK_SIZE, (1 bsl 20)).

sendfile(File, Socket) ->
    case open(File, [read, raw, binary]) of
        {ok, Fd} ->
            Result = sendfile(Fd, Socket, 0, 0, []),
            _ = close(Fd),
            Result;
        Error ->
            Error
    end.

sendfile(File, Socket, Offset, Bytes, Options) when is_pid(File) ->
    case call(File, raw) of
        true -> sendfile_options(File, Socket, Offset, Bytes, Options);
        _ -> {error, badarg}
    end;
sendfile(_, _, _, _, _) ->
    {error, badarg}.

sendfile_options(File, Socket, Offset, Bytes, []) ->
    sendfile_to(File, Socket, Offset, Bytes, ?MAX_CHUNK_SIZE);
sendfile_options(File, Socket, Offset, Bytes, Options) ->
    try proplists:get_value(chunk_size, Options, ?MAX_CHUNK_SIZE) of
        Size when is_integer(Size) -> sendfile_to(File, Socket, Offset, Bytes, min(Size, ?MAX_CHUNK_SIZE));
        _ -> {error, badarg}
    catch
        error:_ -> {error, badarg}
    end.

sendfile_to(File, Socket, Offset, Bytes, Chunk) when is_integer(Offset), is_integer(Bytes) ->
    send_from(File, sender(Socket), Offset, Bytes, Chunk);
sendfile_to(_, _, _, _, _) ->
    {error, badarg}.

sender(Socket) when is_port(Socket) -> fun(Data) -> gen_tcp:send(Socket, Data) end;
sender(Send) when is_function(Send, 1) -> Send;
sender(Socket) -> erlang:error({case_clause, Socket}).

send_from(File, Send, Offset, Bytes, Chunk) when Bytes >= 0 ->
    {ok, Current} = position(File, {cur, 0}),
    case position(File, {bof, Offset}) of
        {ok, _} ->
            Result = send_chunks(File, Send, Bytes, Chunk, 0),
            _ = position(File, {bof, Current}),
            Result;
        Error ->
            Error
    end;
send_from(_, _, _, _, _) ->
    {error, einval}.

% Read and send chunks until Bytes are sent (all of the file for 0), the end of the file or an error.
send_chunks(File, Send, Bytes, Chunk, Sent) when Bytes > Sent; Bytes =:= 0 ->
    Size =
        case Bytes of
            0 -> Chunk;
            _ -> min(Bytes - Sent, Chunk)
        end,
    case read(File, Size) of
        {ok, Data} ->
            case Send(Data) of
                ok -> send_chunks(File, Send, Bytes, Chunk, Sent + iolist_size(Data));
                Error -> Error
            end;
        eof ->
            {ok, Sent};
        Error ->
            Error
    end;
send_chunks(_, _, _, _, Sent) ->
    {ok, Sent}.

% --- Error texts (plan 21 step LIB50) ---

format_error({_Line, file, undefined_script}) ->
    "no value returned from script";
format_error({Line, file, {Class, Reason, Stacktrace}}) ->
    io_lib:format("~w: evaluation failed with reason ~w:~w and stacktrace ~w", [Line, Class, Reason, Stacktrace]);
format_error({Line, file, {Reason, Stacktrace}}) ->
    io_lib:format("~w: evaluation failed with reason ~w and stacktrace ~w", [Line, Reason, Stacktrace]);
format_error({Line, Module, Reason}) ->
    io_lib:format("~w: ~ts", [Line, module_error(Module, Reason)]);
format_error(badarg) ->
    "bad argument";
format_error(system_limit) ->
    "a system limit was hit, probably not enough ports";
format_error(terminated) ->
    "the file server process is terminated";
format_error(Reason) ->
    erl_posix_msg:message(Reason).

% The scanner and parser, whose errors consult/1 and the eval family return, are called literally so a program that
% formats those errors links them.
module_error(erl_scan, Reason) -> erl_scan:format_error(Reason);
module_error(erl_parse, Reason) -> erl_parse:format_error(Reason);
module_error(Module, Reason) -> Module:format_error(Reason).

altname(_) -> erlang:error({not_implemented, altname, 1}).
copy_opened(_, _, _) -> erlang:error({not_implemented, copy_opened, 3}).
datasync(File) -> call(File, {sync, 1}).
ipread_s32bu_p32bu(_, _, _) -> erlang:error({not_implemented, ipread_s32bu_p32bu, 3}).
ipread_s32bu_p32bu_int(_, _, _) -> erlang:error({not_implemented, ipread_s32bu_p32bu_int, 3}).
sync(File) -> call(File, {sync, 0}).
