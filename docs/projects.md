# TOML projects

A project manifest selects ordered groups of Erlang sources and configures each
target independently. All CLI actions accept projects. See the
[working example](../examples/project/project.toml).

## Manifest (schema 1)

```toml
schema_version = 1

[[targets]]
name = "app"
sources = ["main.erl", "src/workers/*.erl", "shared/**/*.erl"]
source_dirs = ["src/support"]
output = "build/app"
entry = "main:main"
build_dir = "build"

[targets.options]
source_search_paths = ["src", "generated"]
include_dirs = ["include", "vendor/include"]
defines = ["DEBUG", "LIMIT=100", 'LABEL="demo"']
enable_features = ["maybe_expr"]
disable_features = ["compr_assign"]
ignore_dirs = ["src/legacy", "deps/*/test"]
ignore_modules = ["telemetry", "*_SUITE"]
keep_exports = false
cache_inference_in_dirs = ["deps/*"]

[targets.options.applications]
my_dependency = "vendor/my_dependency"
```

- `schema_version = 1` and a nonempty `targets` array are required. Unknown
  keys, wrong types, duplicate target names and empty strings are rejected; no
  type coercion. The whole manifest is validated, including unselected targets.
- Target names: case-sensitive ASCII `[A-Za-z0-9_][A-Za-z0-9_.-]*`, unique. A
  name is a build target, not an LLVM triple.
