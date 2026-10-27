# Ports

Programs get ports as OTP defines them, and external I/O goes through them
(decision of plan 11 step 57A, 2026-10-08). The port table, exit rules and port
tasks are in `runtime/src/scheduler/ports.cpp`, the builtins in
`runtime/src/builtins/erlang/ports.cpp`, the drivers and the I/O thread in
`runtime/src/ports/`. OTP goldens: `executables_port_identities`,
`executables_port_input`, `executables_port_spawn`, `executables_file_io`,
`executables_sockets`, `executables_many_ports`, `executables_port_fairness`,
`executables_busy_ports`, `executables_slow_owner`; runtime test
`runtime_port_io`.

## Identity

- A port is an immediate word: tag `0x7` (`TermKind2::port` under the
  `see_termkind2` primary tag, beside pids' `0x3`), payload a port number from
  one process-wide sequence that is never reused, like pid numbers
  ([pids](terms.md#pids-and-references)). A runtime admits only the numbers it
  issued; forged and foreign port words are rejected.
- Printing: `#Port<0.N>`; `port_to_list/1` gives that text and `list_to_port/1`
  parses it (badarg for anything else).
- Order: numbers < atoms < references < funs < ports < pids < tuples, as in OTP;
  ports compare by number.
- A closed port's identity stays valid: `is_port/1` stays true, `port_info/1,2`
  answer `undefined`, sends to it are dropped.
- Ports are immediates, so copying, collection and messages need nothing new.

## Port table and ownership

- Each runtime keeps a port table (number → port) in its executor, guarded by
  the executor mutex like the processes' links and names
  ([workers](processes.md#workers)).
- A port records its driver, its connected process (the opener, changed by
  `port_connect/2` or `{Pid, {connect, New}}`), its links, the monitors held on
  it, an optional registered name, its options and its input/output byte
  counters.
- `open_port/2` links the new port to its opener. When the connected process
  ends, its port closes; a port that closes sends exit signals to its links and
  `'DOWN'` messages to its monitors with its reason (`normal` for a close that
  is not an error, a POSIX atom such as `epipe` otherwise).
- An exit signal reaching a port closes it with its reason (`kill` from `exit/2`
  as `killed`), except `normal` through a link from a process other than the
  connected one, which only removes the link. `exit(Port, normal)` closes the
  port. A connected process that unlinked leaves its port open when it ends.
- A request message from a process other than the connected one, or a malformed
  message, sends the connected process an exit signal `badsig` from the port;
  the port stays open.
- A port's exit signals and messages to a process running on another worker wait
  for that process's time slice to end (they hold only atoms, pids and ports);
  an `exit(Port, Reason)` with a reason term in the sender's heap makes the
  builtin run again until no linked or monitoring process runs elsewhere, as for
  process signals ([workers](processes.md#workers)).
- Ports take registered names (`register/2`) and `monitor(port, Port)`; `link/1`
  and `unlink/1` accept them.

## Builtins and port messages

- `is_port/1` (true for ports), `port_to_list/1`, `list_to_port/1`, `ports/0`.
- `port_info/1,2`: `name`, `links`, `id`, `connected`, `input`, `output`,
  `os_pid`, `monitors`, `monitored_by`, `registered_name`, `queue_size`
  ([busy ports](#busy-ports)).
- `port_close/1`, `port_connect/2`, `port_command/2,3`; the messages
  `Port ! {Pid, {command, Data}}`, `{Pid, close}`, `{Pid, {connect, New}}`.
- `link/1`, `unlink/1`, `monitor(port, P)`, `exit/2` on ports; `register/2` of a
  port.
- `port_control/3`, `port_call/3`: `badarg` for drivers without control; the
  `file` and socket drivers are driven by `port_control/3`.
- `open_port({fd, In, Out}, Opts)`, `open_port({spawn, Command}, Opts)`,
  `open_port({spawn_executable, File}, Opts)`, and the internal drivers of the
  project library (`file`, sockets).

Errors follow OTP: `badarg` for a closed or invalid port and bad arguments, the
POSIX reason (`enoent`, `eacces`) as an `error` for an open that fails.

Messages from a port go to its connected process: `{Port, {data, Data}}`,
`{Port, eof}` (option `eof`), `{Port, {exit_status, Status}}` (option
`exit_status`), `{Port, closed}` (after `{Pid, close}`), `{Port, connected}` (to
the old owner after a connect).

## Data modes and options

| Option | Effect |
| --- | --- |
| `stream` (default), `{packet, N}` (N = 1, 2, 4) | Bytes as they arrive, or messages framed by an N-byte big-endian length that output also gets |
| `{line, L}` | `{eol, Line}` per line, `{noeol, Part}` for parts longer than `L` or an unterminated end |
| `binary` | Data as binaries instead of byte lists |
| `eof` | `{Port, eof}` at end of input; the port stays open until closed |
| `exit_status` | `{Port, {exit_status, S}}` when the program exits (spawn ports) |
| `use_stdio` (default), `nouse_stdio`, `stderr_to_stdout`, `in`, `out`, `hide` | As in OTP (`hide` has no effect) |
| `{args, List}`, `{arg0, A}`, `{env, Env}`, `{cd, Dir}` | Spawn ports ([subprocesses](#subprocesses)) |
| `{busy_limits_port, {Low, High} \| disabled}` | Queued output bytes that make the port busy ([busy ports](#busy-ports)) |

Without `eof`, end of input closes the port with reason `normal`, after the
`exit_status` message when one was asked for. Unknown options are `badarg`.

## Drivers

A driver is a C++ object behind one port (`runtime/src/ports/`): it opens the
resource, accepts output (`port_command`), answers `port_control/3` when it
supports control, reports input and errors as events and closes. Drivers:

- `fd`: existing descriptors, output written synchronously, input through the
  I/O thread.
- `spawn`: a child program with its stdin and stdout as pipes
  ([subprocesses](#subprocesses)).
- `file`: an open file of the project library's `file` module; its operations
  are synchronous `port_control/3` calls that may block the worker running the
  caller for the duration of the disk I/O, as OTP's dirty I/O schedulers do
  ([files](#standard-io-and-files)).
- `tcp_inet`, `udp_inet`: sockets of the project library's `gen_tcp`, `gen_udp`
  and `inet` on Boost.Asio; connects, accepts and receives are asynchronous
  ([sockets](#sockets)).
- `clause_registry` (Windows): the registry of `win32reg`
  ([the registry](#the-registry)).

The internal drivers of `file`, the sockets and the registry are opened with
`{spawn_driver, Name}` under Clause names, and their `port_control/3` operations
are a Clause protocol: programs use the library modules, not OTP's
`prim_inet`/`efile` protocols.

## I/O thread

One I/O thread per runtime (`detail::Reactor`, `runtime/src/ports/reactor.hpp`)
runs a Boost.Asio `io_context`: an I/O completion port on Windows, `epoll` on
Linux, `kqueue` on macOS. The first port that needs it starts it; it serves
every port kind, so a port costs no thread of its own:

- Pipes of spawned programs: overlapped named pipes on Windows
  (`windows::stream_handle`; anonymous pipes cannot be overlapped), plain pipes
  elsewhere (`posix::stream_descriptor`). Output is queued and written in order
  by asynchronous writes.
- `fd` input on Linux and macOS: the thread waits until the descriptor is
  readable, then one `read()` takes what is there, so the program's own
  descriptors keep their blocking mode; a regular file, which cannot be waited
  for, is read at once.
- Program exits: on Windows the system's wait thread pool
  (`RegisterWaitForSingleObject`, as Asio's `object_handle` uses) waits for the
  process handle and the exit is reported on the I/O thread; on Linux and macOS
  a `SIGCHLD` handler (`signal_set`) and `waitpid(WNOHANG)` reap every watched
  program, also after its port closed.
- Sockets ([sockets](#sockets)).
- Exception: an `fd` input handle on Windows (a console or an inherited
  anonymous pipe) cannot be overlapped, so it is read by a blocking thread of
  its own, as libuv and ERTS do; closing the port cancels the read
  (`CancelSynchronousIo`) and lets the thread go.

`runtime/src/ports/io*.cpp` hold the port I/O (`detail::IoService`): its methods
only post work to the I/O thread, where all I/O state lives. The I/O thread
hands input over as it was read: raw bytes, the end of input, a read error, a
program's exit status. Socket events (messages, accepted connections) are handed
over the same way.

The prototype `tests/prototypes/poller/` (`run.py --wsl`) shows the wakeup on
Windows (completion port) and WSL Linux (`poll()`): an idle scheduler thread
wakes 9–91 µs after input, and shutdown stops the I/O thread without input.

At program end every port is closed (child programs see end of input; they are
not killed, as in OTP) and the I/O thread stops: it is joined outside the
executor mutex, because a delivery in progress takes it; a Windows `fd` reader
blocked in a read that cannot be cancelled is detached and delivers nothing
more.

## Port tasks

A port is scheduled like a process: what the I/O thread hands over waits in the
port, and the port waits in the executor's port queue until a scheduler worker
runs its task.

- Workers take a port task and a process time slice in turn while both queues
  have work, so a port flooding input cannot starve processes, and processes
  cannot starve ports; an idle worker takes whatever is queued.
- A task runs under the executor mutex for `PORT_TASK_REDUCTIONS` (a time
  slice's 4,000): each message it delivers costs 100 reductions plus one per 64
  bytes it carries. A port with work left is queued again at the back.
- The task frames input (`InputDecoder`): stream input arrives in the chunks
  reads return; `{packet, N}` holds bytes until a whole packet arrived (an
  incomplete packet at end of input is dropped, as in OTP); `{line, L}` splits
  at `\n`, sends a line longer than `L` as `{noeol, Part}` pieces and an
  unterminated end as `{noeol, Rest}`. `port_info(P, input)` counts every byte
  read, newlines and packet headers included, when it is read.
- End of input sends `{Port, eof}` with option `eof`, else closes the port with
  reason `normal`; a read error closes it with `eio`.
- A message becomes a message to its process at once when that process does not
  run on a worker (waking it like any message), else when its time slice ends,
  so a running process's heap is never touched by another thread.
- Commands, closes, connects and exit signals sent to a port act at once on the
  sender's worker, as ERTS does for a port that is not busy: an `fd` port writes
  its output at once on the caller's worker; the pipe and socket drivers queue
  output and let the I/O thread write it; a busy port suspends the sender
  ([busy ports](#busy-ports)).

## Busy ports

Output a driver queues for the I/O thread (spawned programs' pipes) counts
against the port's busy limits, OTP's `busy_limits_port` (defaults: high 8,192
bytes, low 4,096; `{busy_limits_port, {Low, High}}` or `disabled` as an
`open_port/2` option, limits at least 1, a low limit above the high one lowered
to it):

- A write that takes the queued output to the high limit or above still goes out
  and makes the port busy; it stays busy until the I/O thread has written enough
  that less than the low limit is queued.
- `port_command/2` and `Port ! {Pid, {command, Data}}` to a busy port suspend
  the sender, which writes nothing; once the port is no longer busy, or closes,
  the sender runs its builtin again (a closed port then gives `badarg`, as in
  OTP). A suspended process still receives exit signals.
- `port_command/3` with `nosuspend` returns `false` for a busy port and writes
  nothing; `force` raises `notsup`, as OTP's spawn and `fd` drivers do not allow
  it.
- `port_info(P, queue_size)` is the output queued and not written yet.
- `fd` ports write at once and socket output goes through `port_control/3`, so
  neither is ever busy.

A port also bounds the input it holds:

- When more than 64 KiB of raw input waits for the port's task, the I/O thread
  stops reading the port (a program writing to it then blocks, as on a full
  pipe) until the task has taken it below 32 KiB.
- While the connected process can run (it is running or queued, not waiting in a
  `receive`, suspended or blocked) and already holds 1,024 messages (or has that
  many port signals waiting for its time slice to end), the port's task delivers
  nothing more until that process's slice has ended. A process waiting in a
  `receive` always gets the input, so a receive for a later message of the port
  cannot wait for ever.

## Subprocesses

`open_port({spawn, Command}, Options)` and
`open_port({spawn_executable, File}, Options)` start a program whose stdin and
stdout are pipes of the port:

- `{spawn, Command}`: on Linux and macOS `/bin/sh -c Command`; on Windows
  `CreateProcessW` with `Command` as the command line (it finds the program on
  the search path). `{spawn_executable, File}` runs `File` without a shell, with
  argv[0] `{arg0, A}` (default `File`) and `{args, List}`; on Windows arguments
  are quoted by the rules of `CommandLineToArgvW`.
- `{env, [{Name, Value | false}]}` sets or removes variables of the child,
  `{cd, Dir}` its directory, `stderr_to_stdout` merges stderr into the port;
  otherwise the child shares the program's stderr. `in` alone opens no stdin
  pipe and `out` alone no stdout pipe (the null device instead); with both or
  neither the port has both, as in OTP. `hide` and `overlapped_io` have no
  effect.
- A program that cannot be started raises `error:Reason` with the POSIX reason
  (`enoent`, `eacces`, `enoexec`); bad names and options raise `badarg`.
- Output is queued and written by the I/O thread, with the port's busy limits
  ([busy ports](#busy-ports)); closing the port lets it finish what is queued,
  then closes the program's stdin. The program is never killed; it usually ends
  at end of input.
- Option `exit_status` sends `{Port, {exit_status, S}}` once the program has
  exited (its exit code; on Linux and macOS 128 plus the signal for a program a
  signal ended) and before `{Port, eof}` or the close at end of input; a port
  reading nothing reports it when the program exits, then closes.
- `port_info(P, os_pid)` is the program's process id; `name` is the command or
  file.
- The library's `os:cmd/1` runs `Command` with `COMSPEC /c` on Windows (`cmd`
  when unset) or `/bin/sh -c`, collects stdout and stderr until the program
  closes them and returns the bytes as a list; `os:type/0` is `{win32, nt}`,
  `{unix, linux}` or `{unix, darwin}`; `os:getenv/1` returns a string or
  `false`.

## Standard I/O and files

- `io:format`, `io:put_chars` and `erlang:display` write standard output
  directly ([io](libraries/io.md)). Standard input is read by one library server process,
  registered as `clause_stdin` and started on first use, which owns an
  `{fd, 0, 1}` port: `io:get_line/1,2` writes the prompt, then returns the next
  line with its newline, the rest of the input without one at its end, then
  `eof`; `io:get_chars/2,3` returns up to `N` characters, then `eof`. Requests
  of several processes are answered in arrival order. Input is decoded as UTF-8
  into characters, as OTP reads its terminal; the server keeps OTP's noshell
  `standard_io` options (`binary`, `encoding`, `echo`, `log`; `io:getopts/0,1`,
  `io:setopts/1,2`, plan 21 step LIB63): binary mode returns binaries, `echo`
  writes consumed input to standard output, `get_password` is `{error, enotsup}`
  without a shell, as OTP's.
- File servers answer `getopts` and `setopts` as OTP's file I/O server
  (`binary`/`list` and `encoding`; `file:open/2` takes `{encoding, E}`): I/O
  requests (`io:get_line`, `get_chars`, `put_chars`, `get_until`) encode and
  decode the device encoding, and `file:read/2`, `read_line/1` on an encoded file
  answer as latin1 `get_chars`/`get_line` requests (`no_translation` above 255,
  which stops the server). Output of `io:format/3`, `io:put_chars/2`, `io:nl/1`
  to any pid is an I/O protocol `put_chars` request.
- The library's `file` module drives the runtime's file driver,
  `{spawn_driver, "clause_file"}`, with `port_control/3`. Its operations
  (`ports/file.cpp`: open, read, write, position, read_line, sync, close, and the path
  operations read_file, write_file, delete, rename, list_dir, make_dir, del_dir
  and read_file_info, `ports/file_info.cpp`; get_cwd, set_cwd, read_link,
  read_link_info, make_link and make_symlink, `ports/file_paths.cpp`) are
  synchronous system calls on the caller's worker, outside the executor mutex;
  their replies start with a status byte (0 ok, 1 error and its POSIX reason, 2
  end of file). The protocol is Clause's own. read_line reads 64 KB ahead and
  returns lines from that buffer; any other operation on the file first seeks
  back over the bytes it has not returned, so positions stay exact.
- Path operations make OTP's system calls and no more: a path's status is read
  only once an open or a delete failed (to answer `eisdir` or `eperm`), delete
  unlinks a file and never removes a directory, and read_file reads a file up to
  the size its descriptor reports, as OTP does, with no read probing for its end.
- `port_control/3` hands a binary or `[]` to the driver in place (other iodata
  is flattened first), and a reply becomes the result binary without a copy:
  replies are `ByteBuffer`s (`runtime/include/byte_buffer.hpp`), byte vectors
  that grow without zeroing, so a read fills its reply's bytes once (OPT3.4).
- `file:open/2` returns an I/O server pid that owns a file port and closes when
  the opener ends, as OTP's (on Windows the file is shared for reading, writing
  and deletion, as OTP opens it, so an open file can be deleted or renamed) (modes `read`, `write`, `append`, `exclusive`,
  `binary`, `raw`; other modes are ignored, as OTP ignores options it does not
  use; `write` truncates unless `append` is also given).
- `file:write_file/3` opens with its modes plus `binary` and `write`;
  `file:read_file_info/1,2` return OTP's `#file_info{}` from the path's status
  (following links), with times as local or universal date and time or POSIX
  seconds (`{time, local | universal | posix}`; `raw` is accepted). On Windows
  `inode`, `uid` and `gid` are 0 and `major_device` is the drive number, as OTP
  reports them. `read/2`, `read_line/1`, `write/2`, `position/2` and
  `io:get_line/2`, `io:get_chars/3` on it are requests to that server; after
  `close/1` requests return `{error, terminated}` and `close/1` stays `ok`.
- Positioned I/O and file changes (plan 21 step LIB50): `pread/2,3` and
  `pwrite/2,3` position and then read or write on a file server (`cur` reads
  on, the position moves), as OTP's server does; a `raw` file takes integer
  offsets only (`badarg` otherwise) and keeps its position. `truncate/1` cuts at
  the position; `advise/4` takes OTP's advice atoms (`posix_fadvise` on Linux,
  ignored elsewhere, as OTP); `allocate/3` reserves space (`fallocate`, macOS
  `F_PREALLOCATE`, `{error, enotsup}` on Windows), its arguments checked first
  (`badarg` for non-numbers, `einval` for negatives and floats). As OTP's file
  server, a server whose write, positioned write, sync, truncate or advice
  fails replies with the error and exits, so later requests get
  `{error, terminated}`; a raw file goes on. `write_file_info/2,3` and
  `change_mode/2`, `change_owner/2,3`, `change_group/2`, `change_time/2,3`
  change owner, permissions and times in prim_file's order (Windows: owners
  ignored, only the read-only attribute from the owner write bit, the creation
  time set too). `sendfile/2,5` read a raw file in chunks (at most 1 MiB,
  `chunk_size`) and send them with `gen_tcp:send/2` or the given fun, OTP's
  fallback: Clause has no native sendfile. `format_error/1` gives OTP's texts,
  POSIX ones from `erl_posix_msg:message/1`.
- Lines and failed reads (plan 21 step LIB50C): `read_line/1` and `io:get_line`
  read a CRLF ending as LF (the position counts the CR), as OTP; a file server
  whose read fails replies with the error and exits, and one whose line read
  fails exits without a reply (the caller gets `{error, terminated}`), as OTP's.
- `compressed` and `compressed_one` (plan 21 step LIB50C): writing goes through
  a gzip deflate stream (zlib, window bits 31) finished at close, also when the
  server stops or its opener ends; reading inflates a file that starts with
  gzip's magic bytes (`compressed` reads concatenated members as one,
  `compressed_one` stops after the first) and reads any other file as it is.
  Positions count uncompressed bytes: forward by reading or writing zeros,
  backward only when reading (from the start again), `eof` is `einval`.
  Reading a written file or writing a read one is `ebadf`; `allocate/3` and,
  on raw files, positioned I/O are `enotsup`; a file server stops after
  `truncate`, `advise` and `sync` (`enotsup`) as OTP's. Reading and writing at
  once or appending is `{error, einval}`.
- `{zstd, Options}` (plan 21 step LIB54E): as `compressed`, through a zstd
  context made with `Options` (`{error, badarg}` when it refuses them): each
  write is a frame of its own and closing adds an empty one, as OTP's layer
  writes; reading decompresses the frames read in 8 KiB chunks.
- `ram` (plan 21 step LIB54C): the file is its iodata contents in memory,
  served by a process like other files and closed with its opener; modes
  `read`, `write` and `binary` only (`read` when neither is given, anything
  else `badarg`, `raw` with `ram` raises `badarg`). Reads, writes, positions,
  `pread`/`pwrite` (the position kept), `truncate`, `sync`, `advise` and
  `allocate` answer as OTP's ram file driver: writes past the end fill the gap
  with zeros, offsets and sizes past signed 32 bits are `einval`, writing a
  read-only file is `ebadf`, truncating it `eacces`.
- OTP's I/O protocol: the standard input server and every open file's server
  answer `{io_request, From, ReplyAs, Request}` with
  `{io_reply, ReplyAs, Reply}`, which `io:request/1,2` and `io:requests/1,2`
  send (`standard_io` and `user` name the standard input server, another atom a
  registered process; `{error, arguments}` when it is not registered,
  `{error, calling_self}` for the caller itself). Requests: `get_until`
  (`M:F(Cont, Chars | eof, Extra...)` from `Cont = []`, fed what is buffered or
  4 KiB file chunks until `{done, Result, Rest}`; `Rest` is read again by the
  next request: kept in the buffer, or for a file the position moves back over
  it), `get_line`, `get_chars`, `put_chars` (characters or `M:F(A)`), the old
  forms without an encoding (latin1), and `{requests, List}` (in order, stopping
  at the first `{error, _}`). Characters are bytes, as above; a binary-mode file
  returns a `get_until` list result as a binary. Others get
  `{error, {request, Request}}`. A failing `get_until` or `put_chars` function
  makes a file's server reply `{error, Function}` and exit with the failure, as
  OTP's does; the opener lives on (OTP's file server is not linked to it). Bytes
  a unicode file cannot decode end its server as OTP's: the characters before
  them are fed first, and when the function needs more it gets no more, the
  reply is `{error, {Location, file_io_server, invalid_unicode}, Location}` for
  an `erl_scan:tokens` scan that has started (else `{error, Function}`) and the
  server exits with `invalid_unicode`. Input requests on standard input write
  their prompt.
- Directories, links and copies: `get_cwd/0` spells the current directory as OTP
  does (forward slashes, a lowercase drive letter), `get_cwd/1` takes a Windows
  drive (`enotsup` elsewhere); `set_cwd/1` changes it for the whole program;
  `read_link/1`, `read_link_all/1` (`einval` for a path that is not a link),
  `read_link_info/1,2` (a final link's own status: POSIX `lstat`, on Windows the
  target's fields with type `symlink`), `make_link/2`, `make_symlink/2` (needs a
  privilege on Windows: `eperm`, as OTP), `list_dir_all/1`, `del_dir_r/1`,
  `copy/2,3` (names, `{Name, Modes}` or open files; `{ok, BytesCopied}`),
  `raw_read_file_info/1`, `read_file/2`, `delete/2`, `native_name_encoding/0`
  (`utf8`). `del_dir/1` answers `enoent` for a missing path and `enotdir` for a
  file.
- Clause's `file` is the file layer; Clause's `filelib` and `filename` run on
  it (`filelib:fold_files/5` uses `re`); `erl_prim_loader` is provided over `file`
  with OTP's bare `error` answers and a loader path of `["."]` (`set_path/1`),
  its loading and archive functions declined until plan step 71; `prim_file` and
  `prim_buffer` stay missing, so OTP's `raw_file_io*` callers never enter a
  program.
- Errors follow OTP: `{error, enoent}`, `eexist`, `eisdir` (a directory opened
  or read as a file), `einval` (a negative position; `badarg` for one that is
  no location), `ebadf` (reading a
  write-only or writing a read-only file), `badarg` for bad names and data. File
  names are strings, binaries or atoms, encoded as UTF-8.

## Sockets

A socket is a port, as with OTP's `inet_drv` backend: `is_port(Socket)` is true
and active-mode messages are `{tcp, Socket, Data}`, `{tcp_closed, Socket}`,
`{tcp_error, Socket, Reason}`, `{udp, Socket, Address, Port, Data}`. The subset:
`gen_tcp:connect/2,3,4` (a host and port, or a socket address map),
`listen/2`, `accept/1,2`, `send/2`, `recv/2,3`, `close/1`,
`controlling_process/2`, `shutdown/2`; `gen_udp:open/1,2`, `connect/2,3`,
`send/2,3,4,5` (OTP's destination forms and ancillary data), `recv/2,3`,
`close/1`; `inet:setopts/2`, `getopts/2`, `getstat/1,2`, `info/1`, `i/0,1,2`,
`monitor/1`, `cancel_monitor/1`, `port/1`, `peername/1`, `peernames/1,2`,
`sockname/1`, `socknames/1,2`; modes `{active, true | false | once}`,
`binary`/`list` (`{mode, M}`), `{packet, 0 | 1 | 2 | 4 | raw}`, `{backlog, N}`,
`{ip, Address}`/`{ifaddr, Address}`, `inet`/`inet6`; IPv4 and IPv6 addresses as
tuples, host names as strings or atoms (`loopback` included).

Options beyond the data mode (plan 21 step LIB60, `ports/socket_options.cpp`,
`library/kernel/clause_socket_options.erl`) are driver records of a code and a
value, given when a socket opens (set before it binds or connects) or by
`setopts/2`; every option of a `setopts/2` call is checked before any is set.
The system's options (`reuseaddr`, `keepalive`, `dontroute`, `linger`,
`broadcast`, `sndbuf`, `recbuf`, `nodelay`, `tos`, `tclass`, `ipv6_v6only`,
`multicast_ttl`, `multicast_loop`, `multicast_if`, `add_membership`,
`drop_membership`, `exclusiveaddruse` on Windows, `priority` and `reuseport`
where the system has them) go to `setsockopt` and are read back with
`getsockopt`; the options OTP's `inet_drv` keeps itself (`buffer`, `header`,
`deliver`, `exit_on_close`, `send_timeout`, watermarks, `packet_size`, ...) are
kept with inet_drv's defaults and not applied ([differences](differences.md)).
As inet_drv, a receive buffer raises `buffer` until that is set, and
`getopts/2` leaves out the options a socket type lacks (`broadcast` of a TCP
socket, `nodelay` of a UDP one, `tclass` of IPv4) and refuses unknown ones with
`{error, einval}`. An accepted connection takes over its listening socket's
options. `send/5` with ancillary data sets `tos`, `tclass` or `ttl` for that
datagram alone; on Windows it is `{error, einval}`, as OTP.

Each socket counts its traffic as inet_drv does for `getstat/1,2`: packets
received are reads (TCP) or datagrams, packets sent are `send` calls (with
their packet header) or datagrams, with inet_drv's running averages;
`send_pend` is the output queued and not written. `getstatus` gives inet_drv's
status flags (`open`, `connected`, `connecting`, `listen`, `accepting`), which
`info/1` and `i/0,1,2` print with OTP's names and module column (`inet_tcp`,
`inet6_udp`, ...). Queries of a closed socket answer `{error, einval}`, as OTP.

The `socket` module (plan 21 step LIB62, `library/kernel/socket.erl`) uses the
same driver ports in passive binary mode. A stream socket can be bound before
it listens or connects (operation 22 opens and binds the system socket, which
`listen` turns into the acceptor or `connect` connects); a datagram socket is
bound by `udp_open`, implicitly at its first send. A connection's end answers
every waiting `recv` (also one asking for more bytes than remain). `socket`
keeps the bytes a recv did not take in its own buffer, so `recv/2,3,4` gives
OTP's `{error, {timeout, Data}}` and `{error, {closed, Data}}`.

Implementation (`runtime/src/ports/sockets.cpp`):

- The runtime's I/O thread ([I/O thread](#io-thread)) serves the sockets; every
  socket's state lives on that thread. The library opens a port with
  `{spawn_driver, "tcp_inet" | "udp_inet"}` and drives it with `port_control/3`;
  the calling worker posts the operation to the I/O thread and waits for its
  synchronous reply (status byte 0 and a result, or 1 and a POSIX reason).
- Operations that wait (connect, accept, recv) answer later with a message
  `{clause_socket, Socket, Reply}` to their caller, so the caller blocks in an
  ordinary `receive` and other processes keep running. A timeout cancels the
  request; the cancellation itself answers `cancelled` after anything the socket
  sent before, so a reply that won the race is returned instead of lost.
- Socket messages are described off-heap (`ports/value.hpp`) and built in the
  receiver's heap when delivered ([port tasks](#port-tasks)). An accepted
  connection becomes a new port owned by and linked to the caller of `accept`,
  with the listening socket's mode.
- `controlling_process/2` stops active delivery, moves the socket's messages
  already sent to the old owner to the new one, then reconnects the port, as
  OTP's `inet` does; only the owner may call it (`{error, not_owner}`).
- Output is queued without a cap and written in order on the I/O thread. Closing
  a port sends what is queued, then closes the connection gracefully (FIN); a
  closed port answers every waiting caller `{error, closed}`. Name lookup runs
  on the caller's worker, as it blocks.
- Socket ports are scheduled like other ports ([port tasks](#port-tasks)): a
  socket's messages wait in its port until the port's task delivers them.
- Addresses and hosts: `inet:parse_address/1,2`, `parse_strict_address/1,2`,
  `parse_ipv4_address/1`, `parse_ipv4strict_address/1`, `parse_ipv6_address/1`,
  `parse_ipv6strict_address/1` (OTP's relaxed forms: short and octal or
  hexadecimal IPv4 fields, `::`, IPv4 tails, `%scope` of `fe80::` and `ff02::`),
  `ntoa/1` (OTP's spelling: lowercase hexadecimal, the first longest zero run as
  `::`), `is_ipv4_address/1`, `is_ipv6_address/1`, `is_ip_address/1` are Erlang;
  `gethostname/0` (a socket operation), `getaddr/2`, `getaddrs/2` (the system
  resolver, `getaddrinfo`) and `gethostbyname/1,2` (`h_name` is the name asked,
  no aliases) answer through a socket port opened for the query.
- Services, reverse lookups and interfaces (`inet:getservbyname/2`,
  `getservbyport/2`, `gethostbyaddr/1`, `getifaddrs/0,1`) are `clause_net`
  builtins shared with `net` ([network names](builtins.md#network-names)).
- Clause's `gen_tcp`, `gen_udp`, `inet` and `net` are the network layer; OTP's
  `socket` module (the socket NIF API) is missing, so are `prim_socket`,
  `prim_net`, `prim_inet` and `socket_registry`.

## The registry

`win32reg` (plan 21 step LIB55B) opens `{spawn_driver, "clause_registry"}` on
Windows (`runtime/src/ports/registry_windows.cpp`). The driver keeps OTP's
registry driver's state (the access asked for, the current key and its root,
initially `HKEY_CLASSES_ROOT`) and answers OTP's nine commands as synchronous
`port_control/3` operations with the replies OTP's driver sends as port data:
`o`, `e` and the errno name (registry damage is `eio`, a deleted key `einval`,
other Windows errors as the C runtime maps them), `k` and a key name, `v`, the
type, name and data (strings without their trailing zero, DWORDs big-endian),
`s` and the current key. Listings return every reply, each preceded by its
32-bit size. Names and strings use the ANSI code page, as OTP's driver.

## Not provided

Linked-in drivers and NIFs, `erlang:open_port({spawn_driver, Name})` for OTP
driver names, distribution ports, `port_call/3` on the provided drivers other
than the library's own protocol, busy socket ports, and the `overlapped_io`,
`parallelism` and `busy_limits_msgq` options.
