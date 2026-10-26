%% disk_log's files (plan 21 steps LIB12, LIB52; docs/libraries/library.md): halt, wrap and rotate logs in OTP's formats, so
%% log files are exchangeable with OTP. Writes go through a cache flushed past 64 KiB, by a timer two seconds after
%% the first cached write, and before any read, position or close. Errors are thrown as {error, Reason}. items/1 is a
%% runtime builtin that reads the items of a halt log's bytes for ets's table files.
-module(clause_disk_log).
-export([items/1]).
-export([int_open/4, ext_open/4, logl/1, close/3, truncate/3, chunk/5, sync/2, write_cache/2]).
-export([mf_int_open/7, mf_int_log/3, mf_int_close/2, mf_int_inc/2, mf_ext_inc/2, mf_int_chunk/4, mf_int_chunk_step/3]).
-export([mf_sync/1, mf_write_cache/1, mf_ext_open/7, mf_ext_log/3, mf_ext_close/2]).
-export([open_rotate_log_file/3, do_rotate/2, rotate_ext_log/3, rotate_write_cache/1, rotate_ext_close/1]).
-export([change_size_rotate/2, get_rotate_size/1, rotate_files/2]).
-export([read_index_file/1, read_size_file_version/1, chunk_read_only/5, mf_int_chunk_read_only/4]).
-export([change_size_wrap/3, get_wrap_size/1, is_head/1, position/3, truncate_at/3, fwrite/4, fclose/2]).
-export([set_quiet/1, remove_files/4]).
%% The file layer's own atoms (reasons, tags) never name a called module.
-compile({clause_liveness, strict}).

-include("clause_disk_log.hrl").

%% items(Bytes): {opened | closed, [Binary], BadBytes} of a halt log's bytes, or not_a_log_file.
items(_) -> clause_builtin_placeholder.

% --- Items ---

% The bytes of items and their size: <<Size:32, BIGMAGIC:32, (MD5 of the size for large items), Bytes/binary>>.
logl(Bins) -> logl(Bins, [], 0).

logl([X | T], Bs, Size) ->
    Sz = byte_size(X),
    logl(T, [Bs | item(X, Sz)], Size + ?HEADERSZ + Sz);
logl([], Bs, Size) ->
    {Bs, Size}.

item(X, Sz) when Sz < ?MIN_MD5_TERM ->
    [<<Sz:?SIZESZ/unit:8>>, ?BIGMAGICHEAD | X];
item(X, Sz) ->
    BSz = <<Sz:?SIZESZ/unit:8>>,
    [BSz, ?BIGMAGICHEAD, erlang:md5(BSz) | X].

log(FdC, FileName, X) ->
    {Bs, Size} = logl(X),
    case fwrite(FdC, FileName, Bs, Size) of
        {ok, NewFdC} -> {ok, Size, NewFdC};
        Error -> Error
    end.

