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
| Map printing (`erlang:display/1`, `~w`, `~p`) | Internal layout order (atom-table order for atom keys, hash order above 32 keys) | Map-key order, as OTP's `~kw` | [terms](terms.md#printing) |
| Native record printing (`erlang:display/1`) | Fields in atom-table index order | Definition order, as `~w` | [native records](native-records.md#printing-and-order) |
| `==` between native records whose fields differ only as integer/float | The compiler may fold it to `=:=` (false) | Numeric comparison (true) | [native records](native-records.md#printing-and-order) |
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
| Formatting a large term (`io:format/1,2`) | Runs in Erlang code (`io_lib`) and the group leader, so the process can be preempted while formatting | Runs to completion in one builtin call; other processes wait | [builtins](builtins.md#portions) |
| `process_flag/2` flags | `trap_exit`, `priority`, `message_queue_data`, `min_heap_size` and others | Only `trap_exit`; every other flag raises `badarg` | [processes](processes.md#builtins) |
| Error report order of processes crashing at the same time | Logger order | The order their workers finish them | [processes](processes.md#workers) |
| Signals to a process running on another scheduler | Queued in its signal queue and handled later, while the sender goes on | The sender waits until the target's time slice ends, then acts at once; observable only as timing | [processes](processes.md#workers) |
| `term_to_binary/2` with `compressed` | zlib-compressed when smaller | Uncompressed, still valid for `binary_to_term/1` everywhere; decoding compressed input raises `error:notsup` | [builtins](builtins.md#external-term-format) |
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
| `phash/2`, `phash2/1,2` values | OTP's hash function | Clause's own stable hash with the same ranges and equality guarantee | [builtins](builtins.md#hashing) |
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
| Busy ports | A port with too much queued output suspends senders; `port_command/3` `force` works on drivers that allow it | Output never suspends; `force` raises `notsup` on every driver | [ports](ports.md#io-thread) |
| `{active, N}` of sockets | An integer `N` counts messages before `{tcp_passive, S}` | `exit(badarg)`; only `true`, `false` and `once` | [ports](ports.md#sockets-57f) |
| Socket tuning options (`nodelay`, `keepalive`, `send_timeout`, `delay_send`, buffers) | Applied; other `inet` options (`header`, `{packet, line}`, `http`, ...) work | The listed tuning options are accepted and not applied; other options are `exit(badarg)` | [ports](ports.md#sockets-57f) |
| Socket error reasons | Every POSIX reason the system reports | The common ones (`econnrefused`, `eaddrinuse`, `econnreset`, `etimedout`, ...); others are `eio` | [ports](ports.md#sockets-57f) |
| Port input while the connected process holds 1,024 messages and can run | Keeps reading and delivering | Stops delivering until the process's time slice ends; past 64 KiB held, stops reading, so the writing program blocks | [ports](ports.md#busy-ports) |
| `gen_tcp:close/1` with queued output | Waits up to the `linger`/`send_timeout` setting for the output to leave | Returns at once; the I/O thread sends the queued output, then closes | [ports](ports.md#sockets-57f) |
| `monitor/2` types | `process`, `port`, `time_offset` | `process` and `port`; `time_offset` raises `badarg` (no time offset changes) | [processes](processes.md#monitors) |
| `exit/2`, `exit_signal/2` to a reference | Sends to the process alias, if the reference is an active one | Nothing (no aliases) | [processes](processes.md#exit-signals) |
| Reference numbers | Mix a scheduler identifier and per-scheduler counters (`#Ref<0.178111994.4235460610.214105>`) | One program-wide counter (`#Ref<0.0.0.1>`), so references order by creation | [terms](terms.md#pids-and-references) |

## Errors, stack traces and reports

| Difference | OTP | Clause | Owner |
| --- | --- | --- | --- |
| Stack trace locations | `[{file, F}, {line, L}]`, `error_info` for `error/3` | Always `[]`; no `error_info` | [ABI](abi.md#stack-traces) |
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
| Process memory cap | `max_heap_size` (in words, heap and stack) kills the process with reason `killed` and logs an error report | `--max-heap` / `--max-stack` (bytes) and the runtime-wide `--max-memory` fail the requesting process as `resource_limit`: `clau: runtime failure: entry call failed: resource_limit`, exit 70 | [runtime heap](runtime-heap.md#runtime-memory-limit) |
| Program arguments | `escript` passes every argument to `main/1`; emulator flags (`+t`) come from `%%!` or `ERL_FLAGS` | Leading runtime options (`--max-atoms`, `--max-heap`, `--max-stack`, `--max-memory`, `--args-file`, `--`) are taken out first; `CLAUSE_FLAGS` holds the same options | [executables](executables.md#runtime-options) |
| Uncaught exception in an ordinary entry module | `escript` exits 127 | Exits 1 with one `uncaught exception <class>: <reason>` line (escript sources keep 127) | [executables](executables.md) |
| Compiler diagnostics | `erl_lint` wording (`variable 'X' is unbound`) and warnings | Own wording (`unbound variable X`); OTP lint warnings are mostly not emitted (also unknown native record fields in access, update and patterns) | [semantic](semantic.md#bindings) |
| Behaviour module with a hand-written `behaviour_info/1` | Calls it while compiling users and warns about missing, ill-defined or deprecated callbacks | Not evaluated: its users' callbacks are not checked | [semantic](semantic.md#behaviours) |
| Behaviour modules outside the batch (`gen_server`, `supervisor`, ...) | Loaded from the code path; their callbacks are checked | `behaviour M undefined` warning | [semantic](semantic.md#behaviours) |
| `-behaviour` with a value that is not an atom | Warning prints the term with `~w` (`behaviour [115,116,114] undefined`) | Prints the attribute's source text (`behaviour "str" undefined`); the error is the same | [semantic](semantic.md#behaviours) |
| `module_info(md5)` and the `vsn` OTP derives from it | MD5 of the compiled BEAM code | MD5 of the module's printed source; the derived `vsn` integer differs accordingly | [semantic](semantic.md#predefined-functions) |
| `module_info(compile)` | `version` of OTP's compiler application, `options` given to it | Clause's version; `options` always `[]` | [semantic](semantic.md#predefined-functions) |
| `module_info(functions)` | Also lists the functions OTP generates for funs (`'-f/0-fun-0-'/0`) | Source functions and the predefined ones only | [semantic](semantic.md#predefined-functions) |
| `-nominal` in `module_info(attributes)` | Kept as `{nominal,[{Name,AbstractType,Params}]}` with abstract format | Left out | [semantic](semantic.md#predefined-functions) |
| `erlang:get_module_info/1,2` | The BIF behind `module_info/0,1` | Not provided; `module_info/0,1` return literal data | [semantic](semantic.md#predefined-functions) |
| `-compile({parse_transform, Module})` | Runs the transform over the module's forms | Not applied; warning `[parse transforms] notimpl` and the module compiles untransformed | [compile](compile.md#accepted-source-subset) |
| A function of a Clause library module that Clause has not implemented (an `erlang` BIF, `lists:keysort/2`, `persistent_term:get/1`, ...) | Runs | Compiles; raises `error({not_implemented, F, A})` when called, with `M:F/A` on top of the stack ([statuses](../library/modules.terms)) | [library](library.md#provided-or-missing), [builtins](builtins.md#erlang-module) |
| Import of a module outside the batch | Compiles; the call raises `undef` at run time if the module is missing | `unknown module M` compile error when the import is called (unused imports need no module) | [semantic](semantic.md#imports) |
| `import directive overrides auto-imported BIF` warning | For every auto-imported BIF of `erl_internal:bif/2` | Only for the auto-imported BIFs Clause implements | [semantic](semantic.md#imports) |

## Language edge cases

| Difference | OTP | Clause | Owner |
| --- | --- | --- | --- |
| Integer segment wider than the integer limit, value past it (`<<V:4194241>>` of all ones) | The x86 JIT matches with an invalid term; using it crashes the VM | No match | [terms](terms.md#integers) |
| Zip group whose relaxed and strict generators share a variable | Skip test keeps strict-pattern variables in the relaxed patterns | A rejected step is skipped whenever the strict patterns match on their own | [patterns](patterns.md#comprehensions) |
| Calling an external fun, `M:F(Args)` or `apply/3` of a module outside the program | Loads the module from the code path, `undef` when absent | `undef`; library modules join the program only when it names them with a literal atom | [funs](funs.md#dynamic-calls), [library](library.md#how-programs-get-them) |
| `lists` and `maps` | The full modules; `maps:keys/1`, `values/1`, `to_list/1`, `fold/3` follow the map's internal order (atom-table order for atom keys, hash order above 32 keys) | The [library subset](library.md); map functions follow key order | [library](library.md) |
| Files without `#!` given as escripts | `escript file.erl` skips the first line | Compiled as ordinary modules | [executables](executables.md) |
| Precompiled beam and archive escripts | Run | Not supported | [executables](executables.md) |
| `include_lib` of an application found in several `<app>-<version>` directories | The code path holds one version (boot picks the highest); no diagnostic | The highest version below each searched directory, with one warning per run listing them | [preprocessor](preprocessor.md#compatibility-policies) |
| A `-spec` whose types share no value with the inferred result, entry domain or a call's arguments | Compiles; Dialyzer may warn | A contradicted argument entry domain is a compile error naming the declared and inferred types; a contradicted result or call is a warning, and the inferred types are used | [semantic analysis](semantic.md#inference) |

## io

| Difference | OTP | Clause | Owner |
| --- | --- | --- | --- |
| Control sequences `~e ~f ~g ~x ~X ~+ ~# ~W ~P`, modifier `K` | Formatted | `badarg` | [io](io.md#formats) |
| `~p` of containers nested more than 256 deep | Printed | `system_limit` | [io](io.md#pretty-printing-p) |
| Field widths and `~ts` precision with combining characters or `\r\n` | Count grapheme clusters | Count code points | [io](io.md#formats) |
| Negative precision or pad count (`~.*c` with -1) | Loops forever | `badarg` | [io](io.md#formats) |
| `io` functions other than `format/1,2`, `put_chars/1` | Exist | `unknown module io` | [io](io.md#calls) |

## Parse transforms

| Difference | OTP | Clause | Owner |
| --- | --- | --- | --- |
| Name in the `file` attribute of an included file (`--print-abstr`, transform input) | The include directory joined to the name, as found (`inc.hrl`, `/abs/dir/inc.hrl`, `../inc/x.hrl`) | The path relative to the working directory when the file is under it, else the absolute path | [transforms](transforms.md#interchange) |
| `generated` annotations of imported forms (`.abstr` inputs, transform output) | `erl_lint` and the compiler skip some warnings for generated code | Ignored: the code is checked like source | [transforms](transforms.md#importing-forms) |
| `{error, {Location, Module, Descriptor}}` forms in imported code | Formatted with `Module:format_error/1` | The descriptor's term text; formatting needs OTP | [transforms](transforms.md#importing-forms) |
| A module with preprocessor or parse errors that names a parse transform | The transform runs and gets the errors as `{error, E}` forms; `erl_lint` reports them afterwards | Errors are reported and the transform does not run | [transforms](transforms.md#clause-pipeline) |
| Compile options a transform receives | erlc's full option list | `{features, _}`, `report_warnings`, `report_errors`, `{cwd, _}`, `{outdir, _}` (the working directory), `{i, _}`, `{d, ...}` and `{parse_transform, _}` | [transforms](transforms.md#otp-behavior) |
