# Ports

Decision of plan 11 step 57A (2026-10-08). It replaces the step-53 decision
(no ports, [processes](processes.md#ports)): programs get ports as OTP
defines them, and external I/O goes through them. Steps 57B–57F implement
it; each section names its step. This contract settles the representation,
the driver model and the I/O thread before any source can open a port.

Implemented: identities, the port table, the port builtins and messages,
links, monitors, names and exit signals of ports, and output-only `fd` ports
(step 57B, `runtime/src/scheduler/ports.cpp`, `runtime/src/builtins/erlang/ports.cpp`,
`runtime/src/ports/`; OTP golden `executables_port_identities`); the I/O
thread and `fd` port input with stream, packet and line framing (step 57C,
`runtime/src/ports/io*.cpp`; OTP golden `executables_port_input`);
subprocess ports, `os:type/0`, `os:getenv/1` and `os:cmd/1` (step 57D,
`runtime/src/ports/spawn*.cpp`, `library/stdlib/os.erl`; OTP golden
`executables_port_spawn`); the file driver, the library `file` subset and
standard input through `io:get_line`/`io:get_chars` (step 57E,
`runtime/src/ports/file.cpp`, `library/stdlib/{file,io}.erl`; OTP golden
`executables_file_io`); sockets (step 57F, OTP golden `executables_sockets`);
one event-driven I/O thread for every port kind (step 57G1,
`runtime/src/ports/reactor.cpp`; OTP golden `executables_many_ports`, runtime
test `runtime_port_io`); port tasks on the scheduler workers (step 57G2,
`runtime/src/scheduler/ports.cpp`; OTP golden `executables_port_fairness`);
busy ports and bounded input (step 57G3; OTP goldens `executables_busy_ports`,
`executables_slow_owner`).

## Identity

- A port is an immediate word: tag `0x7` (`TermKind2::port` under the
  `see_termkind2` primary tag, beside pids' `0x3`), payload a port number
  from one process-wide sequence that is never reused, like pid numbers
  ([pids](terms.md#pids-and-references)). A runtime admits only the numbers
  it issued; forged and foreign port words are rejected.
- Printing: `#Port<0.N>`; `port_to_list/1` gives that text and
  `list_to_port/1` parses it (badarg for anything else).
- Order: numbers < atoms < references < funs < ports < pids < tuples, as in
  OTP; ports compare by number.
- A closed port's identity stays valid: `is_port/1` stays true,
  `port_info/1,2` answer `undefined`, sends to it are dropped.
- Ports are immediates, so copying, collection and messages need nothing new.

## Port table and ownership

- Each runtime keeps a port table (number → port) in its executor, guarded
  by the executor mutex like the processes' links and names
  ([workers](processes.md#workers)).
- A port records its driver, its connected process (the opener, changed by
  `port_connect/2` or `{Pid, {connect, New}}`), its links, the monitors held
  on it, an optional registered name, its options and its input/output byte
  counters.
- `open_port/2` links the new port to its opener. When the connected process
  ends, its port closes; a port that closes sends exit signals to its links
  and `'DOWN'` messages to its monitors with its reason (`normal` for a close
  that is not an error, a POSIX atom such as `epipe` otherwise).
- An exit signal reaching a port closes it with its reason (`kill` from
  `exit/2` as `killed`), except `normal` through a link from a process other
  than the connected one, which only removes the link. `exit(Port, normal)`
  closes the port. A connected process that unlinked leaves its port open
  when it ends.
- A request message from a process other than the connected one, or a
  malformed message, sends the connected process an exit signal `badsig`
  from the port; the port stays open.
- A port's exit signals and messages to a process running on another worker
  wait for that process's time slice to end (they hold only atoms, pids and
  ports); an `exit(Port, Reason)` with a reason term in the sender's heap
  makes the builtin run again until no linked or monitoring process runs
  elsewhere, as for process signals ([workers](processes.md#workers)).
- Ports take registered names (`register/2`) and `monitor(port, Port)`;
  `link/1` and `unlink/1` accept them.

## Builtins and port messages

| Builtin or message | Step |
| --- | --- |
| `is_port/1` true for ports, `port_to_list/1`, `list_to_port/1`, `ports/0` | 57B |
| `port_info/1,2` (`name`, `links`, `id`, `connected`, `input`, `output`, `os_pid`, `monitors`, `monitored_by`, `registered_name`) | 57B |
| `port_close/1`, `port_connect/2`, `port_command/2,3` | 57B |
| `Port ! {Pid, {command, Data}}`, `{Pid, close}`, `{Pid, {connect, New}}` | 57B |
| `link/1`, `unlink/1`, `monitor(port, P)`, `exit/2` on ports; `register/2` of a port | 57B |
| `port_control/3`, `port_call/3` | 57B (badarg for drivers without control); used by the 57E/57F drivers |
| `open_port({fd, In, Out}, Opts)` | 57B output; input from 57C |
| `open_port({spawn, Command} \| {spawn_executable, File}, Opts)` | 57D |
| Internal drivers of the project library (`file`, sockets) | 57E, 57F |

The builtins replace the step-53 `[ports] notimpl` diagnostics; feature
`ports` becomes implemented in 57B. Errors follow OTP: `badarg` for a closed
or invalid port and bad arguments, the POSIX reason (`enoent`, `eacces`) as
an `error` for an open that fails.

Messages from a port go to its connected process:
`{Port, {data, Data}}`, `{Port, eof}` (option `eof`),
`{Port, {exit_status, Status}}` (option `exit_status`), `{Port, closed}`
(after `{Pid, close}`), `{Port, connected}` (to the old owner after a
connect).

## Data modes and options

| Option | Effect |
| --- | --- |
| `stream` (default), `{packet, N}` (N = 1, 2, 4) | Bytes as they arrive, or messages framed by an N-byte big-endian length that output also gets |
| `{line, L}` | `{eol, Line}` per line, `{noeol, Part}` for parts longer than `L` or an unterminated end |
| `binary` | Data as binaries instead of byte lists |
| `eof` | `{Port, eof}` at end of input; the port stays open until closed |
| `exit_status` | `{Port, {exit_status, S}}` when the program exits (spawn ports) |
| `use_stdio` (default), `nouse_stdio`, `stderr_to_stdout`, `in`, `out`, `hide` | As in OTP (`hide` has no effect) |
| `{args, List}`, `{arg0, A}`, `{env, Env}`, `{cd, Dir}` | Spawn ports (57D) |
| `{busy_limits_port, {Low, High} \| disabled}` | Queued output bytes that make the port busy ([busy ports](#busy-ports), 57G3) |

Without `eof`, end of input closes the port with reason `normal`, after the
`exit_status` message when one was asked for. Unknown options are `badarg`.

## Drivers

A driver is a C++ object behind one port (`runtime/src/ports/`): it opens the
resource, accepts output (`port_command`), answers `port_control/3` when it
supports control, reports input and errors as events and closes. Drivers:

- `fd`: existing descriptors, output written synchronously (57B), input
  through the I/O thread (57C).
- `spawn`: a child program with its stdin and stdout as pipes (57D).
- `file`: an open file of the project library's `file` module; its operations
  are synchronous `port_control/3` calls that may block the worker running
  the caller for the duration of the disk I/O, as OTP's dirty I/O schedulers
  do (57E).
- `tcp_inet`, `udp_inet`: sockets of the project library's `gen_tcp`,
  `gen_udp` and `inet` on Boost.Asio; connects, accepts and receives are
  asynchronous (57F, [sockets](#sockets-57f)).

The internal drivers of `file` and the sockets are opened with
`{spawn_driver, Name}` under Clause names, and their `port_control/3`
operations are a Clause protocol: programs use the library modules, not
OTP's `prim_inet`/`efile` protocols.

## I/O thread

One I/O thread per runtime (`detail::Reactor`, `runtime/src/ports/reactor.hpp`,
step 57G1) runs a Boost.Asio `io_context`: an I/O completion port on Windows,
`epoll` on Linux, `kqueue` on macOS. The first port that needs it starts it;
it serves every port kind, so a port costs no thread of its own:

- Pipes of spawned programs: overlapped named pipes on Windows
  (`windows::stream_handle`; anonymous pipes cannot be overlapped), plain
  pipes elsewhere (`posix::stream_descriptor`). Output is queued and written
  in order by asynchronous writes.
- `fd` input on Linux and macOS: the thread waits until the descriptor is
  readable, then one `read()` takes what is there, so the program's own
  descriptors keep their blocking mode; a regular file, which cannot be
  waited for, is read at once.
- Program exits: on Windows the system's wait thread pool
  (`RegisterWaitForSingleObject`, as Asio's `object_handle` uses) waits for
  the process handle and the exit is reported on the I/O thread; on Linux and
  macOS a `SIGCHLD` handler (`signal_set`) and `waitpid(WNOHANG)` reap every
  watched program, also after its port closed.
- Sockets ([sockets](#sockets-57f)).
- Exception: an `fd` input handle on Windows (a console or an inherited
  anonymous pipe) cannot be overlapped, so it is read by a blocking thread of
  its own, as libuv and ERTS do; closing the port cancels the read
  (`CancelSynchronousIo`) and lets the thread go.

`runtime/src/ports/io*.cpp` hold the port I/O (`detail::IoService`): its
methods only post work to the I/O thread, where all I/O state lives.

- The I/O thread hands input over as it was read: raw bytes, the end of
  input, a read error, a program's exit status. Socket events (messages,
  accepted connections) are handed over the same way.

## Port tasks

Step 57G2. A port is scheduled like a process: what the I/O thread hands
over waits in the port, and the port waits in the executor's port queue
until a scheduler worker runs its task.

- Workers take a port task and a process time slice in turn while both
  queues have work, so a port flooding input cannot starve processes, and
  processes cannot starve ports; an idle worker takes whatever is queued.
- A task runs under the executor mutex for `PORT_TASK_REDUCTIONS` (a time
  slice's 4,000): each message it delivers costs 100 reductions plus one per
  64 bytes it carries. A port with work left is queued again at the back.
- The task frames input (`InputDecoder`): stream input arrives in the chunks
  reads return; `{packet, N}` holds bytes until a whole packet arrived (an
  incomplete packet at end of input is dropped, as in OTP); `{line, L}`
  splits at `\n`, sends a line longer than `L` as `{noeol, Part}` pieces and
  an unterminated end as `{noeol, Rest}`. `port_info(P, input)` counts every
  byte read, newlines and packet headers included, when it is read.
- End of input sends `{Port, eof}` with option `eof`, else closes the port
  with reason `normal`; a read error closes it with `eio`.
- A message becomes a message to its process at once when that process does
  not run on a worker (waking it like any message), else when its time slice
  ends, so a running process's heap is never touched by another thread.
- Commands, closes, connects and exit signals sent to a port act at once on
  the sender's worker, as ERTS does for a port that is not busy: an `fd` port
  writes its output at once on the caller's worker; the pipe and socket
  drivers queue output and let the I/O thread write it (57D, 57F, 57G1); a
  busy port suspends the sender ([busy ports](#busy-ports)).

The prototype `tests/prototypes/poller/` (`run.py --wsl`) shows the wakeup
on Windows (completion port) and WSL Linux (`poll()`): an idle scheduler
thread wakes 9–91 µs after input, and shutdown stops the I/O thread without
input.

At program end every port is closed (child programs see end of input; they
are not killed, as in OTP) and the I/O thread stops: it is joined outside the
executor mutex, because a delivery in progress takes it; a Windows `fd`
reader blocked in a read that cannot be cancelled is detached and delivers
nothing more.

## Busy ports

Step 57G3. Output a driver queues for the I/O thread (spawned programs'
pipes) counts against the port's busy limits, OTP's `busy_limits_port`
(defaults: high 8,192 bytes, low 4,096; `{busy_limits_port, {Low, High}}`
or `disabled` as an `open_port/2` option, limits at least 1, a low limit
above the high one lowered to it):

- A write that takes the queued output to the high limit or above still
  goes out and makes the port busy; it stays busy until the I/O thread has
  written enough that less than the low limit is queued.
- `port_command/2` and `Port ! {Pid, {command, Data}}` to a busy port
  suspend the sender, which writes nothing; once the port is no longer busy,
  or closes, the sender runs its builtin again (a closed port then gives
  `badarg`, as in OTP). A suspended process still receives exit signals.
- `port_command/3` with `nosuspend` returns `false` for a busy port and
  writes nothing; `force` raises `notsup`, as OTP's spawn and `fd` drivers
  do not allow it.
- `port_info(P, queue_size)` is the output queued and not written yet.
- `fd` ports write at once and socket output goes through `port_control/3`,
  so neither is ever busy.

A port also bounds the input it holds:

- When more than 64 KiB of raw input waits for the port's task, the I/O
  thread stops reading the port (a program writing to it then blocks, as on
  a full pipe) until the task has taken it below 32 KiB.
- While the connected process can run (it is running or queued, not waiting
  in a `receive`, suspended or blocked) and already holds 1,024 messages
  (or has that many port signals waiting for its time slice to end), the
  port's task delivers nothing more until that process's slice has ended.
  A process waiting in a `receive` always gets the input, so a receive for a
  later message of the port cannot wait for ever.

## Subprocesses

Step 57D. `open_port({spawn, Command}, Options)` and
`open_port({spawn_executable, File}, Options)` start a program whose stdin and
stdout are pipes of the port:

- `{spawn, Command}`: on Linux and macOS `/bin/sh -c Command`; on Windows
  `CreateProcessW` with `Command` as the command line (it finds the program
  on the search path). `{spawn_executable, File}` runs `File` without a
  shell, with argv[0] `{arg0, A}` (default `File`) and `{args, List}`; on
  Windows arguments are quoted by the rules of `CommandLineToArgvW`.
- `{env, [{Name, Value | false}]}` sets or removes variables of the child,
  `{cd, Dir}` its directory, `stderr_to_stdout` merges stderr into the port;
  otherwise the child shares the program's stderr. `in` opens no stdin pipe
  and `out` no stdout pipe (the null device instead). `hide` and
  `overlapped_io` have no effect.
- A program that cannot be started raises `error:Reason` with the POSIX
  reason (`enoent`, `eacces`, `enoexec`); bad names and options raise
  `badarg`.
- Output is queued and written by the I/O thread, with the port's busy
  limits ([busy ports](#busy-ports)); closing the port lets it finish what is
  queued, then closes the program's stdin. The program
  is never killed; it usually ends at end of input.
- Option `exit_status` sends `{Port, {exit_status, S}}` once the program has
  exited (its exit code; on Linux and macOS 128 plus the signal for a program
  a signal ended) and before `{Port, eof}` or the close at end of input; a
  port reading nothing reports it when the program exits, then closes.
- `port_info(P, os_pid)` is the program's process id; `name` is the command or
  file.
- The library's `os:cmd/1` runs `Command` with `COMSPEC /c` on Windows (`cmd`
  when unset) or `/bin/sh -c`, collects stdout and stderr until the program
  closes them and returns the bytes as a list; `os:type/0` is `{win32, nt}`,
  `{unix, linux}` or `{unix, darwin}`; `os:getenv/1` returns a string or
  `false`.

## Standard I/O and files

Step 57E.

- `io:format`, `io:put_chars` and `erlang:display` keep writing standard
  output directly ([io](io.md)). Standard input is read by one library
  server process, registered as `clause_stdin` and started on first use,
  which owns an `{fd, 0, 1}` port: `io:get_line/1,2` writes the prompt, then
  returns the next line with its newline, the rest of the input without one
  at its end, then `eof`; `io:get_chars/2,3` returns up to `N` characters,
  then `eof`. Requests of several processes are answered in arrival order.
  Input is returned as bytes (one list element per byte).
- The library's `file` module drives the runtime's file driver,
  `{spawn_driver, "clause_file"}`, with `port_control/3`. Its operations
  (`ports/file.cpp`: open, read, write, position, read_line, close, and the
  path operations read_file, write_file, delete, rename, list_dir, make_dir,
  del_dir) are synchronous system calls on the caller's worker, outside the
  executor mutex; their replies start with a status byte (0 ok, 1 error and
  its POSIX reason, 2 end of file). The protocol is Clause's own.
- `file:open/2` returns an I/O server pid that owns a file port and is linked
  to the opener (modes `read`, `write`, `append`, `exclusive`, `binary`;
  other modes are ignored, as OTP ignores options it does not use).
  `read/2`, `read_line/1`, `write/2`, `position/2` and `io:get_line/2`,
  `io:get_chars/3` on it are requests to that server; after `close/1`
  requests return `{error, terminated}` and `close/1` stays `ok`.
- Errors follow OTP: `{error, enoent}`, `eexist`, `eisdir` (a directory
  opened or read as a file), `einval` (a negative position), `ebadf`
  (reading a write-only or writing a read-only file), `badarg` for bad names
  and data. File names are strings, binaries or atoms, encoded as UTF-8.
- A program-referenced module that is a builtin of the catalog (`io:format`)
  no longer pulls the library module of that name into the program; only a
  call of one of its Erlang functions does.

## Sockets (57F)

A socket is a port, as with OTP's `inet_drv` backend: `is_port(Socket)` is
true and active-mode messages are `{tcp, Socket, Data}`,
`{tcp_closed, Socket}`, `{tcp_error, Socket, Reason}`,
`{udp, Socket, Address, Port, Data}`. The subset: `gen_tcp:connect/3,4`,
`listen/2`, `accept/1,2`, `send/2`, `recv/2,3`, `close/1`,
`controlling_process/2`, `shutdown/2`; `gen_udp:open/1,2`, `send/4`,
`recv/2,3`, `close/1`; `inet:setopts/2`, `inet:port/1`, `inet:peername/1`,
`inet:sockname/1`; modes `{active, true | false | once}`, `binary`/`list`,
`{packet, 0 | 1 | 2 | 4 | raw}`, `{reuseaddr, Bool}`, `{backlog, N}`,
`{ip, Address}`/`{ifaddr, Address}`, `inet`/`inet6`; IPv4 and IPv6 addresses
as tuples, host names as strings or atoms (`loopback` included). The tuning
options `nodelay`, `keepalive`, `send_timeout`, `send_timeout_close`,
`delay_send` and `exit_on_close` are accepted and not applied; any other
option is `exit(badarg)`, as for an invalid one in OTP.

Implementation (`runtime/src/ports/sockets.cpp`):

- The runtime's I/O thread ([I/O thread](#io-thread)) serves the sockets;
  every socket's state lives on that thread. The library opens a port with
  `{spawn_driver, "tcp_inet" | "udp_inet"}` and drives it with
  `port_control/3`; the calling worker posts the operation to the I/O
  thread and waits for its synchronous reply (status byte 0 and a result, or
  1 and a POSIX reason).
- Operations that wait (connect, accept, recv) answer later with a message
  `{clause_socket, Socket, Reply}` to their caller, so the caller blocks in
  an ordinary `receive` and other processes keep running. A timeout cancels
  the request; the cancellation itself answers `cancelled` after anything
  the socket sent before, so a reply that won the race is returned instead
  of lost.
- Socket messages are described off-heap (`ports/value.hpp`) and built in the
  receiver's heap when delivered, under the executor rules of
  [I/O thread](#io-thread). An accepted connection becomes a new port owned
  by and linked to the caller of `accept`, with the listening socket's mode.
- `controlling_process/2` stops active delivery, moves the socket's messages
  already sent to the old owner to the new one, then reconnects the port, as
  OTP's `inet` does; only the owner may call it (`{error, not_owner}`).
- Output is queued without a cap and written in order on the I/O thread.
  Closing a port sends what is queued, then closes the connection gracefully
  (FIN); a closed port answers every waiting caller `{error, closed}`. Name
  lookup runs on the caller's worker, as it blocks.
- Socket ports are scheduled like other ports ([port tasks](#port-tasks)): a
  socket's messages wait in its port until the port's task delivers them.

## Not provided

Linked-in drivers and NIFs, `erlang:open_port({spawn_driver, Name})` for OTP
driver names, distribution ports, `port_call/3` on the provided drivers
other than the library's own protocol, busy socket ports, and the
`overlapped_io`, `parallelism` and `busy_limits_msgq` options.