write_cache(#cache{fd = Fd, c = C}, FName) ->
    erase(write_cache_timer_is_running),
    write_cache(Fd, FName, C).

sync(FdC, FName) -> fsync(FdC, FName).

% Cut a halt log after its file head and write the head item again.
truncate(FdC, FileName, Head) ->
    case truncate_at(FdC, FileName, ?HEADSZ) of
        {ok, _} = Reply when Head =:= none ->
            Reply;
        {ok, FdC1} ->
            {ok, B} = Head,
            case log(FdC1, FileName, [B]) of
                {ok, _NoBytes, NewFdC} -> {ok, NewFdC};
                Reply2 -> Reply2
            end;
        Reply ->
            Reply
    end.

% --- Chunks ---

% {NewFdC, {Cont, Binaries} | {error, Reason} | eof}: up to N items from Pos, or what B (bytes read ahead, or a
% count of bytes to read) still holds.
chunk(FdC, FileName, Pos, B, N) when is_binary(B) ->
    true = byte_size(B) >= ?HEADERSZ,
    handle_chunk_reply(FdC, FileName, Pos, B, N);
chunk(FdC, FileName, Pos, NoBytes, N) ->
    case read_chunk(FdC, FileName, Pos, read_size(NoBytes)) of
        {NewFdC, {ok, Bin}} when byte_size(Bin) < ?HEADERSZ ->
            {NewFdC, {error, {corrupt_log_file, FileName}}};
        {NewFdC, {ok, Bin}} when NoBytes =:= []; byte_size(Bin) >= NoBytes ->
            handle_chunk_reply(NewFdC, FileName, Pos + byte_size(Bin), Bin, N);
        {NewFdC, {ok, _Bin}} ->
            {NewFdC, {error, {corrupt_log_file, FileName}}};
        {NewFdC, eof} when is_integer(NoBytes) ->
            {NewFdC, {error, {corrupt_log_file, FileName}}};
        Other ->
            Other
    end.

read_size([]) -> ?MAX_CHUNK_SIZE;
read_size(NoBytes) -> max(NoBytes, ?MAX_CHUNK_SIZE).

handle_chunk_reply(FdC, FileName, Pos, B, N) ->
    case handle_chunk(B, Pos, N, []) of
        corrupt -> {FdC, {error, {corrupt_log_file, FileName}}};
        {C, []} -> chunk(FdC, FileName, C#continuation.pos, C#continuation.b, N);
        C_Ack -> {FdC, C_Ack}
    end.

handle_chunk(B, Pos, 0, Ack) when byte_size(B) >= ?HEADERSZ ->
    {#continuation{pos = Pos, b = B}, Ack};
handle_chunk(<<Size:?SIZESZ/unit:8, ?BIGMAGICINT:?MAGICSZ/unit:8, Tail/binary>> = B, Pos, N, Ack) when
    Size < ?MIN_MD5_TERM
->
    case Tail of
        <<BinTerm:Size/binary, Tail2/binary>> -> handle_chunk(Tail2, Pos, N - 1, [BinTerm | Ack]);
        _ -> {#continuation{pos = Pos - byte_size(B), b = Size + ?HEADERSZ}, Ack}
    end;
handle_chunk(<<Size:?SIZESZ/unit:8, ?BIGMAGICINT:?MAGICSZ/unit:8, Tail/binary>> = B, Pos, _N, Ack) ->
    MD5 = erlang:md5(<<Size:?SIZESZ/unit:8>>),
    case Tail of
        % The item is always bigger than a chunk.
        <<MD5:16/binary, Bin:Size/binary>> -> {#continuation{pos = Pos, b = []}, [Bin | Ack]};
        <<MD5:16/binary, _/binary>> -> {#continuation{pos = Pos - byte_size(B), b = Size + ?HEADERSZ + 16}, Ack};
        _ when byte_size(Tail) >= 16 -> corrupt;
        _ -> {#continuation{pos = Pos - byte_size(B), b = []}, Ack}
    end;
handle_chunk(<<Size:?SIZESZ/unit:8, ?MAGICINT:?MAGICSZ/unit:8, Tail/binary>> = B, Pos, N, Ack) ->
    % The format before large-item MD5s.
    case Tail of
        <<BinTerm:Size/binary, Tail2/binary>> -> handle_chunk(Tail2, Pos, N - 1, [BinTerm | Ack]);
        _ -> {#continuation{pos = Pos - byte_size(B), b = Size + ?HEADERSZ}, Ack}
    end;
handle_chunk(B, _Pos, _N, _Ack) when byte_size(B) >= ?HEADERSZ ->
    corrupt;
handle_chunk(B, Pos, _N, Ack) ->
    {#continuation{pos = Pos - byte_size(B), b = []}, Ack}.

read_chunk(FdC, FileName, Pos, MaxBytes) ->
    {FdC1, R} = pread(FdC, FileName, Pos + ?HEADSZ, MaxBytes),
    case position(FdC1, FileName, eof) of
        {ok, NewFdC, _Pos} -> {NewFdC, R};
        {Error, NewFdC} -> {NewFdC, Error}
    end.

% Read-only chunks skip bytes that frame no item and count them: {Cont, Binaries, BadBytes}. wrap_log_reader passes
% a plain file descriptor and gets only the reply.
chunk_read_only(#cache{} = FdC, FileName, Pos, B, N) ->
    do_chunk_read_only(FdC, FileName, Pos, B, N);
chunk_read_only(Fd, FileName, Pos, B, N) ->
    {_NFdC, Reply} = do_chunk_read_only(#cache{fd = Fd}, FileName, Pos, B, N),
    Reply.

do_chunk_read_only(FdC, FileName, Pos, B, N) when is_binary(B) ->
    true = byte_size(B) >= ?HEADERSZ,
    handle_chunk_ro_reply(FdC, FileName, Pos, B, N);
do_chunk_read_only(FdC, FileName, Pos, NoBytes, N) ->
    case pread(FdC, FileName, Pos + ?HEADSZ, read_size(NoBytes)) of
        {NewFdC, {ok, Bin}} when byte_size(Bin) < ?HEADERSZ ->
            {NewFdC, {#continuation{pos = Pos + byte_size(Bin), b = []}, [], byte_size(Bin)}};
        {NewFdC, {ok, Bin}} when NoBytes =:= []; byte_size(Bin) >= NoBytes ->
            handle_chunk_ro_reply(NewFdC, FileName, Pos + byte_size(Bin), Bin, N);
        {NewFdC, {ok, Bin}} ->
            {NewFdC, {#continuation{pos = Pos + byte_size(Bin), b = []}, [], byte_size(Bin) - ?HEADERSZ}};
        {NewFdC, eof} when is_integer(NoBytes) ->
            {NewFdC, eof};
        Other ->
            Other
    end.

handle_chunk_ro_reply(FdC, FileName, Pos, B, N) ->
    case handle_chunk_ro(B, Pos, N, [], 0) of
        {#continuation{pos = NewPos, b = NoBytes}, [], 0} -> do_chunk_read_only(FdC, FileName, NewPos, NoBytes, N);
        C_Ack_Bad -> {FdC, C_Ack_Bad}
    end.

handle_chunk_ro(B, Pos, 0, Ack, Bad) when byte_size(B) >= ?HEADERSZ ->
    {#continuation{pos = Pos, b = B}, Ack, Bad};
handle_chunk_ro(<<Size:?SIZESZ/unit:8, ?BIGMAGICINT:?MAGICSZ/unit:8, Tail/binary>> = B, Pos, N, Ack, Bad) when
    Size < ?MIN_MD5_TERM
->
    case Tail of
        <<BinTerm:Size/binary, Tail2/binary>> -> handle_chunk_ro(Tail2, Pos, N - 1, [BinTerm | Ack], Bad);
        _ -> {#continuation{pos = Pos - byte_size(B), b = Size + ?HEADERSZ}, Ack, Bad}
    end;
handle_chunk_ro(<<Size:?SIZESZ/unit:8, ?BIGMAGICINT:?MAGICSZ/unit:8, Tail/binary>> = B, Pos, N, Ack, Bad) ->
    MD5 = erlang:md5(<<Size:?SIZESZ/unit:8>>),
    case Tail of
        <<MD5:16/binary, Bin:Size/binary>> ->
            {#continuation{pos = Pos, b = []}, [Bin | Ack], Bad};
        <<MD5:16/binary, _/binary>> ->
            {#continuation{pos = Pos - byte_size(B), b = Size + ?HEADERSZ + 16}, Ack, Bad};
        <<_BadMD5:16/binary, _:1/unit:8, Tail2/binary>> ->
            handle_chunk_ro(Tail2, Pos, N - 1, Ack, Bad + 1);
        _ ->
            {#continuation{pos = Pos - byte_size(B), b = []}, Ack, Bad}
    end;
handle_chunk_ro(<<Size:?SIZESZ/unit:8, ?MAGICINT:?MAGICSZ/unit:8, Tail/binary>> = B, Pos, N, Ack, Bad) ->
    case Tail of
        <<BinTerm:Size/binary, Tail2/binary>> -> handle_chunk_ro(Tail2, Pos, N - 1, [BinTerm | Ack], Bad);
        _ -> {#continuation{pos = Pos - byte_size(B), b = Size + ?HEADERSZ}, Ack, Bad}
    end;
handle_chunk_ro(B, Pos, N, Ack, Bad) when byte_size(B) >= ?HEADERSZ ->
    <<_:1/unit:8, B2/binary>> = B,
    handle_chunk_ro(B2, Pos, N - 1, Ack, Bad + 1);
handle_chunk_ro(B, Pos, _N, Ack, Bad) ->
    {#continuation{pos = Pos - byte_size(B), b = []}, Ack, Bad}.

% --- Halt logs ---

% Closing a written log marks its head CLOSED.
close(#cache{fd = Fd, c = []}, FileName, read_only) ->
    closed_or_error(file:close(Fd), FileName);
close(#cache{fd = Fd, c = C}, FileName, read_write) ->
    {Reply, _NewFdC} = write_cache(Fd, FileName, C),
    mark(Fd, FileName, ?CLOSED),
    closed_or_error(file:close(Fd), FileName),
    case Reply of
        ok -> ok;
        _ -> throw(Reply)
    end.

closed_or_error(ok, _) -> ok;
closed_or_error(Error, FileName) -> file_error(FileName, Error).

% Open an internal halt log: {ok, {new | existed, FdC, {HeadItems, HeadBytes}, FileSize}}, or {repaired, FdC, Terms,
% BadBytes, FileSize} after a repair of a log not closed. Head is ignored in read_only mode.
int_open(FName, truncate, read_write, Head) ->
    new_int_file(FName, Head);
int_open(FName, Repair, read_write, Head) ->
    case open_read(FName) of
        {ok, Fd} -> existing_int_file(Fd, FName, Repair);
        _Other -> new_int_file(FName, Head)
    end;
int_open(FName, _Repair, read_only, _Head) ->
    case open_read(FName) of
        {ok, Fd} ->
            case file:read(Fd, ?HEADSZ) of
                {ok, Head} -> read_only_int_file(is_head(Head), Fd, FName);
                eof -> not_a_log_file(Fd, FName);
                Error -> file_error_close(Fd, FName, Error)
            end;
        Error ->
            file_error(FName, Error)
    end.

existing_int_file(Fd, FName, Repair) ->
    case file:read(Fd, ?HEADSZ) of
        {ok, FileHead} ->
            case is_head(FileHead) of
                yes -> reopened_int_file(Fd, FName);
                yes_not_closed when Repair -> repair(Fd, FName);
                yes_not_closed -> need_repair(Fd, FName);
                no -> not_a_log_file(Fd, FName)
            end;
        eof ->
            not_a_log_file(Fd, FName);
        Error ->
            file_error_close(Fd, FName, Error)
    end.

reopened_int_file(Fd, FName) ->
    closed_or_error(file:close(Fd), FName),
    case open_update(FName) of
        {ok, Fd2} ->
            mark(Fd2, FName, ?OPENED),
            {FdC, P} = position_close(#cache{fd = Fd2}, FName, eof),
            {ok, {existed, FdC, {0, 0}, P}};
        Error ->
            file_error(FName, Error)
    end.

read_only_int_file(no, Fd, FName) ->
    not_a_log_file(Fd, FName);
read_only_int_file(_, Fd, FName) ->
    {ok, P} = position_close2(Fd, FName, eof),
    {ok, {existed, #cache{fd = Fd}, {0, 0}, P}}.

need_repair(Fd, FName) ->
    _ = file:close(Fd),
    throw({error, {need_repair, FName}}).

not_a_log_file(Fd, FName) ->
    _ = file:close(Fd),
    throw({error, {not_a_log_file, FName}}).

new_int_file(FName, Head) ->
    case open_update(FName) of
        {ok, Fd} ->
            ok = truncate_at_close2(Fd, FName, bof),
            fwrite_close2(Fd, FName, [?LOGMAGIC, ?OPENED]),
            {FdC1, Nh, HeadSz} = int_log_head(Fd, Head),
            {FdC, FileSize} = position_close(FdC1, FName, cur),
            {ok, {new, FdC, {Nh, ?HEADERSZ + HeadSz}, FileSize}};
        Error ->
            file_error(FName, Error)
    end.

int_log_head(Fd, Head) ->
    case lh(Head, internal) of
        {ok, BinHead} ->
            {Bs, Size} = logl([BinHead]),
            {ok, FdC} = fwrite_header(Fd, Bs, Size),
            {FdC, 1, Size};
        none ->
            {#cache{fd = Fd}, 0, 0};
        Error ->
            _ = file:close(Fd),
            throw(Error)
    end.

% Open an external halt log: {ok, {new | existed, FdC, {HeadItems, HeadBytes}, FileSize}}.
ext_open(FName, truncate, read_write, Head) ->
    new_ext_file(FName, Head);
ext_open(FName, _Repair, read_write, Head) ->
    case file:read_file_info(FName) of
        {ok, _FileInfo} ->
            case open_update(FName) of
                {ok, Fd} ->
                    {ok, P} = position_close2(Fd, FName, eof),
                    {ok, {existed, #cache{fd = Fd}, {0, 0}, P}};
                Error ->
                    file_error(FName, Error)
            end;
        _Other ->
            new_ext_file(FName, Head)
    end;
ext_open(FName, _Repair, read_only, _Head) ->
    case open_read(FName) of
        {ok, Fd} ->
            {ok, P} = position_close2(Fd, FName, eof),
            {ok, {existed, #cache{fd = Fd}, {0, 0}, P}};
        Error ->
            file_error(FName, Error)
    end.

new_ext_file(FName, Head) ->
    case open_truncate(FName) of
        {ok, Fd} ->
            {FdC1, HeadSize} = ext_log_head(Fd, Head),
            {FdC, FileSize} = position_close(FdC1, FName, cur),
            {ok, {new, FdC, HeadSize, FileSize}};
        Error ->
            file_error(FName, Error)
    end.

ext_log_head(Fd, Head) ->
    case lh(Head, external) of
        {ok, BinHead} ->
            Size = byte_size(BinHead),
            {ok, FdC} = fwrite_header(Fd, BinHead, Size),
            {FdC, {1, Size}};
        none ->
            {#cache{fd = Fd}, {0, 0}};
        Error ->
            _ = file:close(Fd),
            throw(Error)
    end.

mark(Fd, FileName, What) ->
    {ok, _} = position_close2(Fd, FileName, 4),
    fwrite_close2(Fd, FileName, What).

% The head item: {ok, Binary}, none, or the error of a head function that does not return {ok, Head}.
lh({ok, Bin}, _Format) ->
    {ok, Bin};
lh({M, F, A}, Format) when is_list(A) ->
    case catch apply(M, F, A) of
        {ok, Head} when Format =:= internal -> {ok, term_to_binary(Head)};
        {ok, Bin} when is_binary(Bin) -> {ok, Bin};
        {ok, Bytes} -> head_bytes({M, F, A}, Bytes);
        {'EXIT', Error} -> {error, {invalid_header, {{M, F, A}, Error}}};
        Error -> {error, {invalid_header, {{M, F, A}, Error}}}
    end;
lh({M, F, A}, _Format) ->
    {error, {invalid_header, {M, F, A}}};
lh(none, _Format) ->
    none;
lh(H, _F) ->
    {error, {invalid_header, H}}.

head_bytes(MFA, Bytes) ->
    try
        {ok, list_to_binary(Bytes)}
    catch
        _:_ -> {error, {invalid_header, {MFA, {ok, Bytes}}}}
    end.

% Repair a log not closed: copy the whole items into a new file, count the bytes that frame none, and replace it.
repair(In, File) ->
    FSz = file_size(File),
    case is_quiet() of
        true -> ok;
        _ -> error_logger:info_msg("disk_log: repairing ~tp ...\n", [File])
    end,
    Tmp = add_ext(File, "TMP"),
    {ok, {_Alloc, Out, {0, _}, _FileSize}} = new_int_file(Tmp, none),
    scan_f_read(<<>>, In, Out, {File, FSz, Tmp}, ?MAX_CHUNK_SIZE, 0, 0).

scan_f_read(B, In, Out, {File, FSz, Tmp} = Files, MaxBytes, No, Bad) ->
    case file:read(In, MaxBytes) of
        eof ->
            done_scan(In, Out, Tmp, File, No, Bad + byte_size(B));
        {ok, Bin} ->
            {NB, NMax, Ack, NNo, NBad} = scan_f(list_to_binary([B, Bin]), FSz, [], No, Bad),
            case log(Out, Tmp, lists:reverse(Ack)) of
                {ok, _Size, NewOut} -> scan_f_read(NB, In, NewOut, Files, NMax, NNo, NBad);
                {{error, {file_error, _Filename, Error}}, NewOut} -> repair_err(In, NewOut, Tmp, File, {error, Error})
            end;
        Error ->
            repair_err(In, Out, Tmp, File, Error)
    end.

scan_f(<<Size:?SIZESZ/unit:8, ?BIGMAGICINT:?MAGICSZ/unit:8, Tail/binary>> = B, FSz, Ack, No, Bad) when
    Size < ?MIN_MD5_TERM
->
    scan_f2(B, FSz, Ack, No, Bad, Size, Tail);
scan_f(<<Size:?SIZESZ/unit:8, ?BIGMAGICINT:?MAGICSZ/unit:8, Tail/binary>> = B, FSz, Ack, No, Bad) ->
    MD5 = erlang:md5(<<Size:?SIZESZ/unit:8>>),
    case Tail of
        <<MD5:16/binary, BinTerm:Size/binary, Tail2/binary>> ->
            case is_term(BinTerm) of
                false -> scan_f(Tail2, FSz, Ack, No, Bad + Size);
                true -> scan_f(Tail2, FSz, [BinTerm | Ack], No + 1, Bad)
            end;
        <<MD5:16/binary, _/binary>> ->
            {B, Size - byte_size(Tail) + 16, Ack, No, Bad};
        _ when byte_size(Tail) < 16 ->
            {B, Size - byte_size(Tail) + 16, Ack, No, Bad};
        _ ->
            <<_:8, B2/binary>> = B,
            scan_f(B2, FSz, Ack, No, Bad + 1)
    end;
scan_f(<<Size:?SIZESZ/unit:8, ?MAGICINT:?MAGICSZ/unit:8, Tail/binary>> = B, FSz, Ack, No, Bad) when Size =< FSz ->
    % An item of an uncompressed file is never bigger than the file.
    scan_f2(B, FSz, Ack, No, Bad, Size, Tail);
scan_f(<<_:?HEADERSZ/unit:8, _/binary>> = B, FSz, Ack, No, Bad) ->
    <<_:8, B2/binary>> = B,
    scan_f(B2, FSz, Ack, No, Bad + 1);
scan_f(B, _FSz, Ack, No, Bad) ->
    {B, ?MAX_CHUNK_SIZE, Ack, No, Bad}.

scan_f2(B, FSz, Ack, No, Bad, Size, Tail) ->
    case Tail of
        <<BinTerm:Size/binary, Tail2/binary>> ->
            case is_term(BinTerm) of
                false ->
                    <<_:8, B2/binary>> = B,
                    scan_f(B2, FSz, Ack, No, Bad + 1);
                true ->
                    scan_f(Tail2, FSz, [BinTerm | Ack], No + 1, Bad)
            end;
        _ ->
            {B, Size - byte_size(Tail), Ack, No, Bad}
    end.

is_term(Bin) ->
    try binary_to_term(Bin) of
        _ -> true
    catch
        _:_ -> false
    end.

done_scan(In, Out, OutName, FName, RecoveredTerms, BadChars) ->
    _ = file:close(In),
    case catch fclose(Out, OutName) of
        ok ->
            case file:rename(OutName, FName) of
                ok -> repaired_file(FName, RecoveredTerms, BadChars);
                Error -> remove_and_fail(OutName, FName, Error)
            end;
        Error ->
            _ = file:delete(OutName),
            throw(Error)
    end.

repaired_file(FName, RecoveredTerms, BadChars) ->
    case open_update(FName) of
        {ok, New} ->
            {ok, P} = position_close2(New, FName, eof),
            {repaired, #cache{fd = New}, RecoveredTerms, BadChars, P};
        Error ->
            file_error(FName, Error)
    end.

remove_and_fail(OutName, FName, Error) ->
    _ = file:delete(OutName),
    file_error(FName, Error).

repair_err(In, Out, OutName, ErrFileName, Error) ->
    _ = file:close(In),
    _ = (catch fclose(Out, OutName)),
    % The new file is often the culprit; remove it anyway.
    _ = file:delete(OutName),
    file_error(ErrFileName, Error).

% Whether 8 bytes are a log file head, and whether it was closed.
is_head(<<M:4/binary, S:4/binary>>) when ?LOGMAGIC =:= M, ?CLOSED =:= S -> yes;
is_head(<<M:4/binary, S:4/binary>>) when ?LOGMAGIC =:= M, ?OPENED =:= S -> yes_not_closed;
is_head(Bin) when is_binary(Bin) -> no.

% --- Wrap logs ---

% Open an internal wrap log on files FName.1 .. FName.MaxF, the current one from the index file.
mf_int_open(FName, MaxB, MaxF, Repair, Mode, Head, Version) ->
    {First, Sz, TotSz, NFiles} = read_index_file(Repair, FName, MaxF),
    write_size_file(Mode, FName, MaxB, MaxF, Version),
    NewMaxF = shrinking(NFiles, MaxF),
    case int_file_open(FName, First, 0, 0, Head, Repair, Mode) of
        {ok, FdC, FileName, Lost, {NoItems, NoBytes}, FSz} ->
            % firstPos = NoBytes is not exact for an existing file, whose header end is unknown.
            CurCnt = Sz + NoItems - Lost,
            Handle = #handle{
                filename = FName,
                maxB = MaxB,
                maxF = NewMaxF,
                curF = First,
                cur_fdc = FdC,
                cur_name = FileName,
                cur_cnt = CurCnt,
                acc_cnt = -Sz,
                curB = FSz,
                firstPos = NoBytes,
                noFull = 0,
                accFull = 0
            },
            {ok, Handle, TotSz + CurCnt};
        {repaired, FdC, FileName, Rec, Bad, FSz} ->
            Handle = #handle{
                filename = FName,
                maxB = MaxB,
                cur_name = FileName,
                maxF = NewMaxF,
                curF = First,
                cur_fdc = FdC,
                cur_cnt = Rec,
                acc_cnt = -Rec,
                curB = FSz,
                firstPos = 0,
                noFull = 0,
                accFull = 0
            },
            {repaired, Handle, Rec, Bad, TotSz + Rec}
    end.

shrinking(NFiles, MaxF) when NFiles > MaxF -> {MaxF, NFiles};
shrinking(_, MaxF) -> MaxF.

% Move to the next file: {ok, Handle, Lost} or {error, Error, Handle}.
mf_int_inc(Handle, Head) ->
    mf_inc(Handle, Head, fun wrap_int_log/5, fun(FdC, Name) -> close(FdC, Name, read_write) end).

mf_ext_inc(Handle, Head) ->
    mf_inc(Handle, Head, fun wrap_ext_log/5, fun fclose/2).

mf_inc(Handle, Head, Wrap, Close) ->
    #handle{
        filename = FName,
        cur_cnt = CurCnt,
        acc_cnt = AccCnt,
        cur_name = FileName,
        curF = CurF,
        maxF = MaxF,
        cur_fdc = CurFdC,
        noFull = NoFull
    } = Handle,
    case catch Wrap(FName, CurF, MaxF, CurCnt, Head) of
        {NewF, NewMaxF, NewFdC, NewFileName, Nh, FirstPos, Lost} ->
            Handle1 = Handle#handle{
                cur_fdc = NewFdC,
                curF = NewF,
                cur_name = NewFileName,
                cur_cnt = Nh,
                acc_cnt = AccCnt + CurCnt,
                maxF = NewMaxF,
                firstPos = FirstPos,
                curB = FirstPos,
                noFull = NoFull + 1
            },
            case catch Close(CurFdC, FileName) of
                ok -> {ok, Handle1, Lost};
                % An error in the last file; the new file is open.
                Error -> {error, Error, Handle1}
            end;
        Error ->
            {error, Error, Handle}
    end.

% Log binaries, moving to the next file whenever one is full: {ok, Handle, Logged} or {ok, Handle, Logged, Lost,
% Wraps}, or {error, Error, Handle, Logged, Lost}.
mf_int_log(Handle, Bins, Head) ->
    mf_log(Handle, Bins, Head, {fun int_split_bins/4, fun wrap_int_log/5, fun(F, N) -> close(F, N, read_write) end}).

mf_ext_log(Handle, Bins, Head) ->
    mf_log(Handle, Bins, Head, {fun ext_split_bins/4, fun wrap_ext_log/5, fun fclose/2}).

mf_log(Handle, Bins, Head, Kind) -> mf_log(Handle, Bins, Head, Kind, 0, []).

mf_log(Handle, [], _Head, _Kind, No, []) ->
    {ok, Handle, No};
mf_log(Handle, [], _Head, _Kind, No, Wraps0) ->
    Wraps = lists:reverse(Wraps0),
    {ok, Handle, No, lists:sum(Wraps), Wraps};
mf_log(Handle, Bins, Head, {Split, _, _} = Kind, No0, Wraps) ->
    #handle{curB = CurB, maxB = MaxB, cur_name = FileName, cur_fdc = CurFdC, firstPos = FirstPos0, cur_cnt = CurCnt} =
        Handle,
    case Split(CurB, MaxB, FirstPos0, Bins) of
        {[], _, _, _} ->
            mf_wrap(Handle, Bins, Head, Kind, No0, Wraps);
        {FirstBins, LastBins, NoBytes, N} ->
            case fwrite(CurFdC, FileName, FirstBins, NoBytes) of
                {ok, NewCurFdC} ->
                    Handle1 = Handle#handle{cur_fdc = NewCurFdC, curB = CurB + NoBytes, cur_cnt = CurCnt + N},
                    mf_log(Handle1, LastBins, Head, Kind, No0 + N, Wraps);
                {Error, NewCurFdC} ->
                    {error, Error, Handle#handle{cur_fdc = NewCurFdC}, No0, lists:sum(Wraps)}
            end
    end.

mf_wrap(Handle, Bins, Head, {_, Wrap, Close} = Kind, No0, Wraps) ->
    #handle{
        filename = FName,
        curF = CurF,
        maxF = MaxF,
        acc_cnt = AccCnt,
        noFull = NoFull,
        cur_cnt = CurCnt,
        cur_name = FileName,
        cur_fdc = CurFdC
    } = Handle,
    case catch Wrap(FName, CurF, MaxF, CurCnt, Head) of
        {NewF, NewMaxF, NewFdC, NewFileName, Nh, FirstPos, Lost} ->
            Handle1 = Handle#handle{
                cur_fdc = NewFdC,
                curF = NewF,
                cur_cnt = Nh,
                cur_name = NewFileName,
                acc_cnt = AccCnt + CurCnt,
                maxF = NewMaxF,
                curB = FirstPos,
                firstPos = FirstPos,
                noFull = NoFull + 1
            },
            case catch Close(CurFdC, FileName) of
                ok -> mf_log(Handle1, Bins, Head, Kind, No0 + Nh, [Lost | Wraps]);
                Error -> {error, Error, Handle1, No0 + Nh, Lost + lists:sum(Wraps)}
            end;
        Error ->
            {error, Error, Handle, No0, lists:sum(Wraps)}
    end.

wrap_int_log(FName, CurF, MaxF, CurCnt, Head) ->
    {NewF, NewMaxF} = inc_wrap(FName, CurF, MaxF),
    {ok, NewFdC, NewFileName, Lost, {Nh, FirstPos}, _FileSize} = int_file_open(FName, NewF, CurF, CurCnt, Head),
    {NewF, NewMaxF, NewFdC, NewFileName, Nh, FirstPos, Lost}.

wrap_ext_log(FName, CurF, MaxF, CurCnt, Head) ->
    {NewF, NewMaxF} = inc_wrap(FName, CurF, MaxF),
    {ok, NewFdC, NewFileName, Lost, {Nh, FirstPos}, _FileSize} = ext_file_open(FName, NewF, CurF, CurCnt, Head),
    {NewF, NewMaxF, NewFdC, NewFileName, Nh, FirstPos, Lost}.

% Chunks of a wrap log, from the oldest file on: {NewHandle, Reply}.
mf_int_chunk(Handle, 0, Bin, N) ->
    mf_int_chunk(Handle, {find_first_file(Handle), 0}, Bin, N);
mf_int_chunk(#handle{curF = FileNo, cur_fdc = FdC, cur_name = FileName} = Handle, {FileNo, Pos}, Bin, N) ->
    {NewFdC, Reply} = chunk(FdC, FileName, Pos, Bin, N),
    {Handle#handle{cur_fdc = NewFdC}, conv(Reply, FileNo)};
mf_int_chunk(Handle, {FileNo, Pos}, Bin, N) ->
    other_file_chunk(Handle, {FileNo, Pos}, Bin, N, fun chunk/5, fun mf_int_chunk/4).

mf_int_chunk_read_only(Handle, 0, Bin, N) ->
    mf_int_chunk_read_only(Handle, {find_first_file(Handle), 0}, Bin, N);
mf_int_chunk_read_only(#handle{curF = FileNo, cur_fdc = FdC, cur_name = FileName} = Handle, {FileNo, Pos}, Bin, N) ->
    {NewFdC, Reply} = do_chunk_read_only(FdC, FileName, Pos, Bin, N),
    {Handle#handle{cur_fdc = NewFdC}, conv(Reply, FileNo)};
mf_int_chunk_read_only(Handle, {FileNo, Pos}, Bin, N) ->
    other_file_chunk(Handle, {FileNo, Pos}, Bin, N, fun do_chunk_read_only/5, fun mf_int_chunk_read_only/4).

% A chunk of a file that is not the current one: opened and closed for it; a missing file is skipped (and reported).
other_file_chunk(Handle, {FileNo, Pos}, Bin, N, Chunk, Next) ->
    FName = add_ext(Handle#handle.filename, FileNo),
    NFileNo = inc(FileNo, Handle#handle.maxF),
    case catch int_open(FName, true, read_only, any) of
        {error, _Reason} ->
            case is_quiet() of
                true -> ok;
                _ -> error_logger:info_msg("disk_log: chunk error. File ~tp missing.\n\n", [FName])
            end,
            Next(Handle, {NFileNo, 0}, [], N);
        {ok, {_Alloc, FdC, _HeadSize, _FileSize}} ->
            case Chunk(FdC, FName, Pos, Bin, N) of
                {NewFdC, eof} ->
                    _ = file:close(NewFdC#cache.fd),
                    Next(Handle, {NFileNo, 0}, [], N);
                {NewFdC, Other} ->
                    _ = file:close(NewFdC#cache.fd),
                    {Handle, conv(Other, FileNo)}
            end
    end.

mf_int_chunk_step(Handle, 0, Step) ->
    mf_int_chunk_step(Handle, {find_first_file(Handle), 0}, Step);
mf_int_chunk_step(Handle, {FileNo, _Pos}, Step) ->
    NFileNo = inc(FileNo, Handle#handle.maxF, Step),
    case file:read_file_info(add_ext(Handle#handle.filename, NFileNo)) of
        {ok, _FileInfo} -> {ok, #continuation{pos = {NFileNo, 0}, b = []}};
        _Error -> {error, end_of_log}
    end.

mf_write_cache(#handle{filename = FName, cur_fdc = #cache{fd = Fd, c = C}} = Handle) ->
    erase(write_cache_timer_is_running),
    {Reply, NewFdC} = write_cache(Fd, FName, C),
    {Reply, Handle#handle{cur_fdc = NewFdC}}.

mf_sync(#handle{filename = FName, cur_fdc = FdC} = Handle) ->
    {Reply, NewFdC} = fsync(FdC, FName),
    {Reply, Handle#handle{cur_fdc = NewFdC}};
mf_sync(#rotate_handle{file = FName, cur_fdc = FdC} = Handle) ->
    {Reply, NewFdC} = fsync(FdC, FName),
    {Reply, Handle#rotate_handle{cur_fdc = NewFdC}}.

mf_int_close(#handle{filename = FName, curF = CurF, cur_name = FileName, cur_fdc = CurFdC, cur_cnt = CurCnt}, Mode) ->
    close(CurFdC, FileName, Mode),
    write_index_file(Mode, FName, CurF, CurF, CurCnt),
    ok.

mf_ext_open(FName, MaxB, MaxF, Repair, Mode, Head, Version) ->
    {First, Sz, TotSz, NFiles} = read_index_file(Repair, FName, MaxF),
    write_size_file(Mode, FName, MaxB, MaxF, Version),
    NewMaxF = shrinking(NFiles, MaxF),
    {ok, FdC, FileName, Lost, {NoItems, NoBytes}, CurB} = ext_file_open(FName, First, 0, 0, Head, Repair, Mode),
    CurCnt = Sz + NoItems - Lost,
    Handle = #handle{
        filename = FName,
        maxB = MaxB,
        cur_name = FileName,
        maxF = NewMaxF,
        cur_cnt = CurCnt,
        acc_cnt = -Sz,
        curF = First,
        cur_fdc = FdC,
        firstPos = NoBytes,
        curB = CurB,
        noFull = 0,
        accFull = 0
    },
    {ok, Handle, TotSz + CurCnt}.

mf_ext_close(#handle{filename = FName, curF = CurF, cur_fdc = CurFdC, cur_cnt = CurCnt}, Mode) ->
    Res = (catch fclose(CurFdC, FName)),
    write_index_file(Mode, FName, CurF, CurF, CurCnt),
    Res.

% A new size; with fewer files the extra ones are removed once the log wraps past the new count.
change_size_wrap(#handle{filename = FName} = Handle, {NewMaxB, NewMaxF}, Version) ->
    {_MaxB, MaxF} = get_wrap_size(Handle),
    BiggerMaxF = max(MaxF, old_max_files(Handle#handle.maxF)),
    write_size_file(read_write, FName, NewMaxB, NewMaxF, Version),
    if
        NewMaxF > BiggerMaxF ->
            remove_files(wrap, FName, MaxF + 1, NewMaxF),
            {ok, Handle#handle{maxB = NewMaxB, maxF = NewMaxF}};
        NewMaxF < BiggerMaxF ->
            {ok, Handle#handle{maxB = NewMaxB, maxF = {NewMaxF, BiggerMaxF}}};
        true ->
            {ok, Handle#handle{maxB = NewMaxB, maxF = NewMaxF}}
    end.

% --- Rotate logs ---

change_size_rotate(#rotate_handle{maxB = MaxB, maxF = MaxF, file = FName} = Handle, {NewMaxB, NewMaxF}) ->
    {MaxB, MaxF1} = get_size(MaxB, MaxF),
    if
        NewMaxF > MaxF1 -> remove_files(rotate, FName, MaxF1, NewMaxF);
        NewMaxF < MaxF1 -> remove_files(rotate, FName, NewMaxF, MaxF1);
        true -> ok
    end,
    {ok, Handle#rotate_handle{maxB = NewMaxB, maxF = NewMaxF}}.

open_rotate_log_file(FileName, Size, Head) ->
    try
        case filelib:ensure_dir(FileName) of
            ok ->
                case file:open(FileName, [raw, binary, read, append]) of
                    {ok, Fd} -> opened_rotate_log(Fd, FileName, Size, Head);
                    Error -> Error
                end;
            Error ->
                Error
        end
    catch
        _:Reason -> {error, Reason}
    end.

opened_rotate_log(Fd, FileName, {MaxB, MaxF}, Head) ->
    {FdC1, _HeadSize} = ext_log_head(Fd, Head),
    {FdC, FileSize} = position_close(FdC1, FileName, cur),
    {ok, #file_info{inode = INode}} = file:read_file_info(FileName, [raw]),
    RotHandle = #rotate_handle{
        file = FileName,
        cur_fdc = FdC,
        maxB = MaxB,
        maxF = MaxF,
        curB = FileSize,
        firstPos = FileSize,
        inode = INode
    },
    maybe_remove_archives(MaxF, FileName),
    maybe_update_compress(0, MaxF, FileName),
    {ok, RotHandle}.

maybe_remove_archives(Count, FName) ->
    Archive = rotate_file_name(FName, Count),
    case file:read_file_info(Archive, [raw]) of
        {error, enoent} ->
            ok;
        _ ->
            _ = file:delete(Archive),
            maybe_remove_archives(Count + 1, FName)
    end.

maybe_update_compress(MaxF, MaxF, _FName) ->
    ok;
maybe_update_compress(N, MaxF, FName) ->
    FileName = add_ext(FName, N),
    case file:read_file_info(FileName, [raw]) of
        {ok, _} -> compress_file(FileName);
        _ -> ok
    end,
    maybe_update_compress(N + 1, MaxF, FName).

do_rotate(#rotate_handle{file = FName, maxF = MaxF, cur_fdc = #cache{fd = Fd, c = C}} = RotHandle, Head) ->
    {_, _C1} = write_cache(Fd, FName, C),
    _ = delayed_write_close(Fd),
    rotate_files(FName, MaxF),
    {ok, NewFdC, FileSize} = ensure_open(FName, Head),
    {ok, #file_info{inode = INode}} = file:read_file_info(FName, [raw]),
    RotHandle#rotate_handle{cur_fdc = NewFdC, inode = INode, curB = FileSize, firstPos = FileSize}.

rotate_files(FileName, 0) ->
    _ = file:delete(FileName),
    ok;
rotate_files(FileName, 1) ->
    FileName0 = FileName ++ ".0",
    % The file may be gone; then there is nothing to compress.
    case file:rename(FileName, FileName0) of
        ok -> compress_file(FileName0);
        _ -> ok
    end,
    ok;
rotate_files(FileName, Count) ->
    _ = file:rename(rotate_file_name(FileName, Count - 2), rotate_file_name(FileName, Count - 1)),
    rotate_files(FileName, Count - 1).

rotate_file_name(FileName, Count) -> lists:concat([FileName, ".", Count, ".gz"]).

% Gzip a file into File.gz in 100000-byte reads, then remove it.
compress_file(FileName) ->
    {ok, In} = file:open(FileName, [read, binary]),
    {ok, Out} = file:open(FileName ++ ".gz", [write]),
    Z = zlib:open(),
    ok = zlib:deflateInit(Z, default, deflated, 31, 8, default),
    compress_data(Z, In, Out),
    ok = zlib:deflateEnd(Z),
    zlib:close(Z),
    _ = file:close(In),
    _ = file:close(Out),
    _ = file:delete(FileName),
    ok.

compress_data(Z, In, Out) ->
    case file:read(In, 100000) of
        {ok, Data} ->
            _ = file:write(Out, zlib:deflate(Z, Data)),
            compress_data(Z, In, Out);
        eof ->
            _ = file:write(Out, zlib:deflate(Z, <<>>, finish)),
            ok
    end.

rotate_ext_log(Handle, Bin, Head) -> rotate_ext_log(Handle, Bin, Head, 0).

rotate_ext_log(Handle, [], _Head, N) ->
    {ok, Handle, N};
rotate_ext_log(Handle, Bins, Head, N0) ->
    #rotate_handle{file = FileName, maxB = MaxB, cur_fdc = CurFdC, curB = CurB, firstPos = FirstPos} = Handle,
    case ext_split_bins(CurB, MaxB, FirstPos, Bins) of
        {[], _, _, _} ->
            rotate_ext_log(do_rotate(Handle, Head), Bins, Head, N0);
        {FirstBins, LastBins, NoBytes, N} ->
            case fwrite(CurFdC, FileName, FirstBins, NoBytes) of
                {ok, NewCurFdC} ->
                    Handle1 = Handle#rotate_handle{cur_fdc = NewCurFdC, curB = CurB + NoBytes},
                    rotate_ext_log(Handle1, LastBins, Head, N0 + N);
                {Error, NewCurFdC} ->
                    {error, Error, Handle#rotate_handle{cur_fdc = NewCurFdC}}
            end
    end.

rotate_write_cache(#rotate_handle{file = FName, cur_fdc = #cache{fd = Fd, c = C}} = Handle) ->
    erase(write_cache_timer_is_running),
    {Reply, NewFdC} = write_cache(Fd, FName, C),
    {Reply, Handle#rotate_handle{cur_fdc = NewFdC}}.

rotate_ext_close(#rotate_handle{file = FName, cur_fdc = CurFdC}) -> (catch fclose(CurFdC, FName)).

ensure_open(Filename, Head) ->
    case filelib:ensure_dir(Filename) of
        ok ->
            case open_update(Filename) of
                {ok, Fd} ->
                    {FdC1, _HeadSize} = ext_log_head(Fd, Head),
                    {FdC, FileSize} = position_close(FdC1, Filename, cur),
                    {ok, FdC, FileSize};
                Error ->
                    exit({could_not_reopen_file, Error})
            end;
        Error ->
            exit({could_not_create_dir_for_file, Error})
    end.

% Close again when the first close reports a failed delayed write.
delayed_write_close(Fd) ->
    case file:close(Fd) of
        {error, _} -> file:close(Fd);
        Res -> Res
    end.

% --- Wrap log files ---

int_file_open(FName, NewFile, OldFile, OldCnt, Head) ->
    int_file_open(FName, NewFile, OldFile, OldCnt, Head, truncate, read_write).

int_file_open(FName, NewFile, OldFile, OldCnt, Head, Repair, Mode) ->
    N = add_ext(FName, NewFile),
    case int_open(N, Repair, Mode, Head) of
        {ok, {_Alloc, FdC, HeadSize, FileSize}} ->
            Lost = write_index_file(Mode, FName, NewFile, OldFile, OldCnt),
            {ok, FdC, N, Lost, HeadSize, FileSize};
        {repaired, FdC, Recovered, BadBytes, FileSize} ->
            write_index_file(Mode, FName, NewFile, OldFile, OldCnt),
            {repaired, FdC, N, Recovered, BadBytes, FileSize}
    end.

ext_file_open(FName, NewFile, OldFile, OldCnt, Head) ->
    ext_file_open(FName, NewFile, OldFile, OldCnt, Head, truncate, read_write).

ext_file_open(FName, NewFile, OldFile, OldCnt, Head, Repair, Mode) ->
    FileName = add_ext(FName, NewFile),
    {ok, {_Alloc, FdC, HeadSize, FileSize}} = ext_open(FileName, Repair, Mode, Head),
    Lost = write_index_file(Mode, FName, NewFile, OldFile, OldCnt),
    {ok, FdC, FileName, Lost, HeadSize, FileSize}.

% The index file FName.idx: <<0, 0:32, Version, CurFileNo:32>> and the item count of each file as 64 bits (version
% 2), or older forms with 32-bit counts. {CurFileNo, CurFileSz, TotSz without it, NoFiles}.
read_index_file(truncate, FName, MaxF) ->
    remove_files(wrap, FName, 2, MaxF),
    _ = file:delete(index_file_name(FName)),
    {1, 0, 0, 0};
read_index_file(_, FName, _MaxF) ->
    read_index_file(FName).

read_index_file(FName) ->
    case open_read(index_file_name(FName)) of
        {ok, Fd} ->
            R =
                case file:read(Fd, ?MAX_CHUNK_SIZE) of
                    {ok, <<0, 0:32, Version, CurF:32, Tail/binary>>} when
                        Version =:= ?VERSION, 0 < CurF, CurF < ?MAX_FILES
                    ->
                        parse_index(CurF, Version, 1, Tail, Fd, {0, 0, 0});
                    {ok, <<0, CurF:32, Tail/binary>>} when 0 < CurF, CurF < ?MAX_FILES ->
                        parse_index(CurF, 1, 1, Tail, Fd, {0, 0, 0});
                    {ok, <<CurF, Tail/binary>>} when 0 < CurF ->
                        parse_index(CurF, 1, 1, Tail, Fd, {0, 0, 0});
                    _ErrorOrEof ->
                        {1, 0, 0, 0}
                end,
            _ = file:close(Fd),
            R;
        _Error ->
            {1, 0, 0, 0}
    end.

% Counts: {CurSz, TotSz, NFiles}.
parse_index(CurF, V, N, B, Fd, {CurSz, TotSz, NFiles}) ->
    Bits = count_bits(V),
    case B of
        <<Sz:Bits, Tail/binary>> when N =:= CurF -> parse_index(CurF, V, N + 1, Tail, Fd, {Sz, TotSz, NFiles + 1});
        <<Sz:Bits, Tail/binary>> -> parse_index(CurF, V, N + 1, Tail, Fd, {CurSz, TotSz + Sz, NFiles + 1});
        _ -> parse_more(CurF, V, N, B, Fd, {CurSz, TotSz, NFiles})
    end.

parse_more(CurF, V, N, B, Fd, {CurSz, TotSz, NFiles} = Counts) ->
    case file:read(Fd, ?MAX_CHUNK_SIZE) of
        eof when 0 =:= byte_size(B) -> {CurF, CurSz, TotSz, NFiles};
        {ok, Bin} -> parse_index(CurF, V, N, list_to_binary([B, Bin]), Fd, Counts);
        _ErrorOrEof -> {1, 0, 0, 0}
    end.

count_bits(?VERSION) -> 64;
count_bits(_) -> 32.

% Record the new current file and the old file's item count; the count of the file now reused is lost.
write_index_file(read_only, _FName, _NewFile, _OldFile, _OldCnt) ->
    0;
write_index_file(read_write, FName, NewFile, OldFile, OldCnt) ->
    FileName = index_file_name(FName),
    case open_update(FileName) of
        {ok, Fd} ->
            {Offset, SzSz} = index_layout(Fd, FileName, NewFile),
            NewPos = Offset + (NewFile - 1) * SzSz,
            OldCntBin = <<OldCnt:SzSz/unit:8>>,
            write_counts(Fd, FileName, {NewPos, OldFile, Offset, SzSz}, OldCntBin);
        E ->
            file_error(FileName, E)
    end.

% The offset of the counts and their bytes, the current file written; an index file of the oldest format is
% converted.
index_layout(Fd, FileName, NewFile) ->
    case file:read(Fd, 6) of
        eof ->
            fwrite_close2(Fd, FileName, <<0, 0:32, ?VERSION, NewFile:32>>),
            {10, 8};
        {ok, <<0, 0:32, _Version>>} ->
            pwrite_close2(Fd, FileName, 6, <<NewFile:32>>),
            {10, 8};
        {ok, <<0, _/binary>>} ->
            pwrite_close2(Fd, FileName, 1, <<NewFile:32>>),
            {5, 4};
        {ok, <<_, _/binary>>} ->
            case file:read_file(FileName) of
                {ok, <<_CurF, Tail/binary>>} ->
                    {ok, _} = position_close2(Fd, FileName, bof),
                    NewTail = to_8_bytes(Tail, [], FileName, Fd),
                    fwrite_close2(Fd, FileName, [<<0, 0:32, ?VERSION, NewFile:32>> | NewTail]),
                    {10, 8};
                Error ->
                    file_error_close(Fd, FileName, Error)
            end;
        Error ->
            file_error_close(Fd, FileName, Error)
    end.

write_counts(Fd, FileName, {NewPos, OldFile, Offset, SzSz}, OldCntBin) when OldFile > 0 ->
    R = file:pread(Fd, NewPos, SzSz),
    pwrite_close2(Fd, FileName, Offset + (OldFile - 1) * SzSz, OldCntBin),
    _ = file:close(Fd),
    case R of
        {ok, <<Lost:SzSz/unit:8>>} -> Lost;
        {ok, _} -> throw({error, {invalid_index_file, FileName}});
        eof -> 0;
        Error2 -> file_error(FileName, Error2)
    end;
write_counts(Fd, FileName, {NewPos, _OldFile, _Offset, _SzSz}, OldCntBin) ->
    pwrite_close2(Fd, FileName, NewPos, OldCntBin),
    _ = file:close(Fd),
    0.

to_8_bytes(<<N:32, T/binary>>, NT, FileName, Fd) ->
    to_8_bytes(T, [NT | <<N:64>>], FileName, Fd);
to_8_bytes(B, NT, _FileName, _Fd) when byte_size(B) =:= 0 ->
    NT;
to_8_bytes(_B, _NT, FileName, Fd) ->
    _ = file:close(Fd),
    throw({error, {invalid_index_file, FileName}}).

index_file_trunc(FName, N) ->
    FileName = index_file_name(FName),
    case open_update(FileName) of
        {ok, Fd} ->
            case file:read(Fd, 6) of
                eof ->
                    _ = file:close(Fd),
                    ok;
                {ok, <<0, 0:32, Version>>} when Version =:= ?VERSION ->
                    truncate_index_file(Fd, FileName, 10, 8, N);
                {ok, <<0, _/binary>>} ->
                    truncate_index_file(Fd, FileName, 5, 4, N);
                {ok, <<_, _/binary>>} ->
                    truncate_index_file(Fd, FileName, 1, 4, N);
                Error ->
                    file_error_close(Fd, FileName, Error)
            end;
        Error ->
            file_error(FileName, Error)
    end.

% OTP passes the item count where the count size goes and the reverse; the same is done here.
truncate_index_file(Fd, FileName, Offset, N, SzSz) ->
    Pos = Offset + N * SzSz,
    case Pos > file_size(FileName) of
        true -> ok;
        false -> truncate_at_close2(Fd, FileName, {bof, Pos})
    end,
    ok = file:close(Fd),
    ok.

% The size file FName.siz: <<Version, MaxBytes:64, MaxFiles:32>> (or <<MaxBytes:32, MaxFiles:32>> of version 1).
write_size_file(read_only, _FName, _NewSize, _NewMaxFiles, _Version) ->
    ok;
write_size_file(read_write, FName, NewSize, NewMaxFiles, Version) ->
    FileName = size_file_name(FName),
    Bin =
        case Version of
            ?VERSION -> <<Version, NewSize:64, NewMaxFiles:32>>;
            _ -> <<NewSize:32, NewMaxFiles:32>>
        end,
    case file:write_file(FileName, Bin) of
        ok -> ok;
        E -> file_error(FileName, E)
    end.

% {{MaxBytes, MaxFiles}, Version}; {{0, 0}, 2} without a size file.
read_size_file_version(FName) ->
    case file:read_file(size_file_name(FName)) of
        {ok, <<Version, Size:64, MaxFiles:32>>} when Version =:= ?VERSION -> {{Size, MaxFiles}, Version};
        {ok, <<Size:32, MaxFiles:32>>} -> {{Size, MaxFiles}, 1};
        _ -> {{0, 0}, ?VERSION}
    end.

conv({#continuation{} = More, Terms}, FileNo) ->
    {More#continuation{pos = {FileNo, More#continuation.pos}}, Terms};
conv({#continuation{} = More, Terms, Bad}, FileNo) ->
    {More#continuation{pos = {FileNo, More#continuation.pos}}, Terms, Bad};
conv(Other, _) ->
    Other.

find_first_file(#handle{filename = FName, curF = CurF, maxF = MaxF}) -> fff(FName, inc(CurF, MaxF), CurF, MaxF).

fff(_FName, CurF, CurF, _MaxF) ->
    CurF;
fff(FName, MaybeFirstF, CurF, MaxF) ->
    case file:read_file_info(add_ext(FName, MaybeFirstF)) of
        {ok, _} -> MaybeFirstF;
        _ -> fff(FName, inc(MaybeFirstF, MaxF), CurF, MaxF)
    end.

% The binaries that fit on the current file (at least one on a file with only a header), the rest, their bytes and
% count.
ext_split_bins(CurB, MaxB, FirstPos, Bins) ->
    split_bins(MaxB - CurB, CurB =:= FirstPos, [], Bins, 0, 0, fun(X) -> {byte_size(X), X} end).

int_split_bins(CurB, MaxB, FirstPos, Bins) ->
    split_bins(MaxB - CurB, CurB =:= FirstPos, [], Bins, 0, 0, fun(X) ->
        Sz = byte_size(X),
        {Sz + ?HEADERSZ, item(X, Sz)}
    end).

split_bins(MaxBs, IsFirst, First, [X | Last], Bs, N, Framed) ->
    {Size, XB} = Framed(X),
    NBs = Bs + Size,
    if
        NBs =< MaxBs -> split_bins(MaxBs, IsFirst, [First | XB], Last, NBs, N + 1, Framed);
        % One item may make a file too big, else it would never be written.
        IsFirst, First =:= [] -> {[XB], Last, NBs, N + 1};
        true -> {First, [X | Last], Bs, N}
    end;
split_bins(_, _, First, [], Bs, N, _) ->
    {First, [], Bs, N}.

% The next file number, and the number of files to be; at the new count of a shrinking log the extra files go.
inc_wrap(FName, CurF, {NewMaxF, OldMaxF} = MaxF) when CurF >= NewMaxF ->
    remove_files(wrap, FName, CurF + 1, OldMaxF),
    if
        % The size changed while the current file was past the new count: the index file is truncated next time.
        CurF > NewMaxF ->
            {1, {NewMaxF, CurF}};
        true ->
            index_file_trunc(FName, NewMaxF),
            {1, element(1, MaxF)}
    end;
inc_wrap(_FName, CurF, {NewMaxF, _OldMaxF} = MaxF) ->
    {inc(CurF, NewMaxF), MaxF};
inc_wrap(_FName, CurF, MaxF) ->
    {inc(CurF, MaxF), MaxF}.

inc(N, Max) -> inc(N, Max, 1).

inc(N, {_NewMax, OldMax}, Step) ->
    inc(N, OldMax, Step);
inc(N, Max, Step) ->
    case (N + Step) rem Max of
        Nx when Nx > 0 -> Nx;
        Nx -> Nx + Max
    end.

file_size(Fname) ->
    {ok, Fi} = file:read_file_info(Fname),
    Fi#file_info.size.

% Remove FName.I (or the rotate archives) for N =< I =< Max; the last error is thrown.
remove_files(Type, FName, N, Max) -> remove_files(Type, FName, N, Max, ok).

remove_files(_Type, _FName, N, Max, ok) when N > Max ->
    ok;
remove_files(_Type, _FName, N, Max, {FileName, Error}) when N > Max ->
    file_error(FileName, Error);
remove_files(Type, FName, N, Max, Reply) ->
    FileName =
        case Type of
            wrap -> add_ext(FName, N);
            rotate -> rotate_file_name(FName, N)
        end,
    NewReply =
        case file:delete(FileName) of
            ok -> Reply;
            {error, enoent} -> Reply;
            Error -> {FileName, Error}
        end,
    remove_files(Type, FName, N + 1, Max, NewReply).

get_wrap_size(#handle{maxB = MaxB, maxF = MaxF}) -> get_size(MaxB, MaxF).

get_rotate_size(#rotate_handle{maxB = MaxB, maxF = MaxF}) -> get_size(MaxB, MaxF).

get_size(MaxB, {NewMaxF, _}) -> {MaxB, NewMaxF};
get_size(MaxB, MaxF) -> {MaxB, MaxF}.

old_max_files({_, OldMaxF}) -> OldMaxF;
old_max_files(MaxF) -> MaxF.

index_file_name(F) -> add_ext(F, "idx").

size_file_name(F) -> add_ext(F, "siz").

add_ext(Name, Ext) -> lists:concat([Name, ".", Ext]).

open_read(FileName) -> file:open(FileName, [raw, binary, read]).

open_update(FileName) -> file:open(FileName, [raw, binary, read, write]).

open_truncate(FileName) -> file:open(FileName, [raw, binary, write]).

% --- File access through the cache; errors are thrown ---

% Milliseconds after the first cached write before the cache is written.
-define(TIMEOUT, 2000).

fwrite(FdC, _FN, _B, 0) ->
    % No timer for empty writes.
    {ok, FdC};
fwrite(#cache{fd = Fd, c = C, sz = Sz} = FdC, FileName, B, Size) ->
    Sz1 = Sz + Size,
    C1 = cache_append(C, B),
    case Sz1 > ?MAX_FWRITE_CACHE of
        true ->
            write_cache(Fd, FileName, C1);
        false ->
            maybe_start_timer(C),
            {ok, FdC#cache{sz = Sz1, c = C1}}
    end.

cache_append([], B) -> B;
cache_append(C, B) -> [C | B].

% The first write into an empty cache starts the timer, unless one runs.
maybe_start_timer([]) ->
    case get(write_cache_timer_is_running) of
        true ->
            ok;
        _ ->
            put(write_cache_timer_is_running, true),
            _ = erlang:send_after(?TIMEOUT, self(), {self(), write_cache}),
            ok
    end;
maybe_start_timer(_C) ->
    ok.

fwrite_header(Fd, B, Size) -> {ok, #cache{fd = Fd, sz = Size, c = B}}.

pread(#cache{fd = Fd, c = C}, FileName, Position, MaxBytes) ->
    case write_cache(Fd, FileName, C) of
        {ok, NewFdC} ->
            case file:pread(Fd, Position, MaxBytes) of
                {error, Error} -> {NewFdC, catch file_error(FileName, {error, Error})};
                R -> {NewFdC, R}
            end;
        {Error, NewFdC} ->
            {NewFdC, Error}
    end.

position(#cache{fd = Fd, c = C}, FileName, Pos) ->
    case write_cache(Fd, FileName, C) of
        {ok, NewFdC} ->
            case position2(Fd, FileName, Pos) of
                {ok, Loc} -> {ok, NewFdC, Loc};
                Error -> {Error, NewFdC}
            end;
        Reply ->
            Reply
    end.

position_close(#cache{fd = Fd, c = C}, FileName, Pos) ->
    NewFdC = write_cache_close(Fd, FileName, C),
    {ok, Loc} = position_close2(Fd, FileName, Pos),
    {NewFdC, Loc}.

fsync(#cache{fd = Fd, c = C}, FileName) ->
    case write_cache(Fd, FileName, C) of
        {ok, NewFdC} = Reply ->
            case file:sync(Fd) of
                ok -> Reply;
                Error -> {catch file_error(FileName, Error), NewFdC}
            end;
        Reply ->
            Reply
    end.

truncate_at(FdC, FileName, Pos) ->
    case position(FdC, FileName, Pos) of
        {ok, NewFdC, _Pos} ->
            case file:truncate(NewFdC#cache.fd) of
                ok -> {ok, NewFdC};
                Error -> {catch file_error(FileName, Error), NewFdC}
            end;
        Reply ->
            Reply
    end.

fwrite_close2(Fd, FileName, B) ->
    case file:write(Fd, B) of
        ok -> ok;
        Error -> file_error_close(Fd, FileName, Error)
    end.

pwrite_close2(Fd, FileName, Position, B) ->
    case file:pwrite(Fd, Position, B) of
        ok -> ok;
        {error, Error} -> file_error(FileName, {error, Error})
    end.

position2(Fd, FileName, Pos) ->
    case file:position(Fd, Pos) of
        {error, Error} -> catch file_error(FileName, {error, Error});
        OK -> OK
    end.

position_close2(Fd, FileName, Pos) ->
    case file:position(Fd, Pos) of
        {error, Error} -> file_error_close(Fd, FileName, {error, Error});
        OK -> OK
    end.

truncate_at_close2(Fd, FileName, Pos) ->
    {ok, _} = position_close2(Fd, FileName, Pos),
    case file:truncate(Fd) of
        ok -> ok;
        Error -> file_error_close(Fd, FileName, Error)
    end.

% The cache is empty for a file opened read_only.
fclose(#cache{fd = Fd, c = C}, FileName) ->
    _ = write_cache_close(Fd, FileName, C),
    file:close(Fd).

% Whether repairs and missing chunk files go unreported (the open/1 option quiet), kept in the log process.
set_quiet(Bool) -> put(quiet, Bool).

is_quiet() -> get(quiet) =:= true.

write_cache(Fd, _FileName, []) ->
    {ok, #cache{fd = Fd}};
write_cache(Fd, FileName, C) ->
    case file:write(Fd, C) of
        ok -> {ok, #cache{fd = Fd}};
        Error -> {catch file_error(FileName, Error), #cache{fd = Fd}}
    end.

write_cache_close(Fd, _FileName, []) ->
    #cache{fd = Fd};
write_cache_close(Fd, FileName, C) ->
    case file:write(Fd, C) of
        ok -> #cache{fd = Fd};
        Error -> file_error_close(Fd, FileName, Error)
    end.

file_error(FileName, {error, Error}) -> throw({error, {file_error, FileName, Error}}).

file_error_close(Fd, FileName, {error, Error}) ->
    _ = file:close(Fd),
    throw({error, {file_error, FileName, Error}}).
