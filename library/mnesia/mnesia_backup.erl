%% mnesia's default backup module (plan 21 step MIM1F, docs/libraries/mnesia.md#backups), the backup_module parameter's
%% default: a backup is a disk_log file named by the opaque data. Writing goes to File.BUPTMP, which the commit renames
%% to File; the callbacks and their results are OTP's.
-module(mnesia_backup).
-export([open_write/1, write/2, commit_write/1, abort_write/1, open_read/1, read/1, close_read/1]).

%% A backup being written: the temporary file, the backup's file and the log.
-record(backup, {tmp_file, file, file_desc}).
%% A backup being read: its file, the log and the read position.
-record(restore, {file, file_desc, cont}).

open_write(File) ->
    Tmp = lists:concat([File, ".BUPTMP"]),
    _ = file:delete(Tmp),
    case disk_log:open([{name, make_ref()}, {file, Tmp}, {repair, false}, {linkto, self()}]) of
        {ok, Log} -> {ok, #backup{tmp_file = Tmp, file = File, file_desc = Log}};
        {error, Reason} -> {error, Reason}
    end.

write(#backup{file_desc = Log} = Backup, Items) ->
    case disk_log:log_terms(Log, Items) of
        ok ->
            {ok, Backup};
        {error, Reason} ->
            _ = abort_write(Backup),
            {error, Reason}
    end.

% The log synced and closed, then renamed to the backup's file.
commit_write(#backup{tmp_file = Tmp, file = File, file_desc = Log}) ->
    case close(Log) of
        ok ->
            _ = file:delete(File),
            case file:rename(Tmp, File) of
                ok -> {ok, File};
                {error, Reason} -> {error, Reason}
            end;
        {error, Reason} ->
            {error, Reason}
    end.

close(Log) ->
    case disk_log:sync(Log) of
        ok -> disk_log:close(Log);
        Error -> Error
    end.

abort_write(Backup) ->
    Result = disk_log:close(Backup#backup.file_desc),
    _ = file:delete(Backup#backup.tmp_file),
    case Result of
        ok -> {ok, Backup#backup.file};
        {error, Reason} -> {error, Reason}
    end.

% An existing backup opened for reading; a repaired log is read as far as it goes.
open_read(File) ->
    case file:read_file_info(File) of
        {error, Reason} ->
            {error, Reason};
        _ ->
            Options = [{file, File}, {name, make_ref()}, {repair, false}, {mode, read_only}, {linkto, self()}],
            case disk_log:open(Options) of
                {ok, Log} -> {ok, #restore{file = File, file_desc = Log, cont = start}};
                {repaired, Log, _, _} -> {ok, #restore{file = File, file_desc = Log, cont = start}};
                {error, Reason} -> {error, Reason}
            end
    end.

% The next items; [] at the end.
read(#restore{file_desc = Log, cont = Cont} = Restore) ->
    case disk_log:chunk(Log, Cont) of
        {error, Reason} -> {error, {"Possibly truncated", Reason}};
        eof -> {ok, Restore, []};
        {Next, []} -> read(Restore#restore{cont = Next});
        {Next, Items, _BadBytes} -> {ok, Restore#restore{cont = Next}, Items};
        {Next, Items} -> {ok, Restore#restore{cont = Next}, Items}
    end.

close_read(#restore{file = File, file_desc = Log}) ->
    case disk_log:close(Log) of
        ok -> {ok, File};
        {error, Reason} -> {error, Reason}
    end.
