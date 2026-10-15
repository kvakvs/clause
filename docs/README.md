# Clause documentation

Short reference notes for the current implementation. Each file states behavior
and limits that exist today; plans and step history live in `.agents/`.

| Topic | Document |
| --- | --- |
| Preprocessing (`epp` behavior, includes, macros, features) | [preprocessor.md](preprocessor.md) |
| Parsing, AST ownership, syntax limits | [parser.md](parser.md) |
| TOML projects, target selection, build directory and caches, ignored sources | [projects.md](projects.md) |
| Compiler CLI, artifacts, source and abstract format printing, compile cache, LLVM SDK, PCRE2 and zlib, example | [compile.md](compile.md) |
| Executable entry, arguments, exit status, output streams, linking | [executables.md](executables.md) |
| Debug information (`-g`), debuggers and the Erlang call stack | [debugging.md](debugging.md) |
| Opt-in profiling (`--profile FILE`): entries and self time per function and process | [profiling.md](profiling.md) |
| Parse transforms: host OTP loader, abstract format interchange, transform lookup | [transforms.md](transforms.md) |
| Semantic analysis, types, inference, bindings | [semantic.md](semantic.md) |
| Type specialization at `-O2`: proofs and representation variants | [specialization.md](specialization.md) |
| Generated-code ABI: terms, symbols, registration, failures, roots | [abi.md](abi.md) |
| Patterns, clauses, body matches and comprehensions | [patterns.md](patterns.md) |
| Guards and the guard BIF catalog | [guards.md](guards.md) |
| Term representations (atoms, numbers, containers, maps, bits, records) and printing | [terms.md](terms.md) |
| Native, qualified and anonymous records (OTP 29) | [native-records.md](native-records.md) |
| Function values: `fun F/A`, `fun M:F/A`, closures, named funs, dynamic calls | [funs.md](funs.md) |
| Production builtins: catalog, bridge calls, module builtins, portions, registration | [builtins.md](builtins.md) |
| Library modules under `library/{erts,kernel,stdlib}` compiled with programs, how programs get them, provided and missing modules | [library.md](library.md) |
| Console output: `io:format`, `io:put_chars`, devices, format controls, `~p` layout | [io.md](io.md) |
| Runtime lifecycle, memory, code server, persistent terms, threads, standard output, startup | [runtime.md](runtime.md) |
| Processes: scheduler workers, messages and receive, links, monitors, registered names, timers, introspection, aliases, exit signals | [processes.md](processes.md) |
| Ports: identity, port table, drivers, data modes, the I/O thread, busy ports, subprocesses, standard I/O, files and sockets | [ports.md](ports.md) |
| Process heap contract: word layout, areas, admission, roots, collection | [runtime-heap.md](runtime-heap.md) |
| Execution model: frames, calls, tail calls, yield, exceptions | [execution-model.md](execution-model.md) |
| Deferred-feature (`notimpl`) reporting | [features.md](features.md) |
| OTP source reference pin and refresh procedure | [otp-reference.md](otp-reference.md) |
| Known behavior differences from Erlang/OTP | [differences.md](differences.md) |
| Validation baseline, test modes, platform gaps, benchmarks, history | [validation.md](validation.md) |

Older per-step validation records and evidence JSON were condensed into
[validation history](validation.md#history); full originals remain in Git
history (last present at commit `2777c98`).
