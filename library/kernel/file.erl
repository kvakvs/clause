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
%% OTP 29 exports Clause does not implement yet: stubs raising error({not_implemented, F, A}),
%% or catalog builtins the runtime puts in their place (docs/library.md#runtime-module-replacements).
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

% The project-owned file module (docs/library.md, docs/ports.md#standard-io-and-files): files are ports of the
% runtime's file driver ({spawn_driver, "clause_file"}), driven with port_control/3. An open file is an I/O
% server process that owns its port and is linked to the opener, as in OTP.

-define(OPEN, 1).
-define(READ, 2).
-define(WRITE, 3).
-define(POSITION, 4).
-define(READ_LINE, 5).
-define(CLOSE, 6).
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

% Open File with Modes (read, write, append, exclusive, binary; others are ignored, as OTP ignores options it does
% not use): {ok, IoDevice} or {error, Reason}.
open(File, Modes) when is_list(Modes) ->
    case name(File) of
        error ->
            {error, badarg};
        Name ->
            Self = self(),
            Server = spawn_link(fun() -> serve(Self, Name, Modes) end),
            receive
                {Server, opened, Result} -> Result
            end
    end;
open(_, _) ->
    {error, badarg}.

read(File, Size) when is_integer(Size), Size >= 0 -> call(File, {read, Size});
read(_, _) -> {error, badarg}.

write(File, Bytes) -> call(File, {write, Bytes}).

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
    <<Size:64, Type, Access, ATime:64/signed, MTime:64/signed, CTime:64/signed, Mode:32, Links:32,
        Major:32, Minor:32, Inode:64, Uid:32, Gid:32>>,
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

% The I/O server of one open file.
serve(Opener, Name, Modes) ->
    Port = open_port({spawn_driver, "clause_file"}, [binary]),
    case reply(port_control(Port, ?OPEN, [mode_byte(Modes), Name])) of
        {ok, _} ->
            Opener ! {self(), opened, {ok, self()}},
            loop(Opener, Port, lists:member(binary, Modes));
        Error ->
            unlink(Opener),
            Opener ! {self(), opened, Error}
    end.

loop(Opener, Port, Binary) ->
    receive
        {file_request, From, Ref, close} ->
            port_close(Port),
            From ! {file_reply, Ref, ok};
        {file_request, From, Ref, Request} ->
            From ! {file_reply, Ref, request(Port, Binary, Request)},
            loop(Opener, Port, Binary);
        {io_request, From, ReplyAs, Request} ->
            case io_request(Port, Binary, Request) of
                {stop, Reason, Reply} ->
                    % OTP's file server is not linked to the opener, so the opener outlives this exit.
                    From ! {io_reply, ReplyAs, Reply},
                    unlink(Opener),
                    exit(Reason);
                Reply ->
                    From ! {io_reply, ReplyAs, Reply},
                    loop(Opener, Port, Binary)
            end
    end.

% --- OTP's I/O protocol on an open file (step 41ZC) ---

% Answer one I/O request as OTP's file I/O server does: the reply, or {stop, Reason, Reply} when the server must exit
% after replying (a failing get_until function, an untranslatable put_chars).
io_request(Port, Binary, {get_line, _, _}) ->
    request(Port, Binary, get_line);
io_request(Port, Binary, {get_chars, _, _, Count}) when is_integer(Count), Count >= 0 ->
    case request(Port, Binary, {read, Count}) of
        {ok, Data} -> Data;
        Other -> Other
    end;
io_request(Port, Binary, {get_until, Encoding, _, M, F, Args}) ->
    until(Port, Binary, {Encoding, M, F, Args}, []);
io_request(Port, Binary, {put_chars, Encoding, M, F, A}) ->
    case catch apply(M, F, A) of
        {'EXIT', Reason} -> {stop, Reason, {error, F}};
        Chars -> io_request(Port, Binary, {put_chars, Encoding, Chars})
    end;
io_request(Port, _, {put_chars, Encoding, Chars}) ->
    case unicode:characters_to_binary(Chars, Encoding, latin1) of
        Bytes when is_binary(Bytes) ->
            ok_reply(reply(port_control(Port, ?WRITE, Bytes)));
        _ ->
            {stop, no_translation, {error, {no_translation, Encoding, latin1}}}
    end;
io_request(_, _, Request) ->
    {error, {request, Request}}.

% Feed 4 KiB chunks and then eof to M:F until it is done; the characters it leaves over are read again next.
until(Port, Binary, Until, Cont) ->
    case reply(port_control(Port, ?READ, <<4096:64>>)) of
        {ok, Bytes} -> apply_until(Port, Binary, Until, Cont, binary_to_list(Bytes));
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
    Size = byte_size(unicode:characters_to_binary(Rest, latin1)),
    port_control(Port, ?POSITION, <<1, (-Size):64/signed>>);
unread(_, _) ->
    ok.

% Answer one request of an open file.
request(Port, Binary, {read, Size}) ->
    data(reply(port_control(Port, ?READ, <<Size:64>>)), Binary);
request(Port, Binary, read_line) ->
    data(reply(port_control(Port, ?READ_LINE, [])), Binary);
request(Port, Binary, get_line) ->
    case data(reply(port_control(Port, ?READ_LINE, [])), Binary) of
        {ok, Line} -> Line;
        Other -> Other
    end;
request(Port, _, {write, Bytes}) ->
    try iolist_to_binary(Bytes) of
        Data -> ok_reply(reply(port_control(Port, ?WRITE, Data)))
    catch
        error:badarg -> {error, badarg}
    end;
request(Port, _, {position, Location}) ->
    case location(Location) of
        error ->
            {error, einval};
        {Whence, Offset} ->
            case reply(port_control(Port, ?POSITION, <<Whence, Offset:64/signed>>)) of
                {ok, <<Position:64>>} -> {ok, Position};
                Error -> Error
            end
    end.

% A position as a whence byte (0 start, 1 current, 2 end) and an offset.
location(Offset) when is_integer(Offset) -> {0, Offset};
location(bof) -> {0, 0};
location(cur) -> {1, 0};
location(eof) -> {2, 0};
location({bof, Offset}) when is_integer(Offset) -> {0, Offset};
location({cur, Offset}) when is_integer(Offset) -> {1, Offset};
location({eof, Offset}) when is_integer(Offset) -> {2, Offset};
location(_) -> error.

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

% --- Operations on paths ---

% Run one path operation on a new driver port: Encode builds its data from the file's name, Decode its result.
path_call(File, Operation, Encode, Decode) ->
    case name(File) of
        error ->
            {error, badarg};
        Name ->
            Port = open_port({spawn_driver, "clause_file"}, [binary]),
            Reply = reply(port_control(Port, Operation, Encode(Name))),
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

advise(_, _, _, _) -> erlang:error({not_implemented, advise, 4}).
allocate(_, _, _) -> erlang:error({not_implemented, allocate, 3}).
altname(_) -> erlang:error({not_implemented, altname, 1}).
change_group(_, _) -> erlang:error({not_implemented, change_group, 2}).
change_mode(_, _) -> erlang:error({not_implemented, change_mode, 2}).
change_owner(_, _) -> erlang:error({not_implemented, change_owner, 2}).
change_owner(_, _, _) -> erlang:error({not_implemented, change_owner, 3}).
change_time(_, _) -> erlang:error({not_implemented, change_time, 2}).
change_time(_, _, _) -> erlang:error({not_implemented, change_time, 3}).
consult(_) -> erlang:error({not_implemented, consult, 1}).
copy_opened(_, _, _) -> erlang:error({not_implemented, copy_opened, 3}).
datasync(_) -> erlang:error({not_implemented, datasync, 1}).
eval(_) -> erlang:error({not_implemented, eval, 1}).
eval(_, _) -> erlang:error({not_implemented, eval, 2}).
format_error(_) -> erlang:error({not_implemented, format_error, 1}).
ipread_s32bu_p32bu(_, _, _) -> erlang:error({not_implemented, ipread_s32bu_p32bu, 3}).
ipread_s32bu_p32bu_int(_, _, _) -> erlang:error({not_implemented, ipread_s32bu_p32bu_int, 3}).
path_consult(_, _) -> erlang:error({not_implemented, path_consult, 2}).
path_eval(_, _) -> erlang:error({not_implemented, path_eval, 2}).
path_eval(_, _, _) -> erlang:error({not_implemented, path_eval, 3}).
path_open(_, _, _) -> erlang:error({not_implemented, path_open, 3}).
path_script(_, _) -> erlang:error({not_implemented, path_script, 2}).
path_script(_, _, _) -> erlang:error({not_implemented, path_script, 3}).
pread(_, _) -> erlang:error({not_implemented, pread, 2}).
pread(_, _, _) -> erlang:error({not_implemented, pread, 3}).
pwrite(_, _) -> erlang:error({not_implemented, pwrite, 2}).
pwrite(_, _, _) -> erlang:error({not_implemented, pwrite, 3}).
raw_write_file_info(_, _) -> erlang:error({not_implemented, raw_write_file_info, 2}).
script(_) -> erlang:error({not_implemented, script, 1}).
script(_, _) -> erlang:error({not_implemented, script, 2}).
sendfile(_, _) -> erlang:error({not_implemented, sendfile, 2}).
sendfile(_, _, _, _, _) -> erlang:error({not_implemented, sendfile, 5}).
sync(_) -> erlang:error({not_implemented, sync, 1}).
truncate(_) -> erlang:error({not_implemented, truncate, 1}).
write_file_info(_, _) -> erlang:error({not_implemented, write_file_info, 2}).
write_file_info(_, _, _) -> erlang:error({not_implemented, write_file_info, 3}).
