# Executables

Contract for programs built by `clau -o` or a project build. Positional inputs,
or exactly one selected project target, link into the `-o` path; a project build
links each selected executable target to its manifest `output`
([linking](#linking), [projects](projects.md#executables)).

## Entry selection

The entry is an exported function of arity 1 that receives the argument list. A
selected `FUNCTION` without `FUNCTION/1` may instead be an exported
`FUNCTION/0`: it runs without the program arguments (runtime options are still
taken out). A `/0` or `/1` suffix selects that arity only, so
`--entry benchmarks:run/0` runs `run/0` even when `run/1` is exported too. The
default (no selection) is always `main/1`.

| Source | Spelling | Scope |
| --- | --- | --- |
| CLI | `--entry MODULE[:FUNCTION[/ARITY]]` | Positional batch, or the single selected project target |
| Manifest | `entry = "MODULE[:FUNCTION[/ARITY]]"` in a `[[targets]]` table | That target |
| Default | Only escript, else only module exporting `main/1` | Only when an executable is requested (`-o`, or a project target with `output`) |

- `FUNCTION` defaults to `main`. Names are unquoted atom text: 1–255 Unicode
  scalars, valid UTF-8, no control characters and no `:`. A trailing `/DIGITS`
  after `FUNCTION` is the arity and must be `0` or `1`; other slashes stay part
  of the name. Other spellings are usage errors (CLI, exit 2) or manifest errors
  (exit 1).
- The whole manifest is decoded, so a malformed `entry` fails even in
  unselected targets ([manifest](projects.md)).
- CLI `--entry` overrides the manifest key and requires exactly one selected
  target. It conflicts with check/print actions and `--new-project`; those
  actions ignore manifest `entry` (as they ignore `output`).
- An explicit entry is validated in every compiling mode (default, `--emit`,
  IR/type inspection), together with ordinary semantic diagnostics.

| Failure | Diagnostic (exit 1) |
| --- | --- |
| Module not in the batch | `<origin>: entry module M is not among the compiled modules` (origin: `--entry` or manifest `file:line:col [target t] (entry)`) |
| No `F/1` nor `F/0` | `<file>:<line>:<col>: entry function M:F/1 is not defined` at the module declaration |
| Explicit `F/A` missing | `... entry function M:F/A is not defined; found F/N` at a same-named definition, else at the module declaration |
| Only other arities | `... is not defined; found F/N, but the entry takes the argument list (FUNCTION/1) or nothing (FUNCTION/0)` at that definition |
| `F/1` (else `F/0`) not exported | `<file>:<line>:<col>: entry function M:F/A is not exported` at the definition |
| No selection, no `main/1` export | `no entry point: no module exports main/1; choose the entry with --entry MODULE[:FUNCTION[/ARITY]] (an exported FUNCTION/1, or FUNCTION/0 run without the program arguments; FUNCTION defaults to main, ARITY to 1, else 0)` |
| No selection, several | `ambiguous entry point: main/1 is exported by a, b; choose the entry with ...` (same hint) |

For project targets the hint also names the manifest key: `... --entry
MODULE[:FUNCTION[/ARITY]] or with entry = "MODULE[:FUNCTION[/ARITY]]" in this
target's [[targets]] table of the project manifest ...`, for example:

```toml
[[targets]]
name = "app"
sources = ["src/*.erl"]
entry = "app:start"   # calls app:start/1 (or app:start/0); "app:start/0" calls app:start/0; plain "app" calls app:main/1
```

## Arguments

`Entry(Argv)` receives a proper list of strings (lists of Unicode code points),
excluding the program name and the leading [runtime options](#runtime-options),
otherwise unchanged and in order, like `escript`.

- POSIX: each argument's bytes are decoded as UTF-8; a byte that does not start
  a valid sequence becomes the code point of that byte (Latin-1 fallback).
- Windows: the CRT's wide (UTF-16) argument vector, split by the same rules as
  `argv`; an unpaired surrogate becomes U+FFFD.
- No other option parsing, globbing or environment expansion happens in the
  runtime.

## Runtime options

The runtime reads its options from the `CLAUSE_FLAGS` environment variable
(words split on spaces and tabs, no quoting) and then from the leading
command-line arguments, so the command line wins. Both are parsed the same way;
a value goes in the next argument or after `=`.

| Option | Effect |
| --- | --- |
| `--max-atoms N` | Atom table size, 1 to 2^26 (67,108,864); default 2^20 (1,048,576), like OTP's `+t` |
| `--max-heap BYTES` | Cap on each process's heap block, fragments and off-heap binaries, at least the minimum heap (233 words); default uncapped |
| `--max-stack BYTES` | Cap on each process's frame stack; default uncapped |
| `--min-heap-size WORDS` | Default `min_heap_size` of every process, at least 1, rounded up to a heap size, like OTP's `+hms`; at most `--max-heap`; default 233 ([sizing](runtime-heap.md#sizing-and-budget)) |
| `--max-memory BYTES` | Cap on the memory of all processes together (heaps, off-heap binaries, stacks); default uncapped |
| `--schedulers N` | Scheduler workers running processes, 1 to 1,024; default one per logical processor, like OTP's `+S` ([workers](processes.md#workers)) |
| `--profile FILE` | Write a report of entries and self time per function and per process to `FILE` when the program ends ([profiling](profiling.md)); off by default |
| `--args-file FILE` | Options file like OTP's `vm.args`: reserved, reports `runtime option --args-file is not implemented` |
| `--` | Ends the runtime options; the following arguments all go to `Entry` |

On the command line, parsing stops at the first argument that is not a runtime
option, so `prog data --max-atoms 9` passes all three arguments to the program.
In `CLAUSE_FLAGS` every word must be a runtime option. An invalid value, a word
that is not an option in the variable, or `--args-file` stops the program before
any module is registered: `clau: runtime failure: <reason>`, exit 70. Process
counts are not limited and memory is uncapped by default. Byte values are
decimal without suffixes and round down to whole words; a program reaching a cap
fails with `resource_limit`, exit 70
([runtime memory limit](runtime-heap.md#runtime-memory-limit)).

## Exit status

| Outcome | Status |
| --- | --- |
| Entry returns (any value) | 0 |
| `erlang:halt()` | 0 |
| `erlang:halt(N)`, non-negative integer | `N` (POSIX hosts keep the low 8 bits) |
| `erlang:halt(Slogan)` with a string | Slogan on stderr, then 1 (no crash dump) |
| `erlang:halt(abort)` | Native abort (no flushing) |
| Any exception escaping the entry, including `throw` and `exit(normal)` | Report on stderr, 1 |
| Entry process ended by an exit signal ([processes](processes.md#exit-signals)) | Reported as an uncaught `exit`, 1; reason `normal`: 0 |
| Runtime startup or infrastructure failure (ABI mismatch, registration, memory before entry) | Message on stderr, 70 |
| Host memory exhausted (heap, off-heap binary or stack growth refused; no memory cap by default, [memory exhaustion](runtime-heap.md#failure-behavior)) | `clau: runtime failure: entry call failed: out_of_memory`, 70 |

Invalid `halt/1` arguments raise `badarg` in the caller. When the entry
finishes, the program exits: other processes are stopped without running
further, as with OTP's `halt/1` after `escript` returns. A halt in any process
ends the program with its status, and a runtime failure in any process ends it
with 70; an exception in a spawned process ends only that process
([processes](processes.md)).

`halt/0,1`, `error/1,2,3`, `exit/1` and `throw/1` are callable with or without
the `erlang:` prefix; a local definition or `-compile({no_auto_import, ...})`
keeps the unqualified name local, as in OTP. `halt/2` is not available.
`halt(N)` keeps the low 31 bits of any non-negative integer, as OTP does. A
slogan is a proper list of at most 1,023 Unicode code points. A halt unwinds the
entry through the checked error channel like an error, so it stops the program
only after generated cleanup.

## Output streams

- stdout: `standard_io` output (`io:format/1,2`, `io:put_chars/1`,
  `erlang:display/1`). Buffered; flushed on every exit path except `abort`.
- stderr: the uncaught-exception report, `standard_error` output, runtime
  failures and the error reports of other processes that crash
  ([processes](processes.md#exits)).
- The report is one line `uncaught exception <class>: <reason in ~w form>`,
  without stack frames. Its exact text is not a stable interface; tests match it
  by pattern.

## Startup object

Compiling with an explicit entry (`--entry` or manifest `entry`) adds a startup
module after the batch's modules. With `--emit` it is published as
`clausev1_start.{obj,o,ll,bc}` next to the module artifacts (the name cannot
collide with a module artifact). It contains a constant
`abi::v1::StartupDescriptor`
([startup.hpp](../abi/include/clause/abi/startup.hpp)): ABI revision, term
width, every module descriptor in source order, the entry module/function
spellings and an escript flag. Its `int main(int, char **)` calls the runtime's
`CLAUSE_main_v1`, which:

1. Checks the startup and every module descriptor for ABI revision and width
   before anything is registered; a mismatch exits 70.
2. Starts the runtime and registers all modules; any failure stops before the
   entry and discards the runtime (exit 70), so no Erlang code runs against a
   partial batch.
3. Creates the entry process, builds argv and queues the call of `M:F/1` as the
   main process, then runs it and every process it spawns on the scheduler
   workers ([processes](processes.md)) until it ends.
4. Maps the outcome to the exit status above, printing reports after flushing
   stdout, then releases every process and shuts the runtime down on every path
   (except `halt(abort)`).

`clau -o` links these objects itself ([linking](#linking)). Manual linking (the
[native harness recipe](compile.md#run-the-compiled-module-example) without a
harness source):

```powershell
& $tool --emit obj --entry app --artifact-dir build/app app.erl helper.erl
clang-cl /MT build/app/*.obj build/debug/lib/clause_runtime.lib /Fe:app.exe
```

Any Clang-compatible link of the objects with `Clause::generated_program` works
the same way (see `tests/compiler/linking/startup.cmake`).

## Linking

`clau [-O0|-O2|-Os] -o PATH a.erl b.erl ...` (or
`--project FILE [--target T] -o PATH` for one selected target) compiles the
batch in memory, adds the startup object for the [entry](#entry-selection) and
links an executable:

```sh
clau -O2 -o build/demo examples/compile/answer.erl examples/compile/client.erl
./build/demo          # build/demo.exe on Windows
```

`-Os` additionally places every generated and runtime function and data object
in its own section and links with `--gc-sections` (ELF), `-dead_strip` (Mach-O)
or `/OPT:REF /OPT:ICF` (MSVC), so code no entry path reaches is removed.

`--lto` compiles the modules and the startup module to LLVM bitcode instead of
objects and links with `-flto -fuse-ld=lld`, so LLD optimizes the program's
generated code as one unit against the native runtime archive.
Module and startup descriptors, exported entries and `main` stay referenced and
survive; programs behave as without it. Only LLD reads this compiler's bitcode,
so `--lto` is accepted for Windows MSVC and ELF targets; others fail before
linking with `--lto is not supported for target <triple>: it needs LLD`. `--lto`
needs an executable output (`-o` or a linking project build). The installed
Clang/LLD must be the same LLVM release as `clau` (23.1).

### Export liveness

An executable leaves out the exports it cannot reach, so `-Os` dead-stripping
and `--lto` remove library code the program never calls. Right after call
resolution, before type inference, a whole-batch analysis starts at the entry
function and marks live everything a live function calls directly (also through
`-import` and OTP auto-imports), every `fun F/A` and literal `fun M:F/A`, and
the literal targets of `apply/3`, `spawn/3`, `spawn_link/3`, `spawn_monitor/3`,
`spawn_opt/4,5`, `spawn_request/3,5`, `hibernate/3` and `make_fun/3` (every
arity of `M:F` when the argument list is not literal), and `file:write/2` once
`io:format/3`, `io:fwrite/3`, `io:put_chars/2` or `io:nl/1` is live (their
builtins write to a process device through it). It stays conservative:

- a module whose name atom is used as a value anywhere in live code (passed,
  stored, sent, `?MODULE`, a record tag, `M:F(...)` with a runtime function,
  `fun M:F/A` with a runtime part) keeps every export, which also keeps the
  callbacks of behaviour modules started by name;
- once live code has both a call with a runtime module (`M:F(...)`, `apply/3` or
  a spawn with a variable module, `fun M:F/A` with a variable module,
  `binary_to_term/1,2`) and an atom-producing builtin (`list_to_atom/1`,
  `list_to_existing_atom/1`, `binary_to_atom/1,2`,
  `binary_to_existing_atom/1,2`, `binary_to_term/1,2`, `fun_info/1,2`,
  `fun_info_mfa/1`, `process_info/1,2`, `loaded/0`, `pre_loaded/0`), every
  export of every module stays.

A function nothing live reaches gets no code at all. A left-out export keeps its
name in the module's export table without code, so `function_exported/3` and
`module_info(exports)` answer as if it were exported, while a dynamic call that
reaches it raises `undef`
([differences](differences.md#errors-stack-traces-and-reports)). That can happen
only when a module name comes from outside the analysis: a stack trace, an
exception term or data from outside the program. `--verbose` traces the result
per module (`phase=liveness detail="dropped-exports=3"`, or
`detail="kept-all-exports reason='m' is used as a value in main:main/1"`).

A function nothing live reaches is also never type-inferred or checked against
its `-spec`: inference covers only live code, which is faster for programs that
use little of large library modules and can only narrow live functions' facts,
since unreachable callers no longer join their arguments. A specification
contradicted only in unreachable code is therefore reported for `--emit`
artifacts but not for the executable.

`--keep-exports` (or the project option `keep_exports = true`) keeps every
export callable. The analysis applies to executables (`-o` or a linking project
build) and to IR and assembly inspections given an entry (`--entry` or the
target's manifest `entry`), which then show the executable's code; `--emit`
artifacts, meant for other programs, keep every export.

`-g` keeps Erlang line tables for debuggers: in the executable (ELF), or in a
`<name>.pdb` published beside it (MSVC targets) ([debugging](debugging.md)).

- `PATH` is invocation-relative. For Windows targets, `.exe` is appended when
  the file name has no extension. Its directory must exist.
- Linker: `--linker PATH` (a path or program name), else `clang++` or `clang`
  from `PATH`, then (Windows) `%ProgramFiles%/LLVM/bin`. It runs as
  `<clang> --driver-mode=g++ --target=<triple> -o <staged> <objects> <runtime>`,
  so Clang chooses the platform linker and C/C++ runtime libraries (on Windows
  it locates MSVC and the SDK itself; no developer shell is needed).
- Runtime: `--runtime-library PATH`, else the `clause_runtime` archive of the
  build that produced `clau` (path recorded relative to the executable, e.g.
  `bin/../lib/clause_runtime.lib`). Every native object in the archive must
  match the target's architecture and object format; `--target-triple` for
  another target therefore needs a runtime built for it.
- Objects and the executable are staged in a private `.clause-link-*` directory
  beside the output, which is removed afterwards; it stays there, not in the
  [build directory](projects.md#build-directory), because the final rename must
  not cross volumes. The output is replaced only after a successful link, so
  every failure keeps an existing file unchanged. The output must not be a
  directory or alias an input.
- Linker warnings are forwarded to stderr; `--linker` and `--runtime-library`
  require `--output` or a linking project build.
- Project builds without `-o` link every selected target that has `output` or
  `entry` to its manifest output, creating missing directories, and replace the
  outputs only after all selected targets linked
  ([projects](projects.md#executables)).

| Failure (exit 1) | Diagnostic |
| --- | --- |
| No Clang | `cannot find clang++ or clang on PATH; install LLVM/Clang or pass --linker` / `linker not found: X` |
| No runtime | `runtime library not found: P; build the clause_runtime target or pass --runtime-library` |
| Not an archive | `runtime library is not a static library: P: ...` |
| Wrong target | `runtime library P contains x86_64 coff objects, but the executable targets T; ...` |
| Link error | `linking O failed: <clang> exited with status N:` followed by the linker output (first 64 KiB) |
| Bad destination | `output directory does not exist: D`, `artifact destination is not a regular file: O`, `artifact destination aliases an input: O` |

## Escripts

A source file whose first line starts with `#!` is compiled as an escript, in
any mode and with any file name (positional inputs or project `.erl` files).
Rules follow OTP 29 `escript` for source scripts:

- The `#!` line is ignored. An optional comment on line 2 and a `%%!` emulator
  line (line 2, or line 3 after the comment) are comments; `%%!` arguments
  cannot apply to compiled code and produce a warning.
- If the first form is not `-module(...)`, the module is
  `<file name with '.' replaced by '_'>__escript` (`?MODULE` included). OTP adds
  a timestamp/unique suffix; Clause keeps the name deterministic. The
  synthesized declaration occupies line 1, so later line numbers are unchanged.
- `main/1` is required (`escript does not define main/1`) and implicitly
  exported; other functions follow normal export rules.
- `-mode(compile | interpret | debug | native)` is accepted and ignored; other
  values are errors. Outside escripts `-mode` stays unsupported.
- Entry: without `--entry`, the only escript in the batch is the entry (it wins
  over modules exporting `main/1`); several escripts are ambiguous.
- Exit status as OTP `escript`: an exception escaping the escript entry exits
  127 with `escript: exception <class>: <reason>` on stderr; other rows of the
  exit-status table apply unchanged.
- Files without `#!` are ordinary modules. (OTP `escript file.erl` would skip
  their first line; Clause does not.) Precompiled beam and archive escripts are
  not supported.

## OTP comparison

Goldens for [program fixtures](../tests/fixtures/programs/README.md) run the
entry under OTP with the same rules
([oracle](../tests/compiler/programs/oracle.escript)); the same oracle generates
the [executable golden cases](../tests/fixtures/executables/README.md) that the
end-to-end runner links and runs under every policy. Differences from `escript`
for ordinary modules: uncaught exceptions exit 1 instead of 127, and `main/1`
must be exported. Escript sources keep OTP's rules (see above).