- `sources` (files and wildcard patterns) and `source_dirs` (recursive `.erl`
  and `.abstr` discovery) are optional, but a target needs at least one entry.
  Explicit files must end in `.erl`, or `.abstr` for abstract format forms
  ([transforms](transforms.md#interchange)); headers are dependencies, not
  inputs.
- `parse_transforms` (module names) and `transform_paths` (directories or
  patterns of precompiled `.beam` files) in `options` apply parse transforms to
  the target's modules ([transforms](transforms.md#options)).
- `ignore_dirs` (paths) and `ignore_modules` (module names), both with
  wildcards, in `options` leave sources out of the build
  ([ignored sources](#ignored-sources)).
- `keep_exports = true` in `options` keeps every export of the target's
  executable callable, as `--keep-exports` does for every selected target
  ([export liveness](executables.md#export-liveness)). It must be a boolean.
- `cache_inference_in_dirs` (directory wildcards) in `options` adds directories
  whose modules keep their inference results in the build cache
  ([inference cache](#inference-cache)).
- `options` fields are optional typed frontend options. `defines` use `NAME` (=
  `true`) or `NAME=ERLANG_LITERAL_TERM`; duplicates are errors. A feature cannot
  appear in both feature lists.
- `entry` (optional) selects the executable entry `MODULE[:FUNCTION[/ARITY]]`;
  see [executables](executables.md#entry-selection). CLI `--entry` overrides it
  for a single selected target.
- `build_dir` (optional) is the target's [build directory](#build-directory),
  default `<manifest-dir>/build`.
- `output` is the executable destination, default
  `<manifest-dir>/build/<target>` (`.exe` on Windows). A target with `output` or
  `entry` is an executable target ([executables](#executables)); `--emit` and
  check/print modes ignore both keys' outputs.
- Not supported: root defaults, inheritance, target dependencies, imports,
  profiles, packages, watch, parallel builds. Fields are added only together
  with their behavior.

## CLI and target selection

```text
clau [options] <source.erl>...
clau [options] --project <path> [--target <name>]...
clau --new-project <filename>
```

- Positional sources and `--project` are mutually exclusive. One `--project`,
  any number of `--target`, in any order.
- No `--target`: all targets in manifest order. Selectors keep their order and
  first occurrence. Unknown names fail before any frontend work and list the
  available names.
- `--project test1` loads `test1.toml` when `test1` does not exist.
- CLI frontend options apply to every selected target. CLI-relative paths keep
  invocation-directory meaning; manifest paths are relative to the manifest.
- Without `--emit` or a check/print action, executable targets link to their
  outputs ([executables](#executables)); other targets compile in memory.
- `-o/--output` links the target to that path instead of its `output`
  ([linking](executables.md#linking)); it requires exactly one selected target.
- `--verbose` traces `[pp]` files/includes and `[parse]` sources on stderr.
- `--print-inputs` runs each selected target up to compilation: it preprocesses
  and parses the sources after [discovery](#paths-and-discovery), then adds the
  modules they name from the library and `source_search_paths`, as a build does.
  It prints the target's sources (absolute paths), one per line, then each added
  source as `<path> (referenced at <file>:<line>)`, the earliest place in the
  referring module that names it; added sources are searched in turn. Nothing is
  compiled. With several targets, each list follows a `[target <name>]` line.
  Positional inputs print as given. Diagnostics go to stderr; a parse error
  stops adding referenced modules and exits 1. It replaces other check/print
  actions and conflicts like them with `-o`, `--entry` and compilation switches.
- `--print-env` prints the whole invocation as TOML: one `[[targets]]` table per
  selected target with its resolved sources, search/include directories and
  effective options ([environment report](compile.md#environment-report)).
- Exit 2: usage errors and unknown targets. Exit 1: manifest, discovery,
  frontend or creation failures. Exit 0: success (warnings allowed).

Each selected target is a separate compilation batch with its own preprocessing
sessions. Planning (decode, select, discover, check output collisions) finishes
before any source runs; execution then processes targets/files in order and
latches failures. Payload output goes to stdout in target/file order without
banners (except the `--print-inputs` target lines); context and diagnostics go
to stderr. A target with diagnostics is named once, on a
`<manifest> [target <name>]:` line before its first one; the diagnostics
themselves (`error: file:line:col: message`) carry no tool or target prefix.

```sh
./build/debug/bin/clau --parse-check --project examples/project/project.toml
./build/debug/bin/clau --print-ast --project examples/project/project.toml --target tests --target app
```

## Executables

`clau --project FILE [--target T]...` (no `--emit`, no check/print action) links
every selected target that has an `output` or `entry` key, or that CLI
`-o`/`--entry` addresses, into one executable per target:

- Destination: CLI `-o` (invocation-relative), else `output`
  (manifest-relative), else `<manifest-dir>/build/<target>`. Windows targets add
  `.exe` to a name without extension. Missing directories of manifest outputs
  are created; an explicit `-o` directory must exist.
- Entry: CLI `--entry`, manifest `entry`, else detection
  ([entry selection](executables.md#entry-selection)).
- Targets without `output`, `entry`, `-o` and `--entry` are libraries: they
  compile in memory and write nothing, even if a module exports `main/1`.
- Planning rejects selected targets with the same destination; outputs that
  coincide only after `.exe` is added are rejected before publication. An output
  must not alias a selected source or the manifest.
- Each target compiles and links into its own staging directory. Outputs are
  replaced only after every selected target succeeded, in target order; any
  failure keeps every existing output unchanged. A failure while replacing a
  later output can leave earlier ones already replaced.
- `--linker` and `--runtime-library` apply to every linked target
  ([linking](executables.md#linking)).

```sh
./build/debug/bin/clau --project tests/fixtures/linking/project/project.toml
```

## Creating a project

`--new-project <filename>` writes one annotated manifest and nothing else.

- Appends `.toml` unless the name already ends in `.toml` (ASCII
  case-insensitive): `demo` → `demo.toml`, `demo.config` → `demo.config.toml`.
- Rejects empty or directory-like names, repetition and any combination with
  sources, `--project`, `--target`, `-o`, check/print modes or frontend options.
- The parent must exist. Never overwrites (exclusive creation); cleans up its
  own partial file on write failure. Prints the created path on success.
- Content is deterministic UTF-8/LF with comments: one target `app`,
  `sources = []`, `source_dirs = ["src"]`, empty option lists and the platform
  default output. It decodes immediately; the user adds sources under `src`.

## Build directory

Each project target has a build directory where Clause keeps its own temporary
files and its caches: the target's `build_dir`, relative to the manifest,
default `<manifest-dir>/build`. `--build-dir DIR` (invocation-relative) replaces
it for every selected target. Positional inputs get none unless `--build-dir` is
given.

- Temporary files go below `<build_dir>/tmp`: the parse transform loader's
  workspaces ([transforms](transforms.md#loader)). Each run removes its own; an
  empty `tmp` may remain. Without a build directory they go to the system's
  temporary directory.
- Link staging (`.clause-link-*`) stays beside the executable output, because
  the final rename must stay on the output's volume
  ([linking](executables.md#linking)).
- Check and print modes (`--parse-check`, `--print-ast`, ...) write nothing
  outside the build directory; positional checks without `--build-dir` write no
  file at all.
- `--print-env` prints each target's `build_dir` (`""` when there is none).

### Build caches

Results worth keeping between runs live below `<build_dir>/cache`; `--no-cache`
neither reads nor writes them. They are the parse transform loader's replies and
the host OTP's sticky module list, so a warm build starts no host `erl` for
modules whose transform inputs did not change, each batch's
[compile outcome](compile.md#compile-cache), and the
[inference results](#inference-cache) of modules in configured directories.

- **Interface.** Callers use one project-internal store (`compiler/src/cache/
  store.hpp`): `get`/`put` by input directory, file name, result kind and key,
  plus `invalidate` and `prune`; they never see paths. A value is the caller's
  format version and a list of named parts (a transform run: `reply` and
  `output`; a compile outcome: `failed`, each `diagnostic`, and with outputs
  `triple`, `extension` and one part per module object), so no caller needs a
  codec of its own. A `get` with another format version is a miss. Every
  operation is safe from several threads at once.
- **Layout.** `cache/v2/cache.db` is a SQLite 3.54 database: table `entries`
  (input directory, file, kind, key, format, last use; unique on the address)
  and table `parts` (entry, order, name, size, xxh3-64 checksum, and either the
  bytes or the name of a blob file). Parts larger than 256 KB live in
  `cache/v2/blobs/<xxh3-128 of the bytes>`, so equal parts (the identical
  objects of a batch built under two option sets) are stored once; 256 KB is
  where measured reads stop favoring the database and writes start favoring
  files (`cache_store bench` in `tests/compiler/cache`). A blob file is written
  as a temporary file and renamed before the row naming it commits, so a row
  never names an incomplete file; a crash in between leaves only an orphan file.
- **Keys.** A key is the 128-bit content hash of everything the result depends
  on, never a time stamp: the `clau` version and executable (path, size,
  modification time, so a rebuilt `clau` misses), the result kind, and for a
  transform run the request sent to `erl` (the module's preprocessed forms and
  its compile options: include directories, defines, features), the host OTP
  release (`erl` and the `releases/*/OTP_VERSION` files beside it), every
  project transform source compiled for the run with the files it includes, and
  every `.beam` on the transform code paths. The store's format version is the
  `v<format>` directory; a value's own format version is kept in its row.
  Because the key covers the preprocessed forms, a changed header, define or
  feature, or a new header that shadows an included one, misses exactly the
  modules whose forms or options change; defines and features are compile
  options every transform sees, so they miss every transformed module; a changed
  `.beam` misses every module whose transforms run with that code path.
- **Concurrency.** The database runs in write-ahead-log mode with
  `synchronous=NORMAL`: readers run beside a writer, and writers (one
  transaction per `put`) wait for each other up to 30 s. Several `clau`
  processes sharing a build directory keep every entry. Inside one process a
  mutex serializes the threads that share a store.
- **Damage.** Parts carry sizes and checksums, and blob files are checked
  against their names: a damaged part or a missing blob file is a miss
  (`damaged entry`) and its entry is dropped. A garbage, truncated or foreign
  `cache.db` (SQLite reports it corrupt or not a database, or its application id
  or schema version differ) is renamed to `cache.db.damaged` and created anew;
  when that is impossible the run continues without a cache. None of this is an
  error, and `--verbose` notes it as `[cache] ...`.
- **Network file systems.** Write-ahead logging needs shared memory between
  processes, which network file systems do not provide. When SQLite cannot enter
  WAL mode the store uses its rollback journal (readers then wait for a writer)
  and `--verbose` says so once; when no journal mode works the run continues
  without a cache.
- **Pruning.** At the end of a run that wrote to a store, entries not used for
  30 days are deleted, then blob files no entry names (once older than 10
  minutes, so a concurrent writer's new file survives), and the freed pages are
  returned to the file system (incremental vacuum). Every run removes a leftover
  `cache/v1` tree of the former file backend.
- **Observability.** `--print-stage-times` marks reused stages
  (`[time] transform src/a.erl: 7.254 ms (cached)`); `--verbose` prints
  `[cache] hit|miss <kind> <file>` with the reason of a miss (`no entry`,
  `inputs changed`, `damaged entry`). Diagnostics and transform output are the
  same with and without a hit: they are stored with the entry.
- **Not cached:** preprocessing and parsing (loading a cached result through the
  abstract forms, `transforms/import`, measured slower than parsing again), and
  directory scans: planning is dominated by opening and identifying each source,
  which a scan cache cannot skip.

### Inference cache

Library modules (OTP's `stdlib`, `kernel`, `compiler` and `syntax_tools` sources
found through `source_search_paths`, Clause's own [library](library.md)) rarely
change, so modules in configured directories keep their type inference results
in the build cache, per module, and later compiles reuse them instead of
inferring them again.

- Directories: the implicit OTP application patterns `*/stdlib`, `*/stdlib-*`,
  `*/kernel`, `*/kernel-*`, `*/compiler`, `*/compiler-*`, `*/syntax_tools` and
  `*/syntax_tools-*`, then the target's `cache_inference_in_dirs`, then every
  `--cache-inference-in-dirs PATTERN` (repeatable; positional inputs too).
  Patterns use the [wildcards](#paths-and-discovery) of `sources`; Windows
  separators are accepted.
- A module is covered when a directory containing its source matches a pattern.
  A relative pattern matches the trailing directories of any path, not paths
  below the manifest directory: `*/stdlib` covers
  `/usr/lib/erlang/lib/stdlib/src/lists.erl` and
  `references/otp/lib/stdlib/src/lists.erl`, while an installed
  `.../lib/stdlib-7.0/src/lists.erl` needs `*/stdlib-*`. An absolute pattern
  matches the whole directory path (`C:/Program Files/Erlang OTP/lib/*`).
  Matching is lexical and case-sensitive. The implicit patterns also cover a
  project's own directory of such a name (`apps/compiler`), which only stores
  more entries: reuse is sound for every module.
- `--print-env` prints the effective list as `cache_inference_in_dirs`, implicit
  patterns first. An unsupported construct (`[`, `{`, `!`) is a manifest error,
  or a usage error (exit 2) on the command line.
- Reuse needs a build directory (every project target; positional inputs with
  `--build-dir`) and is off with `--no-cache`. It works for every action that
  analyses (compiling, executables, `--print-types`, IR inspections), also when
  the batch's [compile cache](compile.md#compile-cache) misses.
- Key: `clau`'s identity, the module's fingerprint and those of every batch
  module its atoms name, transitively (remote calls, `fun M:F/A`, `apply`, any
  atom that is a module name); see
  [semantic: inference cache](semantic.md#inference-cache) for why that is all
  its facts depend on. Editing a user module therefore keeps the library hits
  unless a library module names it; editing a library module misses it and every
  covered module that reaches it.
- Entries are kept per source directory, file and project target (kind
  `inference@<target>`, `inference@positional`); a value is one part `results`
  (`semantic/types/inference_store`: graph nodes, summaries and local inputs by
  function, expression facts by syntax position). Results kept by a compile with
  more live functions (for example `--print-types`) serve an executable that
  keeps fewer.
- `--verbose` prints `[cache] hit|miss inference <file>` per covered module
  (miss reasons: `no entry`, `inputs changed`, `damaged entry`, `results do not
  fit` for kept results lacking a function that is live now);
  `--print-stage-times` prints the phases `inference-cache` (with `N of M
  covered modules reused`) and `inference-store`.

## Paths and discovery

- Project path resolves against the invocation directory; the manifest's lexical
  parent is the base for all manifest paths (even through a symlink). Absolute
  paths stay absolute; `..` is allowed. No shell, environment or tilde
  expansion. Use `/` or TOML literal strings for Windows paths.
- Literal relative sources try the base first, then `source_search_paths` in
  order. An existing but unreadable candidate is an error. Search paths do not
  enumerate modules or affect includes.
- When the target's sources name a module that none of them defines (a remote
  call, `fun M:F/A`, `apply` with a literal module, `-behaviour` or `-import`),
  Clause's [library](library.md#how-programs-get-them) is searched first, then
  the search paths in order, for `Module.erl` (an installed OTP never replaces a
  Clause module, and a module Clause records as missing is never taken from a
  search path: [resolution](library.md#which-module-a-name-resolves-to)); a
  found file joins the target like a listed source, and its own references are
  resolved the same way. A listed source with a Clause module's name replaces
  it, with a warning.
- Wildcards: `*` and `?` within a component, `**` as a whole component.
  Brackets, braces, negation and escapes are rejected. Matching is
  case-sensitive; `?` is one Unicode scalar of a valid UTF-8 name.
- Hidden entries are included; discovered directory symlinks are skipped
  (explicit roots may be symlinks); file symlinks are followed.
- Order: `sources` entries in order, each pattern's matches sorted by UTF-8 path
  bytes, then each `source_dirs` expansion sorted the same way. Missing
  literals, unmatched patterns and empty targets are errors.
- Deduplication within a target uses filesystem identity (symlinks, hard links,
  case aliases), keeping the first spelling. The same file in two targets is
  compiled twice because options can differ.
- Only selected targets touch the filesystem.

### Directory index

Lookups by name do not probe every candidate directory. One index per `clau`
invocation, shared by all targets, keeps the entry names of each searched
directory: listed once, non-recursively, when a lookup first needs it, or
recorded by the recursive walks that expand `**` in `sources`,
`source_dirs`, `source_search_paths`, `include_dirs` and `transform_paths`, so
no directory is listed twice. It answers the misses of the module search (the
library, then `source_search_paths`), transform sources, literal `sources` found
through `source_search_paths`, and include resolution (the including file's
directory, the working directory, include directories in order, then
`include_lib` applications); a candidate the index holds is still opened or
checked as before, so precedence, `ignore_dirs` and error reports do not change.
Include names with directory parts (`sub/x.hrl`) are looked up in the directory
they name; names with `..` steps, absolute names, relative directories and
listings that fail are probed as before.

- File name keys fold ASCII case on Windows and macOS, whose volumes ignore case
  by default, so `Foo.hrl` still finds `foo.hrl`; elsewhere keys are exact.
  Names folding cannot match exactly (non-ASCII names there, and on Windows
  names ending in `.` or a space or containing `~`, which may be 8.3 short
  names) are probed.
- The index is a snapshot of one invocation: files that appear while `clau` runs
  (link staging, outputs) are never search inputs.
- `--verbose` ends with `[index] listed=N walked=N lookups=N misses=N probes=N`
  (directories listed on demand, listings recorded by walks, lookups the index
  answered and the misses among them, lookups left to a probe).

## Ignored sources

`ignore_dirs` and `ignore_modules` keep sources out of a target without removing
them from the tree, e.g. vendored tests, legacy code or modules that only matter
under OTP. An ignored source is silently skipped: it is never read, parsed,
compiled or linked, so it may even hold syntax errors.

- `ignore_dirs`: manifest-relative (or absolute) paths with the
  [wildcards](#paths-and-discovery) of `sources`. A source is ignored when its
  path, or a directory containing it, matches: `src/legacy`, `deps/*/test`,
  `**/test` (any `test` directory), `src/*_SUITE.erl`. Matching is lexical; a
  pattern need not match anything.
- `ignore_modules`: module names, or name patterns with `*` and `?` matched
  against the whole name (`*_SUITE`, `legacy_*`, `prim_???`). A source named
  `<Module>.erl` (or `.abstr`) is ignored when an entry matches `<Module>`, and
  a referenced module that matches is never searched for. This includes Clause's
  [library modules](library.md#which-module-a-name-resolves-to).
- A source that `sources` lists literally (no wildcard) is never ignored: the
  rules have no effect on it, and the build warns `listed in sources but matched
  by ignore_modules or ignore_dirs; ignoring it has no effect`. Wildcard matches
  and `source_dirs` are ignored as described.
- Both apply to `sources`, `source_dirs`, literal sources found through
  `source_search_paths`, the [module search](#paths-and-discovery) of
  `source_search_paths` and the library, and the search for
  [parse transform sources](transforms.md#transform-modules). A search passes
  over an ignored `Module.erl` and keeps looking in later directories.
- A module is ignored when `ignore_modules` names it, or when no batch source
  defines it and its only sources found were ignored (by search, or skipped from
  the target's sources, by file name). A module defined by a compiled source is
  never ignored.
- References to an ignored module (`M:F(...)`, `-import`, `fun M:F/A`,
  `apply(M, F, [...])` with literal names) reach a generated placeholder module
  `M` exporting each function named this way; every one returns
  `{module_ignored, M}` instead of running. A function the batch never names
  literally stays `undef` for dynamic calls. A type of an ignored module named
  in a spec or type (`helper:result()`) warns `attempt to import a type from a
  module ignored in project settings: helper:result` and is treated as `term()`.
  `-behaviour(M)` with an ignored `M` warns `behaviour M undefined`.
- `--print-inputs` lists each ignored referenced module after the added sources
  as `ignored module <name> (referenced at <file>:<line>)`; `--print-env` prints
  both keys.

```toml
[targets.options]
source_search_paths = ["deps/**"]
ignore_dirs = ["deps/*/test", "src/legacy"]
ignore_modules = ["telemetry", "*_SUITE"]
```

## Effective options

| Setting | Manifest and CLI combination |
| --- | --- |
| `include_dirs` | Manifest order, after CLI `-I` (last CLI `-I` searched first); an entry with wildcards (`deps/*/include`, `apps/**/include`) expands in place to the existing directories it matches, sorted by UTF-8 path bytes (`**` also matches none, directory symlinks are skipped); an unmatched pattern is an error |
| `source_search_paths` | Fallback roots for literal sources and the modules the target names but does not list; patterns expand like `include_dirs` (unmatched: `unmatched source search path pattern`) |
| `defines` | Manifest first, then CLI; duplicates across both are errors |
| `applications` | Manifest map; CLI entries replace matching names |
| Feature lists | Manifest settings, then ordered CLI changes; CLI wins |
| `output` | Per target, executable targets only; CLI `-o` overrides for a single selected target |
| `entry` | Per target; CLI `--entry` overrides for a single selected target |
| `cache_inference_in_dirs` | Implicit OTP patterns, then the manifest's, then CLI `--cache-inference-in-dirs` ([inference cache](#inference-cache)) |

## Build dependency

Compiler-private toml++ 3.4.0 (MIT), header-only with exceptions, unreleased
syntax disabled. Archive SHA-256
`8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155`.

- macOS: `brew install tomlplusplus` (formula 3.4.0) is detected.
- Windows: CMake downloads and verifies the release into `thirdparty/`.
- Elsewhere: install it, extract to `thirdparty/tomlplusplus-3.4.0`, or pass
  `-DCLAUSE_TOML_ROOT=<path>` (takes precedence, disables download).
- Runtime-only builds do not look for it.

The [build cache](#build-caches) uses the SQLite 3.54.0 amalgamation (public
domain), compiled into the compiler's cache library only (`clause_sqlite`,
without the project's warning flags). CMake downloads
`https://www.sqlite.org/2026/sqlite-amalgamation-3540000.zip` into `thirdparty/`
on every host and verifies its SHA-256
`68e913b0fe8ec6b4e5f391c594a28872884e80d6cd042a1e50c957be46b5aa4d`;
`-DCLAUSE_SQLITE_ROOT=<directory with sqlite3.c and sqlite3.h>` takes precedence
and disables the download.

## Limits

Manifest size 1 MiB; at most 1,024 targets and 100,000 TOML nodes. Each source
expansion allows 100,000 visited entries, 128 directory levels and 16,000,000
wildcard transitions. Exhaustion is an explicit error; these are work bounds,
not I/O deadlines.
