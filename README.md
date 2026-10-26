# Clause

An ahead-of-time compiler for Erlang/OTP 29. `clau` compiles Erlang sources through LLVM and links
them with the C++ runtime `clause_runtime` into native executables. The goal is to build existing
pure Erlang projects into runnable programs that keep Erlang's semantics: isolated process heaps,
preemptive scheduling, message passing and garbage collection.

## Why Clause

- **Native executables:** one self-contained program; no BEAM, no OTP install and no boot scripts on
  the target machine.
- **Erlang semantics:** lightweight processes on multithreaded schedulers, selective `receive`,
  links, monitors, per-process garbage collection and OTP's error terms, printing and term order.
- **Optimized code:** type inference, type specialization at `-O2`, LLVM optimization and link-time
  optimization.
- **Native tooling:** debug line tables for native debuggers, an opt-in profiler, and IR, assembly
  and inferred-type listings.
- **Checked against OTP:** tests compare compiled programs with goldens recorded from Erlang/OTP 29;
  OTP is not needed to build or test.

## Status

- **Language:** the full OTP 29 grammar and preprocessor, patterns, guards, `try`, comprehensions,
  funs, maps, bitstrings, big integers, tuple and native records, proper tail calls and `#!`
  escripts.
- **Runtime:** processes, timers, ports, files, subprocesses and TCP/UDP sockets (`gen_tcp`,
  `gen_udp`, `inet`).
- **Library:** every `erlang` export is implemented or a named stub. Clause ships its own
  `stdlib`/`kernel` modules (`lists`, `maps`, `ets`, `dets`, `string`, `unicode`, `re`, `json`,
  `rand`, `io`, `file`, ...); other modules compile from an OTP source tree on a project's search
  path ([library](docs/libraries/library.md)).
- **Platforms:** validated on Windows x64 and Linux x86-64; 32-bit x86 and Linux ARM have historical
  results; macOS on Apple Silicon is not yet validated ([validation](docs/validation.md)).

Not supported: distribution (a program is the single node `nonode@nohost`), dynamic code loading and
hot upgrades, NIFs and `-on_load`, the `socket` and `net` NIF modules. Unfinished library functions
raise `{not_implemented, F, A}`; deferred language features report a `notimpl` diagnostic
([features](docs/features.md)). Observable differences from OTP are listed in
[differences](docs/differences.md).

## Quick start

Requirements: CMake 3.28+, a C++23 compiler and Python 3. CMake finds or downloads the LLVM 23.1 SDK
and other dependencies; on Windows use a Visual Studio x64 developer shell with Ninja and LLVM/Clang
installed. Details: [building](docs/building.md).

Build:

```sh
cmake --preset debug          # Windows: cmake --preset windows
cmake --build --preset debug  # Windows: cmake --build --preset windows-release
```

`clau` lands in `build/debug/bin` (Windows: `build/windows/bin/Release`).

Write a program; the entry is the module exporting `main/1`, which receives the command-line
arguments as strings:

```erlang
-module(hello).
-export([main/1]).

main(Args) -> io:format("Hello, ~p~n", [Args]).
```

Compile and run it:

```sh
clau -O2 -o hello hello.erl
./hello world
```

For more than a few files, describe targets in a TOML project:

```sh
clau --new-project myapp       # writes an annotated myapp.toml
clau --project myapp.toml
```

`clau --help` lists every option.

## Documentation

The [documentation index](docs/README.md) lists every reference note. Start with:

- [building](docs/building.md): requirements, presets, options, tests, tools;
- [compilation](docs/compile.md): CLI, artifacts, listings;
- [executables](docs/executables.md): entry, arguments, runtime options, exit status;
- [projects](docs/projects.md): TOML manifests, source discovery, caches;
- [library](docs/libraries/library.md): provided and missing modules.
