%% Clause's zip (plan 21 step LIB54, docs/library.md): zip archives on files or in memory, written from OTP's
%% documentation with OTP's layout followed byte for byte (local and central headers, made by UNIX version 6.1,
%% extended timestamp and UNIX uid/gid extra fields, zip64 past 4 GiB, deflate with a full flush every 8 KiB), so
%% archives equal OTP's. Archive I/O goes through an input and an output fun over files or binaries, as OTP's;
%% zip_open/1,2 serve an archive from a linked process.
-module(zip).
-export([
    create/2,
    create/3,
    extract/1,
    extract/2,
    foldl/3,
    list_dir/1,
    list_dir/2,
    t/1,
    table/1,
    table/2,
    tt/1,
    unzip/1,
    unzip/2,
    zip/2,
    zip/3,
    zip_close/1,
    zip_get/1,
    zip_get/2,
    zip_get_crc32/2,
    zip_list_dir/1,
    zip_open/1,
    zip_open/2
]).
-include("zip.types.hrl").
%% The zip options, I/O requests and error atoms never name a called module.
-compile({clause_liveness, strict}).
-compile(nowarn_deprecated_catch).

-include_lib("kernel/include/file.hrl").
-include("include/zip.hrl").

-define(WRITE_BLOCK_SIZE, 8 * 1024).
-define(READ_BLOCK_SIZE, 16 * 1024).

% The options of each operation: the output and input funs, which files to take (unzip), file:open options, a
% feedback fun, the directory names are relative to, compression suffixes (zip), whether to skip directories, and the
% extra fields to read or write.
-record(unzip_opts, {output, input, file_filter, open_opts, feedback, cwd, skip_dirs, extra}).
-record(zip_opts, {output, input, comment, open_opts, feedback, cwd, compress, uncompress, extra}).
-record(list_dir_opts, {input, raw_iterator, open_opts, skip_dirs, extra}).
-record(openzip_opts, {output, open_opts, cwd, skip_dirs, extra}).

% An archive open in its server: the archive comment, its files with their extras, the input handle and fun, the
% output fun, a zlib stream, and the options.
-record(openzip, {zip_comment, files, in, input, output, zlib, cwd, skip_dirs, extra}).

% What OTP keeps beside the public #zip_file{}: the file's CRC-32.
-record(zip_file_extra, {crc32}).

-define(STORED, 0).
-define(DEFLATED, 8).

