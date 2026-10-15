# Compilation

`clau` compiles Erlang/OTP 29 modules through LLVM to verified IR, bitcode or
native objects. `-o/--output` links positional inputs (or one selected project
target), their [entry](executables.md) startup object and the runtime into an
executable ([linking](executables.md#linking)); project builds link executable
targets to their manifest outputs ([projects](projects.md#executables)). Emitted
objects can also run through a C++ harness linked with the runtime (see the
example below).

## Accepted source subset

Named modules of the ordinary OTP 29 language: ordered function clauses with
[patterns](patterns.md) and [guards](guards.md); matches, constructors,
operators and builtins ([builtins](builtins.md)); `case`, `if`, `receive`,
`catch`, `try ... of ... catch Class:Reason:Stack ... after` with
[stack traces](abi.md#stack-traces), `maybe ... else ... end`; list, binary and
map comprehensions ([patterns](patterns.md#comprehensions)); tuple and
[native records](native-records.md); [funs](funs.md), local, remote and dynamic
calls (`M:F(...)`, `apply/2,3`); [processes](processes.md) and
[ports](ports.md). Calls run on explicit process frames with proper tail calls
([execution model](execution-model.md#implementation)); body recursion is
bounded by the process stack (uncapped by default), not the native stack.
Modules a batch names but does not declare come from the
[library](library.md). Comprehension match qualifiers of the experimental
`compr_assign` feature are rejected as `[heap expressions] notimpl`.

Accepted attributes: `module`, `export`, `file`, tuple and native `record`,
`export_record`, `import_record`, type/spec forms, `doc`/`moduledoc`,
`behaviour`/`behavior` ([behaviours](semantic.md#behaviours)), any informational
attribute (`vsn`, `author`, `copyright`, `deprecated`, `dialyzer`, user-defined
`-name(Term)`; kept for
[`module_info(attributes)`](semantic.md#predefined-functions)),
`-import(Module, [F/A, ...])` ([imports](semantic.md#imports)) and `-compile`
with options that change nothing Clause emits: `{no_auto_import, ...}`, warning
options (`nowarn_*`, `warn_*`, `{nowarn_unused_function, [...]}`) and
optimization, debug and reporting hints (`inline`, `{inline, [F/A]}`,
`{inline_size, N}`, `{inline_effort, N}`, `inline_list_funcs`, `debug_info`,
`deterministic`, `report*`, `verbose`, ...). `{parse_transform, Module}` runs
the transform on the host Erlang/OTP before analysis
([parse transforms](transforms.md)). `on_load`, `nifs`, `export_all`, other
`compile` options and parameterized modules are rejected; the diagnostic names
them, for example
`[behavior-changing attributes] notimpl [module="m" operation="-compile option export_all"]`.
Every module gets `module_info/0,1`. Sources starting with `#!` follow
[escript rules](executables.md#escripts) (implicit module and `main/1` export,
`-mode` accepted). Type/spec forms are analyzed but never change generated code.
Syntax-only modes (`--parse-check`, `--print-ast`, `--print-source`, ...) accept
the full grammar.

## Run the compiled-module example

`client:main/1` makes the example a program:

```sh
./build/debug/bin/clau -O2 -o build/demo examples/compile/answer.erl examples/compile/client.erl
./build/demo     # prints 42, -7 and {record,map,binary,list,integer,other}
```

The same modules also run through a C++ harness. From a Windows x64 Developer
PowerShell with the built compiler:

```powershell
$tool = './build/debug/bin/clau.exe'
& $tool -O0 --emit obj --artifact-dir build/example-aot examples/compile/answer.erl examples/compile/client.erl
cmake -S examples/compile -B build/example-native -G Ninja -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Debug "-DGENERATED_DIR:PATH=$((Resolve-Path build/example-aot).Path)"
cmake --build build/example-native
./build/example-native/bin/Debug/compiled_modules.exe
```

It prints `42`, `-7`, `record`, `map`, `binary`, `list`, `integer`, `other`, one
per line. The harness registers modules explicitly, creates a context and
decodes results; it is an example host, not a production entry point. On Unix
use `build/debug/bin/clau`, `clang++` and
`-DGENERATED_DIR="$PWD/build/example-aot"`. CTest `codegen_examples` runs these
commands at every policy.

Other actions on the same sources:

```powershell
& $tool -O2 --emit llvm-ir --artifact-dir build/example-ir examples/compile/answer.erl examples/compile/client.erl
& $tool --print-types --verbose examples/compile/answer.erl examples/compile/client.erl
& $tool --print-ir --print-optimized-ir examples/compile/answer.erl examples/compile/client.erl
& $tool -O2 --no-type-specialization --verbose examples/compile/answer.erl examples/compile/client.erl
```

## Abstract format printing

`--print-abstr` (a frontend action) prints each parsed module as the OTP
[abstract format](https://www.erlang.org/doc/apps/erts/absform.html) forms `epp`
would hand a parse transform, one `file:consult/1` term per line, the same terms
`erlc +to_abstr` writes ([transforms](transforms.md)).

## Source printing

`--print-source` (a frontend action, like `--print-ast`) prints each parsed
module as Erlang source; `--print-types` prints the same text with type
annotations. The printer (`print_source`, `expression_source`, `type_source` in
`printing.hpp`; `compiler/src/printing/source_*`) is reusable:

- Forms follow each other in source order, a blank line around each function;
  the `-file` form the preprocessor adds before a module is left out, those
  around included files are kept.
- The text is the parsed syntax: macros are expanded, includes inlined, and
  comments, original spelling and layout are gone. Clauses and block expressions
  (`case`, `if`, `receive`, `try`, `maybe`, `begin`, funs with more than one
  line) take indented lines of four columns; everything else is on one line.
  Parentheses come only from the source's own groups.
- Printed text parses back to the same syntax tree, and printing it again gives
  the same text (CTest `printing_source` over the parseable fixtures).
- `SourceNotes` adds a note to an expression, printed as a trailing `% Text`
  comment after the punctuation of the line where the expression ends, and
  comment lines above a form. Only whole-line expressions carry notes (body
  expressions and a `case`'s scrutinee; one per line, the outermost), so long
  nested expressions stay readable and the text stays Erlang. Patterns and
  guards carry none.

## Options

| Option | Behavior |
|---|---|
| `--emit obj\|llvm-ir\|llvm-bc` | Publish one artifact per module |
| `--artifact-dir DIR` | Artifact root (requires `--emit`) |
| `-o PATH` / `--output PATH` | Link an executable ([linking](executables.md#linking)); conflicts with `--emit` |
| `--linker PATH`, `--runtime-library PATH` | Clang driver and runtime archive for `-o` |
| `--no-cache` | Neither read nor write [build caches](projects.md#build-caches) |
| `--cache-inference-in-dirs PATTERN` | Repeatable: modules in matching directories keep their inference results in the build cache, besides the implicit OTP application directories ([inference cache](projects.md#inference-cache)) |
| `--build-dir DIR` | Where Clause keeps temporary files (below `DIR/tmp`); replaces every selected project target's `build_dir`; positional inputs have none without it ([build directory](projects.md#build-directory)) |
| `--entry MODULE[:FUNCTION[/ARITY]]` | Executable entry function/1 (else function/0; `/0` or `/1` selects one); validated in every compiling mode and adds the `clausev1_start` startup artifact ([executables](executables.md#startup-object)); IR and assembly inspections then leave out what it cannot reach ([export liveness](executables.md#export-liveness)) |
| `--target-triple TRIPLE` | Target machine; `--target` is project target selection |
| `-O0` / `-O2` / `-Os` | Default generic code + LLVM O0 / bounded specialization + LLVM O2 / LLVM Os, no specialization, one section per symbol and linker dead-stripping of unreferenced code and data |
| `--no-type-specialization` | Disable variants regardless of option order |
| `--lto` | Link the modules as bitcode with LLD link-time optimization (Windows MSVC and ELF targets; needs `-o` or a linking project build) ([linking](executables.md#linking)) |
| `--keep-exports` | Keep every export of an executable callable instead of leaving out those it cannot reach; needs `-o`, a linking project build or an IR/assembly inspection ([export liveness](executables.md#export-liveness)) |
| `-g` | Erlang line tables in objects and linked executables (CodeView/PDB on MSVC targets, DWARF elsewhere) ([debugging](debugging.md)) |
| `--print-ir` / `--print-optimized-ir` | Verified IR before/after LLVM passes, with Erlang source lines as comments |
| `--print-asm` | Each module's target assembly after the selected pipeline, every run of machine code commented with the IR instruction and Erlang line it came from ([assembly listings](#assembly-listings)) |
| `--print-types` | Each module as Erlang source annotated with inferred types ([semantic](semantic.md#--print-types)); stops before LLVM |
| `--verbose` | `[pp]`, `[parse]` and `[comp]` phase events on stderr, then the [directory index](projects.md#directory-index) summary `[index] ...` |
| `--print-stage-times` | `[time] <stage> <subject>: <ms> ms` on stderr: `plan` per project manifest (loading it and expanding every selected target's patterns), `preprocess`, `parse` and `transform` (modules with parse transforms) per source file, `compile` per batch (module count, project target), `link` per executable; ` (cached)` follows a stage that reused a [cached](projects.md#build-caches) result. Before a batch's `compile` total come its phases with the same subject: `index`, `calls`, `liveness` (executables and inspections with an entry), `declared-types`, `inference-cache` (with `N of M covered modules reused`, [inference cache](projects.md#inference-cache)), `inference`, `inference-store`, `contracts`, `lower`, `optimize`, `emit` (those that ran). After `inference`: `inference pass N` lines with the pass's work steps out of the inference work budget (`N of BUDGET work steps`) and how it ended (`inputs changed`, `fun results changed`, `settled`, or the final pass after unsettled passes), naming the function running when the budget ran out; then `inference slowest <module>:<function>/<arity>` for the 10 functions (recursive groups name one member `and N more`) that took longest over all passes, with their work steps and that work's share of the budget of the passes that ran them (`877450 work steps, 43.9% of 10 passes of 200000 budget`), and `widened to term()` when a budget widened one of their facts; then `inference rounds <module>:<function>/<arity> and N more` for the 10 slowest recursive groups: solves over all passes, rounds run against the per-solve cap (8 + 4 per member), solves that reached the cap or spent their eighth of the pass budget (every member widened to `term()`), solves that gave up at once because an earlier pass did (`remembered`) or ended with the pass budget spent, member walks (and those skipped because nothing they read changed) and the share that changed a summary (grown, function-type inputs changed, or flipped back to one of the member's last 4 summaries), and the members changed per round of the last solve in ten groups of rounds. A cached compile prints no phases |
| `--print-env` | Print the resolved compile environment as TOML on stdout and exit without processing sources ([environment report](#environment-report)) |
| `--print-inputs` | Preprocess and parse the inputs, add the modules they name as compilation does, then list every source of the batch on stdout and stop before compilation ([projects](projects.md#cli-and-target-selection)) |

- Without `--emit`, compilation verifies objects in memory and writes nothing
  outside the [build directory](projects.md#build-directory).
- Positional inputs form one batch; each project target is its own batch.
- Artifact roots: `build/aot` (positional) or `build/aot/<hex-target>` under the
  manifest directory. Explicit roots are invocation-relative.
- Names are reversible hex: `answer` → `clausev1_616e73776572__0.obj` (`.o` for
  ELF/Mach-O, `.ll`, `.bc`); the startup object is `clausev1_start.obj`.
- All batches compile and stage before publication. Failures publish nothing and
  keep earlier outputs; replacement is atomic per file, not per batch.
- `--emit` conflicts with `-o`; compilation switches conflict with frontend-only
  actions and `--new-project`. `--print-types` rejects target/optimization
  options.
- IR snapshots are LLVM assembly; multiple snapshots are separated by escaped
  comment headers and are not one parseable module. Use `--emit llvm-ir` for
  tool input. Source-line comments show original text (unexpanded macros) and
  survive optimization through debug locations.

## Assembly listings

`--print-asm` runs the selected pipeline (as `--print-optimized-ir` does), then
prints each input module's assembly from the same target machine that writes
objects, one module at a time. Comments use the target's marker (`#` on x86,
`//` on AArch64 ELF, `;` on Apple, `@` on ARM):

```
# erlang answer.erl:3: value() -> 42.
#     ir store i64 687, ptr %frame.slots, align 8
	movq	$687, 32(%r15,%r12,8)
#     ir br i1 %4, label %return.inline, label %return.service
```

- An `ir` comment opens each run of instructions selected from one IR
  instruction, spelled as `--print-optimized-ir` prints it (same value
  numbering, `switch` folded onto one line, no `!dbg`). Code moved or merged
  across instructions repeats or skips comments as the line table says.
- An `erlang` comment names the physical file, line and text, like IR source
  comments, when the line changes within a function. Instructions lowering
  located nowhere (wrappers, frame setup) have none.
- The listing compiles a copy whose line table numbers IR instructions instead
  of Erlang lines; that line table's directives, debug sections and labels are
  left out. Reassembled, it gives the object `--emit obj` writes.
- With other inspections, each module prints `before`, `after` and `assembly` in
  turn, each after a header (`stage=assembly`). Like IR inspection it rejects
  `--emit`, `--artifact-dir`, `--output` and `--print-types`. x86 listings use
  AT&T syntax.
- With `--entry`, IR and assembly inspections leave out the functions the entry
  cannot reach, as the executable does (`--keep-exports` keeps them).

## Environment report

`--print-env` validates the whole command line (conflicts, readable positional
inputs, project planning), then prints what that command would use as one TOML
document on stdout, exits 0 and reads no source. Paths are absolute, with `/`.

```toml
# clau compile environment (--print-env)
command = ["clau", "--print-env", "-O2", "-o", "app", "main.erl"]  # as given
actions = ["link"]          # see below
working_directory = "/work"

[[targets]]                 # one per selected project target; one for positional inputs
name = "app"                # project targets only
project = "/work/project.toml"
sources = ["/work/src/main.erl"]           # globs and search paths resolved
library_directory = "/clau/library"     # application dirs, searched before source_search_paths
build_dir = "/work/build"   # "" for positional inputs without --build-dir
entry = "main:main"         # or "auto" when linking without --entry
output = "/work/app"        # linking only: final name (".exe" on Windows)
linker = "/usr/bin/clang++" # linking only; "" when none is found
runtime_library = "/clau/lib/libclause_runtime.a"

[targets.options]           # manifest option names
source_search_paths = []    # directories, patterns expanded
include_dirs = []           # search order, patterns expanded
defines = []
applications = { "stdlib" = "/otp/lib/stdlib" }  # last mapping wins
enabled_features = ["maybe_expr"]                # OTP defaults plus changes

[targets.backend]
target_triple = "x86_64-unknown-linux-gnu"  # host when not given
optimization = "O2"
type_specialization = true  # O2 without --no-type-specialization
debug_info = false
lto = false
keep_exports = false        # --keep-exports or the project option
emit = "none"               # or obj, llvm-ir, llvm-bc
artifact_dir = "/work/build/aot"
```

(Arrays print one item per line.) `actions` lists the frontend actions
(`print-pp`, `parse-check`, `print-ast`, `print-source`, `print-abstr`, else
`preprocess-check`; `print-inputs` alone replaces them), else the inspections
(`print-types`, `print-ir`, `print-optimized-ir`, `print-asm`), else `emit`,
`link` (`-o`) or `compile`. In a project build, targets with an `output` link
too. Modules found later by name (through `source_search_paths` or the library)
are not listed: finding them needs parsing (`--print-inputs` does that).
`--print-env` cannot be combined with `--new-project`.

## Compile cache

A batch with a build directory (every project target, positional inputs with
`--build-dir`) keeps its compile outcome in the
[build cache](projects.md#build-caches): the diagnostics in order, failure, and,
when an executable or `--emit` needs them, the objects (or bitcode) of every
module and the startup module. A later build of the same batch with the same
options replays the diagnostics and links or publishes the kept objects exactly
like fresh ones; `--print-stage-times` then shows `[time] compile ... (cached)`.
`--print-ir`, `--print-optimized-ir`, `--print-asm` and `--print-types` always
recompute, and `--no-cache` neither reads nor writes.

- **Reuse unit: the whole batch.** Analysis, type inference, specialization
  budgets (per target), export liveness and the startup module span every
  module, so one module's object depends on facts of others (callee types,
  records, behaviours, which functions stay live). Those facts have no canonical
  form to hash per module today, so a per-module key could not be proven sound;
  any change to any module of the batch recompiles the batch. Inference results
  alone are kept per module for covered library directories
  ([inference cache](projects.md#inference-cache)), so such a recompile reuses
  them.
- **Key.** The `clau` version and executable identity; every input's path and
  fingerprint (each file the frontend read for it, path and bytes, in order: the
  source, its includes and `include_lib` files, and the reply of its parse
  transforms; the preprocessing options; the generated text of placeholders),
  predefined forms and escript status; the resolved triple, optimization level,
  `--emit`, `--no-type-specialization`, `-g`, `--lto`, `--keep-exports`, whether
  an executable is linked, the entry selection and the project target. Entries
  of one target with other options are kept side by side.
- **Storage.** Each output is a part of its own, so an object larger than the
  store's inline limit becomes a content-addressed blob file, shared when two
  option sets produce identical objects.

## Backend

- One LLVM context and target machine per batch. Host triple uses host CPU and
  features; foreign triples use the generic CPU. PIC, small code model.
- Backends: X86, ARM, AArch64 (intersection with the SDK). Unknown or missing
  backends fail; there is no host fallback.
- Verification runs before and after optimization; it checks IR validity, not
  Erlang correctness.
- Budgets per target: 16,384 modules, 1,500,000 AST nodes per module, 16,000,000
  per batch. Serialized output: 64 MiB per module, 256 MiB per batch. Exceeding
  a budget is an ordinary resource diagnostic.

## LLVM SDK

Stable LLVM **23.1.x**, minimum 23.1.1. LLVM is a host dependency: architecture,
C++ standard library and Windows CRT must match the compiler tool. Project code
keeps exceptions/RTTI; no exception may unwind through LLVM. The runtime never
uses LLVM.

- Discovery searches standard prefixes (`/usr`, `/usr/local`, `/opt/homebrew`,
  `/opt/local`, `/opt/llvm`, Linuxbrew, `/Library/Developer/Toolchains`, Program
  Files on Windows), including versioned layouts. Build trees are rejected.
- `LLVM_DIR` selects an SDK explicitly; invalid selections fail without
  fallback.
- If none is found, CMake downloads the pinned **23.1.2** archive (SHA-256
  checked) into `thirdparty/` for Windows x64/ARM64, Linux x64/ARM64 or macOS
  ARM64. `CLAUSE_DOWNLOAD_LLVM=OFF` disables downloads.
- A configure-time link probe checks ABI compatibility.

Reference Windows x64 setup: host clang-cl 23.1.2 in
`C:/Program Files/LLVM/bin`, SDK
`thirdparty/clang+llvm-23.1.2-x86_64-pc-windows-msvc`, Visual Studio 18 x64
tools, Windows SDK 10.0.26100.0, Ninja, `/MT`, `_ITERATOR_DEBUG_LEVEL=0`.
Automatic selection applies the `/MT` and iterator settings; an explicit
`LLVM_DIR` does not, and the link probe then fails.

## PCRE2

The runtime's `re` builtins use PCRE2 **10.47**, the release OTP 29 bundles
(`erts/emulator/pcre/vendor.info`), so pattern syntax, options and captures
behave as OTP's: 8-bit code units, Unicode support, no JIT, stock `pcre2_match`
(OTP's yielding patch is not carried over). PCRE2 is BSD-3-Clause licensed with
the PCRE2 exception for compiled binaries; no PCRE2 file is committed. CMake
(`cmake/Pcre2Dependencies.cmake`) resolves it in this order:

1. `-DCLAUSE_PCRE2_ROOT=<source directory>` takes precedence (no download); the
   version in its `configure.ac` must be 10.47.
2. An installed PCRE2 of at least 10.47 (`CLAUSE_PCRE2_SYSTEM=ON`, the default):
   the CMake package (`find_package(pcre2 CONFIG COMPONENTS 8BIT)`, static
   libraries preferred), else pkg-config `libpcre2-8`, a static `libpcre2-8.a`
   preferred over the shared library.
3. The pinned release, downloaded into `thirdparty/pcre2-10.47` and verified
   (`pcre2-10.47.tar.bz2`, SHA-256
   `47fe8c99461250d42f89e6e8fdaeba9da057855d06eb7fc08d9ca03fd08d7bc7`);
   `-DCLAUSE_DOWNLOAD_PCRE2=OFF` disables the download.

A source directory (1 or 3) builds the object library `clause_pcre2` from the
listed `.c` files with the distributed `config.h.generic`, `pcre2.h.generic` and
`pcre2_chartables.c.dist` copied into the build tree (no generation step, no
project warning flags, outside clang-tidy and Lizard), and its objects are added
to `clause_runtime` itself: the archive is self-contained and every consumer
(`clau` links, nested test programs, `runtime_link`, `examples/compile`) gets
PCRE2 with it. An installed library instead becomes an extra linker input
recorded in `clause_runtime.link` beside the archive (one input per line), which
`clau` adds to every link; a shared library makes the executables depend on it
at run time. Per platform: Windows uses the download (no system PCRE2 is
searched for in practice); Linux and macOS use an installed PCRE2 10.47 or newer
when present, else the download. PCRE2 is always linked into the runtime, also
into programs that never call `re`.

## zlib

The runtime's zlib module and compressed external terms use
zlib **1.3.2**, the release OTP 29 bundles (`erts/emulator/zlib/vendor.info`),
so compressed bytes equal OTP's. CMake compiles its library sources
(`cmake/ZlibDependencies.cmake`, `clause_add_runtime_zlib`) into the object
library `clause_runtime_zlib`, whose objects join `clause_runtime` as PCRE2's
do; `-DCLAUSE_RUNTIME_ZLIB_ROOT=<source directory>` takes precedence, otherwise
the release is downloaded into `thirdparty/zlib-1.3.2` and verified (SHA-256
`bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`) on every
host. zlib's license is the zlib license. The compiler's LLVM keeps its own
zlib; the two never meet in one binary.

Other prerequisites: CMake ≥ 3.28, a C++23 compiler, Boost ≥ 1.90, toml++ 3.4.0
and the quality tools. OTP is needed only for opt-in audits and fixture
regeneration.
