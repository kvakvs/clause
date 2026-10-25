%% Clause's erl_tar (plan 21 step LIB54D, docs/library.md): tar archives on files, open devices or binaries, written
%% from OTP's documentation and following OTP's format rules: USTAR headers with long names split into a prefix, pax
%% extended headers for what does not fit, reading of v7, GNU (long names, sparse files), STAR, USTAR and pax archives,
%% gzip through file's compressed modes, and the user I/O funs of init/3. Archives equal OTP's byte for byte.
-module(erl_tar).
-export([
    add/3,
    add/4,
    close/1,
    create/2,
    create/3,
    extract/1,
    extract/2,
    format_error/1,
    init/3,
    open/2,
    t/1,
    table/1,
    table/2,
    tt/1
]).
-include("erl_tar.types.hrl").
%% The tar options, header fields and error atoms never name a called module.
-compile({clause_liveness, strict}).

-include_lib("kernel/include/file.hrl").

% How files are added: the file information fun, the copy chunk size, verbose output, and the times, mode and owner
% given to binaries (and over file information).
-record(add_opts, {
    read_info,
    chunk_size = 65536,
    verbose = false,
    atime = undefined,
    mtime = undefined,
    ctime = undefined,
    mode = 8#100644,
    uid = 0,
    gid = 0
}).

% How archives are read: the directory names are relative to, whether to keep existing files, the files to extract,
% memory or file output, open modes, verbose output, the extraction chunk size and the extraction size limit.
-record(read_opts, {
    cwd,
    keep_old_files = false,
    files = all,
    output = file,
    open_mode = [],
    verbose = false,
    chunk_size = 65536,
    max_size = infinity
}).

% One archive member as read or written.
-record(tar_header, {
    name = "",
    mode = 8#100644,
    uid = 0,
    gid = 0,
    size = 0,
    mtime,
    typeflag,
    linkname = "",
    uname = "",
    gname = "",
    devmajor = 0,
    devminor = 0,
    atime,
    ctime
}).

% A data fragment of a sparse file, and the fragments of one GNU header or extension block.
-record(sparse_entry, {offset = 0, num_bytes = 0}).
-record(sparse_array, {entries = [], is_extended = false, max_entries = 0}).

% The raw fields of the header formats.
-record(header_v7, {name, mode, uid, gid, size, mtime, checksum, typeflag, linkname}).
-record(header_gnu, {
    header_v7, magic, version, uname, gname, devmajor, devminor, atime, ctime, sparse, real_size
}).
-record(header_star, {header_v7, magic, version, uname, gname, devmajor, devminor, prefix, atime, ctime, trailer}).
-record(header_ustar, {header_v7, magic, version, uname, gname, devmajor, devminor, prefix}).

