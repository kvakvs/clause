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
            loop(Port, lists:member(binary, Modes));
        Error ->
            unlink(Opener),
            Opener ! {self(), opened, Error}
    end.

loop(Port, Binary) ->
    receive
        {file_request, From, Ref, close} ->
            port_close(Port),
            From ! {file_reply, Ref, ok};
        {file_request, From, Ref, Request} ->
            From ! {file_reply, Ref, request(Port, Binary, Request)},
            loop(Port, Binary)
    end.

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
copy(_, _) -> erlang:error({not_implemented, copy, 2}).
copy(_, _, _) -> erlang:error({not_implemented, copy, 3}).
copy_opened(_, _, _) -> erlang:error({not_implemented, copy_opened, 3}).
datasync(_) -> erlang:error({not_implemented, datasync, 1}).
del_dir_r(_) -> erlang:error({not_implemented, del_dir_r, 1}).
delete(_, _) -> erlang:error({not_implemented, delete, 2}).
eval(_) -> erlang:error({not_implemented, eval, 1}).
eval(_, _) -> erlang:error({not_implemented, eval, 2}).
format_error(_) -> erlang:error({not_implemented, format_error, 1}).
get_cwd() -> erlang:error({not_implemented, get_cwd, 0}).
get_cwd(_) -> erlang:error({not_implemented, get_cwd, 1}).
ipread_s32bu_p32bu(_, _, _) -> erlang:error({not_implemented, ipread_s32bu_p32bu, 3}).
ipread_s32bu_p32bu_int(_, _, _) -> erlang:error({not_implemented, ipread_s32bu_p32bu_int, 3}).
list_dir_all(_) -> erlang:error({not_implemented, list_dir_all, 1}).
make_link(_, _) -> erlang:error({not_implemented, make_link, 2}).
make_symlink(_, _) -> erlang:error({not_implemented, make_symlink, 2}).
native_name_encoding() -> erlang:error({not_implemented, native_name_encoding, 0}).
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
raw_read_file_info(_) -> erlang:error({not_implemented, raw_read_file_info, 1}).
raw_write_file_info(_, _) -> erlang:error({not_implemented, raw_write_file_info, 2}).
read_file(_, _) -> erlang:error({not_implemented, read_file, 2}).
read_file_info(_) -> erlang:error({not_implemented, read_file_info, 1}).
read_file_info(_, _) -> erlang:error({not_implemented, read_file_info, 2}).
read_link(_) -> erlang:error({not_implemented, read_link, 1}).
read_link_all(_) -> erlang:error({not_implemented, read_link_all, 1}).
read_link_info(_) -> erlang:error({not_implemented, read_link_info, 1}).
read_link_info(_, _) -> erlang:error({not_implemented, read_link_info, 2}).
script(_) -> erlang:error({not_implemented, script, 1}).
script(_, _) -> erlang:error({not_implemented, script, 2}).
sendfile(_, _) -> erlang:error({not_implemented, sendfile, 2}).
sendfile(_, _, _, _, _) -> erlang:error({not_implemented, sendfile, 5}).
set_cwd(_) -> erlang:error({not_implemented, set_cwd, 1}).
sync(_) -> erlang:error({not_implemented, sync, 1}).
truncate(_) -> erlang:error({not_implemented, truncate, 1}).
write_file(_, _, _) -> erlang:error({not_implemented, write_file, 3}).
write_file_info(_, _) -> erlang:error({not_implemented, write_file_info, 2}).
write_file_info(_, _, _) -> erlang:error({not_implemented, write_file_info, 3}).
