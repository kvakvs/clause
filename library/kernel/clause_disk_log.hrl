%% The records and formats disk_log, clause_disk_log and wrap_log_reader share (plan 21 step LIB52). The file formats
%% are OTP's, so logs are exchangeable with OTP: a halt or wrap log file starts with LOGMAGIC and OPENED or CLOSED,
%% then items <<Size:32, BIGMAGIC:32, Term/binary>> (the MD5 of <<Size:32>> after the magic for items of 65528 bytes
%% or more); a wrap log has an index file (current file and item counts) and a size file.

-include("include/file.hrl").

% The format version of wrap log index and size files.
-define(VERSION, 2).

% Bytes of a file head, an item size, an item magic and an item header.
-define(HEADSZ, 8).
-define(SIZESZ, 4).
-define(MAGICSZ, 4).
-define(HEADERSZ, 8).
-define(MAGICHEAD, <<12, 33, 44, 55>>).
-define(MAGICINT, 203500599).
-define(BIGMAGICHEAD, <<98, 87, 76, 65>>).
-define(BIGMAGICINT, 1649888321).
-define(MIN_MD5_TERM, 65528).

-define(MAX_FILES, 65000).
-define(MAX_BYTES, ((1 bsl 64) - 1)).
-define(MAX_CHUNK_SIZE, 65536).
-define(MAX_FWRITE_CACHE, 65536).

-define(LOGMAGIC, <<1, 2, 3, 4>>).
-define(OPENED, <<6, 7, 8, 9>>).
-define(CLOSED, <<99, 88, 77, 11>>).

% The named tables of the log registry: names to pids and pids to names.
-define(DISK_LOG_NAME_TABLE, disk_log_names).
-define(DISK_LOG_PID_TABLE, disk_log_pids).

% The options of open/1, checked.
-record(arg, {
    name = 0,
    version = undefined,
    file = none,
    repair = true,
    size = infinity,
    old_size = infinity,
    type = halt,
    format = internal,
    linkto = self(),
    head = none,
    mode = read_write,
    notify = false,
    quiet = false,
    options = []
}).

% The bytes written to a file but not yet passed to file:write/2, and their count.
-record(cache, {fd, sz = 0, c = []}).

% A halt log: its file cache, bytes on the file and size limit.
-record(halt, {fdc, curB, size}).

% A wrap log: the base name, the size of each file and the number of files (or {New, Old} while the number shrinks),
% bytes on the current file, its number, cache and name, items on it (header included) and before it, where its
% first item starts, and file overflows since the last info/1 and before.
-record(handle, {filename, maxB, maxF, curB, curF, cur_fdc, cur_name, cur_cnt, acc_cnt, firstPos, noFull, accFull}).

% A rotate log: the active file, its cache and inode, the size limits, bytes on the file and where its first item
% starts.
-record(rotate_handle, {
    file,
    cur_fdc,
    inode,
    file_check,
    maxB,
    maxF,
    curB = 0,
    firstPos,
    compress_on_rotate = true
}).

% An open log as its process keeps it.
-record(log, {
    status = ok,
    name,
    blocked_by = none,
    users = 0,
    filename,
    owners = [],
    type,
    format,
    format_type,
    head = none,
    mode,
    size,
    extra,
    version
}).

% A chunk continuation, as OTP's: the log process, the position (an offset, or {FileNo, Offset} for wrap logs) and
% the bytes read ahead or the count of bytes to read next. disk_log and wrap_log_reader get it from their observed
% types headers and define CONTINUATION_RECORD first.
-ifndef(CONTINUATION_RECORD).
-record(continuation, {pid = self(), pos, b}).
-endif.
