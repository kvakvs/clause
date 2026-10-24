# Library modules

Clause ships its own versions of the OTP modules programs call, written in
Erlang from OTP's documented behavior (not copied from OTP sources) over runtime
[builtins](builtins.md), and compiles them with the programs that use them.

## Contents

Every provided module exports OTP 29's complete list for its name; its inventory
`<module>.terms` ([files](#files)) records which functions are implemented and
which are stubs.

| Module | Contents |
| --- | --- |
| [`lists`](../library/stdlib/lists.erl) | The common functions; data functions such as `sublist/2,3`, `flatten/1,2`, `sort/1`, `usort/1`, `keymember/3`, `zip/2,3`, `merge/2` are runtime builtins; every documented function is implemented ([lists module](builtins.md#lists-module)) |
| [`maps`](../library/stdlib/maps.erl) | Every documented function; `get/3`, `merge/2`, `intersect/2`, `with/2`, ... are runtime builtins, the functions taking funs Erlang ([maps module](builtins.md#maps-module)) |
| [`ordsets`](../library/stdlib/ordsets.erl), [`orddict`](../library/stdlib/orddict.erl), [`proplists`](../library/stdlib/proplists.erl) | Every documented function; the walks over lists are runtime builtins, the functions taking funs Erlang ([collections](builtins.md#collections)) |
| [`calendar`](../library/stdlib/calendar.erl) | Every documented function; the Gregorian arithmetic as runtime builtins, local time and RFC 3339 Erlang ([calendar](builtins.md#calendar)) |
| [`proc_lib`](../library/stdlib/proc_lib.erl), [`sys`](../library/stdlib/sys.erl) | Every documented function: spawning with ancestors and initial calls, synchronous starts (`init_ack`, `init_fail`), labels, `stop/1,3`, crash reports and their text; the system message protocol and debug options (`trace`, `log`, `statistics`, `log_to_file`, installed funs) with OTP's debug list shapes |
| [`gen_server`](../library/stdlib/gen_server.erl), [`gen_statem`](../library/stdlib/gen_statem.erl), [`gen_event`](../library/stdlib/gen_event.erl), [`gen_fsm`](../library/stdlib/gen_fsm.erl), [`gen`](../library/stdlib/gen.erl) | Every documented function in Erlang over `proc_lib` and `sys`, with OTP's messages (`'$gen_call'` with alias tags, `'$gen_cast'`, ...), callback results and actions, request id collections, error terms and report texts; `gen` holds what they share |
| [`supervisor`](../library/stdlib/supervisor.erl), [`supervisor_bridge`](../library/stdlib/supervisor_bridge.erl) | Every documented function: OTP's child specifications and errors, strategies, restart intensity, shutdown rules, `simple_one_for_one` children and automatic shutdown, as a `gen_server` |
| [`logger`](../library/kernel/logger.erl), [`logger_formatter`](../library/kernel/logger_formatter.erl), [`logger_std_h`](../library/kernel/logger_std_h.erl), [`logger_filters`](../library/kernel/logger_filters.erl), [`logger_handler`](../library/kernel/logger_handler.erl) | Every documented function in Erlang: primary, handler and proxy configuration with OTP's checks and error terms, module levels, process metadata, filters, the `?LOG_*` macros of [`kernel/include/logger.hrl`](../library/kernel/include/logger.hrl), formatter templates and the legacy header, `logger_std_h` on devices and files with rotation, compression and `filesync/1`. The `logger` server is Clause's [`clause_logger`](../library/kernel/clause_logger.erl), started at the first use with OTP's default handler on `standard_io` (the environment variable `CLAUSE_LOGGER_TYPE=standard_error` moves it to standard error, as the golden runner does to match goldens made with OTP's handler there); application levels need `application` (plan 21 step LIB24); `logger_disk_log_h` comes with `disk_log` (LIB52) |
| [`error_logger`](../library/kernel/error_logger.erl), [`error_logger_file_h`](../library/stdlib/error_logger_file_h.erl), [`log_mf_h`](../library/stdlib/log_mf_h.erl) | Every documented function: messages and reports as log events with OTP's `error_logger` metadata, report handlers on the `error_logger` event manager fed by the `error_logger` logger handler, `logfile/1` in the old report text, wrap files with OTP's index and record format |
| [`pg`](../library/kernel/pg.erl), [`data_publisher`](../library/kernel/data_publisher.erl) | Every documented function: scopes as `data_publisher` servers with OTP's ETS tables, joins, leaves, members that end, scope and group monitors; the scope has no peers |
| [`global`](../library/kernel/global.erl), [`global_group`](../library/kernel/global_group.erl) | Every documented function as on a node that is not distributed: names (also `{global, Name}` in the behaviours), locks and transactions over a `global_name_server` started at the first use; `global_group` without groups; the locker and distributed internals declined |
| [`rpc`](../library/kernel/rpc.erl), [`erpc`](../library/kernel/erpc.erl) | Every documented function: calls to this node run in spawned processes (`rex` starts at the first use) with OTP's results and errors; other nodes are down (`{badrpc, nodedown}`, `{erpc, noconnection}`) |
| [`net_adm`](../library/kernel/net_adm.erl), [`net_kernel`](../library/kernel/net_kernel.erl), [`auth`](../library/kernel/auth.erl), [`seq_trace`](../library/kernel/seq_trace.erl) | OTP's answers for a node that is not alive (`pang`, `ignored`, `#{started => no}`, `nocookie`); `net_kernel:start/1,2` and the distribution's internals declined; `seq_trace` keeps each process's token without tracing |
| [`error_handler`](../library/kernel/error_handler.erl) | The undefined-call functions, callable directly; the runtime raises `undef` itself and never calls them; `breakpoint/3` declined (no interpreter) |
| [`timer`](../library/stdlib/timer.erl) | Every documented function without OTP's timer server: timed sends to local pids are `erlang:send_after/3`, every other timer a process of its own reached through an alias in its `TRef`; the conversions and `now_diff/2` are runtime builtins ([timer](builtins.md#timer)) |
| [`filename`](../library/stdlib/filename.erl) | Every documented function by OTP's Unix and Windows rules for the host; the name walks are runtime builtins, `absname/1,2` and `basedir/2,3` (from `HOME`, the XDG variables and `APPDATA`) Erlang ([filename](builtins.md#filename)) |
| [`uri_string`](../library/stdlib/uri_string.erl) | Every documented function as a runtime builtin: RFC 3986 parsing with OTP's error fragments, recomposition, normalization, resolution, percent coding and form-urlencoded queries ([uri_string](builtins.md#uri-string)) |
| [`json`](../library/stdlib/json.erl) | Every documented function with OTP's iodata, errors and callback order; `decode/1`, `encode/1`, `format/1,2` and the binary escapers are runtime builtins, the encoders and formatters taking funs and the callback decoders Erlang ([encodings](builtins.md#encodings)) |
| [`rand`](../library/stdlib/rand.erl), [`random`](../library/stdlib/random.erl) | Every documented function with OTP's states and sequences; OTP's algorithms run as runtime builtins, plug-in generators in Erlang ([random numbers](builtins.md#random-numbers)) |
| [`string`](../library/stdlib/string.erl) | Every documented function as a runtime builtin, walking chardata as OTP's `string.erl` does so results keep OTP's shapes ([string](builtins.md#string)) |
| [`base64`](../library/stdlib/base64.erl) | Every documented function as a runtime builtin ([encodings](builtins.md#encodings)) |
| [`array`](../library/stdlib/array.erl) | Every documented function over OTP 29's record; the record operations are runtime builtins, the folds and mapfolds Erlang ([collections](builtins.md#collections)) |
| [`gb_trees`](../library/stdlib/gb_trees.erl), [`gb_sets`](../library/stdlib/gb_sets.erl), [`queue`](../library/stdlib/queue.erl) | Every documented function over OTP's tree and queue shapes; the operations without funs are runtime builtins ([collections](builtins.md#collections)) |
| [`dict`](../library/stdlib/dict.erl), [`sets`](../library/stdlib/sets.erl) | Every documented function over OTP's records (version 1 sets) and maps (version 2); operations without funs are runtime builtins ([collections](builtins.md#collections)) |
| [`io`](../library/stdlib/io.erl) | `get_line/1,2`, `get_chars/2,3` (standard input server or an open file), `fread/2,3` (a `get_until` request over `io_lib:fread/3`), `read/1..4`, `scan_erl_exprs/1..4`, `scan_erl_form/1..4`, `parse_erl_exprs/1..4`, `parse_erl_form/1..4` (`get_until` requests over `erl_scan:tokens/3,4`, parsed by `erl_parse`), `request/1,2`, `requests/1,2` (OTP's I/O protocol, [files](ports.md#standard-io-and-files)); `format/1,2,3`, `fwrite/1,2,3`, `put_chars/1,2` and `nl/0,1` are runtime builtins ([io](io.md)) |
| [`io_lib`](../library/stdlib/io_lib.erl) | Writers, checks, collect functions, the format engine, the pretty printer (`print/1,4`, `~p`, `chars_limit`) and the scanner (`fread/2,3`) as runtime builtins in OTP's deep list shapes, `maps_order` funs sorting keys in `clause_io_lib`; `get_until`, `format_prompt` and `write/3` Erlang ([io_lib](builtins.md#io_lib)) |
| [`sofs`](../library/stdlib/sofs.erl), [`clause_sofs`](../library/stdlib/clause_sofs.erl) | Every function; data operations are runtime builtins, the fun-taking functions and the digraph conversions Erlang ([sets of sets](builtins.md#sets-of-sets)) |
| [`ets`](../library/stdlib/ets.erl) | Tables, reads, writes, walks and updates as runtime builtins over the runtime's tables; `foldl/3`, `foldr/3`, `i/0..3` and `fun2ms/1` Erlang; match specifications (select, match, match_spec families) as builtins over the runtime's interpreter, `match_spec_run/2`, `test_ms/2`, `repair_continuation/2`, `match_delete/2` and `init_table/2` Erlang; table files (`tab2file/2,3`, `file2tab/1,2`, `tabfile_info/1`) Erlang over `file` and `clause_disk_log`; `to_dets/2` and `from_dets/2` Erlang over `dets`; `table/1,2` stubs until `qlc` ([ets](builtins.md#ets)) |
| [`graph`](../library/stdlib/graph.erl) | Every function over OTP's record; the queries and traversals are runtime builtins, the updates Erlang ([graph](builtins.md#graph)) |
| [`digraph`](../library/stdlib/digraph.erl), [`digraph_utils`](../library/stdlib/digraph_utils.erl) | Every function over OTP's three-table record; the updates and reads are Erlang over `ets`, the path searches and digraph_utils' traversals runtime builtins reading the tables, `subgraph/2,3` and `condensation/1` Erlang ([digraph](builtins.md#digraph)) |
| [`dets`](../library/stdlib/dets.erl), [`clause_dets`](../library/stdlib/clause_dets.erl) | Every documented function but `table/1,2` (LIB73): OTP's server and table processes in Erlang over a C++ engine of OTP's version 9 file format, files byte for byte as OTP writes them ([dets](builtins.md#dets)) |
| [`ms_transform`](../library/stdlib/ms_transform.erl) | Every function in Erlang over the abstract format: `parse_transform/2` translates literal funs given to `ets:fun2ms/1` and `dbg:fun2ms/1` in a module's forms, `transform_from_shell/3` a shell fun's clauses, with OTP's match specifications, shadowing warnings and error codes. A module using the transform at compile time still runs the host OTP's `ms_transform` ([parse transforms](transforms.md)) |
| [`erl_parse`](../library/stdlib/erl_parse.erl), [`clause_erl_parse`](../library/stdlib/clause_erl_parse.erl) | Every function: `parse_form/1`, `parse_exprs/1`, `parse_term/1` and yecc's `parse/1`, `parse_and_scan/1` over a runtime parser with OTP's abstract format and errors; `normalise/1`, `abstract/1,2`, `tokens/1,2` and the precedence tables builtins; the annotation walks and `format_error/1` Erlang ([erl_parse](builtins.md#erl-parse)) |
| [`erl_eval`](../library/stdlib/erl_eval.erl), [`clause_erl_eval`](../library/stdlib/clause_erl_eval.erl), [`clause_erl_lint`](../library/stdlib/clause_erl_lint.erl) | Every function in Erlang over the abstract format: bindings as orddicts or maps, local and non-local function handlers (operators, BIFs and errors as `erlang:raise/3` calls go through the latter), interpreted funs with OTP's environment, comprehensions with strict and zip generators, `maybe`, native records, `receive` over `prim_eval`; bit syntax segments are runtime builtins ([erl_eval](builtins.md#erl-eval)). `exprs/2` and `expr/2` check a command with `clause_erl_lint`'s subset of `erl_lint`; `qlc:q/1,2` in evaluated code waits for `qlc` (plan 21 step LIB73) |
| [`erl_error`](../library/stdlib/erl_error.erl) | Every function in Erlang over `io_lib`: `format_exception/3,4` and the internal formatters (`format_exception/6,7,8`, `format_stacktrace/4,5`, `format_call/4,5`, `format_fun/1,2`) with OTP's explanations, layouts, `error_info` callbacks and options. Clause's own stack traces have no locations or `error_info` ([differences](differences.md#errors-stack-traces-and-reports)), so their reports show neither |
| [`erl_pp`](../library/stdlib/erl_pp.erl) | Every function in Erlang: forms, attributes, types and specs, functions, guards and expressions printed with OTP's layout (line width, indentation, string pieces, precedence parentheses), options, hooks and `legalize_vars/1`; identical to OTP over the forms of 357 OTP sources |
| [`erl_scan`](../library/stdlib/erl_scan.erl) | Every function: `string/1,2,3` and the re-entrant `tokens/3,4` over a runtime scanner with OTP's tokens, annotations, options, callbacks and errors; accessors and `format_error/1` Erlang ([erl_scan](builtins.md#erl-scan)) |
| [`erl_internal`](../library/stdlib/erl_internal.erl), [`erl_anno`](../library/stdlib/erl_anno.erl) | Every function as a runtime builtin: OTP's BIF, guard, type test and operator tables, predefined functions, and annotations ([compiler tables](builtins.md#compiler-tables)) |
| [`records`](../library/stdlib/records.erl), [`erl_features`](../library/stdlib/erl_features.erl) | Native record reflection, run-time definitions and compiled definitions with folded defaults ([native records](native-records.md#reflection)); OTP 29's features and those each module was compiled with ([compiler tables](builtins.md#compiler-tables)) |
| [`binary`](../library/stdlib/binary.erl) | Every function; `replace/3,4` and `list_to_bin/1` in Erlang, the others runtime builtins ([binary module](builtins.md#binary-module)) |
| [`math`](../library/stdlib/math.erl) | Every function as a runtime builtin ([math module](builtins.md#math-module)) |
| [`unicode`](../library/stdlib/unicode.erl) | Every function as a runtime builtin: conversions, `bin_is_7bit/1`, byte order marks, normalization forms, categories and identifier properties ([unicode module](builtins.md#unicode-module)) |
| [`unicode_util`](../library/stdlib/unicode_util.erl) | Every function over generated tables, the chardata walks as runtime builtins ([unicode_util](builtins.md#unicode-util)) |
| [`re`](../library/stdlib/re.erl) | Every function but `import/1`, over PCRE2 ([re module](builtins.md#re-module)) |
| [`file`](../library/kernel/file.erl) | Open, read, write, position and close files, whole-file reads and writes, `consult/1`, `eval/1,2`, `script/1,2` and their `path_` forms with `path_open/3` (the file read whole and decoded by its coding comment, UTF-8 by default, as `epp:set_encoding/1` does in [`clause_epp`](../library/stdlib/clause_epp.erl), then scanned form by form with OTP's results and errors), file information and directory operations over the file driver ([files](ports.md#standard-io-and-files)) |
| [`gen_tcp`](../library/kernel/gen_tcp.erl), [`gen_udp`](../library/kernel/gen_udp.erl), [`inet`](../library/kernel/inet.erl) | Sockets over socket ports ([sockets](ports.md#sockets)) |
| [`clause_socket`](../library/kernel/clause_socket.erl) | Clause's own: the socket driver protocol the three modules above share |
| [`os`](../library/kernel/os.erl) | `cmd/1` over a port, environment, time and version queries ([maps module](builtins.md#maps-module)) |
| [`code`](../library/kernel/code.erl) | Loaded-module queries without a code server ([code module](builtins.md#code-module)); paths and loading are stubs |
| [`application`](../library/kernel/application.erl), [`application_controller`](../library/kernel/application_controller.erl) | Every documented function: loading, keys, environments, starts with dependencies and start phases, stops, permissions and restart types with OTP's errors and reports, over a controller started at the first use and [`clause_application_master`](../library/kernel/clause_application_master.erl) ([applications](#applications)); release handling, boot and distributed control declined |
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
with `=:=`, `lists:keyfind/3` with `==`. `lists:sort/1` is a stable runtime
builtin ([comparison and order](terms.md#comparison-and-order)). Stack traces
name the library functions, not OTP's.

## How programs get them

- When a batch (positional inputs or one project target) names a module with a
  literal atom — `M:F(...)` with literal `M`, `fun M:F/A`, `apply(M, F, Args)`
  with literal `M`, `-behaviour(M)` or `-import(M, ...)` — that no input
  defines, the compiler looks for `M.erl` in the library's application
  directories, then in the project's `source_search_paths`
  ([projects](projects.md#paths-and-discovery)), parses and compiles the first
  one found into the batch, then repeats for the modules the added files name
  ([which module a name resolves to](#which-module-a-name-resolves-to)). A call
  `M:F(...)` of a catalog builtin (`io:format/2`, `os:type/0`) names no module.
  `--print-inputs` lists the inputs this adds.
- The library is `library/` of the build tree, found relative to `clau` like the
  runtime archive, with one directory per OTP application (`erts`, `kernel`,
  `stdlib`). `-include_lib("kernel/include/logger.hrl")` and `file.hrl` resolve
  to the library's own headers unless the project names a `kernel` application.
- A module of the batch with the same name replaces the library module, with a
  warning; catalog builtins of that name still win
  ([registration](builtins.md#registration)).
- Library modules are ordinary batch modules: they link into executables and,
  for object or IR output, produce their own artifacts. They are Erlang code, so
  they yield like other code.
- A module reached only through runtime names (`M:F(...)` with `M` a variable)
  is not added: such a call raises `undef` unless something else names the
  module.

## Runtime module replacements

Clause stands in for the OTP modules that ERTS implements in C (NIFs, BIFs, port
drivers, the preloaded modules of `erts/preloaded/src`) and for modules OTP
generates while it builds (`unicode_util`). OTP's `kernel` and `stdlib` are
plain Erlang but call those modules.

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
  `gen_tcp_socket`, `inet_tcp`, ...) never enter a batch. A missing module never
  gets a stub, so a reference to it is a compile-time error, never a run-time
  surprise.

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
  `clause_receive`, `clause_maps`, `clause_hashed`, `clause_array`,
  `clause_features`, `clause_io_lib`, `clause_disk_log`, `clause_dets`) sit
  beside the modules they serve. The library lookup searches every application
  directory; a module name exists in at most one of them.
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

- `library_inventory` checks that every documented function of a provided module
  is in its inventory and is not a stub, except in the modules
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

OTP's `file`, `io` and `code` call registered servers (`file_server_2`,
`code_server`, a group leader's io server) that the `kernel` application starts
at boot. A Clause executable starts none of them: Clause's own `file` and `io`
need no server, Clause's `logger` starts its server at the first use, and an OTP
module that does need one fails the way it fails on OTP without that server
(`noproc`). OTP code that calls `code` at run time therefore does not work.
Booting the whole `kernel` application is not planned.

### Applications

- Specifications come from the program, not a code path: clau compiles every
  `*.app` and `*.app.src` beside the batch's sources, every `*.app` in the
  `ebin` directory next to them, and `ebin/Name.app` or `src/Name.app.src` of
  each `[targets.options.applications]` entry into the generated module
  `clause_applications`, added whenever the batch has `application_controller`.
  The first file of an application name wins. A resource that lists no modules
  gets the batch's modules beside it. Diagnostics point into the resource file.
- `application:load/1` also takes `{application, Name, Keys}` terms. A name with
  no resource is `{error, {"no such file or directory", "Name.app"}}`, as OTP
  answers for a missing file.
- `kernel` (11.0.4) and `stdlib` (8.1) are loaded and running from the start as
  OTP 29.1.1 lists them, without application masters; `kernel` has the default
  environment of its `kernel.app`.
- An application with a start module gets a master that leads its processes'
  group (so `get_application/0`, `get_env/1` and `start_type/0` answer inside
  it), relays their I/O requests to standard I/O, and on stop runs
  `prep_stop/1`, shuts the top process down, runs `stop/1` and kills what is
  left of the group. A permanent application that ends, or a transient one that
  ends abnormally, halts the program with OTP's slogan, exit status 1.
- `logger` and `clause_logger` reach `application` by a runtime name, so only
  programs that use `application` link it.

### Ledger

[Plan 21](../.agents/21-lib.md) schedules every documented `erts`, `kernel` and
`stdlib` module; real programs (luerl first) set priorities. Compile and run it,
classify every error (`unknown module`, `undefined function M:F/A`, unsupported
syntax or feature, other), and assign each to a plan step, another backlog item,
or a recorded decline. With the run's stderr saved to a file,
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
  catalog builtin. `library_rejects` proves it on a deliberately inconsistent
  fixture library.
- CTest `library_freshness` (labels `host_otp`, `full_only`) re-observes every
  inventory and requires it unchanged, then observes, generates, checks and
  compiles a new module end to end. `library_report` runs `clau` on a fixture
  program and checks the report's rows.
