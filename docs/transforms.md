# Parse transforms

## Using parse transforms

- **Requirement:** Erlang/OTP 29 installed on the host, only for compiling
  modules that use a transform. `clau` takes `--erl PATH`, else `erl` on
  `PATH`, else on Windows `%ProgramFiles%\Erlang OTP\bin\erl.exe`.
  `--erl none` never runs host code: modules that need a transform fail.
- **Invoke a transform** in the source with
  `-compile({parse_transform, Module}).` (or inside a `-compile([...])` list),
  or for every module of the command with `--parse-transform Module`
  (repeatable; these run first, like `erlc +'{parse_transform,M}'`).
- **Transform modules** are found as `.beam` files in `--transform-path`
  directories (repeatable) and the project's `transform_paths`, then as
  `Module.erl` among the batch's sources and `source_search_paths` (compiled
  by the host OTP on every build, with the modules they call from there;
  `ignore_dirs` and `ignore_modules` apply),
  then among OTP's modules (`ms_transform`, `qlc`).
- **Precompile** a transform that comes from another library (always), or a
  project transform the host compiler cannot build with the target's include
  directories and defines: `erlc -o deps/lib/ebin deps/lib/src/*.erl` with
  the host OTP, then name `deps/lib/ebin` with `--transform-path` or in
  `transform_paths`. The `.beam` must come from the host's OTP release; a
  `.beam` there wins over a source of the same module.
- **What runs it:** compiling, `--print-ast`, `--print-source` and
  `--print-abstr` (which shows the transformed forms); `--parse-check`,
  `--preprocess-check` and `--print-pp` do not.

```sh
clau --erl "C:/Program Files/Erlang OTP/bin/erl.exe" --transform-path ebin -o app app.erl
```

