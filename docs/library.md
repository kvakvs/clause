# Library modules

Plan 11 step 39 (2026-10-07): a project-owned subset of OTP's `lists` and
`maps` modules, written in Erlang from OTP's documented behavior (not copied
from OTP sources) and compiled with the programs that use it.

## Contents

| Module | Functions |
| --- | --- |
| [`lists`](../library/stdlib/lists.erl) | `append/1,2`, `filter/2`, `foldl/3`, `foldr/3`, `keyfind/3`, `map/2`, `member/2`, `nth/2`, `reverse/1,2`, `seq/2,3`, `sort/1` |
| [`maps`](../library/stdlib/maps.erl) | `find/2`, `fold/3`, `from_list/1`, `get/2`, `keys/1`, `put/3`, `to_list/1`, `values/1` |
| [`file`](../library/kernel/file.erl) | `open/2`, `read/2`, `write/2`, `read_line/1`, `position/2`, `close/1`, `read_file/1`, `write_file/2`, `delete/1`, `rename/2`, `list_dir/1`, `make_dir/1`, `del_dir/1` over the file driver ([files](ports.md#standard-io-and-files)) |
| [`io`](../library/stdlib/io.erl) | `get_line/1,2`, `get_chars/2,3` (standard input server or an open file); `format/1,2` and `put_chars/1` are runtime builtins |
| [`gen_tcp`](../library/kernel/gen_tcp.erl) | `listen/2`, `accept/1,2`, `connect/3,4`, `send/2`, `recv/2,3`, `shutdown/2`, `controlling_process/2`, `close/1` over socket ports ([sockets](ports.md#sockets-57f)) |
| [`gen_udp`](../library/kernel/gen_udp.erl) | `open/1,2`, `send/4`, `recv/2,3`, `controlling_process/2`, `close/1` |
| [`inet`](../library/kernel/inet.erl) | `port/1`, `sockname/1`, `peername/1`, `setopts/2`, `close/1` |
| [`clause_socket`](../library/kernel/clause_socket.erl) | Clause's own: the socket driver protocol the three modules above share |
| [`erlang`](../library/erts/erlang.erl) | Generated stubs of every OTP 29 `erlang` export; builtins replace theirs, `nif_error/1,2` have bodies, others raise `{not_implemented, F, A}` when called ([erlang module](builtins.md#erlang-module)) |
| [`os`](../library/kernel/os.erl) | `cmd/1` over a port ([subprocesses](ports.md#subprocesses)); `type/0` and `getenv/1` are runtime builtins |

Results and error reasons match OTP 29, including the error shapes of OTP's
Erlang implementations (`lists:map(F, x)` is `{case_clause, x}`,
`lists:nth(0, L)` `function_clause`, `lists:seq(1, 10, 0)` `badarg`) and of
its BIFs (`lists:member/2`, `lists:keyfind/3`, `lists:reverse/2`, the `maps`
functions: `badarg`, `{badmap, M}`, `{badkey, K}`). `lists:member/2` matches
with `=:=`, `lists:keyfind/3` with `==`. `lists:sort/1` is a stable merge
sort. Stack traces name the library functions, not OTP's.

## How programs get them

- When a batch (positional inputs or one project target) names a module with
  a literal atom — `M:F(...)` with literal `M`, `fun M:F/A`,
  `apply(M, F, Args)` with literal `M`, `-behaviour(M)` or `-import(M, ...)` —
  that no input defines, the compiler looks for `M.erl` in the library's
  application directories, then in the project's `source_search_paths`
  ([projects](projects.md#paths-and-discovery)), parses and compiles the
  first one found into the batch, then repeats for the modules the added
  files name ([which module a name resolves to](#which-module-a-name-resolves-to),
  plan step 41K). A call `M:F(...)` of a catalog builtin (`io:format/2`,
  `os:type/0`) names no module.
- The library is `library/` of the build tree, found relative to `clau` like
  the runtime archive, with one directory per OTP application (`erts`,
  `kernel`, `stdlib`).
- A module of the batch with the same name replaces the library module, with
  a warning.
- Library modules are ordinary batch modules: they link into executables and,
  for object or IR output, produce their own artifacts.
- A module reached only through runtime names (`M:F(...)` with `M` a
  variable) is not added: such a call raises `undef` unless something else
  names the module.
- Library functions are Erlang code, so they will yield like other code once
  the scheduler exists.

## Runtime module replacements

Plan 11 step 41I (2026-10-12) decides how Clause stands in for the OTP
modules that ERTS implements in C (NIFs, BIFs, port drivers, the preloaded
modules of `erts/preloaded/src`) and for modules OTP generates while it builds
(`unicode_util`). OTP's `kernel` and `stdlib` are plain Erlang but call those
modules; steps 41J onward implement this contract.

### Provided or missing

Whatever needs the runtime and cannot be written in plain Erlang is decided
per module, and per group of functions where a module mixes concerns:

- **Provided:** Clause ships the module with OTP 29's exact export list.
  Every function has one of four statuses: `implemented` (a runtime builtin
  or an Erlang body over existing runtime services), `refused` (returns or
  raises what OTP callers already handle when a feature is absent, such as
  `{error, enotsup}`), `stub` (raises `error({not_implemented, F, A})` with
  `M:F/A` on top of the stack, as the [`erlang` stubs](builtins.md#erlang-module)
  do; not done yet; user direction 2026-10-12), or `declined` (a stub that stays, with a reason such as distribution
  or code loading pending plan step 71). A module starts with every function
  a stub, so it is discoverable and its callers compile; groups then move to
  their decided status.
- **Missing:** Clause ships no module at all, possibly forever, and records
  why and which higher-level Clause modules take its callers' place. The
  low-level `prim_*` file and socket layers are the typical case: programs
  use Clause's `file`, `gen_tcp`, `gen_udp` and `inet` instead, and because
  Clause modules win over OTP's (below), OTP's callers of a missing module
  never enter a batch. A missing module never gets a stub, so a reference to
  it is a compile-time error, never a run-time surprise.

Functions that are trivial for the runtime are implemented without a
separate decision.

### Which module a name resolves to

OTP is optional on the user's machine; when present it must not change what
a build compiles. A module name resolves in this order:

1. **Explicit inputs:** sources the user lists (`sources`, `source_dirs`,
   positional inputs). One that has the name of a Clause module replaces it,
   with a warning naming both files.
2. **Clause modules:** the library modules, provided replacements included.
3. **`source_search_paths`**, as described in
   [projects](projects.md#paths-and-discovery).

The name of a missing module is reserved: a same-named file on a search path
(for example OTP's `erts/preloaded/src/prim_file.erl`) is not used, and the
diagnostic says that Clause does not provide the module. An explicit input
may still provide it, with the replacement warning.

`ignore_modules` applies to Clause modules too: an ignored Clause module is
missing from the batch, and an explicit input can then provide it without a
warning, since nothing is replaced.

### Files

- Library modules live in `library/<application>/`, named after the OTP
  application that owns the module in OTP: `erts` (preloaded counterparts and
  `erlang`), `kernel` (`file`, `gen_tcp`, `gen_udp`, `inet`, `os`, and Clause's
  own `clause_socket`), `stdlib` (`lists`, `maps`, `io`, `unicode_util`). The
  library lookup searches every application directory; a module name exists
  in at most one of them.
- `library/modules.terms` lists every module Clause decides about:
  `{Module, Application, provided}.` or
  `{Module, Application, missing, "reason"}.` The compiler reads it to know
  which names are Clause modules and which are reserved.
- Each provided module has an inventory `<module>.terms` next to it:
  `{Name, Arity, Group, Status}.` per OTP 29 export, sorted by name and arity,
  where `Group` names the decision group (for example `stdlib`,
  `distribution`, `beam`). `erlang.terms` keeps its own format
  (`{Name, Arity, Flags, Status}`, [erlang module](builtins.md#erlang-module)).
- Export lists are observed from a host OTP 29 (`M:module_info(exports)`);
  no OTP source is copied. Results and error terms of implemented functions
  match OTP 29; deliberate differences go to
  [differences](differences.md).
- Every Clause library module exports OTP 29's complete list for its name,
  so OTP code compiled against it finds every function; Clause's own
  `lists`, `maps`, `file`, `io`, `os`, `gen_tcp`, `gen_udp` and `inet` get
  inventories too and stubs for what they do not implement yet.

### Types

Plan 11 step 41K1. Because Clause's modules win over OTP's, OTP code naming
`file:filename()` or `erlang:time_unit()` in its specs must find those types
in Clause's modules. Every provided module whose OTP counterpart exports
types includes `<module>.types.hrl` from its own directory: the
`-export_type` list, then the exported types and the local types and records
they use, as OTP defines them (the documentation shows the same definitions;
the installed documentation carries no type definitions, so they come from
the implementation: the beam's debug information, or the shipped source of a
preloaded module such as `erlang`). `escript
tests/compiler/library/observe_types.escript LIBRARY APP MODULE` writes the
header from the host OTP, and `--check` compares its forms with a fresh
observation (CTest `library_freshness`); `library_modules.py check`
requires each header to be included by its module. Types the headers name
from modules outside the program (`unicode:chardata()`,
`calendar:datetime()`) and OTP's wide unions (`file:posix()` has more
members than the type graph keeps) resolve to `term()` without a warning:
the diagnostics belong to Clause's library, not to the user's program.

### Kernel services at startup

OTP's `file`, `io`, `code` and `logger` call registered servers
(`file_server_2`, `code_server`, a group leader's io server, `logger`) that
the `kernel` application starts at boot. A Clause executable starts none of
them, and decided 2026-10-12 it keeps starting none for now: Clause's own
`file` and `io` need no server, and an OTP module that does need one fails
the way it fails on OTP without that server (`noproc`). Cost: OTP code that
logs through `logger` or calls `code` at run time does not work until a
later ledger entry selects a minimal service set for the startup object.
Booting the whole `kernel` application is not planned.

### Ledger

The luerl project drives this work. After each step, run

```text
clau.bat --project test-luerl --entry luerl:init
```

classify every error (`unknown module`, `undefined function M:F/A`,
unsupported syntax or feature, other), and assign each to a step of plan 11
phase H3, another backlog item, or a recorded decline. With the run's stderr
saved to a file, `python tests/compiler/library/library_modules.py report
FILE` prints the classification as tables: each unknown module with its error
count, calling modules and called functions with arities; undefined functions
by module; other messages with their counts.

### Tools

Plan 11 step 41J. All live in `tests/compiler/library/`:

- `escript observe_module.escript LIBRARY APP MODULE` writes
  `LIBRARY/APP/MODULE.terms` from the host OTP's `MODULE:module_info(exports)`,
  keeping the group and status of every entry already there; a new export
  starts as `{Name, Arity, default, stub}`.
- `python library_modules.py generate LIBRARY APP MODULE` writes the module
  file once, every export a stub; from then on the file is edited by hand.
- `python library_modules.py check ROOT` (CTest `library_inventory`, no OTP)
  reports every disagreement: a provided module without its files, a missing
  module with a file, an inventory not listed as provided, an export not in
  the inventory or the reverse, a `stub`/`declined` entry that is not a stub
  raising `undef`, an `implemented`/`refused` entry without a body or a
  catalog builtin. `library_rejects` proves it on a deliberately
  inconsistent fixture library.
- CTest `library_freshness` (label `host_otp`) re-observes every inventory
  and requires it unchanged, then observes, generates, checks and compiles a
  new module end to end. `library_report` runs `clau` on a fixture program
  and checks the report's rows.

| Module | Decision (2026-10-12) | Reason or plan step |
| --- | --- | --- |
| `init` | provided, all stubs | argument and status answers (41L) |
| `persistent_term` | provided, all stubs | runtime-wide term table (41M) |
| `erts_internal` | provided, all stubs; to group: `stdlib` (`cmp_term/2`, `map_next/3`, `list_to_integer/2`, map helpers) implemented, `distribution` and `beam` declined | 41N; no distribution, no `.beam` files |
| `atomics`, `counters`, `erts_code_purger` | provided, all stubs | 41N |
| `unicode_util` | provided, all stubs | tables generated from the Unicode data (41O) |
| `erl_prim_loader` | provided, all stubs | 41P decides between implementing it over the file driver and shadowing its callers |
| `zlib` | provided, all stubs | 41Q: pinned zlib in the runtime, or refused |
| `prim_eval` | provided, all stubs | 41S |
| `application_controller` | provided, all stubs | shadows OTP's, which calls `prim_file` (luerl names it through OTP's `application`) |
| `lists`, `maps`, `io`, `file`, `os`, `gen_tcp`, `gen_udp`, `inet` | provided, OTP 29's full export lists; what Clause had stays implemented, the rest stubs | grow as the ledger demands |
| `prim_file`, `prim_buffer` | missing | Clause's `file` is the file layer (41P) |
| `prim_socket`, `prim_net`, `prim_inet`, `socket_registry` | missing | Clause's `gen_tcp`, `gen_udp`, `inet` are the network layer (41R) |

Latest luerl layer (2026-10-12, after steps 41K1 and 41K2, with
`NEW_STACKTRACE` defined as luerl's `rebar.config.script` does on OTP 21 and
later, and `inet` no longer in `ignore_modules`): the target compiles and
links with no error. 83 warnings remain: 74 types of the ignored `erl_parse`
and `compile`, 8 types of OTP modules outside the program named by OTP code
the target compiles, one type graph budget widening in that code. The executable starts `luerl:init/0` and exits 0. The next
layer is run-time behavior: which stubs luerl reaches when it runs Lua code.
