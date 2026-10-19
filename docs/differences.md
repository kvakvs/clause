# Differences from Erlang/OTP

Observable behavior where Clause knowingly differs from the pinned OTP 29
(`maint-29`, see [otp-reference.md](otp-reference.md)). Each entry links to the
contract that owns it. Remove an entry once the difference is fixed. Features
not implemented yet (reported as `notimpl`) are not listed here; see
[features.md](features.md) and the plan.

## Maps

| Difference | OTP | Clause | Owner |
| --- | --- | --- | --- |
| Map generator order (`K := V <- M`) | Flat maps (up to 32 keys) iterate in key order, but atom keys in atom-table order, which varies between VM runs; larger maps in hash order | Always canonical key order (term order) | [patterns](patterns.md#comprehensions) |
| `bad_generators` payload of a map generator in a zip group | Its iterator: `{K, V, Next}` chain ending in `none`, in OTP's order | The same chain, built in canonical key order | [patterns](patterns.md#comprehensions) |
| `maps:iterator/1,2`, `maps:next/1`, `erts_internal:map_next/3`, the order `maps` funs run in (`intersect_with/3`, `merge_with/3`, ...), `proplists:get_keys/1`, `sets` of version 2 (`to_list/1`, `fold/3`) | Internal order (atom-table order for atom keys, hash order above 32 keys); an iterator is a position or a `{K, V, Next}` chain | Canonical key order; an iterator continues over a key list | [builtins](builtins.md#erts-internal) |
| Map printing (`erlang:display/1`, `~w`, `~p`) | Internal layout order (atom-table order for atom keys, hash order above 32 keys) | Map-key order, as OTP's `~kw` | [terms](terms.md#printing) |
| Order of `digraph:vertices/1`, `edges/1`, `source_vertices/1`, `sink_vertices/1` and of `digraph_utils`' orderings and components where they start from those lists | The vertex and edge tables' hash order | Clause's hash order ([ETS](#ets)); paths and neighbour lists follow edge insertion in both | [builtins](builtins.md#digraph) |
| Order of `maps:keys/1`, `values/1`, `to_list/1`, `fold/3`, of the members `json:encode/1` writes, and of `graph`'s vertex lists and traversals | The map's internal order (atom-table order for atom keys, hash order above 32 keys) | Key order | [library](library.md) |
| Native record printing (`erlang:display/1`) | Fields in atom-table index order | Definition order, as `~w` | [native records](native-records.md#printing-and-order) |
| `==` between native records whose fields differ only as integer/float | The compiler may fold it to `=:=` (false) | Numeric comparison (true) | [native records](native-records.md#printing-and-order) |
| External native record construction of a module that is not loaded | Fails until the defining module is loaded | Every batch module is loaded at startup; a module outside the batch always fails | [native records](native-records.md#differences-kept) |
| Local fun printing (`#Fun<M.Index.Uniq>`) | Index from the compiler's lambda numbering, `Uniq` a hash of the module code | Index of the module's local funs in source order, `Uniq` always 0 | [funs](funs.md#comparison-and-printing) |
| Order of two local funs of one module | By OTP's index | By source-order index, so funs of different functions can order differently | [funs](funs.md#comparison-and-printing) |
| Anonymous fun names in stack traces (`-f/1-fun-N-`) | `N` from the compiler's numbering | `N` counts the funs of `f/1` in source order | [funs](funs.md#closures) |
| An anonymous fun in a record field default | Each construction site expands its own copy: funs from two sites are unequal | One fun for the default: funs from any construction site are equal | [funs](funs.md#closures) |
| Captured value order of funs created inside comprehensions | Free variables of the comprehension's generated function | Definition order in the enclosing function; visible only when comparing two such funs | [funs](funs.md#closures) |

## Pids, references and processes

| Difference | OTP | Clause | Owner |
| --- | --- | --- | --- |
| Pid numbers | The first user process is about `<0.80.0>`; numbers are reused after the pid table wraps | The first process is `<0.1.0>`; numbers come from one sequence and are never reused | [terms](terms.md#pids-and-references) |
| Code after `spawn(Fun)` of a fun of another arity | The compiler's type analysis may treat the code after the call as unreachable and drop it, so the caller returns the pid at once | The caller goes on; only the new process fails with `{badarity, {Fun, []}}` | [processes](processes.md#builtins) |
| Stack of a `function_clause` raised by a `lists` builtin (`lists:sublist/2`, `lists:max/1`, ...) | The top frame is the Erlang function, often an internal helper (`lists:sublist_2/2`), with its arguments | The caller is on top: builtins push no frame | [builtins](builtins.md#lists-module) |
| Last bit of `math` results (`math:erfc(1)`, ...) | The C library OTP was built with | The C library Clause programs link (Windows: `0.15729920705028513` for `erfc(1)`, OTP `0.1572992070502851`) | [builtins](builtins.md#math-module) |
| `binary:compile_pattern/1` result | `{bm, Ref}` or `{ac, Ref}` with an opaque reference | `{bm, [Pattern]}` or `{ac, Patterns}` | [builtins](builtins.md#binary-module) |
| `binary:referenced_byte_size/1` of a sub-binary | The size of the binary it refers into | Its own size | [builtins](builtins.md#binary-module) |
| Memory of replaced or erased persistent terms | Reclaimed by a literal-area collector process that has every process copy the value out, in the background | Each process copies it out at its next slice (parked ones at once, by the `put`/`erase`), then the area is freed; no collector process | [runtime](runtime.md#persistent-terms) |
| `persistent_term:info/0` and `get/0` | Include the terms OTP's `kernel` and `stdlib` store at boot (26 on OTP 29) | Only the program's own terms | [builtins](builtins.md#persistent-terms) |
| `code:is_loaded/1`, `code:which/1`, `code:all_loaded/0` of a compiled module | The `.beam` file it was loaded from | `preloaded`: every module is linked into the executable | [builtins](builtins.md#code-module) |
| `priority` option of `send/3`, `link/2`, `exit/3`, `exit_signal/3` | Priority messages and signals overtake ordinary ones | Accepted; delivered as ordinary messages and signals | [processes](processes.md#builtins) |
| `spawn/2,4`, `spawn_link/2,4`, `spawn_monitor/2,4` naming another node | A warning report `** Can not start ... on Node **` is logged | Nothing is printed; the useless pid still ends with `noconnection` | [processes](processes.md#builtins) |
| `system_flag/2` flags other than `backtrace_depth` and `min_heap_size` (`fullsweep_after`, `schedulers_online`, `multi_scheduling`, `max_heap_size`, ...) | Change the emulator | Checked and read back without an effect; `system_logger` starts as `undefined` (no logger process); `cpu_topology`, `erts_alloc`, `scheduler_bind_type` are `badarg` | [builtins](builtins.md#system-information) |
| A suspended process's timers (`receive ... after`, `send_after/3` and `start_timer/3` to its pid) | Suspended with the process until it is resumed (OTP 28, 29) | Keep running: a timeout that expires while suspended is taken once the process resumes; a timer message waits in its mailbox | [processes](processes.md#suspending-processes) |
| `process_display/2` output | OTP's stack dump with addresses and stack slots | One `Module:Function/Arity` per frame | [processes](processes.md#builtins) |
| `process_flag(save_calls, N)` | Saves the last N calls for `process_info(Pid, last_calls)` | Kept and read back; no calls are saved | [processes](processes.md#builtins) |
| `adler32_combine/3`, `crc32_combine/3` with a second size above 2^31 - 1 on Windows | Combined | `badarg` (zlib's `z_off_t` is 32 bits there) | [builtins](builtins.md#erlang-module) |
| `init:get_arguments/0`, `init:get_argument/1` | The emulator's flags: `root`, `bindir`, `progname`, `home` and every `-Flag` of the command line | Only `home`; an executable has no emulator flags | [builtins](builtins.md#init-module) |
| `init:script_name/0`, `init:script_id/0` | The boot script's path and `{"Erlang/OTP", "29"}` | The executable's absolute path and `{"Clause", "29"}` | [builtins](builtins.md#init-module) |
| `init:get_status/0`, `init:ensure_loaded/1` in the entry | `{starting, started}` and code loading while `escript`, `-s` or `-eval` code runs during boot; `{started, started}` and `not_allowed` once started | Always the started answers | [builtins](builtins.md#init-module) |
| `init:stop/0,1`, `init:reboot/0` | Return `ok`; `init` then takes the applications down and halts, while the caller keeps running | Halt at once; the call never returns | [builtins](builtins.md#init-module) |
| `init:get_argument(home)` on Windows without `HOMEDRIVE`/`HOMEPATH` | The shell's profile folder | `USERPROFILE`, normally the same folder | [builtins](builtins.md#init-module) |
| `erlang:halt(Slogan)`, `init:stop(Slogan)` | Prints the slogan, writes `erl_crash.dump` in the current directory and says so on stderr; exit status 1 | Prints the slogan on stderr, writes no crash dump; exit status 1 | [executables](executables.md) |
| `init:restart/0,1`, `init:boot/1`, `init:make_permanent/2` | Restart the system in the same node, boot it, or set the boot script of the next restart | Refused: `notsup` error, `{error, notsup}` from `make_permanent/2` | [builtins](builtins.md#init-module) |
| `atomics:new/2` arity | Any positive integer the allocator can serve; a huge one ends the node with a crash dump | 1 to 2^27, else `badarg` | [builtins](builtins.md#atomics-and-counters) |
| `counters` with `write_concurrency` | Per-scheduler counters summed on read; `info/1` memory grows with the scheduler count | One atomics array; `info/1` memory is the array's | [builtins](builtins.md#atomics-and-counters) |
| `erts_internal:term_type/1` | The internal representation: `flatmap` or `hashmap`, `heap_binary`, `refc_binary` or `sub_binary`, ... | Every map `flatmap`, every bitstring `sub_binary` | [builtins](builtins.md#erts-internal) |
| `erl_prim_loader:get_path/0` | The boot code path | `["."]` until `set_path/1` | [ports](ports.md#standard-io-and-files) |
| `file:read_link_info/1,2` of a symbolic link on Windows | The link's own reparse-point data | The target's size and times with type `symlink` | [ports](ports.md#standard-io-and-files) |
| `inet:gethostbyname/1,2` | The resolver's canonical name and aliases (`inet_res`, hosts file, native) | The name asked and no aliases; addresses from the system resolver | [ports](ports.md#sockets) |
| `socket`, `net` modules | The socket NIF API | Missing: a reference is a compile-time error; use `gen_tcp`, `gen_udp`, `inet` | [library](library.md) |
| `zlib` output lists | One binary per internal chunk, nested where a stream ended and restarted | One binary per call, `[]` when there is none; the bytes are OTP's | [builtins](builtins.md#zlib-module) |
| `zlib:safeInflate/2` and long `zlib` calls | Return after a bounded amount of output (`{continue, Output}`) and yield | Inflate all queued input in one call (`{finished, Output}`); a long call holds its scheduler thread | [builtins](builtins.md#zlib-module) |
| Long `re` matches | `re:run/3` and the global loop yield after a bounded amount of work | One match runs to its end and holds its scheduler thread; a global run yields between matches only | [builtins](builtins.md#re-module) |
| `re:compile/2` with `export`, `re:import/1` | An exported pattern `{re_exported_pattern, ...}` that another node imports | `export` is `badarg`; `import/1` raises `{not_implemented, import, 1}` | [builtins](builtins.md#re-module) |
| `re` argument errors | `badarg` raised from `re:run/3` (or `replace`, `split`) with the caller's arguments and `error_info` | `badarg` raised from the `re` helper that rejected the argument, with that helper's arguments | [builtins](builtins.md#re-module) |
| `code:is_sticky/1` | `true` for modules of `kernel`, `stdlib` and `compiler` | Always `false` | [builtins](builtins.md#code-module) |
| Output to a file device (`io:format(File, ...)`) | Characters are encoded as the file's encoding (`latin1` by default: characters above 255 are an error) | Always UTF-8 bytes | [io](io.md#calls) |
| Formatting a large term (`io:format/1,2`) | Runs in Erlang code (`io_lib`) and the group leader, so the process can be preempted while formatting | Runs to completion in one builtin call; other processes wait | [builtins](builtins.md#portions) |
| `process_flag/2` flags | `trap_exit`, `priority`, `message_queue_data`, `min_heap_size` and others | Only `trap_exit` and `min_heap_size`; every other flag raises `badarg` | [processes](processes.md#builtins) |
| Error report order of processes crashing at the same time | Logger order | The order their workers finish them | [processes](processes.md#workers) |
| Exit signals (`exit/2`, `exit_signal/2`) to a process running on another scheduler | Queued in its signal queue and handled later, while the sender goes on | The sender waits until the target's time slice ends, then acts at once; observable only as timing (messages are buffered and the sender goes on) | [processes](processes.md#workers) |
| `term_to_binary/1` of a local fun or a native record | Encoded (`NEW_FUN_EXT`, ...) | `error:notsup`; `fun M:F/A` is encoded | [builtins](builtins.md#external-term-format) |
| `term_to_binary/1` of a map with more than 32 keys | Hash order unless `deterministic` | Always key order (`deterministic`'s bytes) | [builtins](builtins.md#external-term-format) |
| `binary_to_term/1` of another node's pid, port or reference; option `local` of `term_to_binary/2` | External identities; `LOCAL_EXT` encoding | `badarg` | [builtins](builtins.md#external-term-format) |
| Timer time limit | Depends on the native time unit (about 2^49.7 ms on Windows) | Relative time 0..2^49-1 ms on every platform | [processes](processes.md#timers) |
| `unique_integer/0,1` without `positive` | May be negative | Always positive and increasing | [builtins](builtins.md#time) |
| `process_info/2` items `current_function`, `initial_call`, `current_stacktrace`, ... | Reported | `error:{notsup, Item}`; `process_info/1` omits them | [processes](processes.md#introspection) |
| `process_info/2` `reductions`; `statistics/1` `reductions`, `garbage_collection`, `io` | Counted | 0 (not kept) | [builtins](builtins.md#system-information) |
| `garbage_collect/0,1,2` | Collects before returning | Collects at the target's next safepoint | [processes](processes.md#introspection) |
| `erlang:system_info(machine)` | `"BEAM"` | `"Clause"` | [builtins](builtins.md#system-information) |
| `spawn_opt/2..5` tuning options (`priority`, `min_heap_size`, ...) | Applied | Checked and accepted, not applied | [processes](processes.md#spawn-options) |
| `hibernate/3` | Discards the stack; the process ends when `M:F` returns | Keeps the stack; `M:F`'s result returns to the caller of `hibernate/3` | [processes](processes.md#spawn-options) |
| `monitor(port, Item, Options)` with options | Accepted | `badarg` | [processes](processes.md#aliases) |
| `spawn_request_abandon/1` | `true` while the reply is pending | Always `false`: the reply is sent before `spawn_request` returns | [processes](processes.md#spawn-options) |
| `math:log2/1` last bit (Windows) | OTP's Windows build divides `log(X)` by `log(2)` (`math:log2(1.0e308)` is `1023.1538532253077`) | The C library's `log2` (`1023.1538532253076`) | [builtins](builtins.md#math-module) |
| `phash/2`, `phash2/1,2` of pids, ports, references, funs and native records | Hashes of OTP's process, port and reference numbers, a fun's index and unique number, a record definition's hash | Hashes of Clause's numbers, a fun's module, arity or index and captures, a record's field values; every other term hashes to OTP's value | [builtins](builtins.md#hashing) |
| `dict:fold/3`, `map/2`, `filter/2` with a non-fun | `function_clause`, or `{badfun, F}` when the first bucket OTP visits holds an entry | `function_clause` | [builtins](builtins.md#collections) |
| `erlang:is_builtin/3` of `lists:reverse/2` and other C-implemented library functions | `true` | `false`: only the runtime's builtins and `apply/2,3` | [funs](funs.md#fun-information) |
| `fun_info/1,2` `pid`, `uniq`, `new_uniq` of a local fun | Creator pid, compiler checksums | `undefined`, MD5 of the module name | [funs](funs.md#fun-information) |
| `md5_init/0` context | OTP's 92-byte state | Clause's 96-byte state; digests are equal | [builtins](builtins.md#hashing) |
| `erlang:delete_module/1` of a loaded module | Makes its code old (`true`) | `error:notsup`: linked code stays | [builtins](builtins.md#code-loading) |
| `erlang:prepare_loading/2` | Prepares a valid BEAM binary | Always `{error, badfile}` | [builtins](builtins.md#code-loading) |
| `erlang:pre_loaded/0` | The preloaded boot modules | Every module of the program | [builtins](builtins.md#code-loading) |
| `list_to_pid/1` of a number never issued | Returns a pid term | `badarg`: only pids this program created are admitted | [terms](terms.md#pids-and-references) |
| `list_to_port/1` of a number never issued | Returns a port term | `badarg`: only ports this program opened are admitted | [ports](ports.md#identity) |
| `port_info/1,2` values | `id` is a table index; `memory`, `queue_size`, `locking` describe the driver | `id` is the port number; `memory` and `queue_size` are 0; `locking` is `port_level` | [ports](ports.md#builtins-and-port-messages) |
| Writing a file opened only for reading | `{error, eacces}` on Windows, `{error, ebadf}` elsewhere | `{error, ebadf}` everywhere | [ports](ports.md#standard-io-and-files) |
| `io:get_line`, `io:get_chars` and `file:read/2` in list mode on non-ASCII input | Characters decoded per the device encoding | One list element per byte | [ports](ports.md#standard-io-and-files) |
| `os:cmd/1` result | Unicode characters decoded from the output | The output's bytes | [ports](ports.md#subprocesses) |
| Empty arguments of `{spawn_executable, F}` on Windows | Dropped from the command line | Passed as `""` | [ports](ports.md#subprocesses) |
| `{exit_status, S}` and `eof` of a spawned program | Unspecified order, `exit_status` may arrive before the last data | After all data, before `eof` | [ports](ports.md#subprocesses) |
| Busy ports | A port with too much queued output suspends senders, socket ports included; `port_command/3` `force` works on drivers that allow it | Only ports of spawned programs become busy; `fd` and socket ports never do; `force` raises `notsup` on every driver | [ports](ports.md#busy-ports) |
| `{active, N}` of sockets | An integer `N` counts messages before `{tcp_passive, S}` | `exit(badarg)`; only `true`, `false` and `once` | [ports](ports.md#sockets) |
| Socket tuning options (`nodelay`, `keepalive`, `send_timeout`, `delay_send`, buffers) | Applied; other `inet` options (`header`, `{packet, line}`, `http`, ...) work | The listed tuning options are accepted and not applied; other options are `exit(badarg)` | [ports](ports.md#sockets) |
| Socket error reasons | Every POSIX reason the system reports | The common ones (`econnrefused`, `eaddrinuse`, `econnreset`, `etimedout`, ...); others are `eio` | [ports](ports.md#sockets) |
| Port input while the connected process holds 1,024 messages and can run | Keeps reading and delivering | Stops delivering until the process's time slice ends; past 64 KiB held, stops reading, so the writing program blocks | [ports](ports.md#busy-ports) |
| `gen_tcp:close/1` with queued output | Waits up to the `linger`/`send_timeout` setting for the output to leave | Returns at once; the I/O thread sends the queued output, then closes | [ports](ports.md#sockets) |
| `monitor/2` types | `process`, `port`, `time_offset` | `process` and `port`; `time_offset` raises `badarg` (no time offset changes) | [processes](processes.md#monitors) |
| `exit/2`, `exit_signal/2` to a reference | Sends to the process alias, if the reference is an active one | Nothing: exit signals never go to aliases | [processes](processes.md#exit-signals) |
| Reference numbers | Mix a scheduler identifier and per-scheduler counters (`#Ref<0.178111994.4235460610.214105>`) | One program-wide counter (`#Ref<0.0.0.1>`), so references order by creation | [terms](terms.md#pids-and-references) |

## Errors, stack traces and reports

| Difference | OTP | Clause | Owner |
| --- | --- | --- | --- |
| Stack trace locations | `[{file, F}, {line, L}]`, `error_info` for `error/3` | Always `[]`; no `error_info` | [ABI](abi.md#stack-traces) |
| `erlang:raise/3` with `{Fun, Args}` stack entries | Accepted | Rejected | [ABI](abi.md#stack-traces) |
| `function_clause` top frame | Argument list | Arity | [ABI](abi.md#stack-traces) |
| `undef` top frame | `{M, F, Args, []}` of the missing function | The calling function's frame | [funs](funs.md#dynamic-calls) |
| Dynamic call of an export an executable cannot reach (its module named only through a stack trace, an exception or a message from outside the program) | Calls it | `undef`, unless built with `--keep-exports`; `function_exported/3` and `module_info(exports)` still list it | [executables](executables.md#export-liveness) |
| Failing BIF/operator frames | Present (`{erlang, '+', Args, ...}`) | Absent; nothing below the entry function | [ABI](abi.md#stack-traces) |
| Atom table full (`list_to_atom/1`) | The emulator aborts (`no more index entries in atom_tab`) and writes a crash dump | `clau: runtime failure: entry call failed: resource_limit`, exit 70 | [builtins](builtins.md#how-calls-reach-them) |
| `list_to_integer/1,2` with characters above 255 | Its first digits use only each character's low byte (`[16#131]` is 1) | `badarg` | [builtins](builtins.md#how-calls-reach-them) |
| `erlang:function_exported/3` of a BIF | True for every BIF of the emulator | True only for the builtins this runtime provides | [builtins](builtins.md#how-calls-reach-them) |
| Error reports of crashed processes | Sent to the logger, written later by its default handler (on standard output under `erl`, often lost when an escript halts first) | Written on stderr when the process ends, before any later output | [processes](processes.md#exits) |
| Calls to functions that never return | Compiled as tail calls (caller missing from the trace) | Ordinary calls (caller present) | [ABI](abi.md#stack-traces) |
| Host memory exhausted | The emulator reports that it cannot allocate memory, writes a crash dump and stops | `clau: runtime failure: entry call failed: out_of_memory`, exit 70, no dump | [runtime heap](runtime-heap.md#failure-behavior) |
| Storage of maps above 32 keys | An on-heap HAMT of 16-way hashed nodes | An on-heap B+ tree of up to 16 entries per node in key order; heap sizes of large maps differ from OTP's | [runtime heap](runtime-heap.md#map-trees) |
| Process memory cap | `max_heap_size` (in words, heap and stack) kills the process with reason `killed` and logs an error report | `--max-heap` / `--max-stack` (bytes) and the runtime-wide `--max-memory` fail the requesting process as `resource_limit`: `clau: runtime failure: entry call failed: resource_limit`, exit 70 | [runtime heap](runtime-heap.md#runtime-memory-limit) |
| Program arguments | `escript` passes every argument to `main/1`; emulator flags (`+t`) come from `%%!` or `ERL_FLAGS` | Leading runtime options (`--max-atoms`, `--max-heap`, `--max-stack`, `--max-memory`, `--min-heap-size`, `--args-file`, `--`) are taken out first; `CLAUSE_FLAGS` holds the same options | [executables](executables.md#runtime-options) |
| Uncaught exception in an ordinary entry module | `escript` exits 127 | Exits 1 with one `uncaught exception <class>: <reason>` line (escript sources keep 127) | [executables](executables.md) |
| Compiler diagnostics | `erl_lint` wording (`variable 'X' is unbound`) and warnings | Own wording (`unbound variable X`); OTP lint warnings are mostly not emitted (also native record warnings: unknown fields in access, update and patterns, own-module qualified construction without values, unused or header-defined records; a later pattern matching a name exported from a branch) | [semantic](semantic.md#bindings) |
| Behaviour module with a hand-written `behaviour_info/1` | Calls it while compiling users and warns about missing, ill-defined or deprecated callbacks | Not evaluated: its users' callbacks are not checked | [semantic](semantic.md#behaviours) |
| Behaviour modules outside the batch (`gen_server`, `supervisor`, ...) | Loaded from the code path; their callbacks are checked | `behaviour M undefined` warning | [semantic](semantic.md#behaviours) |
| `-behaviour` with a value that is not an atom | Warning prints the term with `~w` (`behaviour [115,116,114] undefined`) | Prints the attribute's source text (`behaviour "str" undefined`); the error is the same | [semantic](semantic.md#behaviours) |
| `module_info(md5)` and the `vsn` OTP derives from it | MD5 of the compiled BEAM code | MD5 of the module's printed source; the derived `vsn` integer differs accordingly | [semantic](semantic.md#predefined-functions) |
| `module_info(compile)` | `version` of OTP's compiler application, `options` given to it | Clause's version; `options` always `[]` | [semantic](semantic.md#predefined-functions) |
| `module_info(functions)` | Also lists the functions OTP generates for funs (`'-f/0-fun-0-'/0`) | Source functions and the predefined ones only | [semantic](semantic.md#predefined-functions) |
| `-nominal` in `module_info(attributes)` | Kept as `{nominal,[{Name,AbstractType,Params}]}` with abstract format | Left out | [semantic](semantic.md#predefined-functions) |
| A function of a Clause library module that Clause has not implemented (an `erlang` BIF, `lists:rmerge/2`, ...) | Runs | Compiles; raises `error({not_implemented, F, A})` when called, with `M:F/A` on top of the stack ([statuses](../library/modules.terms)) | [library](library.md#provided-or-missing), [builtins](builtins.md#erlang-module) |
| Import of a module outside the batch | Compiles; the call raises `undef` at run time if the module is missing | `unknown module M` compile error when the import is called (unused imports need no module) | [semantic](semantic.md#imports) |
| `import directive overrides auto-imported BIF` warning | For every auto-imported BIF of `erl_internal:bif/2` | Only for the auto-imported BIFs Clause implements | [semantic](semantic.md#imports) |

## Language edge cases

| Difference | OTP | Clause | Owner |
| --- | --- | --- | --- |
| Integer segment wider than the integer limit, value past it (`<<V:4194241>>` of all ones) | The x86 JIT matches with an invalid term; using it crashes the VM | No match | [terms](terms.md#integers) |
| Zip group whose relaxed and strict generators share a variable | Skip test keeps strict-pattern variables in the relaxed patterns | A rejected step is skipped whenever the strict patterns match on their own | [patterns](patterns.md#comprehensions) |
| Calling an external fun, `M:F(Args)` or `apply/3` of a module outside the program | Loads the module from the code path, `undef` when absent | `undef`; library modules join the program only when it names them with a literal atom | [funs](funs.md#dynamic-calls), [library](library.md#how-programs-get-them) |
| Files without `#!` given as escripts | `escript file.erl` skips the first line | Compiled as ordinary modules | [executables](executables.md) |
| Precompiled beam and archive escripts | Run | Not supported | [executables](executables.md) |
| Module name of an escript without `-module` | `<name>__escript` plus a timestamp/unique suffix | `<name>__escript`, deterministic | [executables](executables.md#escripts) |
| `include_lib` of an application found in several `<app>-<version>` directories | The code path holds one version (boot picks the highest); no diagnostic | The highest version below each searched directory, with one warning per run listing them | [preprocessor](preprocessor.md#compatibility-policies) |
| A `-spec` whose types share no value with the inferred result, entry domain or a call's arguments | Compiles; Dialyzer may warn | A contradicted argument entry domain is a compile error naming the declared and inferred types; a contradicted result or call is a warning, and the inferred types are used | [semantic analysis](semantic.md#inference) |

## ETS

| Difference | OTP | Clause | Owner |
| --- | --- | --- | --- |
| Order of `first/1`, `next/2`, `tab2list/1`, `foldl/3` and `i/1` over `set`, `bag` and `duplicate_bag` tables, and what `slot/2` numbers | Linear hash slots visited lock by lock; the order changes as the table grows | Parts, then buckets in order, then keys in insertion order within a bucket; a slot is one bucket | [runtime](runtime.md#ets-tables) |
| `ets:all/0` | Includes the tables of OTP's `kernel` and `stdlib`; order of the scheduler replies | Only the program's tables, in no particular order | [builtins](builtins.md#ets) |
| `info/2` `memory` | Allocated words of the table and its objects | Stored object words plus Clause's own overheads | [builtins](builtins.md#ets) |
| `{write_concurrency, auto}` | Lock count adapts to contention (64 to 8192) | 64 fixed parts, as for `true`; `stats` reports 64 locks | [runtime](runtime.md#ets-tables) |
| `compressed` | Stores elements other than the key in external format | Accepted and reported; objects are stored as they are | [builtins](builtins.md#ets) |
| `info/2` `stats` of an `ordered_set` with `write_concurrency`; `data`; `binary` | CA tree `{RouteNodes, BaseNodes, MaxDepth}`; prints the table; the table's off-heap binaries | `false`; `true` without printing; `[]` | [builtins](builtins.md#ets) |
| `badarg` of an `ets` builtin | Carries `error_info` with a cause (`id`, `access`, `type`, ...) | Plain `badarg` | [builtins](builtins.md#ets) |
| Order of `select`, `match` and `match_object` results of hash tables, and which results each chunk of `select/3` holds | Results of each walk step consed, newest first; linear hash slots | Walk order of Clause's parts and buckets; each chunk the first `Limit` results gathered | [builtins](builtins.md#ets) |
| `select_delete/2`, `select_replace/2` over a whole table | Trap every 1000 objects (the table fixed meanwhile) | Run in one piece, each part under its lock | [runtime](runtime.md#ets-tables) |
| Match specifications naming a variable numbered 25 or above | Renumbers every `'$N'` atom of the specification, even inside `{const, ...}` (so `{const, '$30'}` gives `'$0'`) | Variables keep their numbers; `{const, '$30'}` gives `'$30'` | [runtime](runtime.md#match-specifications) |
| Match specification guards and bodies nested deeper than 1000 levels | Compile until the C stack runs low (`system_limit`) | `system_limit` | [runtime](runtime.md#match-specifications) |
| Trace actions in `erlang:match_spec_test/3`: `enable_trace`, `disable_trace`, `trace` with valid flags | Change the caller's trace flags (29.1.1 crashes without a tracer) | `true` after checking the flags (and that a pid argument is alive); nothing changes | [builtins](builtins.md#ets) |
| `ets:tab2file/3` with `{sync, true}` | `disk_log:sync/1` flushes the file to disk | The file is written and closed without a flush to disk (`file:sync/1` is LIB50) | [builtins](builtins.md#ets) |
| `ets:tab2file/2,3` and `file2tab/1,2` | Through a disk_log process named by a fresh reference | Written and read directly with `file` | [builtins](builtins.md#ets) |
| `{process_dump}` and `{current_stacktrace}` in `erlang:match_spec_test/3` | The process's dump text; its stack trace | `<<>>`; `[]` | [builtins](builtins.md#ets) |

## dets

| Difference | OTP | Clause | Owner |
| --- | --- | --- | --- |
| Requests waiting together at a table | Lookups, inserts and deletes queued at once share one cache update; lookups are answered before the writes are made; a fixed table makes some writers sleep 1 ms | Each request in turn, in arrival order | [builtins](builtins.md#dets) |
| Long dets operations (repairs, `init_table/2,3`, compactions, flushes of a large cache) | Erlang code, preempted by reductions | One builtin call each; the table's worker runs nothing else meanwhile | [builtins](builtins.md#dets) |
| `dets` processes | The `dets` server and `dets_sup` under `kernel_safe_sup`, tables under `dets_sup` | A `dets` server started on first use, unsupervised; table processes linked to it | [builtins](builtins.md#dets) |
| `dets:all/0` order | The registry's ETS order | Clause's ETS order ([ETS](#ets)) | [builtins](builtins.md#dets) |
| Reports | A repair is logged at info level; `verbose/0,1` and `DETS_DEBUG` log more; an internal failure is `{dets_bug, ...}` | Nothing is logged | [builtins](builtins.md#dets) |

## io

| Difference | OTP | Clause | Owner |
| --- | --- | --- | --- |
| `~p`, `~w` and `io_lib` writers of containers nested more than 256 deep | Printed | `system_limit` | [io](io.md#pretty-printing-p) |
| `io_lib:print/4` with a column or line length that is not an integer | Depends on the term: printed (guards fail quietly), `function_clause` or `badarith` | `badarith` for any container term | [io_lib](builtins.md#io_lib) |
| Negative precision or pad count (`~.*c` with -1) | Loops forever | `badarg` | [io](io.md#formats) |
| `io:fread/2,3` (any `get_until` request) on standard input or a file with characters above 127 | The I/O server decodes the device's encoding into characters | The scanner sees the input's bytes | [ports](ports.md#standard-io-and-files) |

## Random numbers

| Difference | OTP | Clause | Owner |
| --- | --- | --- | --- |
| Strip heights of `rand`'s normal ziggurat | Tabled constants | Computed to the nearest double from the ziggurat's constants; a height may differ in its last bit, which changes a slow-path accept only for a draw within that bit of the bound (odds near 2^-53 per slow-path draw). Strip widths and bounds are observed from OTP and equal | [builtins](builtins.md#random-numbers) |

## Parse transforms

| Difference | OTP | Clause | Owner |
| --- | --- | --- | --- |
| Name in the `file` attribute of an included file (`--print-abstr`, transform input) | The include directory joined to the name, as found (`inc.hrl`, `/abs/dir/inc.hrl`, `../inc/x.hrl`) | The path relative to the working directory when the file is under it, else the absolute path | [transforms](transforms.md#interchange) |
| `generated` annotations of imported forms (`.abstr` inputs, transform output) | `erl_lint` and the compiler skip some warnings for generated code | Ignored: the code is checked like source | [transforms](transforms.md#importing-forms) |
| `{error, {Location, Module, Descriptor}}` forms in imported code | Formatted with `Module:format_error/1` | The descriptor's term text; formatting needs OTP | [transforms](transforms.md#importing-forms) |
| A module with preprocessor or parse errors that names a parse transform | The transform runs and gets the errors as `{error, E}` forms; `erl_lint` reports them afterwards | Errors are reported and the transform does not run | [transforms](transforms.md#clause-pipeline) |
| Compile options a transform receives | erlc's full option list | `{features, _}`, `report_warnings`, `report_errors`, `{cwd, _}`, `{outdir, _}` (the working directory), `{i, _}`, `{d, ...}` and `{parse_transform, _}` | [transforms](transforms.md#otp-behavior) |
