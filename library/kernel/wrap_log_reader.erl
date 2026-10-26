%% Clause's wrap_log_reader (plan 21 step LIB52, docs/libraries/library.md): reading the chunks of an internal wrap log written
%% by disk_log (or OTP's) without opening it, from the oldest file to the newest or from one file, as OTP's. A file
%% older than the one just read means the log wrapped meanwhile: {error, {is_wrapped, File}}.
-module(wrap_log_reader).
-export([chunk/1, chunk/2, close/1, open/1, open/2]).
-include("wrap_log_reader.types.hrl").

-define(CONTINUATION_RECORD, true).
-include("clause_disk_log.hrl").

% Read from the file after the current one; when the current file is the last written so far (a log not wrapped
% yet: one more than the files recorded), from the first.
open(File) when is_atom(File) ->
    open(atom_to_list(File));
open(File) when is_list(File) ->
    case read_index_file(File) of
        {ok, {CurFileNo, _CurFileSz, _TotSz, NoOfFiles}} when CurFileNo =:= NoOfFiles + 1 ->
            open_int(File, 1, 1);
        {ok, {CurFileNo, _CurFileSz, _TotSz, NoOfFiles}} ->
            FileNo =
                case (CurFileNo + 1) rem NoOfFiles of
                    0 -> NoOfFiles;
                    No -> No
                end,
            open_int(File, FileNo, FileNo);
        Error ->
            Error
    end.

% Read only file FileNo.
open(File, FileNo) when is_atom(File), is_integer(FileNo) ->
    open(atom_to_list(File), FileNo);
open(File, FileNo) when is_list(File), is_integer(FileNo) ->
    case read_index_file(File) of
        {ok, {_CurFileNo, _CurFileSz, _TotSz, NoOfFiles}} when NoOfFiles >= FileNo ->
            open_int(File, FileNo, one);
        {ok, {CurFileNo, _CurFileSz, _TotSz, NoOfFiles}} when CurFileNo =:= FileNo, CurFileNo =:= NoOfFiles + 1 ->
            open_int(File, FileNo, one);
        {ok, _} ->
            {error, {file_not_found, add_ext(File, FileNo)}};
        Error ->
            Error
    end.

close(#wrap_reader{fd = FD}) -> file:close(FD).

chunk(#wrap_reader{} = WR) -> chunk(WR, ?MAX_CHUNK_SIZE, 0).

chunk(#wrap_reader{} = WR, infinity) -> chunk(WR, ?MAX_CHUNK_SIZE, 0);
chunk(#wrap_reader{} = WR, N) when is_integer(N), N > 0 -> chunk(WR, N, 0).

open_int(File, FileNo, FirstFileNo) ->
    FName = add_ext(File, FileNo),
    case file:open(FName, [raw, binary, read]) of
        {ok, Fd} ->
            case file:read(Fd, ?HEADSZ) of
                {ok, Head} -> opened_file(clause_disk_log:is_head(Head), Fd, {File, FileNo, FirstFileNo}, FName);
                _Other -> not_a_log_file(Fd, FName)
            end;
        _Other ->
            {error, {not_a_log_file, FName}}
    end.

% A file with a log head (closed or not) is read.
opened_file(no, Fd, _, FName) ->
    not_a_log_file(Fd, FName);
opened_file(_, Fd, {File, FileNo, FirstFileNo}, FName) ->
    case last_mod_time(FName) of
        {ok, ModTime} ->
            {ok, #wrap_reader{
                fd = Fd, cont = start, file = File, file_no = FileNo, mod_time = ModTime, first_no = FirstFileNo
            }};
        {error, E} ->
            _ = file:close(Fd),
            {error, {file_error, FName, E}}
    end.

not_a_log_file(Fd, FName) ->
    _ = file:close(Fd),
    {error, {not_a_log_file, FName}}.

% Chunks of the current file, then of the next one until the first file read comes round again.
chunk(WR, N, Bad) ->
    #wrap_reader{fd = Fd, cont = Continue, file = File, file_no = CurFileNo, first_no = FirstFileNo} = WR,
    case read_a_chunk(Fd, N, Continue, add_ext(File, CurFileNo)) of
        eof when FirstFileNo =:= one -> {WR, eof};
        eof -> chunk_at_eof(WR, N, Bad);
        {ContOut, [], BadBytes} -> chunk(WR#wrap_reader{cont = ContOut}, N, Bad + BadBytes);
        {ContOut, Chunk, BadBytes} when Bad + BadBytes =:= 0 -> {WR#wrap_reader{cont = ContOut}, Chunk};
        {ContOut, Chunk, BadBytes} -> {WR#wrap_reader{cont = ContOut}, Chunk, Bad + BadBytes};
        Error -> Error
    end.

read_a_chunk(Fd, N, start, FileName) ->
    read_a_chunk(Fd, FileName, 0, [], N);
read_a_chunk(Fd, N, #continuation{pos = Pos, b = B}, FileName) ->
    read_a_chunk(Fd, FileName, Pos, B, N).

% Terms of the binaries read; disk_log does not use the log name in read-only mode.
read_a_chunk(Fd, FileName, Pos, B, N) ->
    case disk_log:ichunk_end(clause_disk_log:chunk_read_only(Fd, FileName, Pos, B, N), foo) of
        {#continuation{} = C, S} -> {C, S, 0};
        Else -> Else
    end.

chunk_at_eof(#wrap_reader{file = File, file_no = CurFileNo, first_no = FirstFileNo} = WR, N, Bad) ->
    case read_index_file(File) of
        {ok, {_, _, _, NoOfFiles}} ->
            case {FirstFileNo, next_file_no(File, CurFileNo, NoOfFiles)} of
                {_, 0} -> {WR, eof};
                {FirstFileNo, FirstFileNo} -> {WR, eof};
                {_, NewFileNo} -> read_next_file(WR, N, NewFileNo, Bad)
            end;
        Error ->
            Error
    end.

% The file after CurFileNo; past the files recorded (a log not wrapped yet) the first, and after the last recorded
% the one written after it when it exists.
next_file_no(File, CurFileNo, NoOfFiles) ->
    case (CurFileNo + 1) rem NoOfFiles of
        _ when CurFileNo > NoOfFiles ->
            1;
        0 when NoOfFiles > 1 ->
            NoOfFiles;
        No when CurFileNo =:= NoOfFiles ->
            case file:read_file_info(add_ext(File, CurFileNo + 1)) of
                {ok, _} -> CurFileNo + 1;
                _ -> No
            end;
        No ->
            No
    end.

read_index_file(File) ->
    case catch clause_disk_log:read_index_file(File) of
        {1, 0, 0, 0} -> {error, {index_file_not_found, File}};
        {error, _Reason} -> {error, {index_file_not_found, File}};
        FileData -> {ok, FileData}
    end.

read_next_file(
    #wrap_reader{file = File, file_no = CurFileNo, mod_time = ModTime, first_no = FirstFileNo} = WR,
    N,
    NewFileNo,
    Bad
) ->
    case last_mod_time(add_ext(File, NewFileNo)) of
        {ok, NewModTime} ->
            Diff =
                calendar:datetime_to_gregorian_seconds(NewModTime) - calendar:datetime_to_gregorian_seconds(ModTime),
            case Diff < 0 of
                true ->
                    {error, {is_wrapped, add_ext(File, CurFileNo)}};
                false ->
                    % The current file is closed only now, so an error above leaves the reader as it was.
                    case open_int(File, NewFileNo, FirstFileNo) of
                        {ok, NWR} ->
                            _ = close(WR),
                            chunk(NWR, N, Bad);
                        Error ->
                            Error
                    end
            end;
        {error, EN} ->
            {error, {file_error, add_ext(File, NewFileNo), EN}}
    end.

last_mod_time(File) ->
    case file:read_file_info(File) of
        {ok, FileInfo} -> {ok, FileInfo#file_info.mtime};
        E -> {error, E}
    end.

add_ext(File, Ext) -> lists:concat([File, ".", Ext]).