% Readers of one member's data: a regular one, and a sparse one that fills the holes with zeros.
-record(reg_file_reader, {handle, num_bytes = 0, pos = 0, size = 0}).
-record(sparse_file_reader, {handle, num_bytes = 0, pos = 0, size = 0, sparse_map = #sparse_array{}}).

-define(S_IFMT, 61440).
-define(S_FIFO, 4096).
-define(S_IFBLK, 24576).
-define(S_IFCHR, 8192).

-define(TYPE_REGULAR, $0).
-define(TYPE_REGULAR_A, 0).
-define(TYPE_LINK, $1).
-define(TYPE_SYMLINK, $2).
-define(TYPE_CHAR, $3).
-define(TYPE_BLOCK, $4).
-define(TYPE_DIR, $5).
-define(TYPE_FIFO, $6).
-define(TYPE_CONT, $7).
-define(TYPE_X_HEADER, $x).
-define(TYPE_GNU_LONGNAME, $L).
-define(TYPE_GNU_LONGLINK, $K).
-define(TYPE_GNU_SPARSE, $S).

-define(PAX_ATIME, <<"atime">>).
-define(PAX_CTIME, <<"ctime">>).
-define(PAX_GID, <<"gid">>).
-define(PAX_GNAME, <<"gname">>).
-define(PAX_LINKPATH, <<"linkpath">>).
-define(PAX_MTIME, <<"mtime">>).
-define(PAX_PATH, <<"path">>).
-define(PAX_SIZE, <<"size">>).
-define(PAX_UID, <<"uid">>).
-define(PAX_UNAME, <<"uname">>).
-define(PAX_XATTR_STR, "SCHILY.xattr.").
-define(PAX_NONE, <<"">>).

-define(FORMAT_UNKNOWN, 0).
-define(FORMAT_V7, 1).
-define(FORMAT_GNU, 2).
-define(FORMAT_STAR, 3).
-define(FORMAT_USTAR, 4).
-define(FORMAT_PAX, 5).

-define(MAGIC_GNU, <<"ustar ">>).
-define(VERSION_GNU, <<" \x00">>).
-define(MAGIC_USTAR, <<"ustar\x00">>).
-define(VERSION_USTAR, <<"00">>).
-define(TRAILER_STAR, <<"tar\x00">>).

-define(BLOCK_SIZE, 512).
-define(MAX_NANO_INT_SIZE, 9).
-define(MAX_INT64, (1 bsl 63 - 1)).
-define(BILLION, 1000000000).
-define(EPOCH, {{1970, 1, 1}, {0, 0, 0}}).

-define(V7_NAME, 0).
-define(V7_NAME_LEN, 100).
-define(V7_MODE, 100).
-define(V7_MODE_LEN, 8).
-define(V7_UID, 108).
-define(V7_UID_LEN, 8).
-define(V7_GID, 116).
-define(V7_GID_LEN, 8).
-define(V7_SIZE, 124).
-define(V7_SIZE_LEN, 12).
-define(V7_MTIME, 136).
-define(V7_MTIME_LEN, 12).
-define(V7_CHKSUM, 148).
-define(V7_CHKSUM_LEN, 8).
-define(V7_TYPE, 156).
-define(V7_TYPE_LEN, 1).
-define(V7_LINKNAME, 157).
-define(V7_LINKNAME_LEN, 100).
-define(STAR_TRAILER, 508).
-define(STAR_TRAILER_LEN, 4).
-define(USTAR_MAGIC, 257).
-define(USTAR_MAGIC_LEN, 6).
-define(USTAR_VERSION, 263).
-define(USTAR_VERSION_LEN, 2).
-define(USTAR_UNAME, 265).
-define(USTAR_UNAME_LEN, 32).
-define(USTAR_GNAME, 297).
-define(USTAR_GNAME_LEN, 32).
-define(USTAR_DEVMAJ, 329).
-define(USTAR_DEVMAJ_LEN, 8).
-define(USTAR_DEVMIN, 337).
-define(USTAR_DEVMIN_LEN, 8).
-define(USTAR_PREFIX, 345).
-define(USTAR_PREFIX_LEN, 155).
-define(GNU_MAGIC, 257).
-define(GNU_MAGIC_LEN, 6).
-define(GNU_VERSION, 263).
-define(GNU_VERSION_LEN, 2).

-define(ZERO_BLOCK, <<0:(?BLOCK_SIZE * 8)>>).

% --- Error texts ---

format_error(invalid_tar_checksum) ->
    "Checksum failed";
format_error(bad_header) ->
    "Unrecognized tar header format";
format_error({bad_header, Reason}) ->
    lists:flatten(io_lib:format("Unrecognized tar header format: ~p", [Reason]));
format_error({invalid_header, negative_size}) ->
    "Invalid header: negative size";
format_error(invalid_sparse_header_size) ->
    "Invalid sparse header: negative size";
format_error(invalid_sparse_map_entry) ->
    "Invalid sparse map entry";
format_error({invalid_sparse_map_entry, Reason}) ->
    lists:flatten(io_lib:format("Invalid sparse map entry: ~p", [Reason]));
format_error(invalid_end_of_archive) ->
    "Invalid end of archive";
format_error(eof) ->
    "Unexpected end of file";
format_error(integer_overflow) ->
    "Failed to parse numeric: integer overflow";
format_error({misaligned_read, Pos}) ->
    lists:flatten(io_lib:format("Read a block which was misaligned: block_size=~p pos=~p", [?BLOCK_SIZE, Pos]));
format_error(invalid_gnu_1_0_sparsemap) ->
    "Invalid GNU sparse map (version 1.0)";
format_error({invalid_gnu_0_1_sparsemap, Format}) ->
    lists:flatten(io_lib:format("Invalid GNU sparse map (version ~s)", [Format]));
format_error(unsafe_path) ->
    "The path points above the current working directory";
format_error(too_big) ->
    "Extraction size exceeds the configured max_size limit";
format_error({Name, Reason}) ->
    lists:flatten(io_lib:format("~ts: ~ts", [Name, format_error(Reason)]));
format_error(Atom) when is_atom(Atom) ->
    file:format_error(Atom);
format_error(Term) ->
    lists:flatten(io_lib:format("~tp", [Term])).

% --- User I/O ---

init(UserData, AccessMode, Fun) when is_function(Fun, 2) ->
    Reader = #reader{handle = UserData, access = AccessMode, func = Fun},
    {ok, Pos, Reader2} = do_position(Reader, {cur, 0}),
    {ok, Reader2#reader{pos = Pos}};
init(_UserData, _AccessMode, _Fun) ->
    {error, badarg}.

% --- Extracting ---

extract(Name) -> extract(Name, []).

extract({binary, Bin}, Opts) when is_list(Opts) ->
    do_extract({binary, Bin}, Opts);
extract({file, Fd}, Opts) when is_list(Opts) ->
    do_extract({file, Fd}, Opts);
extract(#reader{} = Reader, Opts) when is_list(Opts) ->
    do_extract(Reader, Opts);
extract(Name, Opts) when is_list(Name); is_binary(Name), is_list(Opts) ->
    do_extract(Name, Opts).

do_extract(Handle, Opts) when is_list(Opts) ->
    Opts2 = extract_opts(Opts),
    case maybe_inflate_with_limit(Handle, Opts2) of
        {error, _} = Err ->
            Err;
        {ok, Handle2, Opts3} ->
            Acc0 =
                case Opts3#read_opts.output of
                    memory -> [];
                    _ -> ok
                end,
            Acc =
                case Opts3#read_opts.max_size of
                    infinity -> Acc0;
                    _ -> {size_tracked, 0, Acc0}
                end,
            foldl_read(Handle2, fun extract1/4, Acc, Opts3)
    end.

% A compressed binary with a size limit is inflated first, the limit counting inflated bytes.
maybe_inflate_with_limit({binary, Bin}, #read_opts{max_size = MaxSize} = Opts) when
    is_integer(MaxSize), is_binary(Bin)
->
    case lists:member(compressed, Opts#read_opts.open_mode) of
        true ->
            case inflate_with_limit(Bin, MaxSize) of
                {ok, Inflated} ->
                    OpenMode = Opts#read_opts.open_mode -- [compressed],
                    {ok, {binary, Inflated}, Opts#read_opts{open_mode = OpenMode}};
                {error, too_big} ->
                    {error, too_big}
            end;
        false ->
            {ok, {binary, Bin}, Opts}
    end;
maybe_inflate_with_limit(Handle, Opts) ->
    {ok, Handle, Opts}.

inflate_with_limit(Bin, MaxSize) ->
    Z = zlib:open(),
    try
        zlib:inflateInit(Z, 31, cut),
        inflate_with_limit_loop(Z, Bin, MaxSize, 0, [])
    catch
        _:_ -> {ok, Bin}
    after
        zlib:close(Z)
    end.

inflate_with_limit_loop(Z, Bin, MaxSize, Total, Acc) ->
    {Status, Chunks} = zlib:safeInflate(Z, Bin),
    NewTotal = Total + iolist_size(Chunks),
    if
        NewTotal > MaxSize -> {error, too_big};
        Status =:= finished -> {ok, iolist_to_binary(lists:reverse(Acc, Chunks))};
        true -> inflate_with_limit_loop(Z, <<>>, MaxSize, NewTotal, [Chunks | Acc])
    end.

% The fold over members that extracts them; a size-tracked accumulator counts member sizes against max_size.
extract1(eof, Reader, _, {size_tracked, _, Acc}) ->
    extract1(eof, Reader, undefined, Acc);
extract1(#tar_header{size = Size} = Header, Reader0, Opts, {size_tracked, Total, InnerAcc}) ->
    NewTotal = Total + Size,
    case NewTotal > Opts#read_opts.max_size of
        true -> throw({error, too_big});
        false -> ok
    end,
    {ok, NewInnerAcc, Reader1} = extract1(Header, Reader0, Opts, InnerAcc),
    {ok, {size_tracked, NewTotal, NewInnerAcc}, Reader1};
extract1(eof, Reader, _, Acc) when is_list(Acc) ->
    {ok, {ok, lists:reverse(Acc)}, Reader};
extract1(eof, Reader, _, leading_slash) ->
    error_logger:info_msg("erl_tar: removed leading '/' from member names\n"),
    {ok, ok, Reader};
extract1(eof, Reader, _, Acc) ->
    {ok, Acc, Reader};
extract1(#tar_header{name = Name, size = Size} = Header, Reader0, Opts, Acc0) ->
    case check_extract(Name, Opts) of
        true when Opts#read_opts.output =:= memory ->
            case do_read(Reader0, Size) of
                {ok, Bin, Reader1} -> {ok, extract2(Header, Bin, Opts, Acc0), Reader1};
                {error, _} = Err -> throw(Err)
            end;
        true ->
            {ok, Acc0, extract_to_file(Header, Reader0, Opts)};
        false ->
            {ok, Acc0, skip_file(Reader0)}
    end.

extract2(Header, Bin, Opts, Acc) ->
    case write_extracted_element(Header, Bin, Opts) of
        ok ->
            case Header of
                #tar_header{name = "/" ++ _} -> leading_slash;
                #tar_header{} -> Acc
            end;
        {ok, NameBin} when is_list(Acc) ->
            [NameBin | Acc];
        {error, _} = Err ->
            throw(Err)
    end.

% A regular member streams to its file; any other is written from its header.
extract_to_file(#tar_header{name = Name0} = Header, Reader0, Opts) ->
    case typeflag(Header#tar_header.typeflag) of
        regular ->
            Name1 = make_safe_path(Name0, Opts),
            case stream_to_file(Name1, Reader0, Opts) of
                {ok, Reader1} ->
                    read_verbose(Opts, "x ~ts~n", [Name0]),
                    _ = set_extracted_file_info(Name1, Header),
                    Reader1;
                {error, _} = Err ->
                    throw(Err)
            end;
        _ ->
            Reader1 = skip_file(Reader0),
            _ = write_extracted_element(Header, <<>>, Opts),
            Reader1
    end.

stream_to_file(Name, Reader0, Opts) ->
    case Opts#read_opts.keep_old_files andalso existing(Name) of
        true ->
            {ok, skip_file(Reader0)};
        false ->
            case open_output_file(Name) of
                {ok, Fd} ->
                    try
                        stream_to_file_loop(Fd, Reader0, Opts#read_opts.chunk_size)
                    after
                        file:close(Fd)
                    end;
                {error, _} = Err ->
                    Err
            end
    end.

existing(Name) ->
    case file:read_file_info(Name) of
        {ok, _} -> true;
        _ -> false
    end.

open_output_file(Name) ->
    case file:open(Name, [write, raw, binary]) of
        {ok, _} = Ok ->
            Ok;
        {error, enoent} ->
            ok = make_dirs(Name, file),
            file:open(Name, [write, raw, binary]);
        {error, _} = Err ->
            Err
    end.

stream_to_file_loop(_Fd, #reg_file_reader{num_bytes = 0} = Reader, _ChunkSize) ->
    {ok, Reader};
stream_to_file_loop(_Fd, #sparse_file_reader{num_bytes = 0} = Reader, _ChunkSize) ->
    {ok, Reader};
stream_to_file_loop(Fd, Reader, ChunkSize) ->
    case do_read(Reader, ChunkSize) of
        {ok, Bin, Reader1} ->
            case file:write(Fd, Bin) of
                ok -> stream_to_file_loop(Fd, Reader1, ChunkSize);
                {error, _} = Err -> Err
            end;
        {error, _} = Err ->
            Err
    end.

check_extract(_, #read_opts{files = all}) -> true;
check_extract(Name, #read_opts{files = Files}) -> ordsets:is_element(Name, Files).

% --- Listing ---

table(Name) -> table(Name, []).

table(Name, Opts) when is_list(Opts) -> foldl_read(Name, fun table1/4, [], table_opts(Opts)).

table1(eof, Reader, _, Result) ->
    {ok, {ok, lists:reverse(Result)}, Reader};
table1(#tar_header{} = Header, Reader, #read_opts{verbose = Verbose}, Result) ->
    {ok, [table1_attrs(Header, Verbose) | Result], skip_file(Reader)}.

table1_attrs(#tar_header{typeflag = Typeflag, mode = Mode} = Header, true) ->
    #tar_header{name = Name, mtime = Mtime, uid = Uid, gid = Gid, size = Size} = Header,
    {Name, typeflag(Typeflag), Size, Mtime, Mode, Uid, Gid};
table1_attrs(#tar_header{name = Name}, _Verbose) ->
    Name.

typeflag(?TYPE_REGULAR) -> regular;
typeflag(?TYPE_REGULAR_A) -> regular;
typeflag(?TYPE_GNU_SPARSE) -> regular;
typeflag(?TYPE_CONT) -> regular;
typeflag(?TYPE_LINK) -> link;
typeflag(?TYPE_SYMLINK) -> symlink;
typeflag(?TYPE_CHAR) -> char;
typeflag(?TYPE_BLOCK) -> block;
typeflag(?TYPE_DIR) -> directory;
typeflag(?TYPE_FIFO) -> fifo;
typeflag(_) -> unknown.

t(Name) when is_list(Name); is_binary(Name) ->
    case table(Name) of
        {ok, List} -> lists:foreach(fun(N) -> ok = io:format("~ts\n", [N]) end, List);
        Error -> Error
    end.

tt(Name) ->
    case table(Name, [verbose]) of
        {ok, List} -> lists:foreach(fun print_header/1, List);
        Error -> Error
    end.

print_header({Name, Type, Size, Mtime, Mode, Uid, Gid}) ->
    io:format("~s~s ~4w/~-4w ~7w ~s ~s\n", [
        type_to_string(Type), mode_to_string(Mode), Uid, Gid, Size, time_to_string(Mtime), Name
    ]).

type_to_string(regular) -> "-";
type_to_string(directory) -> "d";
type_to_string(link) -> "l";
type_to_string(symlink) -> "s";
type_to_string(char) -> "c";
type_to_string(block) -> "b";
type_to_string(fifo) -> "f";
type_to_string(unknown) -> "?".

% The nine permission bits as rwx letters, lowest bit last.
mode_to_string(Mode) -> mode_to_string(Mode, "xwrxwrxwr", []).

mode_to_string(Mode, [C | T], Acc) when Mode band 1 =:= 1 -> mode_to_string(Mode bsr 1, T, [C | Acc]);
mode_to_string(Mode, [_ | T], Acc) -> mode_to_string(Mode bsr 1, T, [$- | Acc]);
mode_to_string(_, [], Acc) -> Acc.

time_to_string(Secs0) ->
    Secs = calendar:datetime_to_gregorian_seconds(?EPOCH) + Secs0,
    DateTime = calendar:universal_time_to_local_time(calendar:gregorian_seconds_to_datetime(Secs)),
    {{Y, Mon, Day}, {H, Min, _}} = DateTime,
    io_lib:format("~s ~2w ~s:~s ~w", [month(Mon), Day, two_d(H), two_d(Min), Y]).

two_d(N) -> tl(integer_to_list(N + 100)).

month(Mon) -> element(Mon, {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}).

% --- Opening and closing ---

open({binary, Bin}, Mode) when is_binary(Bin) -> do_open({binary, Bin}, Mode);
open({file, Fd}, Mode) -> do_open({file, Fd}, Mode);
open(Name, Mode) when is_list(Name); is_binary(Name) -> do_open(Name, Mode).

do_open(Name, Mode) when is_list(Mode) ->
    case open_mode(Mode) of
        {ok, Access, Raw, Opts} -> open1(Name, Access, Raw, Opts);
        {error, Reason} -> {error, {Name, Reason}}
    end.

% A binary is read as a ram file, inflated first when compressed (as compressed_one would).
open1({binary, Bin0} = Handle, read, _Raw, Opts) when is_binary(Bin0) ->
    Bin =
        case lists:member(compressed, Opts) of
            true -> inflated_binary(Bin0);
            false -> Bin0
        end,
    case file:open(Bin, [ram, binary, read]) of
        {ok, File} -> {ok, #reader{handle = File, access = read, func = fun file_op/2}};
        {error, Reason} -> {error, {Handle, Reason}}
    end;
open1({file, Fd} = Handle, read, [raw], Opts) ->
    case lists:member(compressed, Opts) of
        false ->
            Reader = #reader{handle = Fd, access = read, func = fun file_op/2},
            case do_position(Reader, {cur, 0}) of
                {ok, Pos, Reader2} -> {ok, Reader2#reader{pos = Pos}};
                {error, Reason} -> {error, {Handle, Reason}}
            end;
        true ->
            {error, {Handle, {incompatible_option, compressed}}}
    end;
open1({file, _Fd} = Handle, read, [], _Opts) ->
    {error, {Handle, {incompatible_option, cooked}}};
open1(Name, Access, Raw, Opts0) when is_list(Name); is_binary(Name) ->
    Opts =
        case lists:member(compressed, Opts0) andalso Access == read of
            true -> [compressed_one | (Opts0 -- [compressed])];
            false -> Opts0
        end,
    case file:open(Name, Raw ++ [binary, Access | Opts]) of
        {ok, File} -> {ok, #reader{handle = File, access = Access, func = fun file_op/2}};
        {error, Reason} -> {error, {Name, Reason}}
    end.

inflated_binary(Bin) ->
    Z = zlib:open(),
    zlib:inflateInit(Z, 31, cut),
    try
        IoList = zlib:inflate(Z, Bin),
        zlib:inflateEnd(Z),
        iolist_to_binary(IoList)
    catch
        _:_ -> Bin
    after
        zlib:close(Z)
    end.

open_mode(Mode) -> open_mode(Mode, false, [raw], []).

open_mode(read, _, Raw, _) -> {ok, read, Raw, []};
open_mode(write, _, Raw, _) -> {ok, write, Raw, []};
open_mode([read | Rest], false, Raw, Opts) -> open_mode(Rest, read, Raw, Opts);
open_mode([write | Rest], false, Raw, Opts) -> open_mode(Rest, write, Raw, Opts);
open_mode([compressed | Rest], Access, Raw, Opts) -> open_mode(Rest, Access, Raw, [compressed, read_ahead | Opts]);
open_mode([cooked | Rest], Access, _Raw, Opts) -> open_mode(Rest, Access, [], Opts);
open_mode([], Access, Raw, Opts) -> {ok, Access, Raw, Opts};
open_mode(_, _, _, _) -> {error, einval}.

file_op(write, {Fd, Data}) -> file:write(Fd, Data);
file_op(position, {Fd, Pos}) -> file:position(Fd, Pos);
file_op(read2, {Fd, Size}) -> file:read(Fd, Size);
file_op(close, Fd) -> file:close(Fd).

close(#reader{access = read} = Reader) ->
    ok = do_close(Reader);
close(#reader{access = write} = Reader) ->
    {ok, Reader2} = pad_file(Reader),
    ok = do_close(Reader2),
    ok;
close(_) ->
    {error, einval}.

% The last block padded, then two zero blocks.
pad_file(#reader{pos = Pos} = Reader) ->
    PadCurrent = skip_padding(Pos + ?BLOCK_SIZE),
    do_write(Reader, [<<0:PadCurrent/unit:8>>, ?ZERO_BLOCK, ?ZERO_BLOCK]).

% --- Creating ---

create(Name, FileList) when is_list(Name); is_binary(Name) -> create(Name, FileList, []).

create(Name, FileList, Options) when is_list(Name); is_binary(Name) ->
    Mode = [X || X <- Options, X =:= compressed orelse X =:= cooked],
    case open(Name, [write | Mode]) of
        {ok, TarFile} -> do_create(TarFile, FileList, Options);
        {error, _} = Err -> Err
    end.

do_create(TarFile, [], _Opts) ->
    close(TarFile);
do_create(TarFile, [Entry | Rest], Opts) ->
    {NameInArchive, NameOrBin} =
        case Entry of
            {_, _} -> Entry;
            Name -> {Name, Name}
        end,
    case add(TarFile, NameOrBin, NameInArchive, Opts) of
        ok ->
            do_create(TarFile, Rest, Opts);
        {error, _} = Err ->
            _ = close(TarFile),
            Err
    end.

add(Reader, {NameInArchive, Name}, Opts) when is_list(NameInArchive), is_list(Name) ->
    do_add(Reader, Name, NameInArchive, Opts);
add(Reader, {NameInArchive, Bin}, Opts) when is_list(NameInArchive), is_binary(Bin) ->
    do_add(Reader, Bin, NameInArchive, Opts);
add(Reader, Name, Opts) when is_list(Name) ->
    do_add(Reader, Name, Name, Opts).

add(Reader, NameOrBin, NameInArchive, Options) when
    is_list(NameOrBin); is_binary(NameOrBin), is_list(NameInArchive), is_list(Options)
->
    do_add(Reader, NameOrBin, NameInArchive, Options).

do_add(#reader{access = write} = Reader, Name, NameInArchive, Options) when
    is_list(NameInArchive), is_list(Options)
->
    Opts = #add_opts{read_info = apply_file_info_opts_fun(Options, read_link_info)},
    add1(Reader, Name, NameInArchive, add_opts(Options, Options, Opts));
do_add(#reader{access = read}, _, _, _) ->
    {error, eacces};
do_add(Reader, _, _, _) ->
    {error, {badarg, Reader}}.

add_opts([dereference | T], AllOptions, Opts) ->
    add_opts(T, AllOptions, Opts#add_opts{read_info = apply_file_info_opts_fun(AllOptions, read_file_info)});
add_opts([verbose | T], AllOptions, Opts) ->
    add_opts(T, AllOptions, Opts#add_opts{verbose = true});
add_opts([{chunks, N} | T], AllOptions, Opts) ->
    add_opts(T, AllOptions, Opts#add_opts{chunk_size = N});
add_opts([{atime, Value} | T], AllOptions, Opts) ->
    add_opts(T, AllOptions, Opts#add_opts{atime = Value});
add_opts([{mtime, Value} | T], AllOptions, Opts) ->
    add_opts(T, AllOptions, Opts#add_opts{mtime = Value});
add_opts([{ctime, Value} | T], AllOptions, Opts) ->
    add_opts(T, AllOptions, Opts#add_opts{ctime = Value});
add_opts([{mode, Value} | T], AllOptions, Opts) ->
    add_opts(T, AllOptions, Opts#add_opts{mode = Value});
add_opts([{uid, Value} | T], AllOptions, Opts) ->
    add_opts(T, AllOptions, Opts#add_opts{uid = Value});
add_opts([{gid, Value} | T], AllOptions, Opts) ->
    add_opts(T, AllOptions, Opts#add_opts{gid = Value});
add_opts([_ | T], AllOptions, Opts) ->
    add_opts(T, AllOptions, Opts);
add_opts([], _AllOptions, Opts) ->
    Opts.

% The file information of a name in POSIX seconds, with the time, mode and owner options over it.
apply_file_info_opts_fun(Options, InfoFunction) ->
    fun(F) ->
        case file_info(InfoFunction, F) of
            {ok, FileInfo} -> {ok, do_apply_file_info_opts(Options, FileInfo)};
            Other -> Other
        end
    end.

file_info(read_link_info, F) -> file:read_link_info(F, [{time, posix}]);
file_info(read_file_info, F) -> file:read_file_info(F, [{time, posix}]).

do_apply_file_info_opts([{atime, Value} | T], FileInfo) ->
    do_apply_file_info_opts(T, FileInfo#file_info{atime = Value});
do_apply_file_info_opts([{mtime, Value} | T], FileInfo) ->
    do_apply_file_info_opts(T, FileInfo#file_info{mtime = Value});
do_apply_file_info_opts([{ctime, Value} | T], FileInfo) ->
    do_apply_file_info_opts(T, FileInfo#file_info{ctime = Value});
do_apply_file_info_opts([{mode, Value} | T], FileInfo) ->
    do_apply_file_info_opts(T, FileInfo#file_info{mode = Value});
do_apply_file_info_opts([{uid, Value} | T], FileInfo) ->
    do_apply_file_info_opts(T, FileInfo#file_info{uid = Value});
do_apply_file_info_opts([{gid, Value} | T], FileInfo) ->
    do_apply_file_info_opts(T, FileInfo#file_info{gid = Value});
do_apply_file_info_opts([_ | T], FileInfo) ->
    do_apply_file_info_opts(T, FileInfo);
do_apply_file_info_opts([], FileInfo) ->
    FileInfo.

add1(#reader{} = Reader, Name, NameInArchive, #add_opts{read_info = ReadInfo} = Opts) when is_list(Name) ->
    Res =
        case ReadInfo(Name) of
            {error, Reason0} ->
                {error, {Name, Reason0}};
            {ok, #file_info{type = directory} = Fi} ->
                add_directory(Reader, Name, NameInArchive, Fi, Opts);
            {ok, Fi} ->
                add_entry(Reader, Name, NameInArchive, Fi, Opts)
        end,
    case Res of
        ok -> ok;
        {ok, _Reader} -> ok;
        {error, _Reason} = Err -> Err
    end;
add1(Reader, Bin, NameInArchive, Opts) when is_binary(Bin) ->
    add_verbose(Opts, "a ~ts~n", [NameInArchive]),
    Now = os:system_time(seconds),
    Header = #tar_header{
        name = NameInArchive,
        size = byte_size(Bin),
        typeflag = ?TYPE_REGULAR,
        atime = add_opts_time(Opts#add_opts.atime, Now),
        mtime = add_opts_time(Opts#add_opts.mtime, Now),
        ctime = add_opts_time(Opts#add_opts.ctime, Now),
        uid = Opts#add_opts.uid,
        gid = Opts#add_opts.gid,
        mode = Opts#add_opts.mode
    },
    {ok, Reader2} = add_header(Reader, Header, Opts),
    Padding = skip_padding(byte_size(Bin)),
    case do_write(Reader2, [Bin, <<0:Padding/unit:8>>]) of
        {ok, _Reader3} -> ok;
        {error, Reason} -> {error, {NameInArchive, Reason}}
    end.

add_opts_time(undefined, Now) -> Now;
add_opts_time(Time, _Now) -> Time.

% A file that is not a directory: a symbolic link with its target, a regular file with its contents, or a header.
add_entry(Reader, Name, NameInArchive, #file_info{type = symlink} = Fi, Opts) ->
    add_verbose(Opts, "a ~ts~n", [NameInArchive]),
    {ok, Linkname} = file:read_link(Name),
    add_header(Reader, fileinfo_to_header(NameInArchive, Fi, Linkname), Opts);
add_entry(Reader, Name, NameInArchive, #file_info{type = regular} = Fi, Opts) ->
    add_verbose(Opts, "a ~ts~n", [NameInArchive]),
    Header = fileinfo_to_header(NameInArchive, Fi, false),
    {ok, Reader2} = add_header(Reader, Header, Opts),
    FileSize = Header#tar_header.size,
    {ok, FileSize, Reader3} = do_copy(Reader2, Name, Opts),
    Padding = skip_padding(FileSize),
    do_write(Reader3, <<0:Padding/unit:8>>);
add_entry(Reader, _Name, NameInArchive, Fi, Opts) ->
    add_verbose(Opts, "a ~ts~n", [NameInArchive]),
    add_header(Reader, fileinfo_to_header(NameInArchive, Fi, false), Opts).

% An empty directory is a header of its own; the members of another are added in list_dir's order.
add_directory(Reader, DirName, NameInArchive, Info, Opts) ->
    case file:list_dir(DirName) of
        {ok, []} ->
            add_verbose(Opts, "a ~ts~n", [NameInArchive]),
            add_header(Reader, fileinfo_to_header(NameInArchive, Info, false), Opts);
        {ok, Files} ->
            add_verbose(Opts, "a ~ts~n", [NameInArchive]),
            try add_files(Reader, Files, DirName, NameInArchive, Opts) of
                ok -> ok;
                {error, _} = Err -> Err
            catch
                throw:{error, {_Name, _Reason}} = Err -> Err;
                throw:{error, Reason} -> {error, {DirName, Reason}}
            end;
        {error, Reason} ->
            {error, {DirName, Reason}}
    end.

add_files(_Reader, [], _Dir, _DirInArchive, _Opts) ->
    ok;
add_files(Reader, [Name | Rest], Dir, DirInArchive, #add_opts{read_info = Info} = Opts) ->
    FullName = filename:join(Dir, Name),
    NameInArchive = filename:join(DirInArchive, Name),
    Res =
        case Info(FullName) of
            {error, Reason} -> {error, {FullName, Reason}};
            {ok, #file_info{type = directory} = Fi} -> add_directory(Reader, FullName, NameInArchive, Fi, Opts);
            {ok, Fi} -> add_entry(Reader, FullName, NameInArchive, Fi, Opts)
        end,
    case Res of
        ok -> add_files(Reader, Rest, Dir, DirInArchive, Opts);
        {ok, ReaderNext} -> add_files(ReaderNext, Rest, Dir, DirInArchive, Opts);
        {error, _} = Err -> Err
    end.

% --- Writing headers ---

format_string(String, Size) when length(String) > Size ->
    throw({error, {write_string, field_too_long}});
format_string(String, Size) ->
    Ascii = to_ascii(String),
    if
        byte_size(Ascii) < Size -> [Ascii, 0];
        true -> Ascii
    end.

format_octal(Octal) -> iolist_to_binary(io_lib:fwrite("~.8B", [Octal])).

add_header(#reader{} = Reader, #tar_header{} = Header, Opts) ->
    {ok, Iodata} = build_header(Header, Opts),
    do_write(Reader, Iodata).

write_to_block(Block, IoData, Start) when is_list(IoData) ->
    write_to_block(Block, iolist_to_binary(IoData), Start);
write_to_block(Block, Bin, Start) when is_binary(Bin) ->
    Size = byte_size(Bin),
    <<Head:Start/unit:8, _:Size/unit:8, Rest/binary>> = Block,
    <<Head:Start/unit:8, Bin/binary, Rest/binary>>.

% A USTAR block; values that do not fit go to a pax header written before it.
build_header(#tar_header{} = Header, Opts) ->
    #tar_header{
        name = Name,
        mode = Mode,
        uid = Uid,
        gid = Gid,
        size = Size,
        mtime = Mtime,
        typeflag = Type,
        linkname = Linkname,
        uname = Uname,
        gname = Gname,
        devmajor = Devmaj,
        devminor = Devmin
    } = Header,
    {Block1, Pax0} = write_string(?ZERO_BLOCK, ?V7_NAME, ?V7_NAME_LEN, Name, ?PAX_PATH, #{}),
    Block2 = write_octal(Block1, ?V7_MODE, ?V7_MODE_LEN, Mode),
    {Block3, Pax1} = write_numeric(Block2, ?V7_UID, ?V7_UID_LEN, Uid, ?PAX_UID, Pax0),
    {Block4, Pax2} = write_numeric(Block3, ?V7_GID, ?V7_GID_LEN, Gid, ?PAX_GID, Pax1),
    {Block5, Pax3} = write_numeric(Block4, ?V7_SIZE, ?V7_SIZE_LEN, Size, ?PAX_SIZE, Pax2),
    {Block6, Pax4} = write_numeric(Block5, ?V7_MTIME, ?V7_MTIME_LEN, Mtime, ?PAX_NONE, Pax3),
    {Block7, Pax5} = write_string(Block6, ?V7_TYPE, ?V7_TYPE_LEN, <<Type>>, ?PAX_NONE, Pax4),
    {Block8, Pax6} = write_string(Block7, ?V7_LINKNAME, ?V7_LINKNAME_LEN, Linkname, ?PAX_LINKPATH, Pax5),
    {Block9, Pax7} = write_string(Block8, ?USTAR_UNAME, ?USTAR_UNAME_LEN, Uname, ?PAX_UNAME, Pax6),
    {Block10, Pax8} = write_string(Block9, ?USTAR_GNAME, ?USTAR_GNAME_LEN, Gname, ?PAX_GNAME, Pax7),
    {Block11, Pax9} = write_numeric(Block10, ?USTAR_DEVMAJ, ?USTAR_DEVMAJ_LEN, Devmaj, ?PAX_NONE, Pax8),
    {Block12, Pax10} = write_numeric(Block11, ?USTAR_DEVMIN, ?USTAR_DEVMIN_LEN, Devmin, ?PAX_NONE, Pax9),
    {Block13, Pax11} = set_path(Block12, Pax10),
    PaxEntry =
        case maps:size(Pax11) of
            0 -> [];
            _ -> build_pax_entry(Header, Pax11, Opts)
        end,
    Block14 = set_format(Block13, ?FORMAT_USTAR),
    {ok, [PaxEntry, set_checksum(Block14)]}.

% A long path that splits into a USTAR name and prefix needs no pax path.
set_path(Block0, Pax) ->
    case maps:get(?PAX_PATH, Pax, nil) of
        nil ->
            {Block0, Pax};
        PaxPath ->
            case split_ustar_path(PaxPath) of
                {ok, UstarName, UstarPrefix} ->
                    {Block1, _} = write_string(Block0, ?V7_NAME, ?V7_NAME_LEN, UstarName, ?PAX_NONE, #{}),
                    {Block2, _} = write_string(Block1, ?USTAR_PREFIX, ?USTAR_PREFIX_LEN, UstarPrefix, ?PAX_NONE, #{}),
                    {Block2, maps:remove(?PAX_PATH, Pax)};
                false ->
                    {Block0, Pax}
            end
    end.

set_format(Block0, Format) when Format =:= ?FORMAT_USTAR; Format =:= ?FORMAT_PAX ->
    Block1 = write_to_block(Block0, ?MAGIC_USTAR, ?USTAR_MAGIC),
    write_to_block(Block1, ?VERSION_USTAR, ?USTAR_VERSION);
set_format(_Block, Format) ->
    throw({error, {invalid_format, Format}}).

set_checksum(Block) -> write_octal(Block, ?V7_CHKSUM, ?V7_CHKSUM_LEN, compute_checksum(Block)).

% A pax header named Dir/PaxHeaders.0/Base holding the records in key order.
build_pax_entry(Header, PaxAttrs, Opts) ->
    Path = Header#tar_header.name,
    AsciiPath = to_ascii(filename:join([filename:dirname(Path), "PaxHeaders.0", filename:basename(Path)])),
    Path3 =
        if
            byte_size(AsciiPath) > ?V7_NAME_LEN -> binary_part(AsciiPath, 0, ?V7_NAME_LEN - 1);
            true -> AsciiPath
        end,
    PaxFile = build_pax_file(lists:sort(maps:keys(PaxAttrs)), PaxAttrs),
    Size = byte_size(PaxFile),
    Padding = (?BLOCK_SIZE - (Size rem ?BLOCK_SIZE)) rem ?BLOCK_SIZE,
    PaxHeader = #tar_header{
        name = unicode:characters_to_list(Path3),
        size = Size,
        mtime = Header#tar_header.mtime,
        atime = Header#tar_header.atime,
        ctime = Header#tar_header.ctime,
        typeflag = ?TYPE_X_HEADER
    },
    {ok, PaxHeaderData} = build_header(PaxHeader, Opts),
    [PaxHeaderData, PaxFile, <<0:Padding/unit:8>>].

build_pax_file(Keys, PaxAttrs) -> build_pax_file(Keys, PaxAttrs, []).

% Each record "Length Key=Value\n", its length counted from characters first and from bytes when they differ.
build_pax_file([], _, Acc) ->
    unicode:characters_to_binary(Acc);
build_pax_file([K | Rest], Attrs, Acc) ->
    V = maps:get(K, Attrs),
    Size = sizeof(K) + sizeof(V) + 3,
    Size2 = sizeof(Size) + Size,
    Key = to_string(K),
    Value = to_string(V),
    Record = unicode:characters_to_binary(io_lib:format("~B ~ts=~ts\n", [Size2, Key, Value])),
    case byte_size(Record) of
        Size2 -> build_pax_file(Rest, Attrs, [Acc, Record]);
        Size3 -> build_pax_file(Rest, Attrs, [Acc, io_lib:format("~B ~ts=~ts\n", [Size3, Key, Value])])
    end.

sizeof(Bin) when is_binary(Bin) -> byte_size(Bin);
sizeof(List) when is_list(List) -> length(List);
sizeof(N) when is_integer(N) -> byte_size(integer_to_binary(N));
sizeof(N) when is_float(N) -> byte_size(float_to_binary(N)).

to_string(Bin) when is_binary(Bin) -> unicode:characters_to_list(Bin);
to_string(List) when is_list(List) -> List;
to_string(N) when is_integer(N) -> integer_to_list(N);
to_string(N) when is_float(N) -> float_to_list(N).

% An ASCII path longer than a name: its last part as the name, the parts before as the prefix, when both fit.
split_ustar_path(Path) ->
    case length(Path) =< ?V7_NAME_LEN orelse not is_ascii(Path) of
        true ->
            false;
        false ->
            case filename:split(binary:list_to_bin(Path)) of
                [Part] when byte_size(Part) >= ?V7_NAME_LEN ->
                    false;
                Parts ->
                    case lists:last(Parts) of
                        Name when byte_size(Name) >= ?V7_NAME_LEN ->
                            false;
                        Name ->
                            join_split_ustar_path(lists:sublist(Parts, length(Parts) - 1), {ok, Name, nil})
                    end
            end
    end.

join_split_ustar_path([], Acc) ->
    Acc;
join_split_ustar_path([Part | _], {ok, _, nil}) when byte_size(Part) > ?USTAR_PREFIX_LEN ->
    false;
join_split_ustar_path([Part | _], {ok, _Name, Acc}) when (byte_size(Part) + byte_size(Acc)) > ?USTAR_PREFIX_LEN ->
    false;
join_split_ustar_path([Part | Rest], {ok, Name, nil}) ->
    join_split_ustar_path(Rest, {ok, Name, Part});
join_split_ustar_path([Part | Rest], {ok, Name, Acc}) ->
    join_split_ustar_path(Rest, {ok, Name, <<Acc/binary, $/, Part/binary>>}).

write_octal(Block, Pos, Size, X) ->
    Octal = zero_pad(format_octal(X), Size - 1),
    if
        byte_size(Octal) < Size -> write_to_block(Block, Octal, Pos);
        true -> throw({error, {write_failed, octal_field_too_long}})
    end.

write_string(Block, Pos, Size, Str, PaxAttr, Pax0) ->
    case PaxAttr =/= ?PAX_NONE andalso (length(Str) > Size orelse not is_ascii(Str)) of
        true -> {Block, maps:put(PaxAttr, Str, Pax0)};
        false -> {write_to_block(Block, format_string(Str, Size), Pos), Pax0}
    end.

write_numeric(Block, Pos, Size, X, PaxAttr, Pax0) ->
    Octal = zero_pad(format_octal(X), Size - 1),
    if
        byte_size(Octal) < Size -> {write_to_block(Block, [Octal, 0], Pos), Pax0};
        PaxAttr =/= ?PAX_NONE -> {Block, maps:put(PaxAttr, X, Pax0)};
        true -> throw({error, {write_failed, numeric_field_too_long}})
    end.

zero_pad(Str, Size) when byte_size(Str) >= Size -> Str;
zero_pad(Str, Size) -> <<(binary:copy(<<$0>>, Size - byte_size(Str)))/binary, Str/binary>>.

% --- Reading headers ---

% A block, or eof at two zero blocks (or one zero block at the end of the file).
read_block(Reader) ->
    case do_read(Reader, ?BLOCK_SIZE) of
        eof ->
            throw({error, eof});
        {ok, ?ZERO_BLOCK, Reader1} ->
            case do_read(Reader1, ?BLOCK_SIZE) of
                eof -> eof;
                {ok, ?ZERO_BLOCK, _Reader2} -> eof;
                {ok, _Block, _Reader2} -> throw({error, invalid_end_of_archive});
                {error, _} = Err -> throw(Err)
            end;
        {ok, Block, Reader1} when is_binary(Block) ->
            {ok, Block, Reader1};
        {error, _} = Err ->
            throw(Err)
    end.

get_header(#reader{} = Reader) ->
    case read_block(Reader) of
        eof -> eof;
        {ok, Block, Reader1} -> convert_header(Block, Reader1)
    end.

to_v7(Bin) when is_binary(Bin), byte_size(Bin) =:= ?BLOCK_SIZE ->
    #header_v7{
        name = binary_part(Bin, ?V7_NAME, ?V7_NAME_LEN),
        mode = binary_part(Bin, ?V7_MODE, ?V7_MODE_LEN),
        uid = binary_part(Bin, ?V7_UID, ?V7_UID_LEN),
        gid = binary_part(Bin, ?V7_GID, ?V7_GID_LEN),
        size = binary_part(Bin, ?V7_SIZE, ?V7_SIZE_LEN),
        mtime = binary_part(Bin, ?V7_MTIME, ?V7_MTIME_LEN),
        checksum = binary_part(Bin, ?V7_CHKSUM, ?V7_CHKSUM_LEN),
        typeflag = binary:at(Bin, ?V7_TYPE),
        linkname = binary_part(Bin, ?V7_LINKNAME, ?V7_LINKNAME_LEN)
    };
to_v7(_) ->
    {error, header_block_too_small}.

to_gnu(#header_v7{} = V7, Bin) when is_binary(Bin), byte_size(Bin) =:= ?BLOCK_SIZE ->
    #header_gnu{
        header_v7 = V7,
        magic = binary_part(Bin, ?GNU_MAGIC, ?GNU_MAGIC_LEN),
        version = binary_part(Bin, ?GNU_VERSION, ?GNU_VERSION_LEN),
        uname = binary_part(Bin, 265, 32),
        gname = binary_part(Bin, 297, 32),
        devmajor = binary_part(Bin, 329, 8),
        devminor = binary_part(Bin, 337, 8),
        atime = binary_part(Bin, 345, 12),
        ctime = binary_part(Bin, 357, 12),
        sparse = to_sparse_array(binary_part(Bin, 386, 24 * 4 + 1)),
        real_size = binary_part(Bin, 483, 12)
    }.

to_star(#header_v7{} = V7, Bin) when is_binary(Bin), byte_size(Bin) =:= ?BLOCK_SIZE ->
    #header_star{
        header_v7 = V7,
        magic = binary_part(Bin, ?USTAR_MAGIC, ?USTAR_MAGIC_LEN),
        version = binary_part(Bin, ?USTAR_VERSION, ?USTAR_VERSION_LEN),
        uname = binary_part(Bin, ?USTAR_UNAME, ?USTAR_UNAME_LEN),
        gname = binary_part(Bin, ?USTAR_GNAME, ?USTAR_GNAME_LEN),
        devmajor = binary_part(Bin, ?USTAR_DEVMAJ, ?USTAR_DEVMAJ_LEN),
        devminor = binary_part(Bin, ?USTAR_DEVMIN, ?USTAR_DEVMIN_LEN),
        prefix = binary_part(Bin, 345, 131),
        atime = binary_part(Bin, 476, 12),
        ctime = binary_part(Bin, 488, 12),
        trailer = binary_part(Bin, ?STAR_TRAILER, ?STAR_TRAILER_LEN)
    }.

to_ustar(#header_v7{} = V7, Bin) when is_binary(Bin), byte_size(Bin) =:= ?BLOCK_SIZE ->
    #header_ustar{
        header_v7 = V7,
        magic = binary_part(Bin, ?USTAR_MAGIC, ?USTAR_MAGIC_LEN),
        version = binary_part(Bin, ?USTAR_VERSION, ?USTAR_VERSION_LEN),
        uname = binary_part(Bin, ?USTAR_UNAME, ?USTAR_UNAME_LEN),
        gname = binary_part(Bin, ?USTAR_GNAME, ?USTAR_GNAME_LEN),
        devmajor = binary_part(Bin, ?USTAR_DEVMAJ, ?USTAR_DEVMAJ_LEN),
        devminor = binary_part(Bin, ?USTAR_DEVMIN, ?USTAR_DEVMIN_LEN),
        prefix = binary_part(Bin, 345, 155)
    }.

% The 24-byte entries of a sparse block, the byte after them saying whether an extension block follows.
to_sparse_array(Bin) when is_binary(Bin) ->
    MaxEntries = byte_size(Bin) div 24,
    #sparse_array{
        entries = parse_sparse_entries(Bin, MaxEntries - 1, []),
        max_entries = MaxEntries,
        is_extended = 1 =:= binary:at(Bin, 24 * MaxEntries)
    }.

parse_sparse_entries(<<>>, _, Acc) ->
    Acc;
parse_sparse_entries(_, -1, Acc) ->
    Acc;
parse_sparse_entries(Bin, N, Acc) ->
    case to_sparse_entry(binary_part(Bin, N * 24, 24)) of
        nil -> parse_sparse_entries(Bin, N - 1, Acc);
        Entry -> parse_sparse_entries(Bin, N - 1, [Entry | Acc])
    end.

to_sparse_entry(<<0:96, 0:96>>) ->
    nil;
to_sparse_entry(<<OffsetBin:12/binary, NumBytesBin:12/binary>>) ->
    #sparse_entry{offset = parse_numeric(OffsetBin), num_bytes = parse_numeric(NumBytesBin)}.

get_format(Bin) when is_binary(Bin), byte_size(Bin) =:= ?BLOCK_SIZE -> do_get_format(to_v7(Bin), Bin).

% The format by its magic, or unknown when the checksum (unsigned or signed) does not match.
do_get_format({error, _} = Err, _Bin) ->
    Err;
do_get_format(#header_v7{} = V7, Bin) when is_binary(Bin), byte_size(Bin) =:= ?BLOCK_SIZE ->
    Checksum = parse_octal(V7#header_v7.checksum),
    IsBadChecksum =
        case compute_checksum(Bin) of
            Checksum -> false;
            _ -> compute_signed_checksum(Bin) =/= Checksum
        end,
    case IsBadChecksum of
        true ->
            ?FORMAT_UNKNOWN;
        false ->
            #header_ustar{magic = Magic, version = Version} = to_ustar(V7, Bin),
            #header_star{trailer = Trailer} = to_star(V7, Bin),
            Format =
                if
                    Magic =:= ?MAGIC_USTAR, Trailer =:= ?TRAILER_STAR -> ?FORMAT_STAR;
                    Magic =:= ?MAGIC_USTAR -> ?FORMAT_USTAR;
                    Magic =:= ?MAGIC_GNU, Version =:= ?VERSION_GNU -> ?FORMAT_GNU;
                    true -> ?FORMAT_V7
                end,
            {ok, Format, V7}
    end.

% The header of a block and the reader of its data.
unpack_format(Format, #header_v7{} = V7, Bin, Reader) when is_binary(Bin), byte_size(Bin) =:= ?BLOCK_SIZE ->
    Mtime = parse_numeric(V7#header_v7.mtime),
    Header0 = #tar_header{
        name = parse_string(V7#header_v7.name),
        mode = parse_numeric(V7#header_v7.mode),
        uid = parse_numeric(V7#header_v7.uid),
        gid = parse_numeric(V7#header_v7.gid),
        size = parse_numeric(V7#header_v7.size),
        mtime = Mtime,
        atime = Mtime,
        ctime = Mtime,
        typeflag = V7#header_v7.typeflag,
        linkname = parse_string(V7#header_v7.linkname)
    },
    Typeflag = Header0#tar_header.typeflag,
    Header1 =
        if
            Format > ?FORMAT_V7 -> unpack_modern(Format, V7, Bin, Header0);
            true -> Header0#tar_header{name = safe_join_path("", Header0#tar_header.name)}
        end,
    Header2 =
        case is_header_only_type(Typeflag) of
            true -> Header1#tar_header{size = 0};
            false -> Header1
        end,
    case Typeflag of
        ?TYPE_GNU_SPARSE ->
            Gnu = to_gnu(V7, Bin),
            RealSize = parse_numeric(Gnu#header_gnu.real_size),
            {Sparsemap, Reader2} = parse_sparse_map(Gnu, Reader),
            {Header2#tar_header{size = RealSize}, new_sparse_file_reader(Reader2, Sparsemap, RealSize)};
        _ ->
            Size = Header2#tar_header.size,
            {Header2, #reg_file_reader{handle = Reader, num_bytes = Size, size = Size, pos = 0}}
    end.

unpack_modern(Format, #header_v7{} = V7, Bin, #tar_header{} = Header0) when is_binary(Bin) ->
    Typeflag = Header0#tar_header.typeflag,
    Ustar = to_ustar(V7, Bin),
    H0 = Header0#tar_header{
        uname = parse_string(Ustar#header_ustar.uname), gname = parse_string(Ustar#header_ustar.gname)
    },
    H1 =
        if
            Typeflag =:= ?TYPE_CHAR orelse Typeflag =:= ?TYPE_BLOCK ->
                H0#tar_header{
                    devmajor = parse_numeric(Ustar#header_ustar.devmajor),
                    devminor = parse_numeric(Ustar#header_ustar.devminor)
                };
            true ->
                H0
        end,
    {Prefix, H2} =
        case Format of
            ?FORMAT_USTAR ->
                {parse_string(Ustar#header_ustar.prefix), H1};
            ?FORMAT_STAR ->
                Star = to_star(V7, Bin),
                {parse_string(Star#header_star.prefix), H1#tar_header{
                    atime = parse_numeric(Star#header_star.atime), ctime = parse_numeric(Star#header_star.ctime)
                }};
            _ ->
                {"", H1}
        end,
    H2#tar_header{name = safe_join_path(Prefix, H2#tar_header.name)}.

safe_join_path([], Name) -> filename:join([Name]);
safe_join_path(Prefix, []) -> filename:join([Prefix]);
safe_join_path(Prefix, Name) -> filename:join(Prefix, Name).

new_sparse_file_reader(Reader, Sparsemap, RealSize) ->
    true = validate_sparse_entries(Sparsemap, RealSize),
    #sparse_file_reader{handle = Reader, num_bytes = RealSize, pos = 0, size = RealSize, sparse_map = Sparsemap}.

validate_sparse_entries(Entries, RealSize) -> validate_sparse_entries(Entries, RealSize, 0, 0).

validate_sparse_entries([], _RealSize, _I, _LastOffset) ->
    true;
validate_sparse_entries([#sparse_entry{offset = Offset, num_bytes = NumBytes} | Rest], RealSize, I, LastOffset) ->
    if
        Offset > ?MAX_INT64 - NumBytes -> throw({error, {invalid_sparse_map_entry, offset_too_large}});
        Offset + NumBytes > RealSize -> throw({error, {invalid_sparse_map_entry, offset_too_large}});
        I > 0 andalso LastOffset > Offset -> throw({error, {invalid_sparse_map_entry, overlapping_offsets}});
        true -> ok
    end,
    validate_sparse_entries(Rest, RealSize, I + 1, Offset + NumBytes).

parse_sparse_map(#header_gnu{sparse = Sparse}, Reader) when Sparse#sparse_array.is_extended ->
    parse_sparse_map(Sparse, Reader, []);
parse_sparse_map(#header_gnu{sparse = Sparse}, Reader) ->
    {Sparse#sparse_array.entries, Reader}.

parse_sparse_map(#sparse_array{is_extended = true, entries = Entries}, Reader, Acc) ->
    case read_block(Reader) of
        eof -> throw({error, eof});
        {ok, Block, Reader2} -> parse_sparse_map(to_sparse_array(Block), Reader2, Entries ++ Acc)
    end;
parse_sparse_map(#sparse_array{entries = Entries}, Reader, Acc) ->
    Sorted = lists:sort(fun(#sparse_entry{offset = A}, #sparse_entry{offset = B}) -> A =< B end, Entries ++ Acc),
    {Sorted, Reader}.

% The byte sum of a block with its checksum field read as spaces.
compute_checksum(
    <<H1:?V7_CHKSUM/binary, H2:?V7_CHKSUM_LEN/binary, Rest:(?BLOCK_SIZE - ?V7_CHKSUM - ?V7_CHKSUM_LEN)/binary,
        _/binary>>
) ->
    checksum(H1) + byte_size(H2) * $\s + checksum(Rest).

compute_signed_checksum(
    <<H1:?V7_CHKSUM/binary, H2:?V7_CHKSUM_LEN/binary, Rest:(?BLOCK_SIZE - ?V7_CHKSUM - ?V7_CHKSUM_LEN)/binary,
        _/binary>>
) ->
    signed_checksum(H1) + byte_size(H2) * $\s + signed_checksum(Rest).

checksum(Bin) -> lists:sum(binary_to_list(Bin)).

signed_checksum(Bin) -> lists:sum([B || <<B/signed>> <= Bin]).

% A number in base 256 (two's complement, high bit set) or in octal.
parse_numeric(<<>>) ->
    0;
parse_numeric(<<First, _/binary>> = Bin) when First band 16#80 =/= 0 ->
    Inv =
        if
            First band 16#40 =/= 0 -> 16#00;
            true -> 16#FF
        end,
    {_, N} = lists:foldl(fun(C, Acc) -> base256_digit(C, Inv, Acc) end, {0, 0}, binary_to_list(Bin)),
    if
        (N bsr 63) > 0 -> throw({error, integer_overflow});
        Inv =:= 16#FF -> -1 bxor N;
        true -> N
    end;
parse_numeric(Bin) ->
    parse_octal(Bin).

base256_digit(C, Inv, {I, X}) ->
    C1 = C bxor Inv,
    C2 =
        case I of
            0 -> C1 band 16#7F;
            _ -> C1
        end,
    if
        (X bsr 56) > 0 -> throw({error, integer_overflow});
        true -> {I + 1, (X bsl 8) bor C2}
    end.

% Octal digits, zero bytes and spaces skipped anywhere.
parse_octal(<<Bin/binary>>) -> do_parse_octal(Bin, 0).

do_parse_octal(<<$\s, Rest/binary>>, Acc) ->
    do_parse_octal(Rest, Acc);
do_parse_octal(<<0, Rest/binary>>, Acc) ->
    do_parse_octal(Rest, Acc);
do_parse_octal(<<C, Rest/binary>>, Acc) ->
    Digit = C - $0,
    case Digit band 7 of
        Digit -> do_parse_octal(Rest, Acc bsl 3 bor Digit);
        _ -> throw({error, invalid_tar_checksum})
    end;
do_parse_octal(<<>>, Acc) ->
    Acc.

% The bytes up to the first zero, as UTF-8 or else as Latin-1.
parse_string(Bin) when is_binary(Bin) ->
    N = strlen(Bin, 0),
    <<Prefix:N/binary, _/binary>> = Bin,
    case unicode:characters_to_list(Prefix) of
        Str when is_list(Str) -> Str;
        {incomplete, _Str, _Rest} -> binary_to_list(Bin);
        {error, _Str, _Rest} -> throw({error, {bad_header, invalid_string}})
    end.

strlen(<<>>, N) -> N;
strlen(<<0, _/binary>>, N) -> N;
strlen(<<_, Rest/binary>>, N) -> strlen(Rest, N + 1).

convert_header(Bin, #reader{pos = Pos} = Reader) when byte_size(Bin) =:= ?BLOCK_SIZE, (Pos rem ?BLOCK_SIZE) =:= 0 ->
    case get_format(Bin) of
        ?FORMAT_UNKNOWN -> throw({error, bad_header});
        {ok, Format, V7} -> unpack_format(Format, V7, Bin, Reader);
        {error, Reason} -> throw({error, {bad_header, Reason}})
    end;
convert_header(Bin, #reader{pos = Pos}) when byte_size(Bin) =:= ?BLOCK_SIZE ->
    % OTP throws this three-tuple.
    throw({error, misaligned_read, Pos});
convert_header(Bin, _Reader) when byte_size(Bin) =:= 0 ->
    eof;
convert_header(_Bin, _Reader) ->
    throw({error, eof}).

fileinfo_to_header(Name, #file_info{} = Fi, Link) when is_list(Name) ->
    BaseHeader = #tar_header{
        name = Name,
        mtime = Fi#file_info.mtime,
        atime = Fi#file_info.atime,
        ctime = Fi#file_info.ctime,
        mode = Fi#file_info.mode,
        uid = Fi#file_info.uid,
        gid = Fi#file_info.gid,
        typeflag = ?TYPE_REGULAR
    },
    do_fileinfo_to_header(BaseHeader, Fi, Link).

do_fileinfo_to_header(Header, #file_info{size = Size, type = regular}, _Link) ->
    Header#tar_header{size = Size, typeflag = ?TYPE_REGULAR};
do_fileinfo_to_header(#tar_header{name = Name} = Header, #file_info{type = directory}, _Link) ->
    Header#tar_header{name = Name ++ "/", typeflag = ?TYPE_DIR};
do_fileinfo_to_header(Header, #file_info{type = symlink}, Link) ->
    Header#tar_header{typeflag = ?TYPE_SYMLINK, linkname = Link};
do_fileinfo_to_header(Header, #file_info{type = device, mode = Mode} = Fi, _Link) when
    (Mode band ?S_IFMT) =:= ?S_IFCHR
->
    Header#tar_header{
        typeflag = ?TYPE_CHAR, devmajor = Fi#file_info.major_device, devminor = Fi#file_info.minor_device
    };
do_fileinfo_to_header(Header, #file_info{type = device, mode = Mode} = Fi, _Link) when
    (Mode band ?S_IFMT) =:= ?S_IFBLK
->
    Header#tar_header{
        typeflag = ?TYPE_BLOCK, devmajor = Fi#file_info.major_device, devminor = Fi#file_info.minor_device
    };
do_fileinfo_to_header(Header, #file_info{type = other, mode = Mode}, _Link) when (Mode band ?S_IFMT) =:= ?S_FIFO ->
    Header#tar_header{typeflag = ?TYPE_FIFO};
do_fileinfo_to_header(Header, Fi, _Link) ->
    {error, {invalid_file_type, Header#tar_header.name, Fi}}.

is_ascii(Str) when is_list(Str) -> not lists:any(fun(Char) -> Char >= 16#80 end, Str);
is_ascii(Bin) when is_binary(Bin) -> is_ascii1(Bin).

is_ascii1(<<>>) -> true;
is_ascii1(<<C, _Rest/binary>>) when C >= 16#80 -> false;
is_ascii1(<<_, Rest/binary>>) -> is_ascii1(Rest).

% The characters below 128.
to_ascii(Str) when is_list(Str) -> unicode:characters_to_binary([C || C <- Str, C < 16#80]);
to_ascii(Bin) when is_binary(Bin) -> <<<<C>> || <<C>> <= Bin, C < 16#80>>.

is_header_only_type(?TYPE_SYMLINK) -> true;
is_header_only_type(?TYPE_LINK) -> true;
is_header_only_type(?TYPE_DIR) -> true;
is_header_only_type(_) -> false.

% --- Folding over members ---

foldl_read(#reader{access = read} = Reader, Fun, Accu, #read_opts{} = Opts) when is_function(Fun, 4) ->
    case foldl_read0(Reader, Fun, Accu, Opts) of
        {ok, Result, _Reader2} -> Result;
        {error, _} = Err -> Err
    end;
foldl_read(#reader{access = Access}, _Fun, _Accu, _Opts) ->
    {error, {read_mode_expected, Access}};
foldl_read(TarName, Fun, Accu, #read_opts{} = Opts) when is_function(Fun, 4) ->
    try open(TarName, [read | Opts#read_opts.open_mode]) of
        {ok, #reader{access = read} = Reader} ->
            try
                foldl_read(Reader, Fun, Accu, Opts)
            after
                _ = close(Reader)
            end;
        {error, _} = Err ->
            Err
    catch
        throw:Err -> Err
    end.

foldl_read0(Reader, Fun, Accu, Opts) ->
    try foldl_read1(Fun, Accu, Reader, Opts, #{}) of
        {ok, _, _} = Ok -> Ok
    catch
        throw:{error, {Reason, Format, Args}} ->
            read_verbose(Opts, Format, Args),
            {error, Reason};
        throw:Err ->
            Err
    end.

% Pax and GNU long name headers collect values for the member after them.
foldl_read1(Fun, Accu0, Reader0, Opts, ExtraHeaders) ->
    {ok, Reader1} = skip_unread(Reader0),
    case get_header(Reader1) of
        eof ->
            Fun(eof, Reader1, Opts, Accu0);
        {Header, Reader2} ->
            case Header#tar_header.typeflag of
                ?TYPE_X_HEADER ->
                    {ExtraHeaders2, Reader3} = parse_pax(Reader2),
                    foldl_read1(Fun, Accu0, Reader3, Opts, maps:merge(ExtraHeaders, ExtraHeaders2));
                ?TYPE_GNU_LONGNAME ->
                    {RealName, Reader3} = get_real_name(Reader2),
                    foldl_read1(Fun, Accu0, Reader3, Opts, maps:put(?PAX_PATH, parse_string(RealName), ExtraHeaders));
                ?TYPE_GNU_LONGLINK ->
                    {RealName, Reader3} = get_real_name(Reader2),
                    ExtraHeaders2 = maps:put(?PAX_LINKPATH, parse_string(RealName), ExtraHeaders),
                    foldl_read1(Fun, Accu0, Reader3, Opts, ExtraHeaders2);
                _ ->
                    {ok, NewAccu, Reader3} = Fun(merge_pax(Header, ExtraHeaders), Reader2, Opts, Accu0),
                    foldl_read1(Fun, NewAccu, Reader3, Opts, #{})
            end
    end.

merge_pax(Header, ExtraHeaders) when is_map(ExtraHeaders) -> do_merge_pax(Header, maps:to_list(ExtraHeaders)).

do_merge_pax(Header, []) ->
    Header;
do_merge_pax(Header, [{?PAX_PATH, Path} | Rest]) ->
    do_merge_pax(Header#tar_header{name = unicode:characters_to_list(Path)}, Rest);
do_merge_pax(Header, [{?PAX_LINKPATH, LinkPath} | Rest]) ->
    do_merge_pax(Header#tar_header{linkname = unicode:characters_to_list(LinkPath)}, Rest);
do_merge_pax(Header, [{?PAX_GNAME, Gname} | Rest]) ->
    do_merge_pax(Header#tar_header{gname = unicode:characters_to_list(Gname)}, Rest);
do_merge_pax(Header, [{?PAX_UNAME, Uname} | Rest]) ->
    do_merge_pax(Header#tar_header{uname = unicode:characters_to_list(Uname)}, Rest);
do_merge_pax(Header, [{?PAX_UID, Uid} | Rest]) ->
    do_merge_pax(Header#tar_header{uid = binary_to_integer(Uid)}, Rest);
do_merge_pax(Header, [{?PAX_GID, Gid} | Rest]) ->
    do_merge_pax(Header#tar_header{gid = binary_to_integer(Gid)}, Rest);
do_merge_pax(Header, [{?PAX_ATIME, Atime} | Rest]) ->
    do_merge_pax(Header#tar_header{atime = parse_pax_time(Atime)}, Rest);
do_merge_pax(Header, [{?PAX_MTIME, Mtime} | Rest]) ->
    do_merge_pax(Header#tar_header{mtime = parse_pax_time(Mtime)}, Rest);
do_merge_pax(Header, [{?PAX_CTIME, Ctime} | Rest]) ->
    do_merge_pax(Header#tar_header{ctime = parse_pax_time(Ctime)}, Rest);
do_merge_pax(Header, [{?PAX_SIZE, Size} | Rest]) ->
    do_merge_pax(Header#tar_header{size = binary_to_integer(Size)}, Rest);
do_merge_pax(Header, [_Ignored | Rest]) ->
    do_merge_pax(Header, Rest).

% Seconds of a pax time "Seconds[.Fraction]", the fraction taken to nine digits and then truncated to microseconds.
parse_pax_time(Bin) when is_binary(Bin) ->
    TotalNano =
        case binary:split(Bin, [<<$.>>]) of
            [SecondsStr, NanoStr0] ->
                binary_to_integer(SecondsStr) * ?BILLION + binary_to_integer(nanoseconds(NanoStr0));
            [SecondsStr] ->
                binary_to_integer(SecondsStr) * ?BILLION
        end,
    Micro = TotalNano div 1000,
    Mega = Micro div 1000000000000,
    Micro div 1000000 - Mega * 1000000.

nanoseconds(Digits) when byte_size(Digits) < ?MAX_NANO_INT_SIZE ->
    <<Digits/binary, (binary:copy(<<$0>>, ?MAX_NANO_INT_SIZE - byte_size(Digits)))/binary>>;
nanoseconds(Digits) ->
    binary_part(Digits, 0, ?MAX_NANO_INT_SIZE).

parse_pax(#reg_file_reader{handle = Handle, num_bytes = 0}) ->
    {#{}, Handle};
parse_pax(#reg_file_reader{handle = Handle0, num_bytes = NumBytes}) ->
    case do_read(Handle0, NumBytes) of
        {ok, Bytes, Handle1} -> do_parse_pax(Handle1, Bytes, #{});
        {error, _} = Err -> throw(Err)
    end.

do_parse_pax(Reader, <<>>, Headers) ->
    {Headers, Reader};
do_parse_pax(Reader, Bin, Headers) ->
    {Key, Value, Residual} = parse_pax_record(Bin),
    do_parse_pax(Reader, Residual, maps:put(Key, Value, Headers)).

parse_pax_record(Bin) when is_binary(Bin) ->
    case binary:split(Bin, [<<$\n>>]) of
        [Record, Residual] ->
            case binary:split(Record, [<<$\s>>], [trim_all]) of
                [_Len, Record1] ->
                    case binary:split(Record1, [<<$=>>], [trim_all]) of
                        [AttrName, AttrValue] -> {AttrName, AttrValue, Residual};
                        _Other -> throw({error, malformed_pax_record})
                    end;
                _Other ->
                    throw({error, malformed_pax_record})
            end;
        _Other ->
            throw({error, malformed_pax_record})
    end.

get_real_name(#reg_file_reader{handle = Handle, num_bytes = 0}) ->
    {"", Handle};
get_real_name(#reg_file_reader{handle = Handle0, num_bytes = NumBytes}) ->
    case do_read(Handle0, NumBytes) of
        {ok, RealName, Handle1} -> {RealName, Handle1};
        {error, _} = Err -> throw(Err)
    end;
get_real_name(#sparse_file_reader{num_bytes = NumBytes} = Reader0) ->
    case do_read(Reader0, NumBytes) of
        {ok, RealName, Reader1} -> {RealName, Reader1};
        {error, _} = Err -> throw(Err)
    end.

skip_file(#reg_file_reader{handle = Handle0, pos = Pos, size = Size} = Reader) ->
    AbsPos = Handle0#reader.pos + (Size - Pos) + skip_padding(Size),
    case do_position(Handle0, AbsPos) of
        {ok, _, Handle1} -> Reader#reg_file_reader{handle = Handle1, num_bytes = 0, pos = Size};
        Err -> throw(Err)
    end;
skip_file(#sparse_file_reader{pos = Pos, size = Size} = Reader) ->
    case do_read(Reader, Size - Pos) of
        {ok, _, Reader2} -> Reader2;
        Err -> throw(Err)
    end.

skip_padding(0) -> 0;
skip_padding(Size) when (Size rem ?BLOCK_SIZE) =:= 0 -> 0;
skip_padding(Size) when Size =< ?BLOCK_SIZE -> ?BLOCK_SIZE - Size;
skip_padding(Size) -> ?BLOCK_SIZE - (Size rem ?BLOCK_SIZE).

% The archive reader at the next block boundary, past what a member's reader left unread.
skip_unread(#reader{pos = Pos} = Reader0) when (Pos rem ?BLOCK_SIZE) > 0 ->
    case do_position(Reader0, Pos + skip_padding(Pos + ?BLOCK_SIZE)) of
        {ok, _, Reader1} -> {ok, Reader1};
        Err -> throw(Err)
    end;
skip_unread(#reader{} = Reader) ->
    {ok, Reader};
skip_unread(#reg_file_reader{handle = Handle, num_bytes = 0}) ->
    skip_unread(Handle);
skip_unread(#reg_file_reader{} = Reader) ->
    #reg_file_reader{handle = Handle} = skip_file(Reader),
    {ok, Handle};
skip_unread(#sparse_file_reader{handle = Handle, num_bytes = 0}) ->
    skip_unread(Handle);
skip_unread(#sparse_file_reader{} = Reader) ->
    #sparse_file_reader{handle = Handle} = skip_file(Reader),
    {ok, Handle}.

% --- Writing extracted members ---

write_extracted_element(#tar_header{name = Name, typeflag = Type}, Bin, #read_opts{output = memory} = Opts) ->
    case typeflag(Type) of
        regular ->
            read_verbose(Opts, "x ~ts~n", [Name]),
            {ok, {Name, Bin}};
        _ ->
            ok
    end;
write_extracted_element(#tar_header{name = Name0} = Header, Bin, Opts) ->
    Name1 = make_safe_path(Name0, Opts),
    Created =
        case typeflag(Header#tar_header.typeflag) of
            regular ->
                create_regular(Name1, Name0, Bin, Opts);
            directory ->
                read_verbose(Opts, "x ~ts~n", [Name0]),
                create_extracted_dir(Name1, Opts);
            symlink ->
                read_verbose(Opts, "x ~ts~n", [Name0]),
                create_symlink(Name1, safe_link_name(Header, Opts), Opts);
            Device when Device =:= char; Device =:= block; Device =:= fifo ->
                % Devices and fifos are made as empty files, devices getting their numbers after.
                create_regular(Name1, Name0, <<>>, Opts);
            Other ->
                read_verbose(Opts, "x ~ts - unsupported type ~p~n", [Name0, Other]),
                not_written
        end,
    case Created of
        ok -> set_extracted_file_info(Name1, Header);
        not_written -> ok
    end.

% A name without leading slashes, below cwd, made absolute.
make_safe_path([$/ | Path], Opts) ->
    make_safe_path(Path, Opts);
make_safe_path(Path0, #read_opts{cwd = Cwd}) ->
    case filelib:safe_relative_path(Path0, Cwd) of
        unsafe -> throw({error, {Path0, unsafe_path}});
        Path -> filename:absname(Path, Cwd)
    end.

safe_link_name(#tar_header{name = Name, linkname = Path0}, #read_opts{cwd = Cwd}) ->
    case filelib:safe_relative_path(filename:join(filename:dirname(Name), Path0), Cwd) of
        unsafe -> throw({error, {Path0, unsafe_symlink}});
        _Path -> Path0
    end.

create_regular(Name, NameInArchive, Bin, Opts) ->
    case write_extracted_file(Name, Bin, Opts) of
        not_written ->
            read_verbose(Opts, "x ~ts - exists, not created~n", [NameInArchive]),
            not_written;
        Ok ->
            read_verbose(Opts, "x ~ts~n", [NameInArchive]),
            Ok
    end.

create_extracted_dir(Name, _Opts) ->
    case file:make_dir(Name) of
        ok -> ok;
        {error, enotsup} -> not_written;
        {error, eexist} -> not_written;
        {error, enoent} -> make_dirs(Name, dir);
        {error, Reason} -> throw({error, Reason})
    end.

create_symlink(Name, Linkname, Opts) ->
    case file:make_symlink(Linkname, Name) of
        ok ->
            ok;
        {error, enoent} ->
            ok = make_dirs(Name, file),
            create_symlink(Name, Linkname, Opts);
        {error, eexist} ->
            not_written;
        {error, enotsup} ->
            read_verbose(Opts, "x ~ts - symbolic links not supported~n", [Name]),
            not_written;
        {error, Reason} ->
            throw({error, Reason})
    end.

write_extracted_file(Name, Bin, Opts) ->
    case Opts#read_opts.keep_old_files andalso existing(Name) of
        true -> not_written;
        false -> write_file(Name, Bin)
    end.

write_file(Name, Bin) ->
    case file:write_file(Name, Bin) of
        ok ->
            ok;
        {error, enoent} ->
            case make_dirs(Name, file) of
                ok -> write_file(Name, Bin);
                {error, Reason} -> throw({error, Reason})
            end;
        {error, Reason} ->
            throw({error, Reason})
    end.

set_extracted_file_info(_, #tar_header{typeflag = ?TYPE_SYMLINK}) ->
    ok;
set_extracted_file_info(_, #tar_header{typeflag = ?TYPE_LINK}) ->
    ok;
set_extracted_file_info(Name, #tar_header{typeflag = Type} = Header) when Type =:= ?TYPE_CHAR; Type =:= ?TYPE_BLOCK ->
    #tar_header{mtime = Mtime, mode = Mode, devmajor = Devmajor, devminor = Devminor} = Header,
    file:write_file_info(Name, #file_info{mode = Mode, mtime = Mtime, major_device = Devmajor, minor_device = Devminor});
set_extracted_file_info(Name, #tar_header{mtime = Mtime, mode = Mode}) ->
    file:write_file_info(Name, #file_info{mode = Mode, mtime = Mtime}, [{time, posix}]).

make_dirs(Name, file) -> filelib:ensure_dir(Name);
make_dirs(Name, dir) -> filelib:ensure_dir(filename:join(Name, "*")).

read_verbose(#read_opts{verbose = true}, Format, Args) -> io:format(Format, Args);
read_verbose(_, _, _) -> ok.

add_verbose(#add_opts{verbose = true}, Format, Args) -> io:format(Format, Args);
add_verbose(_, _, _) -> ok.

% --- Archive I/O ---

do_write(#reader{handle = Handle, func = Fun} = Reader0, Data) when is_function(Fun, 2) ->
    case Fun(write, {Handle, Data}) of
        ok ->
            {ok, Pos, Reader1} = do_position(Reader0, {cur, 0}),
            {ok, Reader1#reader{pos = Pos}};
        {error, _} = Err ->
            Err
    end.

do_copy(#reader{func = Fun} = Reader, Source, #add_opts{chunk_size = ChunkSize}) when is_function(Fun, 2) ->
    case file:open(Source, [read, binary]) of
        {ok, SourceFd} ->
            Copied = copy_chunked(Reader, SourceFd, ChunkSize, 0),
            _ = file:close(SourceFd),
            case Copied of
                {ok, _Copied, _Reader2} -> Copied;
                Err -> throw(Err)
            end;
        Err ->
            throw(Err)
    end.

copy_chunked(#reader{} = Reader, Source, ChunkSize, Copied) ->
    case file:read(Source, ChunkSize) of
        {ok, Bin} ->
            {ok, Reader2} = do_write(Reader, Bin),
            copy_chunked(Reader2, Source, ChunkSize, Copied + byte_size(Bin));
        eof ->
            {ok, Copied, Reader};
        Other ->
            Other
    end.

% A position also reads back the absolute position, as Pos may be relative.
do_position(#reader{handle = Handle, func = Fun} = Reader, Pos) when is_function(Fun, 2) ->
    case Fun(position, {Handle, Pos}) of
        {ok, NewPos} ->
            {ok, AbsPos} = Fun(position, {Handle, {cur, 0}}),
            {ok, NewPos, Reader#reader{pos = AbsPos}};
        Other ->
            Other
    end.

do_read(#reg_file_reader{handle = Handle, pos = Pos, size = Size} = Reader, Len) ->
    ActualLen = min(Len, Size - Pos),
    case do_read(Handle, ActualLen) of
        {ok, Bin, Handle2} ->
            NewPos = Pos + ActualLen,
            {ok, Bin, Reader#reg_file_reader{handle = Handle2, pos = NewPos, num_bytes = Size - NewPos}};
        Other ->
            Other
    end;
do_read(#sparse_file_reader{} = Reader, Len) ->
    do_sparse_read(Reader, Len, <<>>);
do_read(#reader{pos = Pos, handle = Handle, func = Fun} = Reader, Len) when is_function(Fun, 2) ->
    case Fun(read2, {Handle, Len}) of
        {ok, List} when is_list(List) ->
            Bin = list_to_binary(List),
            {ok, Bin, Reader#reader{pos = Pos + byte_size(Bin)}};
        {ok, Bin} when is_binary(Bin) ->
            {ok, Bin, Reader#reader{pos = Pos + byte_size(Bin)}};
        Other ->
            Other
    end.

% Data fragments read from the archive, the holes between them (and after the last, as BSD tar) as zeros.
do_sparse_read(#sparse_file_reader{} = Reader, 0, Acc) ->
    {ok, Acc, Reader};
do_sparse_read(#sparse_file_reader{sparse_map = [#sparse_entry{num_bytes = 0} | Entries]} = Reader0, Len, Acc) ->
    do_sparse_read(Reader0#sparse_file_reader{sparse_map = Entries}, Len, Acc);
do_sparse_read(#sparse_file_reader{sparse_map = [], pos = Pos, size = Size} = Reader0, Len, Acc) when Pos < Size ->
    {ok, Bin, Reader1} = read_sparse_hole(Reader0, Size, Len),
    do_sparse_read(Reader1, Len - byte_size(Bin), <<Acc/binary, Bin/binary>>);
do_sparse_read(#sparse_file_reader{sparse_map = []} = Reader, _Len, Acc) ->
    {ok, Acc, Reader};
do_sparse_read(
    #sparse_file_reader{sparse_map = [#sparse_entry{offset = Offset} | _], pos = Pos} = Reader0, Len, Acc
) when
    Pos < Offset
->
    {ok, Bin, Reader1} = read_sparse_hole(Reader0, Offset, Len),
    do_sparse_read(Reader1, Len - byte_size(Bin), <<Acc/binary, Bin/binary>>);
do_sparse_read(#sparse_file_reader{sparse_map = [Entry | Entries], pos = Pos} = Reader0, Len, Acc) ->
    EndPos = Entry#sparse_entry.offset + Entry#sparse_entry.num_bytes,
    case do_read(Reader0#sparse_file_reader.handle, min(Len, EndPos - Pos)) of
        {ok, Bin, Handle} ->
            ActualEndPos = Pos + byte_size(Bin),
            Reader1 =
                case ActualEndPos of
                    EndPos -> Reader0#sparse_file_reader{sparse_map = Entries};
                    _ -> Reader0
                end,
            Reader2 = Reader1#sparse_file_reader{
                handle = Handle, pos = ActualEndPos, num_bytes = Reader1#sparse_file_reader.size - ActualEndPos
            },
            do_sparse_read(Reader2, Len - byte_size(Bin), <<Acc/binary, Bin/binary>>);
        Other ->
            Other
    end.

read_sparse_hole(#sparse_file_reader{pos = Pos, size = Size} = Reader, Offset, Len) ->
    N = min(Offset - Pos, Len),
    {ok, <<0:N/unit:8>>, Reader#sparse_file_reader{num_bytes = Size - (Pos + N), pos = Pos + N}}.

do_close(#reader{handle = Handle, func = Fun}) when is_function(Fun, 2) -> Fun(close, Handle).

% --- Options ---

extract_opts(List) -> extract_opts(List, default_options()).

table_opts(List) -> read_opts(List, default_options()).

default_options() ->
    {ok, Cwd} = file:get_cwd(),
    #read_opts{cwd = Cwd}.

extract_opts([keep_old_files | Rest], Opts) ->
    extract_opts(Rest, Opts#read_opts{keep_old_files = true});
extract_opts([{cwd, Cwd} | Rest], Opts) ->
    extract_opts(Rest, Opts#read_opts{cwd = Cwd});
extract_opts([{files, Files} | Rest], Opts) ->
    extract_opts(Rest, Opts#read_opts{files = ordsets:from_list(Files)});
extract_opts([memory | Rest], Opts) ->
    extract_opts(Rest, Opts#read_opts{output = memory});
extract_opts([{chunks, N} | Rest], Opts) ->
    extract_opts(Rest, Opts#read_opts{chunk_size = N});
extract_opts([{max_size, N} | Rest], Opts) ->
    extract_opts(Rest, Opts#read_opts{max_size = N});
extract_opts([Other | Rest], Opts) ->
    extract_opts(Rest, read_opts([Other], Opts));
extract_opts([], Opts) ->
    Opts.

read_opts([compressed | Rest], Opts = #read_opts{open_mode = OpenMode}) ->
    read_opts(Rest, Opts#read_opts{open_mode = [compressed | OpenMode]});
read_opts([cooked | Rest], Opts = #read_opts{open_mode = OpenMode}) ->
    read_opts(Rest, Opts#read_opts{open_mode = [cooked | OpenMode]});
read_opts([verbose | Rest], Opts) ->
    read_opts(Rest, Opts#read_opts{verbose = true});
read_opts([_ | Rest], Opts) ->
    read_opts(Rest, Opts);
read_opts([], Opts) ->
    Opts.