Clause applies `{parse_transform, Module}` the way `erlc` does. It exports the
parsed module as OTP [abstract format](https://www.erlang.org/doc/apps/erts/absform.html)
forms, runs the transform module on an Erlang/OTP 29 installed on the host,
imports the returned forms as the module's new syntax and compiles that. OTP is
needed only while compiling a module that uses a transform; generated programs
never need it. Decided in plan step 73A; implemented by steps 73B–73G
(`compiler/src/transforms/`).

## OTP behavior

From pinned `lib/compiler/src/compile.erl` (`transform_module`,
`foldl_transform`, `maybe_strip_columns`) and observed under OTP 29.1.1:

- Transforms run as the first standard pass: after `epp` and parsing, before
  `erl_lint`, before `erl_internal` adds `module_info/0,1` and
  `behaviour_info/1`.
- The transform list is every `{parse_transform, M}` of the compile options
  (`erlc +'{parse_transform,m}'`), then of the `-compile` attributes in form
  order. A transform named twice runs twice. Before the first runs, those
  options are removed from the forms: a `-compile` attribute holding only the
  option disappears, a list loses the element.
- `M` must load (`code:ensure_loaded/1`) and export `parse_transform/2`, else
  the error `undefined parse transform 'M'` (no location).
- `M:parse_transform(Forms, Options)` gets the forms and the compile options
  (not the source's `-compile` options): `{features, Enabled}`,
  `report_warnings`, `report_errors`, `{cwd, Dir}`, `{outdir, Dir}`, `{i, Dir}`
  per include directory, `{d, Name[, Value]}` per define, plus any
  `{parse_transform, M}` given as an option.
- It returns `Forms`, `{warning, Forms, Warnings}` or
  `{error, Errors, Warnings}`, where each list is
  `[{File, [{Location, Module, Descriptor}]}]` formatted by
  `Module:format_error(Descriptor)`. An exception is the error
  `error in parse transform 'M': ...` (`compile:format_error/1` of
  `{parse_transform, M, {Class, Reason, Stack}}`). The next transform gets the
  previous one's forms; the first error stops the chain.
- `M:parse_transform_info/0` returning `#{error_location => line}`, or the
  compile option `{error_location, line}`, strips columns from the forms before
  `M` runs and from the final forms.
- Forms carry `{Line, Column}` annotations. `epp` adds
  `{attribute, {1,1}, file, {File, 1}}` first, the same for each included file
  when it starts, and `{attribute, {L,1}, file, {File, L}}` when the includer
  continues at line `L`, the line after its `-include`. Macro definitions and
  other directives leave no form. The list ends with `{eof, Location}`, the
  end of the main file. Preprocessor and parse errors stay in the list as
  `{error, E}` forms and `erl_lint` reports them after the transforms.
- OTP 29 forms that `absform.md` does not list: `{attribute, A, native_record,
  {Name, Fields}}`; record names `{Module, Name}` (qualified) and `[]` (`#_`)
  in `record`/`record_field` expressions; `{type, A, record, [{tuple, A,
  [Module, Name]} | Fields]}` for qualified record types; `{zip, A,
  Qualifiers}` comprehension qualifiers; `{mc, ...}`, `m_generate` and the
  `_strict` generators.
- `erlc +to_abstr` writes the forms after transforms (and `erl_lint`) to
  `File.abstr`, one `~p.` term per form; `erlc File.abstr`
  (`compile:file(F, [from_abstr])`) compiles such a file without a
  preprocessor or parser.

## Clause pipeline

1. Preprocess and parse the module. Preprocessor or parse errors stop here;
   transforms do not run on a module with errors (differs from OTP, which
   passes the errors on as forms).
2. Collect the transforms in OTP's order: `--parse-transform` options and the
   project's `parse_transforms`, then `-compile` attributes. A module without
   any skips the rest and never starts OTP.
3. Remove those options from the forms and export them: `{Line, Column}`
   annotations as `erl_parse` assigns them, `epp`'s `file` attributes, and
   `eof`. The predefined functions are not part of the forms yet.
4. Run the [loader](#loader) once for the module.
5. Import the returned forms by turning them into tokens located at their
   annotations and parsing them with Clause's parser; a `file` attribute
   switches the file of the following forms as in `erl_lint`.
6. Add the predefined functions, then lint, analyze and compile as usual.

Transforms run when a module is compiled and for `--print-ast`,
`--print-source` and `--print-abstr`, which print the transformed module.
`--preprocess-check`, `--parse-check` and `--print-pp` do not run them.

## Interchange

- `clau` and the loader exchange External Term Format files
  (`term_to_binary`, version 131, uncompressed): exact floats, UTF-8 atoms,
  big integers and no text parsing in Erlang. The format itself (tags,
  layouts, bounds checks) lives once in `abi/include/clause/abi/external_term.hpp`,
  independent of any term representation, so the runtime's future
  `term_to_binary/1`, `binary_to_term/1` and distribution reuse it.
  Clause's compiler term model holds atoms,
  integers, floats, proper and improper lists, tuples, maps and bitstrings;
  pids, ports, references and funs are rejected with a diagnostic.
- `.abstr` text is the [`file:consult/1`](https://www.erlang.org/doc/apps/kernel/file.html#consult/1)
  syntax: one term per form, each ending in `.`. `--print-abstr` prints each
  module's forms after transforms in that syntax to stdout, one form per line;
  a `.abstr` input file compiles like `erlc File.abstr`. Clause writes
  canonical text, not OTP's `~p` layout; equal terms are what matter.
  The forms equal `epp:parse_file(F, [{location, {1, 1}}])` for every
  construct of `tests/fixtures/transforms/abstract/`, including `epp`'s
  `file` attributes (an explicit `-file` is annotated
  `[{generated, true}, {location, L}]`) and an `eof` counted from the last
  `-file` of the main file.

## Importing forms

Returned forms, and `.abstr` inputs (positional or project `sources`), become
tokens located at their annotations and go through Clause's parser, so they
get the same syntax checks as source; operator precedence decides where
parentheses are added. `{Line, Column}`, a bare `Line` (shown as `file:line:`)
and property lists with `location` and `file` are accepted; `generated` is
ignored. A file attribute switches the file of the following forms; when that
file exists, imported code keeps its debug lines and source comments.
Malformed forms are errors at their annotation (`unknown expression bogus`,
`expected {Name, Arity}`, `invalid annotation`) and the module fails; OTP's
`{error, _}`/`{warning, _}` forms are reported with their descriptor, which
needs OTP to format. Importing an exported module and exporting it again
gives the same forms.

## Host OTP

- `erl` is found in order: `--erl PATH`, `erl` on `PATH`, then on Windows
  `%ProgramFiles%\Erlang OTP\bin\erl.exe`. Not finding one when a module
  needs a transform is an error naming the transform and the module; there is
  no fallback that skips transforms.
- The host must run OTP 29 (`erlang:system_info(otp_release)`); another
  release is an error.
- The [loader](#loader) runs as `erl -noshell -noinput -pa Dir ... -eval ...`
  (code path directories made absolute) with a time limit of 600 s. Its
  stdout and stderr (including the transform's `io:format` output) are copied
  to `clau`'s stderr after it ends.
  A loader that exits without writing a reply, times out or cannot start is
  an error.
- A transform executes arbitrary code with the user's rights, exactly as it
  would under `erlc`.

## Loader

`compiler/src/transforms/loader/clause_transform_loader.erl` is project-owned
and embedded into `clau`. Each run writes it and the request into a fresh
temporary directory, `clause-transform-*` below `<build_dir>/tmp`
([build directory](projects.md#build-directory)) or, for positional inputs
without `--build-dir`, the system's temporary directory, removed after the
run; `erl` compiles and loads the loader in memory. The request
holds the forms, the options, the transform list, the module's file name and
the project transform sources to build. With a build directory, the reply and
the loader's output are kept in the [build cache](projects.md#build-caches)
and reused while the request and everything it depends on are unchanged; the
host's sticky module list is kept the same way. The loader:

1. checks the OTP release;
2. compiles the requested project transform sources in memory and loads them;
3. for each transform: loads it, strips columns as `parse_transform_info/0`
   asks, calls `parse_transform/2` and formats warnings and errors to
   `{File, Location, Text}` with `Module:format_error/1`;
4. writes `{ok, Forms, Warnings}` or `{error, Errors, Warnings}` and halts.

## Transform modules

A transform module is looked up in this order:

1. **Precompiled `.beam` files** in the directories of `--transform-path DIR`
   (repeatable) and the project option `transform_paths`, which go on the
   loader's code path before OTP's. This is the only way to use a transform
   that comes from a dependency library: build that library with the host OTP
   first (for example `erlc -o deps/lib/ebin deps/lib/src/*.erl`); Clause
   never builds dependencies. The `.beam` must come from the host's OTP major
   release.
2. **The current project's sources**: a `Module.erl` in the batch or its
   `source_search_paths`, compiled by the loader in memory with the target's
   include directories and defines on every run, together with the project
   modules it calls by name (found the same way). The search passes over
   sources that `ignore_dirs` or `ignore_modules` leave out, as the
   [module search](projects.md#ignored-sources) does. Clause looks for these
   sources once per batch and keeps them in memory; the loader still compiles
   them for every module the transform runs on, so precompile a large
   transform to save that time.
   Modules the host loads from its sticky directories (`kernel`, `stdlib`,
   `compiler`, such as `ms_transform` or `qlc_pt` when OTP's sources are on a
   search path) cannot be reloaded, so the host's own modules are used: their
   sources, and the modules only they call, are not looked for or compiled.
   Clause asks the host `erl` for these modules once per invocation.
3. **OTP's own modules** (`ms_transform`, `qlc`) from the host's code path.
   OTP ships `erl_id_trans` only as an example source, not as a module.
   Their headers resolve as every `include_lib` does
   ([preprocessor](preprocessor.md#compatibility-policies)): map the
   application, as in
   `--app-dir "stdlib=C:/Program Files/Erlang OTP/lib/stdlib-8.1"` for
   `include_lib("stdlib/include/ms_transform.hrl")`, or name the transform
   directly with `-compile({parse_transform, ms_transform})`.

Compiling a project transform can fail where its real build succeeds:
headers of dependencies, a transform of the transform, or macros that only
another build tool defines. Clause then reports the compiler's messages
against the transform's source with the hint to precompile it into a `.beam`
with the host OTP and pass its directory with `--transform-path`. A project
transform is also compiled by Clause as an ordinary module when it is part
of the batch; its own calls (`erl_syntax`, `erl_parse`, ...) may then need
library modules Clause does not ship, so keep transforms out of a target's
`sources` when only the transform needs them.

## Diagnostics

| Situation | Message |
| --- | --- |
| No host OTP | `File: parse transform 'M' needs Erlang/OTP 29 on the host; pass --erl` |
| `--erl` names no executable | `File: erl not found: PATH` |
| Wrong OTP release | `parse transforms need Erlang/OTP 29 on the host, found N` |
| Transform not found | `File: undefined parse transform 'M'` (OTP's text) |
| Transform raised | `File: error in parse transform 'M': ...` (OTP's text) |
| Transform warning or error | `File:Line:Column: Text` as returned |
| Transform source does not compile | its messages, then the precompile hint |
| A transform path does not exist | `File: parse transform path is not a directory: DIR` |
| A `.beam` on a transform path does not load | `File: cannot load parse transform 'M' from PATH: Reason` |
| Loader failed, timed out or wrote no reply | `parse transform loader failed: ...` |
| Invalid returned forms | the parser's diagnostic at the form's location |

## Options

| CLI | Project (`[targets.options]`) | Meaning |
| --- | --- | --- |
| `--parse-transform M` | `parse_transforms = ["m"]` | Apply `M` to every module, before its own `-compile` transforms (CLI ones first) |
| `--transform-path DIR` | `transform_paths = ["deps/*/ebin"]` | Code path for precompiled transform `.beam` files; patterns as in `source_search_paths` |
| `--erl PATH` or `none` | — | Host `erl` executable; `none` never runs host code |
| `--print-abstr` | — | Print the forms after transforms instead of compiling |

A project that uses its own transform and a precompiled one from a dependency:

```toml
[[targets]]
name = "app"
sources = ["src/*.erl"]

[targets.options]
source_search_paths = ["transforms"]  # transforms/pt_rewrite.erl, compiled for each build
parse_transforms = ["pt_dep"]         # applied to every module of the target
transform_paths = ["deps/*/ebin"]     # deps/ptdep/ebin/pt_dep.beam, built with erlc first
```

Keep transform modules out of a target's `sources` when the program does not
call them: as ordinary modules they would be compiled by Clause too, and their
calls into `erl_parse` or `erl_syntax` need modules Clause does not ship.
