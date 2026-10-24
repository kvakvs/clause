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

% The project-owned file module (docs/library.md, docs/ports.md#standard-io-and-files): files are ports of the
% runtime's file driver ({spawn_driver, "clause_file"}), driven with port_control/3. An open file is an I/O
% server process that owns its port and is linked to the opener, as in OTP.

-define(OPEN, 1).
-define(READ, 2).
-define(WRITE, 3).
-define(POSITION, 4).
-define(READ_LINE, 5).
-define(CLOSE, 6).
% Flushing an open file to its device, a flag byte 1 for the data only (plan 21 step LIB23).
-define(SYNC, 7).
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
request(Port, _, {sync, DataOnly}) ->
    ok_reply(reply(port_control(Port, ?SYNC, <<DataOnly>>)));
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
    Read = read_all(Fd, []),
    _ = close(Fd),
    case Read of
        {ok, Bytes} -> {ok, decode_source(Bytes)};
        Error -> Error
    end.

read_all(Fd, Acc) ->
    case read(Fd, 65536) of
        {ok, Bytes} -> read_all(Fd, [Bytes | Acc]);
        eof -> {ok, iolist_to_binary(lists:reverse(Acc))};
        Error -> Error
    end.

% epp reads a coding comment from the first 512 bytes of a file.
decode_source(Bytes) ->
    case
        clause_epp:read_encoding_from_binary(binary:part(Bytes, 0, min(512, byte_size(Bytes))), [])
    of
        latin1 ->
            {binary_to_list(Bytes), valid};
        _ ->
            case unicode:characters_to_list(Bytes, utf8) of
                Chars when is_list(Chars) -> {Chars, valid};
                {_, Chars, _} -> {Chars, invalid}
            end
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

% The next form's scan as a get_until request with erl_scan:tokens/3 sees it: eof follows the last character. A form
% that reaches a byte that is not UTF-8 fails as OTP's file server fails, which then ends.
next_tokens({_, terminated} = Source, _Line) ->
    {{error, terminated}, Source};
next_tokens({Chars, Validity}, Line) ->
    scan_on(erl_scan:tokens([], Chars, Line), Validity, Line).

scan_on({done, Result, Rest}, Validity, _Line) ->
    {Result, {Rest, Validity}};
scan_on({more, Cont}, valid, Line) ->
    scan_on(erl_scan:tokens(Cont, eof, Line), valid, Line);
scan_on({more, _}, invalid, Line) ->
    {{error, {Line, file_io_server, invalid_unicode}, Line}, {[], terminated}}.

advise(_, _, _, _) -> erlang:error({not_implemented, advise, 4}).
allocate(_, _, _) -> erlang:error({not_implemented, allocate, 3}).
altname(_) -> erlang:error({not_implemented, altname, 1}).
change_group(_, _) -> erlang:error({not_implemented, change_group, 2}).
change_mode(_, _) -> erlang:error({not_implemented, change_mode, 2}).
change_owner(_, _) -> erlang:error({not_implemented, change_owner, 2}).
change_owner(_, _, _) -> erlang:error({not_implemented, change_owner, 3}).
change_time(_, _) -> erlang:error({not_implemented, change_time, 2}).
change_time(_, _, _) -> erlang:error({not_implemented, change_time, 3}).
copy_opened(_, _, _) -> erlang:error({not_implemented, copy_opened, 3}).
datasync(File) -> call(File, {sync, 1}).
format_error(_) -> erlang:error({not_implemented, format_error, 1}).
ipread_s32bu_p32bu(_, _, _) -> erlang:error({not_implemented, ipread_s32bu_p32bu, 3}).
ipread_s32bu_p32bu_int(_, _, _) -> erlang:error({not_implemented, ipread_s32bu_p32bu_int, 3}).
pread(_, _) -> erlang:error({not_implemented, pread, 2}).
pread(_, _, _) -> erlang:error({not_implemented, pread, 3}).
pwrite(_, _) -> erlang:error({not_implemented, pwrite, 2}).
pwrite(_, _, _) -> erlang:error({not_implemented, pwrite, 3}).
raw_write_file_info(_, _) -> erlang:error({not_implemented, raw_write_file_info, 2}).
sendfile(_, _) -> erlang:error({not_implemented, sendfile, 2}).
sendfile(_, _, _, _, _) -> erlang:error({not_implemented, sendfile, 5}).
sync(File) -> call(File, {sync, 0}).
truncate(_) -> erlang:error({not_implemented, truncate, 1}).
write_file_info(_, _) -> erlang:error({not_implemented, write_file_info, 2}).
write_file_info(_, _, _) -> erlang:error({not_implemented, write_file_info, 3}).
