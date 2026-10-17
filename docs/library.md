# Library modules

Clause ships its own versions of the OTP modules programs call, written in
Erlang from OTP's documented behavior (not copied from OTP sources) over
runtime [builtins](builtins.md), and compiles them with the programs that use
them.

## Contents

Every provided module exports OTP 29's complete list for its name; its
inventory `<module>.terms` ([files](#files)) records which functions are
implemented and which are stubs.

| Module | Contents |
| --- | --- |
| [`lists`](../library/stdlib/lists.erl) | The common functions; data functions such as `sublist/2,3`, `flatten/1,2`, `usort/1`, `keymember/3`, `zip/2,3`, `merge/2` are runtime builtins; every documented function is implemented ([lists module](builtins.md#lists-module)) |
| [`maps`](../library/stdlib/maps.erl) | Every documented function; `get/3`, `merge/2`, `intersect/2`, `with/2`, ... are runtime builtins, the functions taking funs Erlang ([maps module](builtins.md#maps-module)) |
| [`ordsets`](../library/stdlib/ordsets.erl), [`orddict`](../library/stdlib/orddict.erl), [`proplists`](../library/stdlib/proplists.erl) | Every documented function; the walks over lists are runtime builtins, the functions taking funs Erlang ([collections](builtins.md#collections)) |
| [`io`](../library/stdlib/io.erl) | `get_line/1,2`, `get_chars/2,3` (standard input server or an open file), `request/1,2`, `requests/1,2` (OTP's I/O protocol, [files](ports.md#standard-io-and-files)); `format/1,2,3`, `fwrite/1,2,3`, `put_chars/1,2` and `nl/0,1` are runtime builtins ([io](io.md)) |
| [`binary`](../library/stdlib/binary.erl) | Every function; `replace/3,4` and `list_to_bin/1` in Erlang, the others runtime builtins ([binary module](builtins.md#binary-module)) |
| [`math`](../library/stdlib/math.erl) | Every function as a runtime builtin ([math module](builtins.md#math-module)) |
| [`unicode`](../library/stdlib/unicode.erl) | Conversions, `bin_is_7bit/1` and byte order marks as runtime builtins ([unicode module](builtins.md#unicode-module)); normalization and properties are stubs |
| [`unicode_util`](../library/stdlib/unicode_util.erl) | Every function over generated tables ([unicode_util](builtins.md#unicode-util)) |
| [`re`](../library/stdlib/re.erl) | Every function but `import/1`, over PCRE2 ([re module](builtins.md#re-module)) |
| [`file`](../library/kernel/file.erl) | Open, read, write, position and close files, whole-file reads and writes, file information and directory operations over the file driver ([files](ports.md#standard-io-and-files)) |
| [`gen_tcp`](../library/kernel/gen_tcp.erl), [`gen_udp`](../library/kernel/gen_udp.erl), [`inet`](../library/kernel/inet.erl) | Sockets over socket ports ([sockets](ports.md#sockets)) |
| [`clause_socket`](../library/kernel/clause_socket.erl) | Clause's own: the socket driver protocol the three modules above share |
| [`os`](../library/kernel/os.erl) | `cmd/1` over a port, environment, time and version queries ([maps module](builtins.md#maps-module)) |
| [`code`](../library/kernel/code.erl) | Loaded-module queries without a code server ([code module](builtins.md#code-module)); paths and loading are stubs |
| [`application_controller`](../library/kernel/application_controller.erl) | All stubs; shadows OTP's, which calls `prim_file` (luerl reaches it through OTP's `application`) |
| [`erlang`](../library/erts/erlang.erl) | Generated stubs of every OTP 29 `erlang` export; catalog builtins have placeholder bodies, `nif_error/1,2` have bodies, others raise `{not_implemented, F, A}` when called ([erlang module](builtins.md#erlang-module)) |
| [`init`](../library/erts/init.erl) | A started system's answers; `boot/1`, `restart/0,1` and `make_permanent/2` refused ([init module](builtins.md#init-module)) |
| [`persistent_term`](../library/erts/persistent_term.erl) | Every function as a runtime builtin ([persistent terms](builtins.md#persistent-terms)) |
| [`atomics`](../library/erts/atomics.erl), [`counters`](../library/erts/counters.erl) | Every function ([atomics and counters](builtins.md#atomics-and-counters)) |
| [`erts_internal`](../library/erts/erts_internal.erl), [`erts_code_purger`](../library/erts/erts_code_purger.erl) | What `stdlib` and `kernel` call; distribution, `.beam`, emulator and purger-process groups declined ([erts_internal](builtins.md#erts-internal)) |
| [`erl_prim_loader`](../library/erts/erl_prim_loader.erl) | File queries over `file`; loading and archives declined ([files](ports.md#standard-io-and-files)) |
| [`zlib`](../library/erts/zlib.erl) | Every function over the pinned zlib ([zlib module](builtins.md#zlib-module)) |
| [`prim_eval`](../library/erts/prim_eval.erl) | `'receive'/2` over the steps of a compiled receive ([prim eval](builtins.md#prim-eval)) |

Results and error reasons match OTP 29, including the error shapes of OTP's
Erlang implementations (`lists:map(F, x)` is `{case_clause, x}`,
`lists:nth(0, L)` `function_clause`, `lists:seq(1, 10, 0)` `badarg`) and of its
BIFs (`lists:member/2`, `lists:keyfind/3`, `lists:reverse/2`, the `maps`
functions: `badarg`, `{badmap, M}`, `{badkey, K}`). `lists:member/2` matches
with `=:=`, `lists:keyfind/3` with `==`. `lists:sort/1` is a stable merge sort
whose passes alternate ascending and descending runs, so no merge reverses a run.
Stack traces name the library functions, not OTP's.

## How programs get them

- When a batch (positional inputs or one project target) names a module with a
  literal atom — `M:F(...)` with literal `M`, `fun M:F/A`, `apply(M, F, Args)`
  with literal `M`, `-behaviour(M)` or `-import(M, ...)` — that no input
  defines, the compiler looks for `M.erl` in the library's application
  directories, then in the project's `source_search_paths`
  ([projects](projects.md#paths-and-discovery)), parses and compiles the first
  one found into the batch, then repeats for the modules the added files name
  ([which module a name resolves to](#which-module-a-name-resolves-to)). A
  call `M:F(...)` of a catalog builtin (`io:format/2`, `os:type/0`) names no
  module. `--print-inputs` lists the inputs this adds.
- The library is `library/` of the build tree, found relative to `clau` like the
  runtime archive, with one directory per OTP application (`erts`, `kernel`,
  `stdlib`).
- A module of the batch with the same name replaces the library module, with a
  warning; catalog builtins of that name still win
  ([registration](builtins.md#registration)).
- Library modules are ordinary batch modules: they link into executables and,
  for object or IR output, produce their own artifacts. They are Erlang code,
  so they yield like other code.
- A module reached only through runtime names (`M:F(...)` with `M` a variable)
  is not added: such a call raises `undef` unless something else names the
  module.

## Runtime module replacements

Clause stands in for the OTP modules that ERTS implements in C (NIFs, BIFs,
port drivers, the preloaded modules of `erts/preloaded/src`) and for modules
OTP generates while it builds (`unicode_util`). OTP's `kernel` and `stdlib`
are plain Erlang but call those modules.

### Provided or missing

Whatever needs the runtime and cannot be written in plain Erlang is decided per
module, and per group of functions where a module mixes concerns:

- **Provided:** Clause ships the module with OTP 29's exact export list. Every
  function has one of four statuses: `implemented` (a runtime builtin or an
  Erlang body over existing runtime services), `refused` (returns or raises what
  OTP callers already handle when a feature is absent, such as
  `{error, enotsup}`), `stub` (raises `error({not_implemented, F, A})` with
  `M:F/A` on top of the stack, as the
  [`erlang` stubs](builtins.md#erlang-module) do; not done yet), or `declined`
  (a stub that stays, with a reason such as distribution or code loading,
  [step 71](../.agents/11-plan.md#step-71)). A new module starts with every
  function a stub, so it is discoverable and its callers compile; groups then
  move to their decided status.
- **Builtin placeholders:** a function a catalog builtin implements keeps its
  export and the body `F(_, ...) -> clause_builtin_placeholder.`, which never
  runs: registration binds the builtin in its place, and a qualified call
  `M:F(...)` compiles to the bridge. Only an unqualified call inside the module
  would reach the placeholder, so the module calls its own builtins qualified;
  `library_inventory` rejects a placeholder that names no catalog builtin, a
  catalog builtin left as a `not_implemented` stub and an unqualified call of a
  placeholder.
- **Missing:** Clause ships no module at all, possibly forever, and records why
  and which higher-level Clause modules take its callers' place
  (`library/modules.terms`). The low-level `prim_*` file and socket layers
  (`prim_file`, `prim_buffer`, `prim_socket`, `prim_net`, `prim_inet`,
  `socket_registry`) and `socket`, `net` are the missing modules: programs use
  Clause's `file`, `gen_tcp`, `gen_udp` and `inet` instead, and because Clause
  modules win over OTP's (below), OTP's callers of a missing module (OTP's
  `gen_tcp_socket`, `inet_tcp`, ...) never enter a batch. A missing module
  never gets a stub, so a reference to it is a compile-time error, never a
  run-time surprise.

Functions that are trivial for the runtime are implemented without a separate
decision.

### Which module a name resolves to

OTP is optional on the user's machine; when present it must not change what a
build compiles. A module name resolves in this order:

1. **Explicit inputs:** sources the user lists (`sources`, `source_dirs`,
   positional inputs). One that has the name of a Clause module replaces it,
   with a warning naming both files.
2. **Clause modules:** the library modules, provided replacements included.
3. **`source_search_paths`**, as described in
   [projects](projects.md#paths-and-discovery).

The name of a missing module is reserved: a same-named file on a search path
(for example OTP's `erts/preloaded/src/prim_file.erl`) is not used, and the
diagnostic says that Clause does not provide the module. An explicit input may
still provide it, with the replacement warning.

`ignore_modules` applies to Clause modules too: an ignored Clause module is
missing from the batch, and an explicit input can then provide it without a
warning, since nothing is replaced.

### Files

- Library modules live in `library/<application>/`, named after the OTP
  application that owns the module in OTP: `erts` (preloaded counterparts and
  `erlang`), `kernel`, `stdlib`. Clause's private helper modules
  (`clause_socket`, `clause_re`, `clause_unicode`, `clause_zlib`,
  `clause_receive`, `clause_maps`) sit beside the modules they serve. The library lookup
  searches every application directory; a module name exists in at most one
  of them.
- `library/modules.terms` lists every module Clause decides about:
  `{Module, Application, provided}.` or
  `{Module, Application, missing, "reason"}.` The compiler reads it to know
  which names are Clause modules and which are reserved.
- Each provided module has an inventory `<module>.terms` next to it:
  `{Name, Arity, Group, Status}.` per OTP 29 export, sorted by name and arity,
  where `Group` names the decision group (for example `stdlib`, `distribution`,
  `beam`). `erlang.terms` keeps its own format (`{Name, Arity, Flags, Status}`,
  [erlang module](builtins.md#erlang-module)).
- Export lists are observed from a host OTP 29 (`M:module_info(exports)`); no
  OTP source is copied. Results and error terms of implemented functions match
  OTP 29; deliberate differences go to [differences](differences.md).

### Documented functions

`library/documented.terms` lists the public functions OTP 29 documents for
`erts`, `kernel` and `stdlib`: `{Module, Application, Name, Arity}.` per
function whose module and function docs are not hidden and which is not
deprecated, observed from the host OTP's doc chunks by
`tests/compiler/library/observe_docs.escript` (run explicitly). Undocumented
exports stay in the inventories but may remain stubs.

- `library_inventory` checks that every documented function of a provided
  module is in its inventory and is not a stub, except in the modules
  `library_modules.py` lists as partial with the plan step that completes them
  ([plan 21](../.agents/21-lib.md)).
- `python tests/compiler/library/library_modules.py coverage .` prints each
  documented module's implemented and documented counts.

### Types

Because Clause's modules win over OTP's, OTP code naming `file:filename()` or
`erlang:time_unit()` in its specs must find those types in Clause's modules.
Every provided module whose OTP counterpart exports types includes
`<module>.types.hrl` from its own directory: the `-export_type` list, then the
exported types and the local types and records they use, as OTP defines them
(the documentation shows the same definitions; the installed documentation
carries no type definitions, so they come from the implementation: the beam's
debug information, or the shipped source of a preloaded module such as
`erlang`). `escript tests/compiler/library/observe_types.escript LIBRARY APP
MODULE` writes the header from the host OTP, and `--check` compares its forms
with a fresh observation (CTest `library_freshness`); `library_modules.py check`
requires each header to be included by its module. Types the headers name from
modules outside the program (`unicode:chardata()`, `calendar:datetime()`) and
OTP's wide unions (`file:posix()` has more members than the type graph keeps)
resolve to `term()` without a warning: the diagnostics belong to Clause's
library, not to the user's program.

### Kernel services at startup

OTP's `file`, `io`, `code` and `logger` call registered servers
(`file_server_2`, `code_server`, a group leader's io server, `logger`) that the
`kernel` application starts at boot. A Clause executable starts none of them:
Clause's own `file` and `io` need no server, and an OTP module that does need
one fails the way it fails on OTP without that server (`noproc`). OTP code that
logs through `logger` or calls `code` at run time therefore does not work.
Booting the whole `kernel` application is not planned.

### Ledger

[Plan 21](../.agents/21-lib.md) schedules every documented `erts`, `kernel`
and `stdlib` module; real programs (luerl first) set priorities. Compile and run it, classify every error
(`unknown module`, `undefined function M:F/A`, unsupported syntax or feature,
other), and assign each to a plan step, another backlog item, or a recorded
decline. With the run's stderr saved to a file,
`python tests/compiler/library/library_modules.py report FILE` prints the
classification as tables: each unknown module with its error count, calling
modules and called functions with arities; undefined functions by module; other
messages with their counts.

### Tools

All live in `tests/compiler/library/`:

- `escript observe_module.escript LIBRARY APP MODULE` writes
  `LIBRARY/APP/MODULE.terms` from the host OTP's `MODULE:module_info(exports)`,
  keeping the group and status of every entry already there; a new export starts
  as `{Name, Arity, default, stub}`.
- `python library_modules.py generate LIBRARY APP MODULE` writes the module file
  once, every export a stub; from then on the file is edited by hand.
- `python library_modules.py check ROOT` (CTest `library_inventory`, no OTP)
  reports every disagreement: a provided module without its files, a missing
  module with a file, an inventory not listed as provided, an export not in the
  inventory or the reverse, a `stub`/`declined` entry that is not a
  `not_implemented` stub, an `implemented`/`refused` entry without a body or a
  catalog builtin.
  `library_rejects` proves it on a deliberately inconsistent fixture library.
- CTest `library_freshness` (labels `host_otp`, `full_only`) re-observes every inventory and
  requires it unchanged, then observes, generates, checks and compiles a new
  module end to end. `library_report` runs `clau` on a fixture program and
  checks the report's rows.