-define(OS_MADE_BY_UNIX, 3).
-define(VERSION_NEEDED_STORE, 10).
-define(VERSION_NEEDED_DEFLATE, 20).
-define(VERSION_NEEDED_ZIP64, 45).
-define(VERSION_MADE_BY, 61).
% File names and comments in UTF-8.
-define(GP_BIT_11, 16#800).

-define(LOCAL_FILE_MAGIC, 16#04034b50).
-define(LOCAL_FILE_HEADER_SZ, (4 + 2 + 2 + 2 + 2 + 2 + 4 + 4 + 4 + 2 + 2)).
-define(LOCAL_FILE_HEADER_CRC32_OFFSET, 4 + 2 + 2 + 2 + 2 + 2).

% A local file header; the fields up to gid are at the same positions as in #cd_file_header{}.
-record(local_file_header, {
    version_needed,
    gp_flag,
    comp_method,
    last_mod_time,
    last_mod_date,
    crc32,
    comp_size,
    uncomp_size,
    mtime,
    atime,
    ctime,
    uid = 0,
    gid = 0,
    file_name_length,
    extra_field_length,
    info
}).

-define(EXTRA_OPTIONS, [extended_timestamp, uid_gid]).
-define(X0001_ZIP64, 16#0001).
-define(X5455_EXTENDED_TIMESTAMP, 16#5455).
-define(X7875_UNIX3, 16#7875).

-define(CENTRAL_FILE_HEADER_SZ, (4 + 2 + 2 + 2 + 2 + 2 + 2 + 4 + 4 + 4 + 2 + 2 + 2 + 2 + 2 + 4 + 4)).
-define(CENTRAL_FILE_MAGIC, 16#02014b50).

-define(DEFAULT_REGULAR_FILE_MODE, 8#644).
-define(DEFAULT_DIRECTORY_FILE_MODE, 8#744).

-record(cd_file_header, {
    version_needed,
    gp_flag,
    comp_method,
    last_mod_time,
    last_mod_date,
    crc32,
    comp_size,
    uncomp_size,
    mtime,
    atime,
    ctime,
    uid = 0,
    gid = 0,
    version_made_by,
    os_made_by,
    file_name_length,
    extra_field_length,
    file_comment_length,
    disk_num_start,
    internal_attr,
    external_attr,
    local_header_offset
}).

-define(END_OF_CENTRAL_DIR_64_LOCATOR_MAGIC, 16#07064b50).
-define(END_OF_CENTRAL_DIR_64_LOCATOR_SZ, (4 + 8 + 4)).
-define(END_OF_CENTRAL_DIR_64_MAGIC, 16#06064b50).
-define(END_OF_CENTRAL_DIR_64_SZ, (2 + 2 + 4 + 4 + 8 + 8 + 8 + 8)).
-define(END_OF_CENTRAL_DIR_MAGIC, 16#06054b50).
-define(END_OF_CENTRAL_DIR_SZ, (4 + 2 + 2 + 2 + 2 + 4 + 4 + 2)).
-define(MAX_INT32, 16#FFFFFFFF).
-define(MAX_INT16, 16#FFFF).

-record(eocd, {
    eocd,
    version_made_by = 10,
    os_made_by = <<"UNIX">>,
    extract_version = 10,
    disk_num,
    start_disk_num,
    entries_on_disk,
    entries,
    size,
    offset,
    zip_comment_length,
    extra
}).

-define(CATCH(Expr), (catch (Expr))).

% --- Extracting ---

unzip(F) -> unzip(F, []).

unzip(F, Options) ->
    case ?CATCH(do_unzip(F, Options)) of
        {ok, R} -> {ok, R};
        Error -> {error, Error}
    end.

do_unzip(F, Options) ->
    Opts = get_unzip_options(F, Options),
    #unzip_opts{input = Input, open_opts = OpO, extra = ExtraOpts} = Opts,
    try Input({open, F, OpO -- [write]}, []) of
        In0 ->
            {Info, In1} = get_central_dir(In0, fun raw_file_info_etc/5, Input, ExtraOpts),
            Z = zlib:open(),
            Files =
                try
                    get_z_files(Info, Z, In1, Opts, [])
                after
                    zlib:close(Z),
                    Input(close, In1)
                end,
            {ok, Files}
    catch
        % An archive that fails to open gives its file:open error alone.
        throw:{_FN, {_, Error}} -> Error
    end.

% Fun(Name, GetInfo, GetBin, Acc) over the files of an archive.
foldl(Fun, Acc0, {_Filename, Binary}) ->
    foldl(Fun, Acc0, Binary);
foldl(Fun, Acc0, Archive) when is_function(Fun, 4) ->
    case zip_open(Archive, [memory]) of
        {ok, Handle} ->
            {ok, Files} = zip_list_dir(Handle),
            Acc1 = lists:foldl(fun(File, Acc) -> fold_file(Fun, Handle, File, Acc) end, Acc0, Files),
            ok = zip_close(Handle),
            {ok, Acc1};
        {error, bad_eocd} ->
            {error, "Not an archive file"};
        {error, Reason} ->
            {error, Reason}
    end;
foldl(_, _, _) ->
    {error, einval}.

fold_file(_Fun, _Handle, #zip_comment{}, Acc) ->
    Acc;
fold_file(Fun, Handle, #zip_file{name = Name, info = Info}, Acc) ->
    GetInfo = fun() -> Info end,
    GetBin =
        case lists:last(Name) of
            $/ ->
                fun() -> <<>> end;
            _ ->
                fun() ->
                    case zip_get(Name, Handle) of
                        {ok, {Name, Data}} -> Data;
                        {error, Error} -> throw({Name, Error})
                    end
                end
        end,
    Fun(Name, GetInfo, GetBin, Acc).

% --- Creating ---

zip(F, Files) -> zip(F, Files, []).

zip(F, Files, Options) ->
    case ?CATCH(do_zip(F, Files, Options)) of
        {ok, R} -> {ok, R};
        Error -> {error, Error}
    end.

do_zip(F, Files, Options) ->
    Opts = get_zip_options(Files, Options),
    #zip_opts{output = Output, open_opts = OpO} = Opts,
    try Output({open, F, OpO}, []) of
        Out0 ->
            Z = zlib:open(),
            try
                {Out1, LHS, Pos} = put_z_files(Files, Z, Out0, 0, Opts, []),
                zlib:close(Z),
                Out2 = put_central_dir(LHS, Pos, Out1, Opts),
                {ok, Output(flush, Output({close, F}, Out2))}
            catch
                C:R:Stk ->
                    _ = ?CATCH(zlib:close(Z)),
                    Output(flush, Output({close, F}, Out0)),
                    erlang:raise(C, R, Stk)
            end
    catch
        throw:{_FN, {_, Error}} -> Error
    end.

% --- Listing ---

list_dir(F) -> list_dir(F, []).

list_dir(F, Options) ->
    case ?CATCH(do_list_dir(F, Options)) of
        {ok, R} -> {ok, R};
        Error -> {error, Error}
    end.

do_list_dir(F, Options) ->
    Opts = get_list_dir_options(F, Options),
    #list_dir_opts{input = Input, open_opts = OpO, raw_iterator = RawIterator, skip_dirs = SkipDirs, extra = ExtraOpts} =
        Opts,
    try Input({open, F, OpO}, []) of
        In0 ->
            {Info, In1} = get_central_dir(In0, RawIterator, Input, ExtraOpts),
            Input(close, In1),
            case SkipDirs of
                true -> {ok, [E || E <- Info, not is_dir_entry(E)]};
                false -> {ok, Info}
            end
    catch
        throw:{_FN, {_, Error}} -> Error
    end.

is_dir_entry(#zip_file{name = Name}) -> lists:last(Name) =:= $/;
is_dir_entry(#zip_comment{}) -> false.

% --- Open archives ---

zip_open(Archive) -> zip_open(Archive, []).

zip_open(Archive, Options) ->
    Self = self(),
    Pid = spawn_link(fun() -> server_init(Self) end),
    request(Self, Pid, {open, Archive, Options}).

zip_get(Pid) when is_pid(Pid) -> request(self(), Pid, get).

zip_close(Pid) when is_pid(Pid) -> request(self(), Pid, close).

zip_get(FileName, Pid) when is_pid(Pid) -> request(self(), Pid, {get, FileName}).

zip_get_crc32(FileName, Pid) when is_pid(Pid) -> request(self(), Pid, {get_crc32, FileName}).

zip_list_dir(Pid) when is_pid(Pid) -> request(self(), Pid, list_dir).

request(Self, Pid, Req) ->
    Pid ! {Self, Req},
    receive
        {Pid, R} -> R
    end.

% --- Printing ---

t(F) when is_pid(F) -> openzip_t(request(self(), F, get_state));
t(F) -> t(F, fun raw_short_print_info_etc/5).

t(F, RawPrint) ->
    case ?CATCH(do_t(F, RawPrint)) of
        ok -> ok;
        Error -> {error, Error}
    end.

do_t(F, RawPrint) ->
    Input = get_input(F),
    try Input({open, F, [raw]}, []) of
        In0 ->
            {_Info, In1} = get_central_dir(In0, RawPrint, Input, ?EXTRA_OPTIONS),
            Input(close, In1),
            ok
    catch
        throw:{_FN, {_, Error}} -> Error
    end.

tt(F) when is_pid(F) -> openzip_tt(request(self(), F, get_state));
tt(F) -> t(F, fun raw_long_print_info_etc/5).

% --- Options ---

get_unzip_opt([], Opts) ->
    Opts;
get_unzip_opt([verbose | Rest], Opts) ->
    get_unzip_opt(Rest, Opts#unzip_opts{feedback = fun verbose_unzip/1});
get_unzip_opt([cooked | Rest], #unzip_opts{open_opts = OpO} = Opts) ->
    get_unzip_opt(Rest, Opts#unzip_opts{open_opts = OpO -- [raw]});
get_unzip_opt([memory | Rest], Opts) ->
    get_unzip_opt(Rest, Opts#unzip_opts{output = fun binary_io/2});
get_unzip_opt([{cwd, CWD} | Rest], Opts) ->
    get_unzip_opt(Rest, Opts#unzip_opts{cwd = CWD});
get_unzip_opt([{file_filter, F} | Rest], Opts) ->
    Filter1 = fun({ZipFile, _Extra, _CWD}) -> F(ZipFile) end,
    get_unzip_opt(Rest, Opts#unzip_opts{file_filter = fun_and_1(Filter1, Opts#unzip_opts.file_filter)});
get_unzip_opt([{file_list, L} | Rest], Opts) ->
    FileInList = fun({ZipFile, _Extra, _CWD}) -> file_in_list(ZipFile, L) end,
    get_unzip_opt(Rest, Opts#unzip_opts{file_filter = fun_and_1(FileInList, Opts#unzip_opts.file_filter)});
get_unzip_opt([keep_old_files | Rest], Opts) ->
    Keep = fun({ZipFile, _Extra, CWD}) -> keep_old_file(ZipFile, CWD) end,
    get_unzip_opt(Rest, Opts#unzip_opts{file_filter = fun_and_1(Keep, Opts#unzip_opts.file_filter)});
get_unzip_opt([skip_directories | Rest], Opts) ->
    get_unzip_opt(Rest, Opts#unzip_opts{skip_dirs = true});
get_unzip_opt([{extra, What} = O | Rest], Opts) when is_list(What) ->
    % OTP goes on with the zip options here, which only an empty rest gets through.
    case lists:all(fun(E) -> lists:member(E, ?EXTRA_OPTIONS) end, What) of
        true -> get_zip_opt(Rest, Opts#unzip_opts{extra = What});
        false -> throw({bad_option, O})
    end;
get_unzip_opt([Unknown | _Rest], _Opts) ->
    throw({bad_option, Unknown}).

get_list_dir_opt([], Opts) ->
    Opts;
get_list_dir_opt([cooked | Rest], #list_dir_opts{open_opts = OpO} = Opts) ->
    get_list_dir_opt(Rest, Opts#list_dir_opts{open_opts = OpO -- [raw]});
get_list_dir_opt([names_only | Rest], Opts) ->
    get_list_dir_opt(Rest, Opts#list_dir_opts{raw_iterator = fun raw_name_only/5});
get_list_dir_opt([skip_directories | Rest], Opts) ->
    get_list_dir_opt(Rest, Opts#list_dir_opts{skip_dirs = true});
get_list_dir_opt([{extra, What} = O | Rest], Opts) when is_list(What) ->
    case lists:all(fun(E) -> lists:member(E, ?EXTRA_OPTIONS) end, What) of
        true -> get_zip_opt(Rest, Opts#list_dir_opts{extra = What});
        false -> throw({bad_option, O})
    end;
get_list_dir_opt([Unknown | _Rest], _Opts) ->
    throw({bad_option, Unknown}).

get_zip_opt([], Opts) ->
    Opts;
get_zip_opt([verbose | Rest], Opts) ->
    get_zip_opt(Rest, Opts#zip_opts{feedback = fun verbose_zip/1});
get_zip_opt([cooked | Rest], #zip_opts{open_opts = OpO} = Opts) ->
    get_zip_opt(Rest, Opts#zip_opts{open_opts = OpO -- [raw]});
get_zip_opt([memory | Rest], Opts) ->
    get_zip_opt(Rest, Opts#zip_opts{output = fun binary_io/2});
get_zip_opt([{cwd, CWD} | Rest], Opts) ->
    get_zip_opt(Rest, Opts#zip_opts{cwd = CWD});
get_zip_opt([{comment, C} | Rest], Opts) ->
    get_zip_opt(Rest, Opts#zip_opts{comment = C});
get_zip_opt([{compress, Which} = O | Rest], Opts) ->
    get_zip_opt(Rest, Opts#zip_opts{compress = suffixes(Which, Opts#zip_opts.compress, O)});
get_zip_opt([{uncompress, Which} = O | Rest], Opts) ->
    get_zip_opt(Rest, Opts#zip_opts{uncompress = suffixes(Which, Opts#zip_opts.uncompress, O)});
get_zip_opt([{extra, What} = O | Rest], Opts) when is_list(What) ->
    case lists:all(fun(E) -> lists:member(E, ?EXTRA_OPTIONS) end, What) of
        true -> get_zip_opt(Rest, Opts#zip_opts{extra = What});
        false -> throw({bad_option, O})
    end;
get_zip_opt([Unknown | _Rest], _Opts) ->
    throw({bad_option, Unknown}).

suffixes(all, _Old, _O) -> all;
suffixes(Suffixes, _Old, _O) when is_list(Suffixes) -> lists:usort(Suffixes);
suffixes({add, Suffixes}, Old, _O) when is_list(Suffixes) -> lists:usort(Old ++ Suffixes);
suffixes({del, Suffixes}, Old, _O) when is_list(Suffixes) -> lists:usort(Old -- Suffixes);
suffixes(_, _Old, O) -> throw({bad_option, O}).

silent(_) -> ok.

verbose_unzip(FN) -> io:format("extracting: ~ts\n", [io_lib:write_string(FN)]).

verbose_zip(FN) -> io:format("adding: ~ts\n", [io_lib:write_string(FN)]).

all(_) -> true.

file_in_list(#zip_file{name = FileName}, List) -> lists:member(FileName, List);
file_in_list(_, _) -> false.

keep_old_file(#zip_file{name = FileName}, CWD) ->
    FileName1 = add_cwd(CWD, FileName),
    not (filelib:is_file(FileName1) orelse filelib:is_dir(FileName1));
keep_old_file(_, _) ->
    false.

fun_and_1(Fun1, Fun2) -> fun(A) -> Fun1(A) andalso Fun2(A) end.

get_zip_options(Files, Options) ->
    Opts = #zip_opts{
        output = fun file_io/2,
        input = get_zip_input({files, Files}),
        open_opts = [raw, write],
        comment = "",
        feedback = fun silent/1,
        cwd = "",
        compress = all,
        uncompress = [".Z", ".zip", ".zoo", ".arc", ".lzh", ".arj"],
        extra = ?EXTRA_OPTIONS
    },
    #zip_opts{comment = Comment} = Opts1 = get_zip_opt(Options, Opts),
    % Characters 127 to 255 are encoded as UTF-8.
    {Comment1, _} = encode_string(Comment),
    Opts1#zip_opts{comment = Comment1}.

get_unzip_options(F, Options) ->
    Opts = #unzip_opts{
        file_filter = fun all/1,
        output = fun file_io/2,
        input = get_input(F),
        open_opts = [raw],
        feedback = fun silent/1,
        skip_dirs = false,
        cwd = "",
        extra = [extended_timestamp]
    },
    get_unzip_opt(Options, Opts).

get_openzip_options(Options) ->
    Opts = #openzip_opts{
        open_opts = [raw, read], output = fun file_io/2, cwd = "", skip_dirs = false, extra = ?EXTRA_OPTIONS
    },
    get_openzip_opt(Options, Opts).

get_input(F) when is_binary(F) -> fun binary_io/2;
get_input(F) when is_list(F) -> fun file_io/2;
get_input(_) -> throw(einval).

get_zip_input({F, B}) when is_binary(B), is_list(F) -> fun binary_io/2;
get_zip_input({F, B, #file_info{}}) when is_binary(B), is_list(F) -> fun binary_io/2;
get_zip_input({F, #file_info{}, B}) when is_binary(B), is_list(F) -> fun binary_io/2;
get_zip_input(F) when is_list(F) -> fun file_io/2;
get_zip_input({files, []}) -> fun binary_io/2;
get_zip_input({files, [File | _]}) -> get_zip_input(File);
get_zip_input(_) -> throw(einval).

get_list_dir_options(F, Options) ->
    Opts = #list_dir_opts{
        raw_iterator = fun raw_file_info_public/5,
        input = get_input(F),
        open_opts = [raw],
        skip_dirs = false,
        extra = [extended_timestamp]
    },
    get_list_dir_opt(Options, Opts).

table(F) -> list_dir(F).

table(F, O) -> list_dir(F, O).

create(F, Fs) -> zip(F, Fs).

create(F, Fs, O) -> zip(F, Fs, O).

extract(F) -> unzip(F).

extract(F, O) -> unzip(F, O).

% --- Writing ---

put_central_dir(LHS, Pos, Out0, #zip_opts{output = Output, comment = Comment, extra = ExtraOpts}) ->
    {Out1, Sz} = put_cd_files_loop(LHS, Output, ExtraOpts, Out0, 0),
    put_eocd(length(LHS), Pos, Sz, Comment, Output, Out1).

put_cd_files_loop([], _Output, _ExtraOpts, Out, Sz) ->
    {Out, Sz};
put_cd_files_loop([{LH, Name, Pos} | LHRest], Output, ExtraOpts, Out0, Sz0) ->
    #local_file_header{comp_size = CompSize, uncomp_size = UnCompSize, info = FI} = LH,
    Extra = encode_extra(UnCompSize, CompSize, Pos, FI#file_info{atime = undefined}, ExtraOpts),
    CDFH = cd_file_header_from_lh_pos_and_extra(LH, Pos, Extra),
    Out1 = Output({write, [<<?CENTRAL_FILE_MAGIC:32/little>>, cd_file_header_to_bin(CDFH), Name, Extra]}, Out0),
    Sz1 =
        Sz0 + ?CENTRAL_FILE_HEADER_SZ + CDFH#cd_file_header.file_name_length +
            CDFH#cd_file_header.extra_field_length,
    put_cd_files_loop(LHRest, Output, ExtraOpts, Out1, Sz1).

put_eocd(N, Pos, Sz, Comment, Output, Out0) when Pos < ?MAX_INT32, N < ?MAX_INT16, Sz < ?MAX_INT32 ->
    EOCD = #eocd{
        disk_num = 0,
        start_disk_num = 0,
        entries_on_disk = N,
        entries = N,
        size = Sz,
        offset = Pos,
        zip_comment_length = length(Comment)
    },
    Output({write, [<<?END_OF_CENTRAL_DIR_MAGIC:32/little>>, eocd_to_bin(EOCD), Comment]}, Out0);
put_eocd(N, Pos, Sz, Comment, Output, Out0) ->
    EOCD64 = #eocd{
        os_made_by = ?OS_MADE_BY_UNIX,
        version_made_by = ?VERSION_MADE_BY,
        extract_version = ?VERSION_NEEDED_ZIP64,
        disk_num = 0,
        start_disk_num = 0,
        entries_on_disk = N,
        entries = N,
        size = Sz,
        offset = Pos,
        extra = <<>>
    },
    BEOCD64 = eocd64_to_bin(EOCD64),
    Out1 = Output(
        {write, [<<?END_OF_CENTRAL_DIR_64_MAGIC:32/little, (iolist_size(BEOCD64)):64/little>>, BEOCD64]}, Out0
    ),
    % The start disk, the zip64 end record's offset and the number of disks.
    Out2 = Output(
        {write, <<?END_OF_CENTRAL_DIR_64_LOCATOR_MAGIC:32/little, 0:32/little, (Pos + Sz):64/little, 1:32/little>>},
        Out1
    ),
    EOCD = #eocd{
        disk_num = 0,
        start_disk_num = 0,
        entries_on_disk = min(N, ?MAX_INT16),
        entries = min(N, ?MAX_INT16),
        size = min(Sz, ?MAX_INT32),
        offset = min(Pos, ?MAX_INT32),
        zip_comment_length = length(Comment)
    },
    Output({write, [<<?END_OF_CENTRAL_DIR_MAGIC:32/little>>, eocd_to_bin(EOCD), Comment]}, Out2).

get_filename({Name, _}, Type) ->
    get_filename(Name, Type);
get_filename({Name, _, _}, Type) ->
    get_filename(Name, Type);
get_filename(Name, regular) ->
    sanitize_filename(Name);
get_filename(Name, directory) ->
    % A directory name ends with a slash.
    case lists:reverse(Name) of
        [$/ | _Rev] -> sanitize_filename(Name);
        Rev -> sanitize_filename(lists:reverse([$/ | Rev]))
    end.

add_cwd(_CWD, {_Name, _} = F) ->
    F;
add_cwd("", F) ->
    F;
add_cwd(CWD, F) ->
    TrailingSlash =
        case lists:last(F) of
            $/ -> "/";
            _ -> ""
        end,
    string:trim(filename:join(CWD, F), trailing, "/") ++ TrailingSlash.

% Files under 10 bytes and directories are stored; others are deflated unless their suffix says not to.
get_comp_method(_, N, _, _) when is_integer(N), N < 10 ->
    ?STORED;
get_comp_method(_, _, _, directory) ->
    ?STORED;
get_comp_method(F, _, #zip_opts{compress = Compress, uncompress = Uncompress}, _) ->
    Ext = filename:extension(F),
    Test = fun(Which) -> Which =:= all orelse lists:member(Ext, Which) end,
    case Test(Compress) andalso not Test(Uncompress) of
        true -> ?DEFLATED;
        false -> ?STORED
    end.

put_z_files([], _Z, Out, Pos, _Opts, Acc) ->
    {Out, lists:reverse(Acc), Pos};
put_z_files([F | Rest], Z, Out0, Pos0, Opts, Acc) ->
    #zip_opts{input = Input, output = Output, open_opts = OpO, feedback = FB, cwd = CWD, extra = ExtraOpts} = Opts,
    F1 = add_cwd(CWD, F),
    FileInfo = Input({file_info, F1, [{time, posix}]}, []),
    Type = FileInfo#file_info.type,
    UncompSize =
        case Type of
            regular -> FileInfo#file_info.size;
            directory -> 0
        end,
    FileName0 = get_filename(F, Type),
    {FileName, GPFlag} = encode_string(FileName0),
    CompMethod = get_comp_method(FileName, UncompSize, Opts, Type),
    Extra = encode_extra(UncompSize, 0, 0, FileInfo, ExtraOpts),
    LH = local_file_header_from_info_method_name(FileInfo, UncompSize, CompMethod, FileName, GPFlag, Extra),
    Out1 = Output({write, [<<?LOCAL_FILE_MAGIC:32/little>>, local_file_header_to_bin(LH)]}, Out0),
    Out2 = Output({write, FileName}, Out1),
    % Where the extra data starts.
    Pos1 = Pos0 + ?LOCAL_FILE_HEADER_SZ + LH#local_file_header.file_name_length,
    Out3 = Output({write, Extra}, Out2),
    {Out4, CompSize, CRC} = put_z_file(CompMethod, UncompSize, Out3, F1, {0, Input, Output, OpO, Z}, Type),
    Pos2 = Pos1 + LH#local_file_header.extra_field_length + CompSize,
    FB(FileName0),
    Out5 = Output({pwrite, Pos0 + ?LOCAL_FILE_HEADER_CRC32_OFFSET, <<CRC:32/little>>}, Out4),
    Out6 =
        case UncompSize >= ?MAX_INT32 of
            % A zip64 file keeps its compressed size in the extra field, after its tag, size and uncompressed size.
            true -> Output({pwrite, Pos1 + 2 + 2 + 8, <<CompSize:64/little>>}, Out5);
            false -> Output({pwrite, Pos0 + ?LOCAL_FILE_HEADER_CRC32_OFFSET + 4, <<CompSize:32/little>>}, Out5)
        end,
    Out7 = Output({seek, eof, 0}, Out6),
    LH2 = LH#local_file_header{uncomp_size = UncompSize, comp_size = CompSize, crc32 = CRC},
    ThisAcc = [{LH2, FileName, Pos0}],
    {Out8, SubAcc, Pos3} =
        case Type of
            regular ->
                {Out7, ThisAcc, Pos2};
            directory ->
                Files = Input({list_dir, F1}, []),
                put_z_files(reverse_join_files(F, Files, []), Z, Out7, Pos2, Opts, ThisAcc)
        end,
    put_z_files(Rest, Z, Out8, Pos3, Opts, lists:reverse(SubAcc) ++ Acc).

reverse_join_files(Dir, [File | Files], Acc) -> reverse_join_files(Dir, Files, [filename:join([Dir, File]) | Acc]);
reverse_join_files(_Dir, [], Acc) -> Acc.

% The extra fields: zip64 first, so the compressed size can be patched, then the extended timestamp and uid/gid.
encode_extra(UnCompSize, CompSize, Pos, FileInfo, ExtraOpts) ->
    [
        encode_extra_zip64(UnCompSize, CompSize, Pos),
        [encode_extra_extended_timestamp(FileInfo) || lists:member(extended_timestamp, ExtraOpts)],
        [encode_extra_uid_gid(FileInfo) || lists:member(uid_gid, ExtraOpts)]
    ].

encode_extra_header(Header, Value) -> [<<Header:16/little, (iolist_size(Value)):16/little>>, Value].

encode_extra_zip64(UncompSize, CompSize, Pos) when UncompSize >= ?MAX_INT32 ->
    encode_extra_header(?X0001_ZIP64, [
        <<UncompSize:64/little, CompSize:64/little>>, [<<Pos:64/little>> || Pos >= ?MAX_INT32]
    ]);
encode_extra_zip64(_UncompSize, _CompSize, Pos) when Pos >= ?MAX_INT32 ->
    encode_extra_header(?X0001_ZIP64, <<Pos:64/little>>);
encode_extra_zip64(_, _, _) ->
    <<>>.

encode_extra_extended_timestamp(FI) ->
    {Mbit, MSystemTime} = timestamp_bit(1, FI#file_info.mtime),
    {Abit, ASystemTime} = timestamp_bit(2, FI#file_info.atime),
    encode_extra_header(?X5455_EXTENDED_TIMESTAMP, [Abit bor Mbit, MSystemTime, ASystemTime]).

timestamp_bit(Bit, Time) ->
    case datetime_to_system_time(Time) of
        undefined -> {0, <<>>};
        Seconds -> {Bit, <<(datetime_to_system_time(Seconds)):32/little>>}
    end.

encode_extra_uid_gid(#file_info{uid = Uid, gid = Gid}) when Uid =/= undefined, Gid =/= undefined ->
    encode_extra_header(?X7875_UNIX3, <<1, 4, Uid:32/little, 4, Gid:32/little>>);
encode_extra_uid_gid(_) ->
    <<>>.

-define(MAX_WBITS, 15).

put_z_file(_Method, Sz, Out, _F, {Pos, _, _, _, _}, directory) ->
    {Out, Pos + Sz, 0};
put_z_file(_Method, 0, Out, _F, {Pos, _, _, _, _}, regular) ->
    {Out, Pos, 0};
put_z_file(?STORED, UncompSize, Out0, F, {Pos0, Input, Output, OpO, _Z}, regular) ->
    In1 = Input({open, F, OpO -- [write]}, []),
    {Out1, Pos1, In2, CRC} = put_z_data_loop(
        UncompSize, In1, Out0, Pos0, {Input, Output, fun(Data, _Sync) -> Data end}, 0
    ),
    Input(close, In2),
    {Out1, Pos1, CRC};
put_z_file(?DEFLATED, UncompSize, Out0, F, {Pos0, Input, Output, OpO, Z}, regular) ->
    In1 = Input({open, F, OpO -- [write]}, []),
    ok = zlib:deflateInit(Z, default, deflated, -?MAX_WBITS, 8, default),
    Deflate = fun(Data, Sync) -> zlib:deflate(Z, Data, Sync) end,
    {Out1, Pos1, In2, CRC} = put_z_data_loop(UncompSize, In1, Out0, Pos0, {Input, Output, Deflate}, 0),
    ok = zlib:deflateEnd(Z),
    Input(close, In2),
    {Out1, Pos1, CRC}.

% Blocks of 8 KiB, each flushed fully, the last finished.
put_z_data_loop(0, In, Out, Pos, _Funs, CRC0) ->
    {Out, Pos, In, CRC0};
put_z_data_loop(UncompSize, In0, Out0, Pos0, {Input, Output, DeflateFun} = Funs, CRC0) ->
    N = min(?WRITE_BLOCK_SIZE, UncompSize),
    case Input({read, N}, In0) of
        {eof, _In1} ->
            {Out0, Pos0};
        {Uncompressed, In1} ->
            CRC1 = erlang:crc32(CRC0, Uncompressed),
            Compressed = DeflateFun(Uncompressed, get_sync(N, UncompSize)),
            Out1 = Output({write, Compressed}, Out0),
            put_z_data_loop(UncompSize - N, In1, Out1, Pos0 + iolist_size(Compressed), Funs, CRC1)
    end.

get_sync(N, N) -> finish;
get_sync(_, _) -> full.

% --- Central directory iterators ---

raw_name_only(#cd_file_header{}, FileName, _FileComment, _BExtraField, Acc) -> [FileName | Acc];
raw_name_only(#eocd{}, _, _Comment, _, Acc) -> Acc.

raw_short_print_info_etc(#cd_file_header{}, FileName, _FileComment, _BExtraField, Acc) ->
    print_file_name(FileName),
    Acc;
raw_short_print_info_etc(#eocd{} = EOCD, X, Comment, Y, Acc) ->
    raw_long_print_info_etc(EOCD, X, Comment, Y, Acc).

print_file_name(FileName) -> io:format("~ts\n", [FileName]).

raw_long_print_info_etc(
    #cd_file_header{comp_size = CompSize, uncomp_size = UncompSize} = CDFH,
    FileName,
    FileComment,
    _BExtraField,
    Acc
) ->
    print_header(CompSize, file_header_mtime_to_datetime(CDFH), UncompSize, FileName, FileComment),
    Acc;
raw_long_print_info_etc(#eocd{}, _, Comment, _, Acc) ->
    print_comment(Comment),
    Acc.

print_header(CompSize, MTime, UncompSize, FileName, FileComment) ->
    io:format("~10w ~s ~10w ~3w% ~ts ~ts\n", [
        CompSize, time_to_string(MTime), UncompSize, get_percent(CompSize, UncompSize), FileName, FileComment
    ]).

print_comment("") -> ok;
print_comment(Comment) -> io:format("Archive comment: ~ts\n", [Comment]).

get_percent(_, 0) -> 100;
get_percent(CompSize, Size) -> round(CompSize * 100 / Size).

time_to_string({{Y, Mon, Day}, {H, Min, _}}) ->
    io_lib:format("~s ~2w ~s:~s ~w", [month(Mon), Day, two_d(H), two_d(Min), Y]).

two_d(N) -> tl(integer_to_list(N + 100)).

month(Mon) -> element(Mon, {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}).

% --- Headers ---

cd_file_header_from_lh_pos_and_extra(LH, Pos, Extra) ->
    #local_file_header{
        version_needed = LHVersionNeeded,
        gp_flag = GPFlag,
        comp_method = CompMethod,
        last_mod_time = LastModTime,
        last_mod_date = LastModDate,
        crc32 = CRC32,
        comp_size = CompSize,
        uncomp_size = UncompSize,
        file_name_length = FileNameLength,
        info = #file_info{type = Type, mode = Mode}
    } = LH,
    VersionNeeded =
        case Pos >= ?MAX_INT32 of
            true -> ?VERSION_NEEDED_ZIP64;
            false -> LHVersionNeeded
        end,
    #cd_file_header{
        os_made_by = ?OS_MADE_BY_UNIX,
        version_made_by = ?VERSION_MADE_BY,
        version_needed = VersionNeeded,
        gp_flag = GPFlag,
        comp_method = CompMethod,
        last_mod_time = LastModTime,
        last_mod_date = LastModDate,
        crc32 = CRC32,
        comp_size =
            case UncompSize >= ?MAX_INT32 of
                true -> ?MAX_INT32;
                false -> CompSize
            end,
        uncomp_size = min(UncompSize, ?MAX_INT32),
        file_name_length = FileNameLength,
        extra_field_length = iolist_size(Extra),
        file_comment_length = 0,
        disk_num_start = 0,
        internal_attr = 0,
        external_attr = external_mode(Mode, Type) bsl 16,
        local_header_offset = min(Pos, ?MAX_INT32)
    }.

external_mode(undefined, regular) -> ?DEFAULT_REGULAR_FILE_MODE;
external_mode(undefined, directory) -> ?DEFAULT_DIRECTORY_FILE_MODE;
external_mode(Mode, _) -> Mode band 8#777.

cd_file_header_to_bin(#cd_file_header{
    os_made_by = OsMadeBy,
    version_made_by = VersionMadeBy,
    version_needed = VersionNeeded,
    gp_flag = GPFlag,
    comp_method = CompMethod,
    last_mod_time = LastModTime,
    last_mod_date = LastModDate,
    crc32 = CRC32,
    comp_size = CompSize,
    uncomp_size = UncompSize,
    file_name_length = FileNameLength,
    extra_field_length = ExtraFieldLength,
    file_comment_length = FileCommentLength,
    disk_num_start = DiskNumStart,
    internal_attr = InternalAttr,
    external_attr = ExternalAttr,
    local_header_offset = LocalHeaderOffset
}) ->
    <<VersionMadeBy:8, OsMadeBy:8, VersionNeeded:16/little, GPFlag:16/little, CompMethod:16/little,
        LastModTime:16/little, LastModDate:16/little, CRC32:32/little, CompSize:32/little, UncompSize:32/little,
        FileNameLength:16/little, ExtraFieldLength:16/little, FileCommentLength:16/little, DiskNumStart:16/little,
        InternalAttr:16/little, ExternalAttr:32/little, LocalHeaderOffset:32/little>>.

local_file_header_to_bin(#local_file_header{
    version_needed = VersionNeeded,
    gp_flag = GPFlag,
    comp_method = CompMethod,
    last_mod_time = LastModTime,
    last_mod_date = LastModDate,
    crc32 = CRC32,
    comp_size = CompSize,
    uncomp_size = UncompSize,
    file_name_length = FileNameLength,
    extra_field_length = ExtraFieldLength
}) ->
    <<VersionNeeded:16/little, GPFlag:16/little, CompMethod:16/little, LastModTime:16/little, LastModDate:16/little,
        CRC32:32/little, CompSize:32/little, UncompSize:32/little, FileNameLength:16/little,
        ExtraFieldLength:16/little>>.

eocd_to_bin(#eocd{
    disk_num = DiskNum,
    start_disk_num = StartDiskNum,
    entries_on_disk = EntriesOnDisk,
    entries = Entries,
    size = Size,
    offset = Offset,
    zip_comment_length = ZipCommentLength
}) ->
    <<DiskNum:16/little, StartDiskNum:16/little, EntriesOnDisk:16/little, Entries:16/little, Size:32/little,
        Offset:32/little, ZipCommentLength:16/little>>.

eocd64_to_bin(#eocd{
    os_made_by = OsMadeBy,
    version_made_by = VersionMadeBy,
    extract_version = ExtractVersion,
    disk_num = DiskNum,
    start_disk_num = StartDiskNum,
    entries_on_disk = EntriesOnDisk,
    entries = Entries,
    size = Size,
    offset = Offset,
    extra = Extra
}) ->
    <<VersionMadeBy:8, OsMadeBy:8, ExtractVersion:16/little, DiskNum:32/little, StartDiskNum:32/little,
        EntriesOnDisk:64/little, Entries:64/little, Size:64/little, Offset:64/little, Extra/binary>>.

% The CRC and the sizes are patched after the data; the modification time is local DOS time.
local_file_header_from_info_method_name(Info, UncompSize, CompMethod, Name, GPFlag, Extra) ->
    #file_info{mtime = MTime, atime = ATime, uid = Uid, gid = Gid} = Info,
    CreationTime = os:system_time(second),
    {ModDate, ModTime} = dos_date_time_from_datetime(
        calendar:system_time_to_local_time(datetime_to_system_time(MTime), second)
    ),
    VersionNeeded =
        case UncompSize >= ?MAX_INT32 of
            true -> ?VERSION_NEEDED_ZIP64;
            false when CompMethod =:= ?STORED -> ?VERSION_NEEDED_STORE;
            false when CompMethod =:= ?DEFLATED -> ?VERSION_NEEDED_DEFLATE
        end,
    #local_file_header{
        version_needed = VersionNeeded,
        gp_flag = GPFlag,
        comp_method = CompMethod,
        last_mod_time = ModTime,
        last_mod_date = ModDate,
        mtime = datetime_to_system_time(MTime),
        atime = datetime_to_system_time(ATime),
        ctime = datetime_to_system_time(CreationTime),
        uid = Uid,
        gid = Gid,
        crc32 = -1,
        comp_size = ?MAX_INT32,
        uncomp_size = min(UncompSize, ?MAX_INT32),
        file_name_length = length(Name),
        extra_field_length = iolist_size(Extra),
        info = Info
    }.

% --- The open archive server ---

openzip_open(F, Options) ->
    case ?CATCH(do_openzip_open(F, Options)) of
        {ok, OpenZip} -> {ok, OpenZip};
        Error -> {error, Error}
    end.

do_openzip_open(F, Options) ->
    #openzip_opts{output = Output, open_opts = OpO, cwd = CWD, skip_dirs = SkipDirs, extra = ExtraOpts} =
        get_openzip_options(Options),
    Input = get_input(F),
    try Input({open, F, OpO -- [write]}, []) of
        In0 ->
            {[#zip_comment{comment = C} | Files], In1} = get_central_dir(
                In0, fun raw_file_info_etc/5, Input, ExtraOpts
            ),
            {ok, #openzip{
                zip_comment = C,
                files = Files,
                in = In1,
                input = Input,
                output = Output,
                zlib = zlib:open(),
                cwd = CWD,
                skip_dirs = SkipDirs,
                extra = ExtraOpts
            }}
    catch
        throw:{_FN, {_, Error}} -> Error
    end.

openzip_get(OpenZip) ->
    case ?CATCH(do_openzip_get(OpenZip)) of
        {ok, Result} -> {ok, Result};
        Error -> {error, Error}
    end.

do_openzip_get(#openzip{files = Files, in = In0, input = Input, output = Output, zlib = Z} = OpenZip) ->
    #openzip{cwd = CWD, skip_dirs = SkipDirs, extra = ExtraOpts} = OpenZip,
    ZipOpts = #unzip_opts{
        output = Output,
        input = Input,
        file_filter = fun all/1,
        open_opts = [],
        feedback = fun silent/1,
        cwd = CWD,
        skip_dirs = SkipDirs,
        extra = ExtraOpts
    },
    {ok, get_z_files(Files, Z, In0, ZipOpts, [])};
do_openzip_get(_) ->
    throw(einval).

openzip_get_crc32(FileName, #openzip{files = Files}) ->
    case file_name_search(FileName, Files) of
        {_, #zip_file_extra{crc32 = CRC}} -> {ok, CRC};
        _ -> throw(file_not_found)
    end.

openzip_get(FileName, OpenZip) ->
    case ?CATCH(do_openzip_get(FileName, OpenZip)) of
        {ok, Result} -> {ok, Result};
        Error -> {error, Error}
    end.

do_openzip_get(F, #openzip{files = Files, in = In0, input = Input, output = Output, zlib = Z} = OpenZip) ->
    #openzip{cwd = CWD, extra = ExtraOpts} = OpenZip,
    case file_name_search(F, Files) of
        {#zip_file{offset = Offset}, _} = ZFile ->
            In1 = Input({seek, bof, Offset}, In0),
            case get_z_file(In1, Z, {Input, Output, [], fun silent/1}, CWD, ZFile, {fun all/1, false, ExtraOpts}) of
                {file, R, _In2} -> {ok, Output(flush, R)};
                _ -> throw(file_not_found)
            end;
        _ ->
            throw(file_not_found)
    end;
do_openzip_get(_, _) ->
    throw(einval).

% Names compare as Unicode in normalization form C.
file_name_search(Name, Files) ->
    Different = fun({ZipFile, _}) -> not string:equal(ZipFile#zip_file.name, Name, false, nfc) end,
    case lists:dropwhile(Different, Files) of
        [ZFile | _] -> ZFile;
        [] -> false
    end.

openzip_list_dir(#openzip{zip_comment = Comment, files = Files}) ->
    {ZipFiles, _Extras} = lists:unzip(Files),
    {ok, [#zip_comment{comment = Comment} | ZipFiles]};
openzip_list_dir(_) ->
    {error, einval}.

openzip_close(#openzip{in = In0, input = Input, zlib = Z}) ->
    Input(close, In0),
    zlib:close(Z);
openzip_close(_) ->
    {error, einval}.

% The server traps exits to close the archive when its opener ends.
server_init(Parent) ->
    process_flag(trap_exit, true),
    server_loop(Parent, not_open).

server_loop(Parent, OpenZip) ->
    receive
        {From, {open, Archive, Options}} ->
            case openzip_open(Archive, Options) of
                {ok, NewOpenZip} ->
                    From ! {self(), {ok, self()}},
                    server_loop(Parent, NewOpenZip);
                Error ->
                    From ! {self(), Error}
            end;
        {From, close} ->
            From ! {self(), openzip_close(OpenZip)};
        {From, get} ->
            From ! {self(), openzip_get(OpenZip)},
            server_loop(Parent, OpenZip);
        {From, {get, FileName}} ->
            From ! {self(), openzip_get(FileName, OpenZip)},
            server_loop(Parent, OpenZip);
        {From, {get_crc32, FileName}} ->
            From ! {self(), openzip_get_crc32(FileName, OpenZip)},
            server_loop(Parent, OpenZip);
        {From, list_dir} ->
            From ! {self(), openzip_list_dir(OpenZip)},
            server_loop(Parent, OpenZip);
        {From, get_state} ->
            From ! {self(), OpenZip},
            server_loop(Parent, OpenZip);
        {'EXIT', Parent, Reason} ->
            _ = openzip_close(OpenZip),
            exit({parent_died, Reason});
        _ ->
            % Another message ends the server, as OTP's.
            {error, bad_msg}
    end.

openzip_tt(#openzip{zip_comment = ZipComment, files = Files}) ->
    print_comment(ZipComment),
    [
        print_header(CompSize, MTime, UncompSize, FileName, FileComment)
     || {#zip_file{comp_size = CompSize, name = FileName, comment = FileComment, info = FI}, _} <- Files,
        #file_info{size = UncompSize, mtime = MTime} <- [FI]
    ],
    ok.

openzip_t(#openzip{zip_comment = ZipComment, files = Files}) ->
    print_comment(ZipComment),
    [print_file_name(FileName) || {#zip_file{name = FileName}, _} <- Files],
    ok.

get_openzip_opt([], Opts) ->
    Opts;
get_openzip_opt([cooked | Rest], #openzip_opts{open_opts = OO} = Opts) ->
    get_openzip_opt(Rest, Opts#openzip_opts{open_opts = OO -- [raw]});
get_openzip_opt([memory | Rest], Opts) ->
    get_openzip_opt(Rest, Opts#openzip_opts{output = fun binary_io/2});
get_openzip_opt([{cwd, CWD} | Rest], Opts) ->
    get_openzip_opt(Rest, Opts#openzip_opts{cwd = CWD});
get_openzip_opt([skip_directories | Rest], Opts) ->
    get_openzip_opt(Rest, Opts#openzip_opts{skip_dirs = true});
get_openzip_opt([{extra, What} = O | Rest], Opts) when is_list(What) ->
    case lists:all(fun(E) -> lists:member(E, ?EXTRA_OPTIONS) end, What) of
        true -> get_zip_opt(Rest, Opts#openzip_opts{extra = What});
        false -> throw({bad_option, O})
    end;
get_openzip_opt([Unknown | _Rest], _Opts) ->
    throw({bad_option, Unknown}).

% --- Reading the central directory ---

get_central_dir(In0, RawIterator, Input, ExtraOpts) ->
    {Size, In1} = Input({position, eof, 0}, In0),
    Max = min(16#ffff + ?END_OF_CENTRAL_DIR_SZ + ?END_OF_CENTRAL_DIR_64_LOCATOR_SZ, Size),
    {{EOCD, BComment}, In2} = get_end_of_central_dir(In1, ?END_OF_CENTRAL_DIR_SZ, Max, Input),
    EOCD#eocd.disk_num == 0 orelse throw(multiple_disks_not_supported),
    In3 = Input({seek, bof, EOCD#eocd.offset}, In2),
    % The archive comment has no encoding flag.
    Out0 = RawIterator(EOCD, "", heuristic_to_string(BComment), <<>>, []),
    get_cd_loop(EOCD#eocd.entries, In3, {RawIterator, Input, ExtraOpts}, Out0).

get_cd_loop(0, In, _Funs, Acc) ->
    {lists:reverse(Acc), In};
get_cd_loop(N, In0, {RawIterator, Input, ExtraOpts} = Funs, Acc0) ->
    {BCD, In1} =
        case Input({read, ?CENTRAL_FILE_HEADER_SZ}, In0) of
            {<<?CENTRAL_FILE_MAGIC:32/little, XBCD/binary>>, In} -> {XBCD, In};
            _ -> throw(bad_central_directory)
        end,
    CD = cd_file_header_from_bin(BCD),
    #cd_file_header{file_name_length = FileNameLen, extra_field_length = ExtraLen, file_comment_length = CommentLen} =
        CD,
    {B2, In2} = Input({read, FileNameLen + ExtraLen + CommentLen}, In1),
    {FileName, BExtra, Comment} =
        get_filename_extra_comment(B2, {FileNameLen, ExtraLen, CommentLen}, CD#cd_file_header.gp_flag),
    ExtraCD = update_extra_fields(CD, BExtra, ExtraOpts),
    get_cd_loop(N - 1, In2, Funs, RawIterator(ExtraCD, FileName, Comment, BExtra, Acc0)).

% The fields read from the extra fields; the local and central header records keep them at the same positions.
update_extra_fields(FileHeader, BExtra, ExtraOpts) ->
    ExtendedTimestamp = lists:member(extended_timestamp, ExtraOpts),
    UidGid = lists:member(uid_gid, ExtraOpts),
    lists:foldl(
        fun
            ({?X0001_ZIP64, Data}, Acc) -> update_zip64(Acc, Data);
            ({?X5455_EXTENDED_TIMESTAMP, Data}, Acc) when ExtendedTimestamp -> update_extended_timestamp(Acc, Data);
            ({?X7875_UNIX3, Data}, Acc) when UidGid -> update_unix3(Acc, Data);
            (_, Acc) -> Acc
        end,
        FileHeader,
        parse_extra(BExtra)
    ).

update_zip64(FH, <<UnComp:64/little, Rest/binary>>) when element(#cd_file_header.uncomp_size, FH) == ?MAX_INT32 ->
    update_zip64(setelement(#cd_file_header.uncomp_size, FH, UnComp), Rest);
update_zip64(FH, <<Comp:64/little, Rest/binary>>) when element(#cd_file_header.comp_size, FH) == ?MAX_INT32 ->
    update_zip64(setelement(#cd_file_header.comp_size, FH, Comp), Rest);
update_zip64(FH, <<Offset:64/little, Rest/binary>>) when
    element(#cd_file_header.local_header_offset, FH) == ?MAX_INT32
->
    update_zip64(setelement(#cd_file_header.local_header_offset, FH, Offset), Rest);
update_zip64(FH, <<DiskNumStart:32/little, Rest/binary>>) when
    element(#cd_file_header.disk_num_start, FH) == ?MAX_INT32
->
    update_zip64(setelement(#cd_file_header.disk_num_start, FH, DiskNumStart), Rest);
update_zip64(FH, _) ->
    % Some archives put unused data here.
    FH.

update_extended_timestamp(FileHeader, <<_:5, HasCre:1, HasAcc:1, HasMod:1, Data/binary>>) ->
    {FHMod, DataMod} = update_extended_timestamp(FileHeader, HasMod, Data, #cd_file_header.mtime),
    {FHAcc, DataAcc} = update_extended_timestamp(FHMod, HasAcc, DataMod, #cd_file_header.atime),
    {FHCre, <<>>} = update_extended_timestamp(FHAcc, HasCre, DataAcc, #cd_file_header.ctime),
    FHCre.

update_extended_timestamp(FH, 1, <<Value:32/little, Rest/binary>>, Field) -> {setelement(Field, FH, Value), Rest};
update_extended_timestamp(FH, 1, <<>>, _Field) -> {FH, <<>>};
update_extended_timestamp(FH, 0, Data, _Field) -> {FH, Data}.

update_unix3(FH, <<1, UidSize, Uid:(UidSize * 8)/little, GidSize, Gid:(GidSize * 8)/little>>) ->
    setelement(#cd_file_header.gid, setelement(#cd_file_header.uid, FH, Uid), Gid);
update_unix3(FH, <<Vsn, _/binary>>) when Vsn =/= 1 ->
    FH.

parse_extra(<<Tag:16/little, Sz:16/little, Data:Sz/binary, Rest/binary>>) -> [{Tag, Data} | parse_extra(Rest)];
parse_extra(<<>>) -> [].

get_filename_extra_comment(B, {FileNameLen, ExtraLen, CommentLen}, GPFlag) ->
    try
        <<BFileName:FileNameLen/binary, BExtra:ExtraLen/binary, BComment:CommentLen/binary>> = B,
        % Comments are read by heuristics whatever bit 11 says, as some archivers set it only for names.
        {binary_to_chars(BFileName, GPFlag), BExtra, heuristic_to_string(BComment)}
    catch
        _:_ -> throw(bad_central_directory)
    end.

get_end_of_central_dir(In0, Sz, MaxCentralDirSize, Input) ->
    In1 = Input({seek, eof, -Sz}, In0),
    {B, In2} = Input({read, Sz}, In1),
    case find_eocd(B) of
        none when Sz =:= MaxCentralDirSize ->
            throw(bad_eocd);
        none ->
            get_end_of_central_dir(In2, min(Sz + Sz, MaxCentralDirSize), MaxCentralDirSize, Input);
        {EOCD64Location, EOCD, Comment} ->
            case find_eocd64(In2, EOCD64Location, {EOCD, Comment}, Input) of
                none -> throw(bad_eocd64);
                {EOCD64, In3} -> {EOCD64, In3}
            end;
        Header ->
            {Header, In2}
    end.

find_eocd(
    <<?END_OF_CENTRAL_DIR_64_LOCATOR_MAGIC:32/little, EOCD64StartDiskNum:32/little, EOCD64Offset:64/little,
        EOCD64TotalDisk:32/little, ?END_OF_CENTRAL_DIR_MAGIC:32/little, DiskNum:16/little, StartDiskNum:16/little,
        EntriesOnDisk:16/little, Entries:16/little, Size:32/little, Offset:32/little, ZipCommentLength:16/little,
        Comment:ZipCommentLength/binary>>
) ->
    % OTP's guard: entries count only when both counts are at their limit.
    case
        DiskNum =:= ?MAX_INT16 orelse StartDiskNum =:= ?MAX_INT16 orelse
            (EntriesOnDisk =:= ?MAX_INT16 andalso Entries =:= ?MAX_INT16) orelse Size =:= ?MAX_INT32 orelse
            Offset =:= ?MAX_INT32
    of
        true ->
            EOCD = #eocd{
                disk_num = DiskNum,
                start_disk_num = StartDiskNum,
                entries_on_disk = EntriesOnDisk,
                entries = Entries,
                size = Size,
                offset = Offset,
                zip_comment_length = ZipCommentLength
            },
            {{EOCD64StartDiskNum, EOCD64Offset, EOCD64TotalDisk}, EOCD, Comment};
        false ->
            none
    end;
find_eocd(
    <<?END_OF_CENTRAL_DIR_MAGIC:32/little, DiskNum:16/little, StartDiskNum:16/little, EntriesOnDisk:16/little,
        Entries:16/little, Size:32/little, Offset:32/little, ZipCommentLength:16/little,
        Comment:ZipCommentLength/binary>>
) ->
    case
        DiskNum =:= ?MAX_INT16 orelse StartDiskNum =:= ?MAX_INT16 orelse EntriesOnDisk =:= ?MAX_INT16 orelse
            Entries =:= ?MAX_INT16 orelse Size =:= ?MAX_INT32 orelse Offset =:= ?MAX_INT32
    of
        % A zip64 end record's locator should come before this one.
        true ->
            none;
        false ->
            EOCD = #eocd{
                disk_num = DiskNum,
                start_disk_num = StartDiskNum,
                entries_on_disk = EntriesOnDisk,
                entries = Entries,
                size = Size,
                offset = Offset,
                zip_comment_length = ZipCommentLength
            },
            {EOCD, Comment}
    end;
find_eocd(<<_:8, Rest/binary>>) when byte_size(Rest) > ?END_OF_CENTRAL_DIR_SZ - 4 ->
    find_eocd(Rest);
find_eocd(_) ->
    none.

find_eocd64(In0, {_EOCD64StartDiskNum, EOCD64Offset, _EOCD64TotalDisk}, {EOCD, Comment}, Input) ->
    In1 = Input({seek, bof, EOCD64Offset}, In0),
    case Input({read, 4 + 8}, In1) of
        {<<?END_OF_CENTRAL_DIR_64_MAGIC:32/little, EOCDSize:64/little>>, In2} ->
            ExtraSize = EOCDSize - ?END_OF_CENTRAL_DIR_64_SZ,
            case Input({read, EOCDSize}, In2) of
                {
                    <<VersionMadeBy:8, OsMadeBy:8, ExtractVersion:16/little, DiskNum:32/little, StartDiskNum:32/little,
                        EntriesOnDisk:64/little, Entries:64/little, Size:64/little, Offset:64/little,
                        Extra:ExtraSize/binary>>,
                    In3
                } ->
                    EOCD64 = EOCD#eocd{
                        eocd = EOCD,
                        version_made_by = VersionMadeBy,
                        os_made_by = os_id_to_atom(OsMadeBy),
                        extract_version = ExtractVersion,
                        disk_num = DiskNum,
                        start_disk_num = StartDiskNum,
                        entries_on_disk = EntriesOnDisk,
                        entries = Entries,
                        size = Size,
                        offset = Offset,
                        extra = parse_extra(Extra)
                    },
                    {{EOCD64, Comment}, In3};
                {eof, InEOF} ->
                    {eof, InEOF};
                _ ->
                    none
            end;
        {eof, InEOF} ->
            {eof, InEOF};
        _ ->
            none
    end.

os_id_to_atom(No) when is_integer(No), No >= 0, No =< 19 ->
    element(No + 1, {
        <<"MS-DOS and OS/2">>,
        <<"Amiga">>,
        <<"OpenVMS">>,
        <<"UNIX">>,
        <<"VM/CMS">>,
        <<"Atari ST">>,
        <<"OS/2 H.P.F.S">>,
        <<"Macintosh">>,
        <<"Z-System">>,
        <<"CP/M">>,
        <<"Windows NTFS">>,
        <<"MVS">>,
        <<"VSE">>,
        <<"Acorn Risc">>,
        <<"VFAT">>,
        <<"alternate MVS">>,
        <<"BeOS">>,
        <<"Tandem">>,
        <<"OS/400">>,
        <<"OS X (Darwin)">>
    });
os_id_to_atom(No) ->
    No.

raw_file_info_etc(
    #cd_file_header{comp_size = CompSize, local_header_offset = Offset, crc32 = CRC} = CD,
    FileName,
    FileComment,
    BExtraField,
    Acc
) ->
    FileInfo = cd_file_header_to_file_info(FileName, CD, BExtraField),
    ZipFile = #zip_file{name = FileName, info = FileInfo, comment = FileComment, offset = Offset, comp_size = CompSize},
    [{ZipFile, #zip_file_extra{crc32 = CRC}} | Acc];
raw_file_info_etc(#eocd{}, _, Comment, _, Acc) ->
    [#zip_comment{comment = Comment} | Acc].

raw_file_info_public(CD, FileName, FileComment, BExtraField, Acc0) ->
    case raw_file_info_etc(CD, FileName, FileComment, BExtraField, Acc0) of
        [{ZF, #zip_file_extra{}} | T] -> [ZF | T];
        Other -> Other
    end.

cd_file_header_to_file_info(FileName, #cd_file_header{uncomp_size = UncompSize} = CDFH, _ExtraField) ->
    Type =
        case lists:last(FileName) of
            $/ -> directory;
            _ -> regular
        end,
    Mode =
        case CDFH#cd_file_header.os_made_by of
            <<"UNIX">> -> (CDFH#cd_file_header.external_attr bsr 16) band 8#777;
            _ when Type =:= directory -> ?DEFAULT_DIRECTORY_FILE_MODE;
            _ -> ?DEFAULT_REGULAR_FILE_MODE
        end,
    #file_info{
        size = UncompSize,
        type = Type,
        access = read_write,
        atime = file_header_atime_to_datetime(CDFH),
        mtime = file_header_mtime_to_datetime(CDFH),
        ctime = file_header_ctime_to_datetime(CDFH),
        mode = Mode,
        links = 1,
        major_device = 0,
        minor_device = 0,
        inode = 0,
        uid = CDFH#cd_file_header.uid,
        gid = CDFH#cd_file_header.gid
    }.

% --- Reading files ---

get_z_files([], _Z, _In, #unzip_opts{output = Output}, Acc) ->
    flush_and_reverse(Output, Acc, []);
get_z_files([#zip_comment{comment = _} | Rest], Z, In, Opts, Acc) ->
    get_z_files(Rest, Z, In, Opts, Acc);
get_z_files([{#zip_file{offset = Offset} = ZipFile, ZipExtra} | Rest], Z, In0, Opts, Acc0) ->
    #unzip_opts{
        input = Input,
        output = Output,
        open_opts = OpO,
        file_filter = Filter,
        feedback = FB,
        cwd = CWD,
        skip_dirs = SkipDirs,
        extra = ExtraOpts
    } = Opts,
    case Filter({ZipFile, ZipExtra, CWD}) of
        true ->
            In1 = Input({seek, bof, Offset}, In0),
            {In2, Acc1} =
                case
                    get_z_file(
                        In1, Z, {Input, Output, OpO, FB}, CWD, {ZipFile, ZipExtra}, {Filter, SkipDirs, ExtraOpts}
                    )
                of
                    {Type, GZD, Inx} when Type =:= file; Type =:= dir -> {Inx, [GZD | Acc0]};
                    {_, Inx} -> {Inx, Acc0}
                end,
            get_z_files(Rest, Z, In2, Opts, Acc1);
        _ ->
            get_z_files(Rest, Z, In0, Opts, Acc0)
    end.

flush_and_reverse(Output, [H | T], Acc) -> flush_and_reverse(Output, T, [Output(flush, H) | Acc]);
flush_and_reverse(_Output, [], Acc) -> Acc.

get_z_file(In0, Z, {Input, Output, OpO, FB}, CWD, {ZipFile, ZipExtra}, {Filter, SkipDirs, ExtraOpts}) ->
    case Input({read, ?LOCAL_FILE_HEADER_SZ}, In0) of
        {eof, In1} ->
            {eof, In1};
        {<<?LOCAL_FILE_MAGIC:32/little, B/binary>>, In1} ->
            LH = local_file_header_from_bin(B),
            #local_file_header{
                gp_flag = GPFlag,
                comp_method = CompMethod,
                file_name_length = FileNameLen,
                extra_field_length = ExtraLen
            } = LH,
            {BFileN, In3} = Input({read, FileNameLen + ExtraLen}, In1),
            {FileName, BLHExtra} = get_filename_extra(FileNameLen, ExtraLen, BFileN, GPFlag),
            LHExtra = update_extra_fields(LH, BLHExtra, ExtraOpts),
            % A file with a data descriptor (bit 3) has its sizes and CRC in the central directory.
            {CompSize, CRC32} =
                case GPFlag band 8 =:= 8 of
                    true -> {ZipFile#zip_file.comp_size, ZipExtra#zip_file_extra.crc32};
                    false -> {LHExtra#local_file_header.comp_size, LHExtra#local_file_header.crc32}
                end,
            ReadAndWrite =
                case check_valid_location(CWD, FileName) of
                    {true, FileName1} -> true;
                    {false, FileName1} -> Filter({ZipFile#zip_file{name = FileName1}, ZipExtra, CWD})
                end,
            FileNameWithCwd = add_cwd(CWD, FileName1),
            IsDir = lists:last(FileName) =:= $/,
            case ReadAndWrite andalso not (IsDir andalso SkipDirs) of
                true ->
                    {Type, Out, In} =
                        case IsDir of
                            true ->
                                {dir, Output({ensure_path, FileNameWithCwd}, []), In3};
                            false ->
                                {Out1, In4, CRC, _UncompSize} =
                                    get_z_data(CompMethod, In3, FileNameWithCwd, {CompSize, Input, Output, OpO, Z}),
                                In5 = skip_z_data_descriptor(GPFlag, Input, In4),
                                FB(FileName),
                                CRC =:= CRC32 orelse throw({FileName, bad_crc}),
                                {file, Out1, In5}
                        end,
                    FileInfo = local_file_header_to_file_info(
                        Output({file_info, FileNameWithCwd}, Out), LHExtra, ZipFile
                    ),
                    SetFileInfo = fun(O) -> Output({set_file_info, FileNameWithCwd, FileInfo, [{time, local}]}, O) end,
                    Out2 =
                        case Type of
                            dir -> Output({delay, SetFileInfo}, Out);
                            file -> SetFileInfo(Out)
                        end,
                    {Type, Out2, In};
                false ->
                    {ignore, In3}
            end;
        Else ->
            throw({bad_local_file_header, Else})
    end.

local_file_header_to_file_info(FI, LFH, ZipFile) ->
    FI#file_info{
        mode = ZipFile#zip_file.info#file_info.mode,
        mtime = file_header_mtime_to_datetime(LFH),
        atime = file_header_atime_to_datetime(LFH),
        ctime = file_header_ctime_to_datetime(LFH)
    }.

% Names climbing above the target directory are reported and given to the filter as their last part.
check_valid_location(CWD, FileName) ->
    TrailingSlash =
        case lists:last(FileName) of
            $/ -> "/";
            _ -> ""
        end,
    {IsValid, Name} =
        case check_dir_level(FileName) of
            {FileOrDir, invalid} ->
                CWD1 =
                    case CWD of
                        "" -> "./";
                        _ -> CWD
                    end,
                error_logger:format("Illegal path: ~ts, extracting in ~ts~n", [add_cwd(CWD, FileName), CWD1]),
                {false, FileOrDir};
            {_FileOrDir, valid} ->
                {true, FileName}
        end,
    {IsValid, string:trim(Name, trailing, "/") ++ TrailingSlash}.

% "." parts are dropped; a name of only "." is returned as it is, as OTP does.
check_dir_level(FileName) ->
    case [Component || Component <- filename:split(FileName), not string:equal(Component, ".")] of
        [_ | _] = Parts ->
            try cdl_1(Parts, 0) of
                Res -> Res
            catch
                error:_ -> error({invalid_filename, FileName})
            end;
        [] ->
            {".", invalid}
    end.

cdl_1([".." | Parts], Level) when Level > 0 ->
    cdl_1(Parts, Level - 1);
cdl_1([".." | Parts], _Level) ->
    % ".." itself is never given to the filter.
    Last = lists:last(Parts),
    false = string:equal(Last, ".."),
    {Last, invalid};
cdl_1([FileOrDir], Level) ->
    true = Level >= 0,
    {FileOrDir, valid};
cdl_1([_Dir | Parts], Level) ->
    cdl_1(Parts, Level + 1).

get_filename_extra(FileNameLen, ExtraLen, B, GPFlag) ->
    try
        <<BFileName:FileNameLen/binary, BExtra:ExtraLen/binary>> = B,
        {sanitize_filename(binary_to_chars(BFileName, GPFlag)), BExtra}
    catch
        _:_ -> throw(bad_file_header)
    end.

% Absolute names lose their root and are reported.
sanitize_filename(Filename) ->
    case filename:pathtype(Filename) of
        relative ->
            Filename;
        _ ->
            Relative = filename:join(tl(filename:split(Filename))),
            error_logger:format("Illegal absolute path: ~ts, converting to ~ts~n", [Filename, Relative]),
            relative = filename:pathtype(Relative),
            Relative
    end.

get_z_data(?DEFLATED, In0, FileName, {CompSize, Input, Output, OpO, Z}) ->
    ok = zlib:inflateInit(Z, -?MAX_WBITS),
    Out0 = Output({open, FileName, [write | OpO]}, []),
    {In1, Out1, UncompSize, CRC} = get_z_data_loop(CompSize, 0, In0, Out0, {Input, Output, Z}, 0),
    _ = ?CATCH(zlib:inflateEnd(Z)),
    {Output({close, FileName}, Out1), In1, CRC, UncompSize};
get_z_data(?STORED, In0, FileName, {CompSize, Input, Output, OpO, _Z}) ->
    Out0 = Output({open, FileName, [write | OpO]}, []),
    {In1, Out1, CRC} = copy_data_loop(CompSize, In0, Out0, {Input, Output}, 0),
    {Output({close, FileName}, Out1), In1, CRC, CompSize};
get_z_data(_, _, _, _) ->
    throw(bad_file_header).

copy_data_loop(0, In, Out, _Funs, CRC) ->
    {In, Out, CRC};
copy_data_loop(CompSize, In0, Out0, {Input, Output} = Funs, CRC0) ->
    N = min(?READ_BLOCK_SIZE, CompSize),
    case Input({read, N}, In0) of
        {eof, In1} ->
            {Out0, In1};
        {Uncompressed, In1} ->
            CRC1 = erlang:crc32(CRC0, Uncompressed),
            copy_data_loop(CompSize - N, In1, Output({write, Uncompressed}, Out0), Funs, CRC1)
    end.

get_z_data_loop(0, UncompSize, In, Out, _Funs, CRC0) ->
    {In, Out, UncompSize, CRC0};
get_z_data_loop(CompSize, UncompSize, In0, Out0, {Input, Output, Z} = Funs, CRC0) ->
    N = min(?READ_BLOCK_SIZE, CompSize),
    case Input({read, N}, In0) of
        {eof, In1} ->
            {Out0, In1};
        {Compressed, In1} ->
            Uncompressed = zlib:inflate(Z, Compressed),
            CRC1 = erlang:crc32(CRC0, Uncompressed),
            Out1 = Output({write, Uncompressed}, Out0),
            get_z_data_loop(CompSize - N, UncompSize + iolist_size(Uncompressed), In1, Out1, Funs, CRC1)
    end.

skip_z_data_descriptor(GPFlag, Input, In0) when GPFlag band 8 =:= 8 -> Input({seek, cur, 12}, In0);
skip_z_data_descriptor(_GPFlag, _Input, In0) -> In0.

% Times from the extended timestamp, else the DOS time.
file_header_mtime_to_datetime(FH) -> header_time(element(#cd_file_header.mtime, FH), FH).

file_header_atime_to_datetime(FH) -> header_time(element(#cd_file_header.atime, FH), FH).

file_header_ctime_to_datetime(FH) ->
    case element(#cd_file_header.ctime, FH) of
        undefined -> undefined;
        Ctime -> calendar:system_time_to_local_time(Ctime, second)
    end.

header_time(undefined, FH) ->
    dos_date_time_to_datetime(element(#cd_file_header.last_mod_date, FH), element(#cd_file_header.last_mod_time, FH));
header_time(Seconds, _FH) ->
    calendar:system_time_to_local_time(Seconds, second).

% Seconds past 59 wrap through gregorian seconds, as unzip does.
dos_date_time_to_datetime(DosDate, DosTime) ->
    <<Hour:5, Min:6, DoubleSec:5>> = <<DosTime:16>>,
    <<YearFrom1980:7, Month:4, Day:5>> = <<DosDate:16>>,
    Date = {YearFrom1980 + 1980, Month, Day},
    case DoubleSec > 29 of
        true ->
            GSec = calendar:datetime_to_gregorian_seconds({Date, {Hour, Min, 0}}) + DoubleSec * 2,
            calendar:gregorian_seconds_to_datetime(GSec);
        false ->
            {Date, {Hour, Min, DoubleSec * 2}}
    end.

dos_date_time_from_datetime({{Year, _Month, _Day}, {_Hour, _Min, _Sec}}) when Year < 1980 ->
    error_logger:format("Found timestamp before 1980, using 1st of Jan 1980~n", []),
    dos_date_time_from_datetime({{1980, 1, 1}, {0, 0, 0}});
dos_date_time_from_datetime({{Year, Month, Day}, {Hour, Min, Sec}}) ->
    <<DosTime:16>> = <<Hour:5, Min:6, (Sec div 2):5>>,
    <<DosDate:16>> = <<(Year - 1980):7, Month:4, Day:5>>,
    {DosDate, DosTime}.

datetime_to_system_time(undefined) -> undefined;
datetime_to_system_time(PosixTime) when is_integer(PosixTime) -> PosixTime;
datetime_to_system_time(DateTime) -> erlang:universaltime_to_posixtime(erlang:localtime_to_universaltime(DateTime)).

% --- Binary and file I/O ---

pwrite_binary(B, Pos, Bin) when byte_size(B) =:= Pos -> append_bins(Bin, B);
pwrite_binary(B, Pos, Bin) -> iolist_to_binary(pwrite_iolist(B, Pos, Bin)).

append_bins([Bin | Bins], B) when is_binary(Bin) -> append_bins(Bins, <<B/binary, Bin/binary>>);
append_bins([List | Bins], B) when is_list(List) -> append_bins(Bins, append_bins(List, B));
append_bins(Bin, B) when is_binary(Bin) -> <<B/binary, Bin/binary>>;
append_bins([_ | _] = List, B) -> <<B/binary, (iolist_to_binary(List))/binary>>;
append_bins([], B) -> B.

pwrite_iolist(B, Pos, Bin) ->
    {Left, Right} = split_binary(B, Pos),
    Sz = iolist_size(Bin),
    R =
        case Right of
            <<_:Sz/binary, Rest/binary>> -> Rest;
            _ -> <<>>
        end,
    [Left, Bin | R].

binary_to_chars(B, GPFlag) when GPFlag band ?GP_BIT_11 =:= 0 -> binary_to_list(B);
binary_to_chars(B, _GPFlag) when is_binary(B) ->
    case unicode:characters_to_list(B) of
        List when is_list(List) -> List
    end.

heuristic_to_string(B) when is_binary(B) ->
    case unicode:characters_to_binary(B) of
        B -> unicode:characters_to_list(B);
        _ -> binary_to_list(B)
    end.

% A name with characters above 127 is written in UTF-8 with bit 11 set.
encode_string(String) ->
    case lists:any(fun(C) -> C > 127 end, String) of
        true ->
            case unicode:characters_to_binary(String) of
                B when is_binary(B) -> {binary_to_list(B), ?GP_BIT_11};
                _ -> throw({bad_unicode, String})
            end;
        false ->
            {String, 0}
    end.

cd_file_header_from_bin(
    <<VersionMadeBy:8, OsMadeBy:8, VersionNeeded:16/little, GPFlag:16/little, CompMethod:16/little,
        LastModTime:16/little, LastModDate:16/little, CRC32:32/little, CompSize:32/little, UncompSize:32/little,
        FileNameLength:16/little, ExtraFieldLength:16/little, FileCommentLength:16/little, DiskNumStart:16/little,
        InternalAttr:16/little, ExternalAttr:32/little, LocalHeaderOffset:32/little>>
) ->
    #cd_file_header{
        version_made_by = VersionMadeBy,
        os_made_by = os_id_to_atom(OsMadeBy),
        version_needed = VersionNeeded,
        gp_flag = GPFlag,
        comp_method = CompMethod,
        last_mod_time = LastModTime,
        last_mod_date = LastModDate,
        crc32 = CRC32,
        comp_size = CompSize,
        uncomp_size = UncompSize,
        file_name_length = FileNameLength,
        extra_field_length = ExtraFieldLength,
        file_comment_length = FileCommentLength,
        disk_num_start = DiskNumStart,
        internal_attr = InternalAttr,
        external_attr = ExternalAttr,
        local_header_offset = LocalHeaderOffset
    };
cd_file_header_from_bin(_) ->
    throw(bad_cd_file_header).

local_file_header_from_bin(
    <<VersionNeeded:16/little, GPFlag:16/little, CompMethod:16/little, LastModTime:16/little, LastModDate:16/little,
        CRC32:32/little, CompSize:32/little, UncompSize:32/little, FileNameLength:16/little,
        ExtraFieldLength:16/little>>
) ->
    #local_file_header{
        version_needed = VersionNeeded,
        gp_flag = GPFlag,
        comp_method = CompMethod,
        last_mod_time = LastModTime,
        last_mod_date = LastModDate,
        crc32 = CRC32,
        comp_size = CompSize,
        uncomp_size = UncompSize,
        file_name_length = FileNameLength,
        extra_field_length = ExtraFieldLength
    };
local_file_header_from_bin(_) ->
    throw(bad_local_file_header).

% Archives and files in memory: a position and a binary; written files are {Name, Binary}.
binary_io({file_info, FN, Opts}, A) ->
    FI = binary_io({file_info, FN}, A),
    case proplists:get_value(time, Opts, local) of
        local ->
            FI;
        posix ->
            FI#file_info{
                atime = datetime_to_system_time(FI#file_info.atime),
                mtime = datetime_to_system_time(FI#file_info.mtime),
                ctime = datetime_to_system_time(FI#file_info.ctime)
            }
    end;
binary_io({file_info, {_Filename, _B, #file_info{} = FI}}, _A) ->
    FI;
binary_io({file_info, {_Filename, #file_info{} = FI, _B}}, _A) ->
    FI;
binary_io({file_info, {_Filename, B}}, A) ->
    binary_io({file_info, B}, A);
binary_io({file_info, Filename}, A) when is_list(Filename) ->
    binary_io({file_info, {Filename, <<>>}}, A);
binary_io({file_info, B}, _) ->
    {Type, Size} =
        if
            is_binary(B) -> {regular, byte_size(B)};
            B =:= directory -> {directory, 0}
        end,
    Now = calendar:local_time(),
    Mode =
        case Type of
            directory -> ?DEFAULT_DIRECTORY_FILE_MODE;
            regular -> ?DEFAULT_REGULAR_FILE_MODE
        end,
    #file_info{
        size = Size,
        type = Type,
        access = read_write,
        atime = Now,
        mtime = Now,
        ctime = Now,
        mode = Mode,
        links = 1,
        major_device = 0,
        minor_device = 0,
        inode = 0,
        uid = 0,
        gid = 0
    };
binary_io({open, {_Filename, B, _FI}, _Opts}, _) when is_binary(B) ->
    {0, B};
binary_io({open, {_Filename, _FI, B}, _Opts}, _) when is_binary(B) ->
    {0, B};
binary_io({open, {_Filename, B}, _Opts}, _) when is_binary(B) ->
    {0, B};
binary_io({open, B, _Opts}, _) when is_binary(B) ->
    {0, B};
binary_io({open, Filename, _Opts}, _) when is_list(Filename) ->
    {0, <<>>};
binary_io({read, N}, {Pos, B}) when Pos >= byte_size(B) ->
    {eof, {Pos + N, B}};
binary_io({read, N}, {Pos, B}) when Pos + N > byte_size(B) ->
    <<_:Pos/binary, Read/binary>> = B,
    {Read, {byte_size(B), B}};
binary_io({pread, Pos, N}, {OldPos, B}) ->
    case B of
        <<_:Pos/binary, Read:N/binary, _Rest/binary>> -> {Read, {Pos + N, B}};
        _ -> {eof, {OldPos, B}}
    end;
binary_io({read, N}, {Pos, B}) ->
    <<_:Pos/binary, Read:N/binary, _/binary>> = B,
    {Read, {Pos + N, B}};
binary_io({seek, bof, Pos}, {_OldPos, B}) ->
    {Pos, B};
binary_io({seek, cur, Pos}, {OldPos, B}) ->
    {OldPos + Pos, B};
binary_io({seek, eof, Pos}, {_OldPos, B}) ->
    {byte_size(B) + Pos, B};
binary_io({position, Loc, Adj}, File) ->
    {Pos, _} = NewFile = binary_io({seek, Loc, Adj}, File),
    {Pos, NewFile};
binary_io({pwrite, Pos, Data}, {OldPos, B}) ->
    {OldPos, pwrite_binary(B, Pos, Data)};
binary_io({write, Data}, {Pos, B}) ->
    {Pos + iolist_size(Data), pwrite_binary(B, Pos, Data)};
binary_io(close, {_Pos, B}) ->
    B;
binary_io({close, FN}, {_Pos, B}) ->
    {FN, B};
binary_io({list_dir, _F}, _B) ->
    [];
binary_io({set_file_info, _F, _FI}, B) ->
    B;
binary_io({set_file_info, _F, _FI, _O}, B) ->
    B;
binary_io({ensure_path, Dir}, _B) ->
    {Dir, <<>>};
binary_io({delay, Fun}, B) ->
    % Nothing is delayed in memory.
    Fun(B);
binary_io(flush, FN) ->
    FN.

% Archives and files on disk; errors are thrown with the call that failed.
file_io({file_info, FN}, _) ->
    case file:read_file_info(FN) of
        {ok, Info} -> Info;
        {error, E} -> throw({FN, {{file, file_info, [FN]}, E}})
    end;
file_io({file_info, FN, Opts}, _) ->
    case file:read_file_info(FN, Opts) of
        {ok, Info} -> Info;
        {error, E} -> throw({FN, {{file, file_info, [FN, Opts]}, E}})
    end;
file_io({open, FN, Opts}, _) ->
    case lists:member(write, Opts) of
        true -> ok = filelib:ensure_dir(FN);
        _ -> ok
    end,
    case file:open(FN, Opts ++ [binary]) of
        {ok, H} -> {H, FN};
        {error, E} -> throw({FN, {{file, open, [FN, Opts ++ [binary]]}, E}})
    end;
file_io({read, N}, {H, FN} = S) ->
    case file:read(H, N) of
        {ok, B} -> {B, S};
        eof -> {eof, S};
        {error, E} -> throw({FN, {{file, read, [H, N]}, E}})
    end;
file_io({pread, Pos, N}, {H, FN} = S) ->
    case file:pread(H, Pos, N) of
        {ok, B} -> {B, S};
        eof -> {eof, S};
        {error, E} -> throw({FN, {{file, pread, [H, Pos, N]}, E}})
    end;
file_io({seek, How, Pos}, {H, FN} = S) ->
    case file:position(H, {How, Pos}) of
        {ok, _NewPos} -> S;
        {error, E} -> throw({FN, {{file, position, [H, {S, Pos}]}, E}})
    end;
file_io({position, How, Pos}, {H, FN} = S) ->
    case file:position(H, {How, Pos}) of
        {ok, NewPos} -> {NewPos, S};
        {error, E} -> throw({FN, {{file, position, [H, {S, Pos}]}, E}})
    end;
file_io({write, Data}, {H, FN} = S) ->
    case file:write(H, Data) of
        ok -> S;
        {error, E} -> throw({FN, {{file, write, [H, Data]}, E}})
    end;
file_io({pwrite, Pos, Data}, {H, FN} = S) ->
    case file:pwrite(H, Pos, Data) of
        ok -> S;
        {error, E} -> throw({FN, {{file, pwrite, [H, Pos, Data]}, E}})
    end;
file_io({close, FN}, {H, FN}) ->
    case file:close(H) of
        ok -> #{name => FN, flush => []};
        {error, Error} -> throw({{FN, {file, close, [H]}, Error}})
    end;
file_io(close, {_H, FN} = S) ->
    file_io({close, FN}, S);
file_io({list_dir, FN}, _S) ->
    case file:list_dir(FN) of
        {ok, Files} -> Files;
        {error, Error} -> throw({FN, {file, list_dir, [FN]}, Error})
    end;
file_io({set_file_info, FN, FI}, S) ->
    case file:write_file_info(FN, FI) of
        ok -> S;
        {error, Error} -> throw({FN, {file, write_file_info, [FN, FI]}, Error})
    end;
file_io({set_file_info, FN, FI, O}, S) ->
    case file:write_file_info(FN, FI, O) of
        ok -> S;
        {error, Error} -> throw({FN, {file, write_file_info, [FN, FI, O]}, Error})
    end;
file_io({ensure_path, Dir}, _S) ->
    case filelib:ensure_path(Dir) of
        ok -> #{name => Dir, flush => []};
        {error, E} -> {Dir, {file, ensure_path, [Dir]}, E}
    end;
file_io({delay, Fun}, #{flush := Flush} = H) ->
    H#{flush := [Fun | Flush]};
file_io(flush, #{name := Name, flush := Flush}) ->
    _ = [F(Name) || F <- Flush],
    Name.
