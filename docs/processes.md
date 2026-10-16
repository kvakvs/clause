# Processes

## Executor

One runtime runs its processes on its scheduler workers ([workers](#workers),
[execution model](execution-model.md)). Startup makes the entry function's call
the first process, the main process, and runs the executor until it ends:

- Runnable processes wait in their worker's first-in, first-out run queue
  ([workers](#workers)). A worker runs the first one for a time slice of 4,000
  reductions (OTP's `CONTEXT_REDS`), then puts it back at the end of its queue,
  unless it ended.
- Every function entry spends one reduction: calls, tail calls, fun and dynamic
  calls, and builtins. With none left, the entry does not happen: the process
  records the function it was entering, keeps its arguments in its registers
  (they stay collection roots) and returns to the executor, which later resumes
  it by repeating the entry ([execution model](execution-model.md#operations)).
  Each comprehension loop head spends one too, so a loop without calls also
  yields. Builtins whose work grows with a list or binary argument run in
  portions and yield between them ([portions](builtins.md#portions)).
- Each process owns its heap and stack. Arguments of a new process are copied
  into its heap
  ([copying between heaps](runtime-heap.md#copying-between-heaps)).
- A process ends when its first call returns or raises. An ended process's
  context, heap and stack are released at once; its pid stays a valid term and
  `is_process_alive/1` returns false for it.
- A process waiting in a `receive` is in no queue; a send to it queues it again
  ([receive](#receive)).
- When the main process ends, the program ends with its outcome
  ([executables](executables.md)): processes still queued or waiting are
  released without running further, as an OTP escript halts when `main/1`
  returns.
- An exit signal that ends the main process ends the program
  ([exit signals](#exit-signals)).
- `erlang:halt/0,1` in any process ends the program with its status. A runtime
  failure in any process (memory exhausted, an optional cap exceeded, an
  internal error) ends the program as a runtime failure (exit 70). An Erlang
  exception ends only the process that raised it ([exits](#exits)).
- Host invocations of exported functions (`CLAUSE_invoke_v1`) run their function
  in the calling context to completion, resuming it after each yield without
  running other processes; programs started by `CLAUSE_main_v1` use the
  executor.

## Workers

The executor runs processes on `RuntimeOptions::schedulers` worker threads: the
thread that started the program and one more thread per further worker.
Programs take the count from `--schedulers N` (1 to 1,024,
[runtime options](executables.md#runtime-options)); the default is one worker
per logical processor, as OTP's `+S`.

- Each worker has its own run queue, as each ERTS scheduler does: a process
  stays with the worker it last ran on, so its heap stays in that core's caches.
  A spawned process starts on its parent's worker. Idle workers sleep until a
  process is queued for them or there is work to steal; the timer thread queues
  processes whose receive timeout expires
  ([receive timeouts](#receive-timeouts)).
- Processes migrate as on ERTS, which balances its run queues periodically:
  every 4 slices a worker compares its queue with its neighbor's and takes one
  process from it when the neighbor holds at least two more. A worker with an
  empty queue steals one process from the longest queue holding at least two, or
  the only process of a sleeping worker, which it runs before that worker wakes.
- Wakeups are lazy, as ERTS keeps processes that talk to each other on one
  scheduler: a running process that wakes a process of a sleeping, empty
  worker queues it on its own worker, which takes it when its slice ends;
  another worker is woken only when a queue then holds more than one process.
  The timer and I/O threads, port tasks and workers between slices queue a
  process for its own worker, or for an idle worker when its own one is busy.
- One executor mutex guards the run queues, the waiting processes, the timers,
  the registered names, the links and monitors of every process, and every
  process that is not running. A running process's heap, stack, mailbox and
  failure channel belong to its worker alone; Erlang code runs without the lock.
- Links, monitors and names change under the lock at once, also for a process
  running elsewhere. A message to a process running on another worker cannot go
  into its heap yet: it is copied into a buffer of its own and delivered, after
  earlier buffered ones, when the receiver's slice ends
  ([message buffers](runtime-heap.md#message-buffers)). An exit signal of
  `exit/2` or `exit_signal/2` to such a process does nothing and its process
  ends its slice (`builtins::Blocked`); it waits until the target's slice ends,
  then runs first and repeats the builtin with the same arguments. The target is
  held until then, so the repeated builtin cannot find it running again. A
  process that ends with links or monitors whose processes run elsewhere is
  finished likewise once their slices end.
- So signals keep their order: a message buffered before a blocked exit signal
  of the same sender reaches the receiver first, and `is_process_alive/1` after
  `exit(Pid, kill)` is false, as OTP promises for signals from the caller.
- A spawn links or monitors the new process (`spawn_link`, `spawn_monitor`)
  before any worker can run it. A spawned process may run before its parent goes
  on, so a program that monitors or links to a short-lived process after
  `spawn/1` can see `noproc`, as on a multi-scheduler OTP.
- The main process's end, a halt or a runtime failure stops the program once
  every worker has finished its current slice; then the other processes are
  released.
- Shared runtime services are synchronized ([threads](runtime.md#threads)):
  atoms, the code server, pid and port numbers and the memory account.
- Output of different processes interleaves in the order their writes happen.
  Each `io:format` and `erlang:display` call writes its text at once.

Wakeups and shutdown need no further mechanism, because every change of a
process's scheduling state happens under the executor mutex:

- A message, `'EXIT'` or `'DOWN'` for a waiting process queues it in the same
  critical section that delivers it, waking a worker as above. A process that
  is still in the slice in which it began to wait cannot get a message then (its
  senders wait for the slice to end), so the delivery always finds it parked.
- A worker that finds no runnable process sleeps until another worker, a send or
  the timer thread queues one, or the program ends. Workers never read the clock
  for timers; parking a process with a timeout earlier than every other wakes
  only the timer thread.
- A message and an expiring timeout of one receive may race: whichever comes
  first queues the process and cancels the other, and the receive takes a
  message that arrived before it resumed, as OTP's does.
- An exit signal to a waiting, queued, held or blocked process takes it out of
  wherever it is before it is finished.
- When the program ends, every worker finishes its current slice and stops;
  `run()` returns after joining them, and every other process the executor
  started is released, so the runtime shuts down with no context left.
- The OTP golden `executables_wakeups` stresses this with 1, 2, 4 and all
  workers: receivers whose 0–2 ms timeouts race their senders' messages, a chain
  of 16 linked spinning processes ended by one exit signal with every member
  monitored, monitored processes ending as their watcher wakes, a program ending
  while processes spin, wait and flood each other, and a halt in another
  process.

### Receive timeouts

Timers live in a hierarchical hashed timer wheel, as ERTS keeps them
(`scheduler/timer_wheel`):

- Ticks are milliseconds since the executor started. Six levels of 64 slots
  cover 2^36 ticks, more than the longest timeout (4,294,967,295 ms). A timer
  sits in the slot of the highest 6-bit digit in which its tick differs from the
  wheel's current tick; occupied slots are bitmaps per level.
- Arming (a waiting process is parked) and cancelling (a message, an exit signal
  or the process's end comes first) unlink or link an intrusive list node kept
  with the process's scheduling state: constant time. A deadline becomes the
  tick at or after it, so no timeout fires early.
- One timer thread per run sleeps until the next occupied slot (the first
  occupied slot after the current one on any level), reads the clock once when
  it wakes and advances the wheel to that tick: higher-level slots reached at
  their start move their timers down, and the level-0 slot fires. Only slots
  whose time has come are consulted; waiting processes are never scanned.
  Expired processes are queued in tick order.
- The wheel is shared by the workers under the executor mutex. `after 0` never
  waits and `infinity` never arms a timer.
- `--profile` reports the time slices and the timer thread's clock readings
  ([profiling](profiling.md)). Timers fire only as precisely as the host's timed
  waits (on Windows the default timer resolution).
- `runtime_timer_wheel` arms 20,000 timers over every level, cancels 90% and
  advances in uneven steps: each remaining timer fires once, in the first
  advance that reaches its tick, in tick order; no cancelled one fires.

## Exits

A process other than the main one ends with an exit reason, as in OTP
(`detail::exit_reason`, `process/exits`). Exit signals carry it to linked
processes ([links](#links)) and `'DOWN'` messages to monitoring ones
([monitors](#monitors)); error reports show it.

| How the process ends | Exit reason | Error report |
| --- | --- | --- |
| Its first call returns | `normal` | None |
| `exit(Reason)` (also `normal`, `kill`) | `Reason` | None |
| An error (`error/1,2,3`, `badarith`, `{badmatch, V}`, `undef`, ...) | `{Reason, Stack}` | Yes |
| An uncaught `throw(Value)` | `{{nocatch, Value}, Stack}` | Yes |
| An exit signal ends it ([exit signals](#exit-signals)) | The signal's reason, `killed` for `exit(Pid, kill)` | None |

An error report is written on stderr when the process ends, after flushing
standard output, in the format of OTP's default logger handler:

```text
=ERROR REPORT==== 8-Oct-2026::03:42:15.983000 ===
Error in process <0.8.0> with exit value:
{boom,[{crash_reports,'-main/1-fun-6-',0,[]}]}

```

The header has the local time; the reason is laid out as `~p` does. The main
process does not write one: its uncaught exception is the program's
([executables](executables.md)).

## Messages

`Dest ! Msg` and `erlang:send(Dest, Msg)` evaluate `Dest` first and return
`Msg`.

| `Dest` | Effect |
| --- | --- |
| A pid of a live process | `Msg` is copied flat into the receiver's heap ([copying between heaps](runtime-heap.md#copying-between-heaps)), as OTP copies messages, and appended to its signal inbox |
| A pid of a process that has ended | Nothing; the send succeeds |
| A [registered name](#registered-names) | As for its pid; `badarg` when no live process has the name |
| `{Name, nonode@nohost}` of two atoms | As for the pid registered as `Name`; nothing when there is none |
| `{Name, Node}` for any other node | Nothing (there are no other nodes) |
| A reference | As for the owner of an active [alias](#aliases); nothing otherwise |
| Anything else | `badarg` |

- Messages of one sender arrive in the order it sent them; a self-send is a
  message like any other.
- Every message is a collection root of its receiver until a receive takes it
  ([runtime heap](runtime-heap.md#roots-and-safe-points)).
- The receiver's mailbox (`Mailbox`) keeps a signal inbox, where sends append,
  and a message queue that receive scans from a saved position: arrived messages
  move behind the queue when a receive examines them.
- When the receiver's heap refuses the copy (an optional cap such as
  `--max-heap`, or the host out of memory), nothing is delivered and the sending
  process fails as a runtime failure (exit 70), like any process that exceeds a
  cap.

## Receive

`receive` selects among its clauses like `case`, over the messages of the
mailbox:

- The mailbox scan starts at the oldest message. Each message is matched against
  the clauses in order (patterns and guards, which may read bindings from before
  the `receive`); the first message some clause matches is removed and that
  clause's body runs. Messages no clause matches stay in the mailbox in their
  order; the next receive starts at the oldest message again.
- When every message has been examined, the process waits
  (`CLAUSE_wait_frame_v1`, a builtin entered like a call): it leaves the run
  queue until a send delivers a message to it, then the scan continues with the
  messages that arrived. Waiting processes keep their frames and messages as
  collection roots and collect when resumed
  ([waiting processes](runtime-heap.md#waiting-and-suspended-processes)).
- Names bound in every clause, and in the `after` body when there is one, are
  exported after the `receive`, as for `case`; a call in a clause or `after`
  body's tail position is a tail call, so a server loop runs in constant stack.
- `after T -> Body`: `T` is evaluated first, before the scan. When the receive
  would wait, `T` must be `infinity` or an integer in 0..4294967295
  (milliseconds), else `error:timeout_value`, after which the next receive scans
  from the oldest message; a message that matches at once never
  checks it, as in OTP. `after 0` runs `Body` as soon as every message has been
  examined. A finite timeout starts when the receive first waits and is not
  restarted by messages that match no clause; when it expires, `Body` runs from
  the bindings before the receive and the next receive scans from the oldest
  message. A message that arrives before the timeout expires is taken. A receive
  with only `after` is a sleep (`timer:sleep/1`'s idiom).
- Generated code: a loop head peeks at the next unexamined message
  (`CLAUSE_receive_v1` `peek`, into a root slot), clause selection takes a
  matched message (`take`) before its body or skips an unmatched one (`skip`)
  and loops; with no message left the loop enters the wait with the timeout,
  which answers `true` (scan again) or `false` (timed out: `restart`, then the
  `after` body).
- The executor keeps a timer per waiting process with a finite timeout in a
  timer wheel ([receive timeouts](#receive-timeouts)): an expired one queues the
  process again. Timeouts are measured on a monotonic clock in milliseconds and
  never fire early.
- When every process waits without a timeout for a message nothing can send, the
  program waits forever, as OTP's does. A host invocation (`CLAUSE_invoke_v1`)
  that would wait fails with `busy` instead: no other process runs during it.

## Links

A link connects two processes both ways (`Signals` in each context: the linked
pids in link order and the `trap_exit` flag).

- `link(Pid)` links the caller to a live process and returns `true`; linking to
  itself or again does nothing. For a process that has ended it raises
  `error:noproc`, or, when the caller traps exits, returns `true` and sends the
  caller `{'EXIT', Pid, noproc}`, as OTP's local `link/1` does.
- `unlink(Pid)` removes the link on both sides; the link has no effect after it
  returns. `true` also when there was no link.
- `spawn_link/1,3` link the new process to the caller before it runs.
- When a process ends, every linked process gets an exit signal with its exit
  reason ([exits](#exits)) and the link is gone. Linked processes are signalled
  in the order the links were made.

## Monitors

A monitor is one-way: the monitoring process holds it (by reference, in
`Signals`) and the monitored process keeps the reference and the monitoring pid,
so it can send the message when it ends.

- `monitor(process, Pid)` returns a new reference. When the process ends, the
  caller gets `{'DOWN', Ref, process, Pid, Reason}` with its exit reason
  ([exits](#exits)); for a process that has already ended it gets the message at
  once with reason `noproc`. Monitoring itself creates nothing. Every call makes
  a separate monitor with its own message; the messages of one process go out in
  the order the monitors were made.
- `demonitor(Ref)` stops the monitor: no `'DOWN'` of it arrives afterwards. It
  returns `true`, also for a reference that is not an active monitor of the
  caller.
- `demonitor(Ref, Options)`: `info` returns whether the monitor was still
  active; `flush` removes the oldest `{_, Ref, _, _, _}` message when it was not
  (its `'DOWN'` is already in the mailbox), as OTP does.
- `spawn_monitor/1,3` return `{Pid, Ref}`, the monitor made before the new
  process runs.
- A process that ends drops the monitors it holds.
- `monitor(process, Name)` and `monitor(process, {Name, nonode@nohost})` monitor
  the process registered as `Name` (`noproc` at once when there is none); its
  `'DOWN'` names `{Name, nonode@nohost}` instead of the pid. `{Name, Node}` for
  another node is `badarg`.
- `monitor(port, Port | Name)` monitors a port
  ([ports](ports.md#port-table-and-ownership)):
  `{'DOWN', Ref, port, Port, Reason}` when it closes. A pid for `port`, or a
  port for `process`, is `badarg`.

## Registered names

The executor keeps one table of names (atoms) to pids, and each process its own
name (`Signals::name`).

- `register(Name, Pid)` names a live process: `badarg` for `undefined`, a name
  in use, a process that already has a name, a process that has ended, or a
  `Pid` that is not a pid. A process may register itself.
- `unregister(Name)` releases the name (`badarg` when no process has it);
  `whereis(Name)` is the pid or `undefined`; `registered()` lists the names in
  atom-table order (OTP's order is unspecified too).
- A process's name is released as it ends, before its links and monitors are
  signalled, so a `'DOWN'` or `'EXIT'` receiver can register the name again, as
  in OTP.

## Process dictionary

`put/2`, `get/0,1`, `get_keys/0,1` and `erase/0,1` behave as OTP's: `put/2` and
`erase/1` return the previous value or `undefined`, `erase/0` returns every
`{Key, Value}` and empties the dictionary, keys match with exact equality (`1`
and `1.0` are two keys).

- Each process has its own dictionary; a spawned process starts empty.
- Entries are words of the process heap held in `ProcessDictionary`
  ([process_dictionary.hpp](../runtime/include/process_dictionary.hpp)) and
  visited as collection roots ([roots](runtime-heap.md#roots-and-safe-points));
  they are released with the process.
- The order of `get/0` and `get_keys/0,1` is unspecified, as in OTP. Lookup
  scans the entries linearly; immediate keys compare by word. `put/2`, `get/1`
  and `erase/1` take and return the entry words as they are, admitting no term.

## Timers

`start_timer/3,4` and `send_after/3,4` (`builtins/erlang/timers.cpp`,
`scheduler/timers.cpp`) arm a timer in the executor's wheel (the one
[receive timeouts](#receive-timeouts) use) that sends `Msg`, or
`{timeout, TimerRef, Msg}`, to a pid or registered name when it fires.

- The message is copied when the timer starts into a `MessageBuffer`
  ([message buffers](runtime-heap.md#message-buffers)), a heap block of the
  timer's own, and copied again into the receiver's heap when it fires, as a
  send copies: any term, local funs included, travels unchanged.
- A receiver running on a worker gets the message when its slice ends. A
  registered name is resolved when the timer fires; a name nobody holds, or a
  pid of an ended process, drops the message. Timers sent to a pid are released
  when that process ends.
- Time is relative milliseconds in 0..2^49-1, or with `{abs, true}` an absolute
  `erlang:monotonic_time(millisecond)`; anything else is `badarg`. A deadline
  past the wheel's reach is armed again when that slot comes.
- `cancel_timer/1,2` and `read_timer/1,2` return the milliseconds left or
  `false`; `{async, true}` returns `ok` and sends `{cancel_timer, Ref, Left}`
  (`{read_timer, ...}`), `{info, false}` returns `ok` without a result.

## Introspection

`process_info/1,2` (`builtins/erlang/introspection.cpp`,
`scheduler/introspection.cpp`) reads another process under the executor's lock
and copies its terms (messages, dictionary) into the caller's heap; a process
running on another worker is read once its slice ends. A pid of no live process
gives `undefined`.

- Items: `registered_name` (`[]` alone when unregistered), `status` (`running`,
  `runnable`, `waiting`), `message_queue_len`, `messages`, `links`, `monitors`,
  `monitored_by`, `dictionary`, `trap_exit`, `group_leader`, `heap_size`,
  `total_heap_size`, `stack_size`, `memory` (words of the heap, its fragments
  and off-heap binaries, and the stack), `reductions` (0), `priority`
  (`normal`), `error_handler`, `garbage_collection`. `process_info/1` lists them
  from `status` to `garbage_collection`.
- OTP items Clause does not keep (`current_function`, `initial_call`,
  `current_stacktrace`, ...) raise `error:{notsup, Item}`; unknown items are
  `badarg`.
- `processes/0` lists live pids in creation order. A spawned process inherits
  its parent's group leader; the first process leads itself. `group_leader/2` of
  a pid of no live process is `badarg`.
- `garbage_collect/0,1,2` asks for a collection at the target's next safepoint
  and returns `true` (`false` for no live process); with `{async, RequestId}` it
  returns `async` and sends `{garbage_collect, RequestId, Result}`. `yield/0`
  ends the time slice at the next function entry.

## Spawn options

Sources: `builtins/erlang/spawning.cpp`, `scheduler/aliases.cpp`.

- `spawn_opt/2..5` take `link`, `monitor`, `{monitor, MonitorOptions}` and the
  tuning options `{priority, P}`, `{fullsweep_after, N}`, `{min_heap_size, N}`,
  `{min_bin_vheap_size, N}`, `{max_heap_size, S}`, `{message_queue_data, M}`,
  `{async_dist, B}`; `{min_heap_size, N}` sets the child's
  [minimum heap](runtime-heap.md#sizing-and-budget), the other tuning options
  are checked and accepted, not applied. A
  node other than `nonode@nohost` is `badarg`.
- `spawn_request/1..5` spawns at once and sends `{Tag, ReqId, ok, Pid}`
  (`{Tag, ReqId, error, Reason}` on failure, `badopt` for an invalid option) as
  `{reply_tag, Tag}` and `{reply, yes | no | error_only | success_only}` ask;
  with `monitor` the request id is the monitor reference.
  `spawn_request_abandon/1` returns `false`: the reply is already sent.
- `hibernate(M, F, A)` waits until the mailbox holds a message, then calls
  `M:F(A...)` in place of the call; the stack is kept, so the result returns to
  the caller.

## Aliases

An alias is a reference messages can be sent to (`Alias ! Msg`): `alias/0,1`
(`explicit_unalias`, or `reply` for one message), or a monitor made with
`monitor/3`'s `{alias, explicit_unalias | demonitor | reply_demonitor}`
(`demonitor` ends with the monitor, `reply_demonitor` also ends the monitor on
its first message). `unalias/1` ends an alias of the caller. A send to a
reference that is no active alias, or whose owner ended, is dropped. A reply
alias is consumed only when its message is delivered, after any wait for a busy
owner. `monitor/3`'s `{tag, Tag}` replaces `'DOWN'` in the monitor message; port
monitors take no options.

## Nodes

A Clause program is one node that is not distributed, `nonode@nohost`, and
answers as such an OTP node (`builtins/erlang/node.cpp`): `nodes/0` is `[]`,
`nodes(this | known)` `[nonode@nohost]`, other types `[]`; `nodes/2`
gives `{nonode@nohost, #{connection_id => undefined, node_type =>
this}}` for the keys asked. `is_alive/0` is `false`, `monitor_node/2,3` raise
`error:notalive` and `disconnect_node/1` returns `ignored`.

## Exit signals

Every exit signal comes from a running process (`exit/2`, `exit_signal/2`,
`link/1`) or from the end of a process. The executor acts on it at once when its
target does not run on another worker, else once the target's slice ended
([workers](#workers), `scheduler/signals`): an ended target leaves the run queue
or its wait and is finished (its links signalled, its error report written, its
context released) before the sending builtin returns. A long linked chain ends
process by process without recursion.

| Signal at a process | Not trapping exits | Trapping exits |
| --- | --- | --- |
| `exit(Pid, kill)`, `exit_signal(Pid, kill)` | Ends with reason `killed` | Ends with reason `killed` |
| Reason `normal` (from a link, or sent to another process) | Nothing | `{'EXIT', From, normal}` message |
| `exit(self(), normal)` | Ends with reason `normal` (OTP's quirk) | `{'EXIT', Self, normal}` message |
| `exit_signal(self(), normal)` | Nothing | `{'EXIT', Self, normal}` message |
| Any other reason, including `kill` from a link | Ends with that reason | `{'EXIT', From, Reason}` message |

- `process_flag(trap_exit, Bool)` sets trapping and returns the previous setting
  (initially `false`); `process_flag(min_heap_size, N)` sets the
  [minimum heap](runtime-heap.md#sizing-and-budget) from the next collection and
  returns the previous one.
- A process ended by a signal it sent itself (`exit(self(), kill)`) unwinds past
  every `catch`, `try` handler and `after` body: the signal is not an exception
  (`CallError::exited` in the failure channel).
- `exit/2` and `exit_signal/2` return `true`; to a process that has ended they
  do nothing. A reference destination does nothing either, even an active
  [alias](#aliases).
- An exit signal that ends the main process ends the program like an uncaught
  `exit` ([exit status](executables.md#exit-status)): reason `normal` exits 0.
- When the receiver's heap refuses an exit reason or `'EXIT'` message, the
  receiver fails as a runtime failure (exit 70).

## Ports

Ports are specified in [ports](ports.md): a port is linked to its opener, takes
part in links, monitors, exit signals and registered names like a process, and
talks to its connected process with messages.

## Builtins

| Builtin | Behavior |
| --- | --- |
| `spawn(Fun)` | `badarg` unless `Fun` is a fun; otherwise a new process calls `Fun()`, raising `{badarity, {Fun, []}}` in that process for another arity |
| `spawn(M, F, Args)` | `badarg` unless `M` and `F` are atoms and `Args` a proper list; otherwise a new process calls `M:F(Args...)`, raising `undef` in that process when no module exports it and no builtin has that name |
| `spawn_link(Fun)`, `spawn_link(M, F, Args)` | As `spawn`, and the new process is linked to the caller ([links](#links)) |
| `is_process_alive(Pid)` | `badarg` unless `Pid` is a pid; true while its process has not ended |
| `spawn_monitor(Fun)`, `spawn_monitor(M, F, Args)` | As `spawn`, returning `{Pid, Ref}` of a new [monitor](#monitors) |
| `link(Pid)`, `unlink(Pid)` | `badarg` unless `Pid` is a pid ([links](#links)) |
| `monitor(process, Item)` | `badarg` for another type or item; a reference ([monitors](#monitors)); `Item` is a pid or a registered name |
| `register(Name, Pid)`, `unregister(Name)`, `whereis(Name)`, `registered()` | See [registered names](#registered-names); `Name` must be an atom |
| `demonitor(Ref)`, `demonitor(Ref, Options)` | `badarg` unless `Ref` is a reference and `Options` a proper list of `flush` and `info` |
| `exit(Dest, Reason)`, `exit_signal(Dest, Reason)` | `badarg` unless `Dest` is a pid or reference; `true` after the [exit signal](#exit-signals) |
| `process_flag(trap_exit, Bool)`, `process_flag(min_heap_size, N)` | The previous setting; `badarg` for another flag, a non-boolean or a negative or non-integer size |
| `erlang:send(Dest, Msg)`, `Dest ! Msg` | `Msg`, after sending it ([messages](#messages)); `send/2` is not auto-imported |

The new process joins the end of its parent's worker queue
([workers](#workers)); `spawn` returns its pid at once.
