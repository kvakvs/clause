# Library modules

Clause provides Erlang implementations of OTP library modules, derived from OTP's documented
behavior without copying OTP source. These modules use runtime [builtins](../builtins.md) and
are compiled with the programs that reference them.

## Contents

Each provided module exports the complete OTP 29 interface for that module name. Its
`<module>.terms` inventory records the implementation status of each function; see [files](#files).

| Module | Contents |
| --- | --- |
| [`lists`](../../library/stdlib/lists.erl) | All documented functions. Data operations including `sublist/2,3`, `flatten/1,2`, `sort/1`, `usort/1`, `keymember/3`, `zip/2,3` and `merge/2` are runtime builtins ([lists module](../builtins.md#lists-module)) |
| [`maps`](../../library/stdlib/maps.erl) | All documented functions. Operations including `get/3`, `merge/2`, `intersect/2` and `with/2` are runtime builtins; functions accepting function arguments are implemented in Erlang ([maps module](../builtins.md#maps-module)) |
| [`ordsets`](../../library/stdlib/ordsets.erl), [`orddict`](../../library/stdlib/orddict.erl), [`proplists`](../../library/stdlib/proplists.erl) | All documented functions. List traversal operations are runtime builtins; functions accepting function arguments are implemented in Erlang ([collections](../builtins.md#collections)) |
| [`calendar`](../../library/stdlib/calendar.erl) | All documented functions. Gregorian calendar arithmetic uses runtime builtins; local time and RFC 3339 operations are implemented in Erlang ([calendar](../builtins.md#calendar)) |
| [`proc_lib`](../../library/stdlib/proc_lib.erl), [`sys`](../../library/stdlib/sys.erl) | All documented functions: process creation with ancestors and initial calls, synchronous startup (`init_ack`, `init_fail`), labels, `stop/1,3`, crash reports and report formatting; system message handling and debug options (`trace`, `log`, `statistics`, `log_to_file`, installed functions) with OTP-compatible debug list structures |
| [`gen_server`](../../library/stdlib/gen_server.erl), [`gen_statem`](../../library/stdlib/gen_statem.erl), [`gen_event`](../../library/stdlib/gen_event.erl), [`gen_fsm`](../../library/stdlib/gen_fsm.erl), [`gen`](../../library/stdlib/gen.erl) | All documented functions, implemented in Erlang using `proc_lib` and `sys`. Messages (`'$gen_call'` with alias tags, `'$gen_cast'`), callback results and actions, request identifier collections, errors and report text match OTP. `gen` provides shared functionality |
| [`supervisor`](../../library/stdlib/supervisor.erl), [`supervisor_bridge`](../../library/stdlib/supervisor_bridge.erl) | All documented functions, implemented as `gen_server` behaviors: OTP-compatible child specifications and errors, supervision strategies, restart intensity, shutdown rules, `simple_one_for_one` children and automatic shutdown |
| [`logger`](../../library/kernel/logger.erl), [`logger_formatter`](../../library/kernel/logger_formatter.erl), [`logger_std_h`](../../library/kernel/logger_std_h.erl), [`logger_filters`](../../library/kernel/logger_filters.erl), [`logger_handler`](../../library/kernel/logger_handler.erl) | All documented functions, implemented in Erlang: primary, handler and proxy configuration with OTP-compatible validation and errors, module levels, process metadata, filters, [`kernel/include/logger.hrl`](../../library/kernel/include/logger.hrl) `?LOG_*` macros, formatter templates and the legacy header. `logger_std_h` supports devices and files, rotation, compression and `filesync/1`; `logger_disk_log_h` supports halt and wrap disk logs. Both use [`clause_logger_h`](../../library/kernel/clause_logger_h.erl) with OTP-compatible common handler options and validation. [`clause_logger`](../../library/kernel/clause_logger.erl) starts on first use with OTP's default handler on `standard_io`. `CLAUSE_LOGGER_TYPE=standard_error` selects standard error, as used by the golden runner to match OTP fixtures. Application log levels require `application` (plan 21, step LIB24) |
| [`disk_log`](../../library/kernel/disk_log.erl), [`wrap_log_reader`](../../library/kernel/wrap_log_reader.erl), [`clause_disk_log`](../../library/kernel/clause_disk_log.erl), [`clause_disk_log_server`](../../library/kernel/clause_disk_log_server.erl) | All documented functions, implemented in Erlang: internal and external halt, wrap and rotate logs with byte-for-byte OTP-compatible formats, including log headers, item frames with large-item MD5s, wrap index and size files, and rotate archives. Each log has a process managing owners, users, notifications, blocking with queued requests, look-ahead logging, a size- and timer-flushed write cache, repair and read-only chunks with invalid-byte counts. The `disk_log_server` registry uses named tables `disk_log_names` and `disk_log_pids`; `wrap_log_reader` uses the same chunk reader |
| [`error_logger`](../../library/kernel/error_logger.erl), [`error_logger_file_h`](../../library/stdlib/error_logger_file_h.erl), [`log_mf_h`](../../library/stdlib/log_mf_h.erl) | All documented functions: messages and reports emitted as log events with OTP-compatible `error_logger` metadata; report handlers on the `error_logger` event manager receive events from the `error_logger` logger handler. `logfile/1` uses the legacy report format; wrap files use OTP-compatible index and record formats |
| [`pg`](../../library/kernel/pg.erl), [`data_publisher`](../../library/kernel/data_publisher.erl) | All documented functions: scopes implemented as `data_publisher` servers with OTP-compatible ETS tables, membership joins, leaves and process termination handling, and scope and group monitors. Scopes have no peers |
| [`global`](../../library/kernel/global.erl), [`global_group`](../../library/kernel/global_group.erl) | All documented functions for a non-distributed node: name registration, including `{global, Name}` in behaviors, locks and transactions through `global_name_server`, started on first use. `global_group` operates without groups. Locker and distribution internals are declined |
| [`rpc`](../../library/kernel/rpc.erl), [`erpc`](../../library/kernel/erpc.erl) | All documented functions. Local calls execute in spawned processes with OTP-compatible results and errors; `rex` starts on first use. Remote nodes are unavailable, producing `{badrpc, nodedown}` or `{erpc, noconnection}` |
| [`net_adm`](../../library/kernel/net_adm.erl), [`net_kernel`](../../library/kernel/net_kernel.erl), [`auth`](../../library/kernel/auth.erl), [`seq_trace`](../../library/kernel/seq_trace.erl) | OTP-compatible results for a node without distribution (`pang`, `ignored`, `#{started => no}`, `nocookie`). `net_kernel:start/1,2` and distribution internals are declined. `seq_trace` maintains per-process tokens without emitting traces |
| [`heart`](../../library/kernel/heart.erl), [`erl_debugger`](../../library/kernel/erl_debugger.erl), [`erl_epmd`](../../library/kernel/erl_epmd.erl), [`erl_ddll`](../../library/kernel/erl_ddll.erl), [`erl_boot_server`](../../library/kernel/erl_boot_server.erl), [`peer`](../../library/stdlib/peer.erl), [`slave`](../../library/stdlib/slave.erl), [`pool`](../../library/stdlib/pool.erl) | Features a Clause node lacks, answering as OTP does without them: `heart` calls raise `badarg` (no heart program), `erl_debugger:supported/0` is `false` and its other functions raise `undef`, `erl_epmd` finds no epmd (`{error, address}`, `noport`, registration exits `noproc`), `erl_ddll` loads nothing (`{error, no_ddll_available}`, `not_loaded`, monitors report `unloaded`) with OTP's error texts, `peer:start/1` and `slave:start/1,2,3` fail with `not_alive` after OTP's option checks, and `pool` runs its globally registered master over this node alone. `erl_boot_server` is declined (network boot) |
| [`trace`](../../library/kernel/trace.erl), [`erl_tracer`](../../library/erts/erl_tracer.erl) | Tracing is unsupported. All `trace` functions and the `erl_tracer` default tracer (`enabled/3`, `trace/5`) raise `notsup`, matching `erlang` tracing functions ([erlang module](../builtins.md#erlang-module)). `erl_tracer:behaviour_info/1` returns OTP's callback list |
| [`error_handler`](../../library/kernel/error_handler.erl) | Undefined-call handlers are directly callable. The runtime raises `undef` without invoking them, except for calls into [interpreted modules](#module-interpreter), which `undefined_function/3` passes to `clause_interp`. `breakpoint/3` is declined because the interpreter has no break points |
| [`timer`](../../library/stdlib/timer.erl) | All documented functions without OTP's timer server. Timed sends to local pids use `erlang:send_after/3`; each other timer uses a separate process addressed through an alias in its `TRef`. Time conversions and `now_diff/2` are runtime builtins ([timer](../builtins.md#timer)) |
| [`filelib`](../../library/stdlib/filelib.erl), [`clause_filelib`](../../library/stdlib/clause_filelib.erl) | All documented functions. Runtime builtins compile and match wildcards with OTP-compatible rules and errors. Erlang implementations provide directory traversal, queries, `ensure_dir/1`, `ensure_path/1`, `fold_files/5,6`, `find_file/2,3`, `find_source/1,2,3` using the kernel `source_search_rules` or OTP defaults, and `safe_relative_path/2`. Undocumented variants accepting a module such as `file` or `erl_prim_loader` are also implemented. The debugging function `compile_wildcard/1` remains a stub ([filelib](../builtins.md#filelib)) |
| [`file_sorter`](../../library/stdlib/file_sorter.erl) | All documented functions, implemented in Erlang. In-memory runs of `size` bytes, merges of at most `no_files` temporary files, 16 KiB chunks, stream ordering by buffered objects and file numbers, and consistency checks follow OTP's algorithm. Equal-term ordering and `unique` retention match OTP. Supported formats are `binary_term`, `binary`, `term` and custom functions; options include `header`, `compressed` through `file`'s gzip layer, `tmpdir`, and input and output functions. Error terms match OTP |
| [`zip`](../../library/stdlib/zip.erl) | All documented functions, implemented in Erlang with byte-for-byte OTP-compatible archive output: UNIX 6.1 local and central headers, extended timestamp and UNIX uid/gid fields, zip64 beyond 4 GiB, and 8 KiB deflate blocks with full flushes through `zlib`. Input and output accept files or binaries through OTP-compatible functions. Supports filters, `cwd`, `keep_old_files`, `skip_directories`, `foldl/3`, linked-process `zip_open/1,2` archives, `t/1`, `tt/1`, OTP-compatible errors and [`stdlib/include/zip.hrl`](../../library/stdlib/include/zip.hrl) records |
| [`erl_tar`](../../library/stdlib/erl_tar.erl) | All documented functions, implemented in Erlang with OTP-compatible byte-level format rules: USTAR headers; prefix splitting and pax extended headers for long or non-ASCII names, links and owner names, and numbers exceeding octal fields; reading of v7, GNU long-name, long-link and sparse formats, STAR, USTAR and pax, including base-256 fields and pax times. Supports `compressed` through `file`'s gzip layer, binary input as `ram` files, devices, `init/3` custom I/O functions, `max_size`, `keep_old_files`, `files`, `cwd`, `verbose`, unsafe-path validation, OTP-compatible errors and `format_error/1` |
| [`zstd`](../../library/stdlib/zstd.erl), [`clause_zstd`](../../library/stdlib/clause_zstd.erl) | All documented functions: one-shot and streaming compression and decompression, parameters, raw or referenced dictionaries, frame headers and dictionary identifiers. Erlang wrappers use runtime builtins holding libzstd 1.5.7 contexts, corresponding to OTP's NIF ([zstd](../builtins.md#zstd-module)). `file:open/2` accepts `{zstd, Options}` |
| [`beam_lib`](../../library/stdlib/beam_lib.erl), [`erl_abstract_code`](../../library/stdlib/erl_abstract_code.erl) | All documented functions, implemented in Erlang with OTP-compatible BEAM chunk handling: IFF traversal and errors, both atom table encodings, symbol tables, attributes, compile information, literals, debug information and abstract code through `erl_abstract_code`, documentation using [`kernel/include/eep48.hrl`](../../library/kernel/include/eep48.hrl) records, MD5s over significant chunks, file, directory and release comparison and stripping, gzip-compressed BEAM files and `build_module/1`. The crypto key server decrypts debug information through dynamic `crypto` calls; link crypto support with `--with-crypto` |
| [`io_ansi`](../../library/stdlib/io_ansi.erl) | All documented functions, implemented in Erlang using OTP 29's built-in ANSI virtual terminal sequences for operation without terminfo. Terminfo queries return `{error, enotsup}`. `format/1,2,3` and `render/1,2` use `io_lib`; `fwrite/1,2,3,4` issue `put_ansi` requests rendered by the standard input server. Includes `scan/1` and `enabled/0,1` based on the device terminal flag ([io module](../builtins.md#io-module)) |
| [`win32reg`](../../library/stdlib/win32reg.erl) | All documented functions: OTP-compatible registry key parsing for root, absolute, relative and `..` paths; current key handling; integer, string and binary values; enumeration and deletion through the [Windows registry driver](../ports.md#the-registry). Other platforms return `{error, enotsup}`, matching OTP |
| [`argparse`](../../library/stdlib/argparse.erl), [`clause_argparse`](../../library/stdlib/clause_argparse.erl) | All functions: validation, parsing and errors with OTP-compatible argument maps; options, flags, `nargs`, actions, types with choices, limits and regular expressions, custom types, subcommands, prefixes and defaults. `help/1,2` uses OTP-compatible layout and templates; `run/3` supports every handler form. Runtime builtins delegate function invocation to Erlang ([argparse](../builtins.md#argparse)). The executable name is used when `progname` is absent |
| [`escript`](../../library/stdlib/escript.erl) | `create/2` and `extract/2` use OTP-compatible section layout and header parsing: shebang, comment and emulator argument lines, and source, BEAM and archive bodies through `zip`. `script_name/0` returns the executable path. Escript execution through `start/0,1` and `parse_file/1` is declined |
| [`filename`](../../library/stdlib/filename.erl) | All documented functions using OTP's host-specific Unix and Windows path rules. Path traversal operations are runtime builtins; `absname/1,2` and `basedir/2,3`, using `HOME`, XDG variables and `APPDATA`, are implemented in Erlang ([filename](../builtins.md#filename)) |
| [`uri_string`](../../library/stdlib/uri_string.erl) | All documented functions as runtime builtins: RFC 3986 parsing with OTP-compatible error fragments, recomposition, normalization, resolution, percent encoding and form-urlencoded queries ([uri_string](../builtins.md#uri-string)) |
| [`json`](../../library/stdlib/json.erl) | All documented functions with OTP-compatible iodata structures, errors and callback ordering. `decode/1`, `encode/1`, `format/1,2` and binary escaping are runtime builtins; encoders, formatters and decoders accepting callbacks are implemented in Erlang ([encodings](../builtins.md#encodings)) |
| [`rand`](../../library/stdlib/rand.erl), [`random`](../../library/stdlib/random.erl) | All documented functions with OTP-compatible states and sequences. OTP algorithms are runtime builtins; plug-in generators are implemented in Erlang ([random numbers](../builtins.md#random-numbers)) |
| [`string`](../../library/stdlib/string.erl) | All documented functions as runtime builtins. Chardata traversal follows OTP's `string.erl` to preserve result structures ([string](../builtins.md#string)) |
| [`base64`](../../library/stdlib/base64.erl) | All documented functions as runtime builtins ([encodings](../builtins.md#encodings)) |
| [`array`](../../library/stdlib/array.erl) | All documented functions using OTP 29's record representation. Record operations are runtime builtins; folds and mapfolds are implemented in Erlang ([collections](../builtins.md#collections)) |
| [`gb_trees`](../../library/stdlib/gb_trees.erl), [`gb_sets`](../../library/stdlib/gb_sets.erl), [`queue`](../../library/stdlib/queue.erl) | All documented functions using OTP-compatible tree and queue representations. Operations without function arguments are runtime builtins ([collections](../builtins.md#collections)) |
| [`dict`](../../library/stdlib/dict.erl), [`sets`](../../library/stdlib/sets.erl) | All documented functions using OTP-compatible dictionary records, version 1 set records and version 2 set maps. Operations without function arguments are runtime builtins ([collections](../builtins.md#collections)) |
| [`io`](../../library/stdlib/io.erl) | `get_line/1,2` and `get_chars/2,3` through the standard input server or an open file; `fread/2,3` through `get_until` requests using `io_lib:fread/3`; `read/1..4`, `scan_erl_exprs/1..4`, `scan_erl_form/1..4`, `parse_erl_exprs/1..4` and `parse_erl_form/1..4` through `get_until` requests using `erl_scan:tokens/3,4` and `erl_parse`; `request/1,2` and `requests/1,2` using [OTP's I/O protocol](../ports.md#standard-io-and-files); `getopts/0,1`, `setopts/1,2`, `columns/0,1` and `rows/0,1` through `clause_io` terminal-size builtins; `get_password/0,1` and `write/1,2`. `format/1,2,3`, `fwrite/1,2,3`, `put_chars/1,2` and `nl/0,1` are runtime builtins ([io](io.md)) |
| [`io_lib`](../../library/stdlib/io_lib.erl) | Runtime builtins for writers, validation, collection functions, formatting, pretty printing (`print/1,4`, `~p`, `chars_limit`) and scanning (`fread/2,3`), preserving OTP's nested list structures. `maps_order` comparison functions sort keys in `clause_io_lib`. `get_until`, `format_prompt` and `write/3` are implemented in Erlang ([io_lib](../builtins.md#io_lib)) |
| [`sofs`](../../library/stdlib/sofs.erl), [`clause_sofs`](../../library/stdlib/clause_sofs.erl) | All functions. Data operations are runtime builtins; functions accepting function arguments and digraph conversions are implemented in Erlang ([sets of sets](../builtins.md#sets-of-sets)) |
| [`ets`](../../library/stdlib/ets.erl) | Runtime builtins for table creation, reads, writes, traversal and updates. `foldl/3`, `foldr/3`, `i/0..3` and `fun2ms/1` are implemented in Erlang. The select, match and match_spec families use the runtime match-specification interpreter; `match_spec_run/2`, `test_ms/2`, `repair_continuation/2`, `match_delete/2` and `init_table/2` are implemented in Erlang. Table files (`tab2file/2,3`, `file2tab/1,2`, `tabfile_info/1`) use Erlang over `file` and `clause_disk_log`; `to_dets/2` and `from_dets/2` use `dets`; `table/1,2` uses `qlc:table/2` ([ets](../builtins.md#ets)) |
| [`graph`](../../library/stdlib/graph.erl) | All functions using OTP's record representation. Queries and traversals are runtime builtins; updates are implemented in Erlang ([graph](../builtins.md#graph)) |
| [`digraph`](../../library/stdlib/digraph.erl), [`digraph_utils`](../../library/stdlib/digraph_utils.erl) | All functions using OTP's three-table record representation. Reads and updates use Erlang over `ets`; path searches and `digraph_utils` traversals use runtime builtins that read the tables. `subgraph/2,3` and `condensation/1` are implemented in Erlang ([digraph](../builtins.md#digraph)) |
| [`dets`](../../library/stdlib/dets.erl), [`clause_dets`](../../library/stdlib/clause_dets.erl) | All documented functions. Erlang server and table processes use a C++ implementation of OTP's version 9 file format, producing byte-for-byte OTP-compatible files. `table/1,2` is implemented in Erlang using `qlc:table/2` ([dets](../builtins.md#dets)) |
| [`ms_transform`](../../library/stdlib/ms_transform.erl) | All functions, implemented in Erlang using the abstract format. `parse_transform/2` translates literal functions passed to `ets:fun2ms/1` and `dbg:fun2ms/1`; `transform_from_shell/3` translates shell function clauses. Generated match specifications, shadowing warnings and error codes match OTP. Compile-time transforms run in Clause's [transform host](../transforms.md#transform-host) |
| [`qlc_pt`](../../library/stdlib/qlc_pt.erl), [`clause_qlc_pt`](../../library/stdlib/clause_qlc_pt.erl) | Runtime support for the qlc parse transform, undocumented in OTP: `transform_expression/2`, `transform_from_evaluator/2` and `parse_transform/2` are runtime builtins producing OTP-compatible query code, messages and annotations. `ms_transform` executes in Erlang when requested ([qlc_pt](../builtins.md#qlc-pt)). Compiled modules including `qlc.hrl` get it in Clause's [transform host](../transforms.md#transform-host) |
| [`qlc`](../../library/stdlib/qlc.erl) | All functions, implemented in Erlang from OTP documentation. Query handles support compiled queries using the transform's `qlc_lc` handles, lists, `table/2` tables, `append/1,2`, `sort/1,2` and `keysort/2,3`. Evaluation supports constant lookups, match specifications, merge and lookup joins, `cache` with `ets` or `list` storage and temporary-file spill, `unique`, `file_sorter` sorting and process-based cursors. `info/1,2` uses `erl_pp`; `string_to_handle/1,2,3` uses `qlc_pt`. Errors and `format_error/1`, handles, tables, process dictionary keys and temporary filenames match OTP. [`stdlib/include/qlc.hrl`](../../library/stdlib/include/qlc.hrl) selects the `qlc` transform ([qlc](../builtins.md#qlc)) |
| [`compile`](../../library/compiler/compile.erl), [`clause_compile`](../../library/compiler/clause_compile.erl) | All documented functions but the internal pass functions (declined). `format_error/1` with OTP's texts, `env_compiler_options/0` (`ERL_COMPILER_OPTIONS`), `output_generated/1` and `noenv_output_generated/1` (plan 21 LIB77A). `file/1,2`, `forms/1,2`, `noenv_file/2`, `noenv_forms/2` (LIB77E1) run OTP's front passes: `epp` with `{i, Dir}` and `{d, ...}`, parse transforms from options and `-compile` attributes, `erl_lint`, record expansion, the listings `P`, `E`, `dpp`, `dabstr`, `dexp`, `to_pp`, `to_abstr`, `to_exp` and `from_abstr` input, with OTP's reports (source quotes from a [runtime builtin](../builtins.md#compile)), `warnings_as_errors`, return shapes and `.beam` files. The binary is an [interpreter image](#module-interpreter) with OTP's `Attr`, `CInf`, `Dbgi` (`debug_info`) and extra chunks, which `code:load_binary/3` loads; options that need OTP's code generator (Core Erlang and assembly listings, `core_transform`, `from_core`, `from_asm`, `makedep`, encrypted `debug_info`) fail ([differences](../differences.md#compile)) |
| [`shell`](../../library/stdlib/shell.erl), [`clause_shell`](../../library/stdlib/clause_shell.erl), [`clause_shell_eval`](../../library/stdlib/clause_shell_eval.erl), [`clause_shell_commands`](../../library/stdlib/clause_shell_commands.erl), [`clause_shell_records`](../../library/stdlib/clause_shell_records.erl) | All documented functions, implemented in Erlang from OTP documentation. The [interactive shell](#interactive-shell) uses standard input and provides OTP-compatible prompts, banner, history (`e/1`, `v/1`, `h/0`), bindings, record commands (`rd`, `rr`, `rl`, `rp`, `rf`), prompt-defined functions, types and specifications, exception reports through `erl_error`, result formatting with depth 30 and record support, restricted shells, `catch_exception/1` and settings functions |
| [`shell_default`](../../library/stdlib/shell_default.erl), [`c`](../../library/stdlib/c.erl) | All functions. `c` commands such as `i/0`, `regs/0`, `m/0,1`, `ls/0,1`, `flush/0` and `help/0` produce OTP-compatible output. Compilation commands (`c/1,2,3`, `lc/1`, `nc/1,2`), `xm/1` and `y/1,2` report failures matching OTP without compiler, tools or parsetools applications. `h/1,2,3` finds no documentation in linked code. Debugger `i` commands report the missing application |
| [`edlin`](../../library/stdlib/edlin.erl), [`edlin_expand`](../../library/stdlib/edlin_expand.erl) | `edlin:keymap/0` returns the calling line editor's keymap, or `undefined` outside a line editor. `edlin_expand:expand/1,2` is implemented in Erlang and completes modules, functions (`Mod:`, `fun Mod:Fun/` arities), BIFs, shell commands, shell-defined functions, keywords, bindings, record names and fields, map keys and file paths with OTP-compatible sections and legacy matches. Argument suggestions use `Mod:Fun/Arity` heads because linked code contains no specifications |
| [`shell_docs`](../../library/stdlib/shell_docs.erl), [`clause_shell_docs_markdown`](../../library/stdlib/clause_shell_docs_markdown.erl) | All functions, implemented in Erlang from OTP documentation. `render/2..5`, `render_type/2..5` and `render_callback/2..5` render supplied `docs_v1` chunks: `application/erlang+html`, `text/markdown` converted according to OTP shell rules, and other `text/` formats without conversion. Layout, wrapping, bullets and ANSI styles match OTP. Includes `normalize/1`, `validate/1` and `supported_tags/0`. Output matched OTP for 51,249 renderings of OTP 29 documentation chunks. Linked code has no documentation chunks for `code:get_doc/1` |
| [`erl_parse`](../../library/stdlib/erl_parse.erl), [`clause_erl_parse`](../../library/stdlib/clause_erl_parse.erl) | All functions. `parse_form/1`, `parse_exprs/1`, `parse_term/1` and yecc's `parse/1` and `parse_and_scan/1` use a runtime parser with OTP-compatible abstract forms and errors. `normalise/1`, `abstract/1,2`, `tokens/1,2` and precedence tables are runtime builtins. Annotation traversal and `format_error/1` are implemented in Erlang ([erl_parse](../builtins.md#erl-parse)) |
| [`erl_eval`](../../library/stdlib/erl_eval.erl), [`clause_erl_eval`](../../library/stdlib/clause_erl_eval.erl) | All functions, implemented in Erlang using the abstract format: bindings as orddicts or maps; local and non-local function handlers, with operators, BIFs and errors represented as `erlang:raise/3` calls routed through the non-local handler; interpreted functions with OTP-compatible environments; comprehensions with strict and zip generators; `maybe`; native records; and `receive` through `prim_eval`. Pattern matching, bit syntax segments and constant folding are runtime builtins ([erl_eval](../builtins.md#erl-eval)). `exprs/2` and `expr/2` validate commands with `erl_lint:exprs_opt/3`. Query list comprehensions passed to `qlc:q/1,2` use `qlc:transform_from_evaluator/2` with local calls hidden, matching OTP |
| [`erl_error`](../../library/stdlib/erl_error.erl) | All functions, implemented in Erlang using `io_lib`: `format_exception/3,4` and internal formatters (`format_exception/6,7,8`, `format_stacktrace/4,5`, `format_call/4,5`, `format_fun/1,2`) with OTP-compatible explanations, layout, `error_info` callbacks and options. Clause stack traces contain neither locations nor `error_info`, so these are absent from generated reports ([differences](../differences.md#errors-stack-traces-and-reports)) |
| [`erl_pp`](../../library/stdlib/erl_pp.erl), [`clause_erl_pp`](../../library/stdlib/clause_erl_pp.erl) | All functions. Runtime builtins format forms, attributes, types, specifications, functions, guards and expressions with OTP-compatible line width, indentation, string splitting, precedence parentheses and options ([erl_pp](../builtins.md#erl-pp)). Erlang code handles hooks and `legalize_vars/1`. Output matched OTP for forms from 347 OTP sources and suites, and for 80 of those inputs with five option sets including hooks |
| [`erl_lint`](../../library/stdlib/erl_lint.erl), [`clause_erl_lint`](../../library/stdlib/clause_erl_lint.erl) | All functions. Runtime builtins perform checks with OTP-compatible errors, warnings, options and diagnostic ordering ([erl_lint](../builtins.md#erl-lint)); `format_error/1` provides matching text. Erlang code invokes behavior `behaviour_info/1` functions and the `is_guard_test/3` callback for local calls before classification. Results matched OTP for 481 OTP sources and suites, 474 suite snippets and 816 command expressions |
| [`erl_expand_records`](../../library/stdlib/erl_expand_records.erl) | `module/2` is a runtime builtin ([erl_expand_records](../builtins.md#erl-expand-records)). Tuple records are expanded into tuple operations; native and imported native records are retained; imported and BIF calls are module-qualified; `is_record/3` guard tests become patterns. Supports `dialyzer`, `expand_inits` and `[no_]strict_record_tests`. Results matched OTP for forms from 347 OTP sources and test suites |
| [`epp`](../../library/stdlib/epp.erl), [`clause_epp`](../../library/stdlib/clause_epp.erl) | All functions, implemented in Erlang except `-if` condition evaluation, which uses a runtime builtin ([epp](../builtins.md#epp)). Each open source has a server producing OTP-compatible forms, macros with arguments and overloading, `??Arg`, `?FUNCTION_NAME`, predefined and `macros` option definitions, includes, conditionals, `-file`, `-feature`, `-error`/`-warning`, documentation files and errors. Files are read in full and decoded using their coding comments. Results matched OTP for 1,110 `parse_file`/`scan_file` calls on OTP sources and suites. `-include_lib` searches only the include path until `code:lib_dir/1` is available (plan 21, step LIB61) |
| [`erl_scan`](../../library/stdlib/erl_scan.erl) | All functions. `string/1,2,3` and reentrant `tokens/3,4` use a runtime scanner with OTP-compatible tokens, annotations, options, callbacks and errors. Accessors and `format_error/1` are implemented in Erlang ([erl_scan](../builtins.md#erl-scan)) |
| [`erl_internal`](../../library/stdlib/erl_internal.erl), [`erl_anno`](../../library/stdlib/erl_anno.erl) | All functions as runtime builtins: OTP-compatible BIF, guard, type-test and operator tables, predefined functions and annotations ([compiler tables](../builtins.md#compiler-tables)) |
| [`records`](../../library/stdlib/records.erl), [`erl_features`](../../library/stdlib/erl_features.erl) | Native record reflection, runtime definitions and compiled definitions with folded defaults ([native records](../native-records.md#reflection)); OTP 29 feature definitions and each module's compile-time feature set ([compiler tables](../builtins.md#compiler-tables)) |
| [`binary`](../../library/stdlib/binary.erl) | All functions. `replace/3,4` and `list_to_bin/1` are implemented in Erlang; other functions are runtime builtins ([binary module](../builtins.md#binary-module)) |
| [`math`](../../library/stdlib/math.erl) | All functions as runtime builtins ([math module](../builtins.md#math-module)) |
| [`unicode`](../../library/stdlib/unicode.erl) | All functions as runtime builtins: conversions, `bin_is_7bit/1`, byte order marks, normalization forms, categories and identifier properties ([unicode module](../builtins.md#unicode-module)) |
| [`unicode_util`](../../library/stdlib/unicode_util.erl) | All functions using generated tables; chardata traversal uses runtime builtins ([unicode_util](../builtins.md#unicode-util)) |
| [`re`](../../library/stdlib/re.erl) | All functions except `import/1`, implemented using PCRE2 ([re module](../builtins.md#re-module)) |
| [`file`](../../library/kernel/file.erl) | All documented functions: opening, reading, writing, positioning and closing disk, `compressed` and memory-backed `ram` files; positioned reads and writes; `truncate/1`, `advise/4`, `allocate/3`; whole-file reads and writes; `consult/1`, `eval/1,2`, `script/1,2` and their `path_` variants with `path_open/3`. Source files are read in full, decoded using coding comments with UTF-8 as the default, matching `epp:set_encoding/1` in [`clause_epp`](../../library/stdlib/clause_epp.erl), and scanned form by form with OTP-compatible results and errors. Includes file metadata queries and updates (`write_file_info/2,3`, `change_*`), directory operations through the file driver, `sendfile/2,5` using OTP's chunked fallback, and `format_error/1` ([files](../ports.md#standard-io-and-files)) |
| [`erl_posix_msg`](../../library/stdlib/erl_posix_msg.erl) | OTP's internal interface for `file:format_error/1`: `message/1` is a runtime builtin ([POSIX messages](../builtins.md#posix-messages)) |
| [`gen_tcp`](../../library/kernel/gen_tcp.erl), [`gen_udp`](../../library/kernel/gen_udp.erl), [`inet`](../../library/kernel/inet.erl), [`clause_socket`](../../library/kernel/clause_socket.erl), [`clause_socket_options`](../../library/kernel/clause_socket_options.erl) | Socket operations through socket ports, including options, statistics, status and listings ([sockets](../ports.md#sockets)). `inet:get_rc/0` uses `clause_inet_config` |
| [`socket`](../../library/kernel/socket.erl) | All documented functions, implemented in Erlang through socket driver ports ([sockets](../ports.md#sockets)). A `clause_socket_registry` process and table maintain `{'$socket', Ref}` handles, owners, states, counters, metadata, monitors and listings. Supports open/bind/listen/accept/connect, send/sendto/sendmsg/sendv/sendfile/sendmmsg, recv/recvfrom/recvmsg/recvmmsg with OTP-compatible partial-data errors, `nowait` completion messages on Windows and select messages elsewhere, `cancel/2`, OTP-compatible option names and invalid-option errors, `info/0,1`, `i/0,1,2` and `supports/0,1,2`. SCTP calls raise `notsup`; interface requests are refused, matching OTP on Windows |
| [`gen_sctp`](../../library/kernel/gen_sctp.erl) | All documented functions with unsupported SCTP handling. `open/0,1,2` returns `{error, eprotonosupport}` after OTP-compatible option validation. Other socket operations treat handles as closed or foreign; `error_string/1` returns OTP-compatible text |
| [`inet_res`](../../library/kernel/inet_res.erl), [`clause_inet_config`](../../library/kernel/clause_inet_config.erl) | All documented functions, implemented in Erlang. The DNS client uses `gen_udp` and `gen_tcp` with OTP-compatible retries, alternative name servers, EDNS, TCP fallback for truncation, SERVFAIL waits, errors, search lists, CNAME chains and hostent records. Messages use the `clause_net` DNS codec ([network names](../builtins.md#network-names)). Resolver configuration follows OTP's non-distributed-node setup: none on Windows, and `resolv.conf` and hosts files elsewhere |
| [`net`](../../library/kernel/net.erl), [`clause_net`](../../library/kernel/clause_net.erl) | All documented functions. Host, address and service lookups and interface enumeration use runtime builtins with `prim_net`-compatible results; filters are implemented in Erlang. Network namespaces are unsupported ([network names](../builtins.md#network-names)) |
| [`clause_socket`](../../library/kernel/clause_socket.erl) | Private implementation of the socket driver protocol shared by the socket wrapper modules |
| [`os`](../../library/kernel/os.erl) | `cmd/1,2` uses a port with OTP-compatible shell protocol, `max_size` and `exception_on_failure`. Environment, time and version queries, `find_executable/1,2` and `set_signal/2` are runtime builtins ([os module](../builtins.md#os-module)) |
| [`code`](../../library/kernel/code.erl) | Loaded-module queries without a code server. A persistent term stores the code path with OTP-compatible path modifications, `lib_dir`/`priv_dir` lookups, `where_is_file/1`, `all_available/0` and `clash/0`; loading operations report the state of linked code ([code module](../builtins.md#code-module)) |
| [`application`](../../library/kernel/application.erl), [`application_controller`](../../library/kernel/application_controller.erl) | All documented functions: loading, resource keys, environments, dependency-aware starts and start phases, stops, permissions and restart types with OTP-compatible errors and reports. Uses a controller started on first use and [`clause_application_master`](../../library/kernel/clause_application_master.erl) ([applications](#applications)). Release handling, boot and distributed control are declined |
| [`erlang`](../../library/erts/erlang.erl) | Generated stubs for every OTP 29 `erlang` export. Catalog builtins have placeholder bodies; `nif_error/1,2` have Erlang implementations; remaining functions raise `{not_implemented, F, A}` ([erlang module](../builtins.md#erlang-module)) |
| [`init`](../../library/erts/init.erl) | Query results corresponding to an initialized system. `boot/1`, `restart/0,1` and `make_permanent/2` are refused ([init module](../builtins.md#init-module)) |
| [`persistent_term`](../../library/erts/persistent_term.erl) | All functions as runtime builtins ([persistent terms](../builtins.md#persistent-terms)) |
| [`atomics`](../../library/erts/atomics.erl), [`counters`](../../library/erts/counters.erl) | All functions ([atomics and counters](../builtins.md#atomics-and-counters)) |
| [`erts_internal`](../../library/erts/erts_internal.erl), [`erts_code_purger`](../../library/erts/erts_code_purger.erl) | Interfaces required by `stdlib` and `kernel`. Distribution, `.beam`, emulator and purger-process function groups are declined ([erts_internal](../builtins.md#erts-internal)) |
| [`erl_prim_loader`](../../library/erts/erl_prim_loader.erl) | File queries implemented through `file`. Loading and archive operations are declined ([files](../ports.md#standard-io-and-files)) |
| [`zlib`](../../library/erts/zlib.erl) | All functions using the pinned zlib version ([zlib module](../builtins.md#zlib-module)) |
| [`prim_eval`](../../library/erts/prim_eval.erl) | `'receive'/2` uses the steps of a compiled receive operation ([prim eval](../builtins.md#prim-eval)) |
| `public_key`'s ASN.1 modules (`'OTP-PKIX'`, `'PKIX1Explicit-2009'`, ...) and [`public_key.hrl`](../../library/public_key/include/public_key.hrl) | Generated at build time from [`library/public_key/asn1/`](../../library/public_key/asn1/) into `library-asn1/public_key/` in the build tree ([public_key](public_key.md)) |
| [`public_key`](../../library/public_key/public_key.erl), [`clause_public_key_codec`](../../library/public_key/clause_public_key_codec.erl), [`clause_public_key_keys`](../../library/public_key/clause_public_key_keys.erl), [`clause_public_key_cert`](../../library/public_key/clause_public_key_cert.erl), [`clause_public_key_path`](../../library/public_key/clause_public_key_path.erl), [`clause_public_key_crl`](../../library/public_key/clause_public_key_crl.erl), [`clause_public_key_ocsp`](../../library/public_key/clause_public_key_ocsp.erl), [`clause_public_key_test_data`](../../library/public_key/clause_public_key_test_data.erl), [`clause_public_key_cacerts`](../../library/public_key/clause_public_key_cacerts.erl), [`clause_public_key`](../../library/public_key/clause_public_key.erl) | Application `public_key`, with [`public_key.app`](../../library/public_key/public_key.app): DER and PEM encoding and decoding, PEM encryption, PKCS #8 private keys of all types, and certificates in `plain` and `otp` representations using generated modules ([functions](public_key.md#functions), [public key builtins](../builtins.md#public-key)); key conversion, signatures, RSA encryption, key generation and agreement, and certificate signing through `crypto` (`clause_public_key_keys`, [keys](public_key.md#keys-and-signatures)); names, issuer and subject identifiers, CRL issuers and signatures, distribution points and hostname verification (`clause_public_key_cert`, [certificates](public_key.md#certificates-and-names)); path validation (`clause_public_key_path`, [path validation](public_key.md#path-validation)); CRL validation (`clause_public_key_crl`, [CRL validation](public_key.md#crl-validation)); OCSP validation (`clause_public_key_ocsp`, [OCSP validation](public_key.md#ocsp-validation)); test certificates (`clause_public_key_test_data`, [test data](public_key.md#test-data)); trust stores (`clause_public_key_cacerts`, [trust stores](public_key.md#trust-stores)) |
| [`clause_asn1`](../../library/asn1/clause_asn1.erl) | Runtime codec invoked by modules generated by `clau` from ASN.1 specifications ([ASN.1 codec](../builtins.md#asn1-codec), [ASN.1](../asn1.md)). OTP's `asn1` application interfaces `asn1ct` and `asn1rt_nif` are not provided |
| [`crypto`](../../library/crypto/crypto.erl), [`clause_crypto`](../../library/crypto/clause_crypto.erl) | Application `crypto`, with [`crypto.app`](../../library/crypto/crypto.app), implemented using OpenSSL's libcrypto in the crypto runtime archive ([crypto module](../builtins.md#crypto-module), [OpenSSL](crypto.md)). Supports detected algorithm lists (`supports/0,1`, `hash_info/1`, `info/0`, `info_lib/0`), digests and hash states, HMAC/CMAC/Poly1305 and MAC states, `pbkdf2_hmac/5`, `hash_equals/2`, `exor/2`, `mod_pow/3`, ciphers and cipher states (`crypto_init/3,4`, `crypto_update/2`, `crypto_final/1`, `crypto_get_data/1`, `crypto_one_time/4,5`, `cipher_info/1`), AEAD (`crypto_one_time_aead/4,6,7`, `crypto_one_time_aead_init/4`), strong random numbers, and `crypto`, `crypto_cache`, `crypto_aes` and `crypto_prng1` `rand` plugins with OTP-compatible error terms. Engines raise `notsup`. FIPS mode is selectable at run time or mandatory with `clau --fips`. Public key operations include `sign/4,5`, `verify/5,6`, RSA encryption, `generate_key/2,3`, `compute_key/4` and `privkey_to_pubkey/2` for RSA, DSA, DH, ECDSA/ECDH with named and explicit curves, EdDSA, X25519/X448, ML-DSA, SLH-DSA and SRP. `ec_curve/1` and `ec_curves/0` obtain explicit parameters from linked libcrypto. ML-KEM uses `encapsulate_key/2` and `decapsulate_key/3` |
| [`ssl`](../../library/ssl/ssl.erl), [`clause_ssl_algorithms`](../../library/ssl/clause_ssl_algorithms.erl), [`clause_ssl_app`](../../library/ssl/clause_ssl_app.erl), [`clause_ssl_engine`](../../library/ssl/clause_ssl_engine.erl), [`clause_ssl_pem_cache`](../../library/ssl/clause_ssl_pem_cache.erl), [`clause_ssl_options`](../../library/ssl/clause_ssl_options.erl), [`clause_ssl_connection`](../../library/ssl/clause_ssl_connection.erl), [`clause_ssl_certificate`](../../library/ssl/clause_ssl_certificate.erl), [`clause_ssl_alert`](../../library/ssl/clause_ssl_alert.erl), [`clause_ssl_hello`](../../library/ssl/clause_ssl_hello.erl), [`clause_ssl_secrets`](../../library/ssl/clause_ssl_secrets.erl), [`clause_ssl_session`](../../library/ssl/clause_ssl_session.erl), [`clause_ssl_session_db`](../../library/ssl/clause_ssl_session_db.erl), [`clause_ssl_tickets`](../../library/ssl/clause_ssl_tickets.erl), [`ssl_session_cache_api`](../../library/ssl/ssl_session_cache_api.erl), [`ssl_crl_cache`](../../library/ssl/ssl_crl_cache.erl), [`ssl_crl_cache_api`](../../library/ssl/ssl_crl_cache_api.erl), [`clause_ssl_crls`](../../library/ssl/clause_ssl_crls.erl) | Application `ssl`, with [`ssl.app`](../../library/ssl/ssl.app), every documented function (plan 21 step LIB81): the utility functions (cipher suite lists, names and filters, signature algorithms, curves, groups, versions, `format_error/1`, `start/0,1`, `stop/0`, `clear_pem_cache/0`) as OTP's ([ssl](ssl.md#utility-functions), [ssl builtins](../builtins.md#ssl)); TLS 1.2 and 1.3 connections over the libssl engine ([connections](ssl.md#connections), [engine](ssl.md#engine), [ssl engine builtins](../builtins.md#ssl-engine)); the hello pause with `handshake_continue/2,3` and `handshake_cancel/1`, `close/2` downgrades and `shutdown/2` (LIB81C2); renegotiation, key updates, exporters, `prf/5` and the secrets of `keep_secrets` (LIB81D1, [ssl](ssl.md#renegotiation-key-updates-and-exporters)); TLS 1.2 session reuse with OTP's caches and `ssl_session_cache_api` stores (LIB81D2a, [ssl](ssl.md#session-reuse)); TLS 1.3 session tickets (LIB81D2b, [ssl](ssl.md#session-tickets)) and early data (LIB81D2c, [ssl](ssl.md#early-data)); `sni_hosts` and `sni_fun` (LIB81D3a, [ssl](ssl.md#server-name-indication)); custom transports and automatic rekeys (LIB81D3b, [ssl](ssl.md#automatic-rekeys)); CRL checks and `ssl_crl_cache` (LIB81D4a, [ssl](ssl.md#crl-checks)); TLS 1.2 PSK (LIB81D5, [ssl](ssl.md#pre-shared-keys)); OCSP stapling and `debug` protocol traces not done (optional), SRP declined |
| [`erl_syntax`](../../library/syntax_tools/erl_syntax.erl), [`prettypr`](../../library/syntax_tools/prettypr.erl), [`erl_prettypr`](../../library/syntax_tools/erl_prettypr.erl), [`erl_syntax_lib`](../../library/syntax_tools/erl_syntax_lib.erl), [`erl_comment_scan`](../../library/syntax_tools/erl_comment_scan.erl), [`erl_recomment`](../../library/syntax_tools/erl_recomment.erl), [`epp_dodger`](../../library/syntax_tools/epp_dodger.erl), [`clause_epp_dodger`](../../library/syntax_tools/clause_epp_dodger.erl), [`merl`](../../library/syntax_tools/merl.erl), [`merl_transform`](../../library/syntax_tools/merl_transform.erl), [`clause_merl`](../../library/syntax_tools/clause_merl.erl) | Application `syntax_tools`, with [`syntax_tools.app`](../../library/syntax_tools/syntax_tools.app) (plan 21 LIB77E): `erl_syntax`'s 303 functions over OTP's trees, wrappers and erl_parse forms, as runtime builtins ([erl_syntax](syntax_tools.md#erl_syntax), [builtins](../builtins.md#erl-syntax)); `prettypr` ([prettypr](syntax_tools.md#prettypr)) and `erl_prettypr` with OTP's text, hooks and options ([erl_prettypr](syntax_tools.md#erl_prettypr)); `erl_syntax_lib`'s folds, analyses and binding annotations ([erl_syntax_lib](syntax_tools.md#erl_syntax_lib)); comment scanning and recommenting ([erl_comment_scan](syntax_tools.md#erl_comment_scan), [erl_recomment](syntax_tools.md#erl_recomment)); parsing without the preprocessor ([epp_dodger](syntax_tools.md#epp_dodger)); `merl`'s quoting, templates and matching with `merl.hrl` and `merl_transform` ([merl](syntax_tools.md#merl)) |

Results and error reasons match OTP 29, including those produced by Erlang implementations
and BIFs. For example, `lists:map(F, x)` raises `{case_clause, x}`, `lists:nth(0, L)` raises
`function_clause`, and `lists:seq(1, 10, 0)` raises `badarg`. BIFs such as `lists:member/2`,
`lists:keyfind/3`, `lists:reverse/2` and the `maps` functions use the applicable `badarg`,
`{badmap, M}` or `{badkey, K}` error. `lists:member/2` uses exact equality (`=:=`), whereas
`lists:keyfind/3` uses non-exact equality (`==`). `lists:sort/1` is a stable runtime builtin;
see [comparison and order](../terms.md#comparison-and-order). Stack traces identify Clause's
library functions rather than OTP's implementation functions.

## Interactive shell

`shell:start_interactive/0,1` starts the Erlang shell on the standard streams (plan 21, step LIB74).
`clau --shell` links the shell into an executable and selects it as the entry point unless
`--entry` specifies another function; see [compile options](../compile.md#options).

The `clause_shell` process performs the role of OTP's `user_drv`: it prints the system version,
starts the shell server and restarts it after a shell exits through `exit().`. The server reads
commands through `io`'s standard input server, repeats the prompt for each continuation line,
maintains history and starts the evaluator. The evaluator checks expressions with `erl_lint`,
expands records and evaluates them with `erl_eval`. Results use the shell's record definitions
and the runtime pretty printer, `clause_io_lib:pretty/2`; see [io_lib](../builtins.md#io_lib).
End of input prints `*** Terminating erlang (nonode@nohost)` and terminates the program with
exit status 0.

Shell commands resolve modules by name. `--shell` therefore links every library module and
preserves every export; see [export liveness](../executables.md#export-liveness). Without this
option, the shell can access only modules referenced by the program. Input uses the terminal's
line editing and is read as delivered by the terminal. `edlin` line editing, history keys and tab
completion are not integrated; see [differences](../differences.md#interactive-shell).

## Module interpreter

[`clause_interp`](../../library/stdlib/clause_interp.erl) (plan 21, step LIB77B) runs a module from
its abstract forms, so a program can call modules that were not compiled into it. Clause's
[transform host](../transforms.md#transform-host) uses it for project parse transforms.

- `clause_interp:load(Forms)` checks the forms with `erl_lint`, expands records, adds
  `module_info/0,1` and `behaviour_info/1` and returns `{ok, Module, Warnings}`, or
  `{error, Errors, Warnings}` in the shape of `compile:forms/2`. Loading a module of the same name
  again replaces it; a module the program links cannot be interpreted.
  `clause_interp:unload(Module)` removes it.
- Calls of the program's code into the module (`M:F(...)`, `apply/3`, `fun M:F/A`, `spawn` with
  a module, function and arguments, behaviour callbacks) enter
  `error_handler:undefined_function/3`, as OTP's calls of modules that are not loaded do; the
  handler passes them to the interpreter ([dynamic calls](../funs.md#dynamic-calls)). A function
  the module does not export raises `undef`.
- `erlang:function_exported/3`, `erlang:module_loaded/1`, `erlang:loaded/0` and the `code` module's
  `ensure_loaded/1`, `is_loaded/1`, `which/1`, `all_loaded/0` and `module_status/0,1` see the module
  as loaded from the file of its first `file` attribute, as `code:load_binary/3` with that file
  name would show it ([code module](../builtins.md#code-module)).
- `clause_interp:set_path(Dirs)` (plan 21 LIB77C) also interprets modules from `.beam` files in
  `Dirs`: a call of a module the program neither links nor interprets loads `Dir/Module.beam`'s
  `debug_info` (`beam_lib:chunks(File, [abstract_code])`) first, as OTP's code server loads a
  module on its first call; `ensure_loaded(Module)` does it ahead, returning `{module, Module}` or
  `{error, {Reason, File}}` (`no_debug_info` among the reasons). The module then shows as loaded
  from its `.beam`.
- `compile`'s binaries are interpreter images (plan 21 LIB77E1): `clause_interp:beam(Forms,
  Chunks)` wraps linted forms in a BEAM container (an `AtU8` chunk naming the module, `Attr`,
  compile's chunks and a `ClFm` chunk with the forms), and `clause_interp:load_binary/3` interprets
  one. `code:load_binary/3`, `code:load_abs/1,2` and `code:load_file/1` hand binaries to it when
  the program has `clause_interp` (any program that calls `compile`; `code` reaches it through a
  non-literal module, so its users do not link the interpreter); other binaries, images of
  another module and images of a linked module give `{error, badfile}`. `module_info(compile)`
  then shows the image's compile information, and `set_path/1` loads images from `.beam` files
  as well.
- Each function runs as an `erl_eval` fun. Local calls, imported functions and `fun F/A` reach the
  interpreter's non-local function handler, and calls in tail position stay tail calls.
- Exceptions leaving interpreted code show OTP's frames for the interpreted functions,
  `{Module, Function, Arity, [{file, File}, {line, Line}]}`, with the arguments for
  `function_clause` and `undef`. The interpreter keeps those frames on a stack of its own in the
  process dictionary; [differences](../differences.md#errors-stack-traces-and-reports) lists what
  it does not reproduce.

<a id="how-programs-get-them"></a>

## Module discovery

A compilation batch consists of positional inputs or one project target. If a batch references
a module by a literal atom and no input defines that module, the compiler searches for `M.erl`
in the library application directories, then in the project's `source_search_paths`; see
[project discovery](../projects.md#paths-and-discovery). The first matching file is parsed and
compiled into the batch. Discovery repeats for references in the added modules according to the
[module resolution order](#which-module-a-name-resolves-to).

References that trigger discovery are `M:F(...)` with literal `M`, `fun M:F/A`,
`apply(M, F, Args)` with literal `M`, `-behaviour(M)` and `-import(M, ...)`. A call to a catalog
builtin, such as `io:format/2` or `os:type/0`, does not itself trigger module discovery.
`--print-inputs` lists the additional inputs.

- **Library roots:** `library/` in the build tree is located relative to `clau`, using the same
  mechanism as the runtime archive. It contains one directory per OTP application, including
  `erts`, `kernel` and `stdlib`. The second root, `library-asn1/`, contains modules generated
  from library ASN.1 specifications; see [public_key](public_key.md).
- **Headers:** `-include_lib("kernel/include/logger.hrl")` and the corresponding references to
  `file.hrl`, `eep48.hrl` and `inet.hrl` (`#hostent{}`) resolve to Clause's headers unless the
  project defines a `kernel` application. The same rule applies to `stdlib/include/zip.hrl`,
  `qlc.hrl`, `ms_transform.hrl` and `assert.hrl` when no project `stdlib` application is defined.
  `ms_transform.hrl` selects a transform executed by the host OTP; see
  [transforms](../transforms.md). `assert.hrl` provides OTP's macros, error terms and `NOASSERT`,
  `NODEBUG` and `ASSERT` switches. `syntax_tools/include/merl.hrl` resolves to Clause's header unless the project defines a `syntax_tools` application; it selects `merl_transform`, which runs in the transform host. OTP internal headers `erl_bits.hrl` and `erl_compile.hrl`,
  distribution headers `dist.hrl`, `dist_util.hrl` and `net_address.hrl`, and the SCTP header
  `inet_sctp.hrl` are not provided.
- **Overrides:** a batch module with the same name replaces the library module and produces a
  warning. Catalog builtins retain precedence; see [registration](../builtins.md#registration).
- **Compilation:** library modules are ordinary batch modules. They link into executables and
  produce separate artifacts for object or IR output. Their Erlang code yields according to
  the same rules as other Erlang code.
- **Dynamic references:** a reference using a runtime module name, such as `M:F(...)` with
  variable `M`, does not add a module. The call raises `undef` unless another reference includes
  the module in the batch.

## Runtime module replacements

Clause provides replacements for OTP modules implemented by ERTS in C, including NIFs, BIFs,
port drivers and preloaded modules under `erts/preloaded/src`. It also replaces modules generated
during OTP builds, such as `unicode_util`. OTP's Erlang `kernel` and `stdlib` modules depend on
these interfaces.

### Provided or missing

Runtime-dependent functionality is classified per module and, where necessary, per function group.
A module is either provided or missing:

- **Provided:** the module exports the exact OTP 29 interface. New modules initially contain
  stubs for every function so that discovery and caller compilation can proceed. Function groups
  are then assigned the statuses defined below.
- **Missing:** no module is supplied. The library catalog records the reason and the
  higher-level Clause interfaces that replace its callers. Examples include the low-level file
  and socket modules `prim_file`, `prim_buffer`, `prim_socket`, `prim_net`, `prim_inet` and
  `socket_registry`. Programs use Clause's `file`, `gen_tcp`, `gen_udp`, `inet`, `net` and
  `socket` interfaces. Clause modules take precedence over OTP modules, so OTP implementations
  that depend on these missing modules, such as `gen_tcp_socket` and `inet_tcp`, are not
  discovered. Missing modules have no stubs; unresolved references produce compile-time errors.

Each function in a provided module has one of four statuses:

| Status | Behavior |
| --- | --- |
| `implemented` | Runtime builtin or Erlang implementation using existing runtime services |
| `refused` | Returns or raises an OTP-compatible unsupported-feature result, such as `{error, enotsup}` |
| `stub` | Raises `error({not_implemented, F, A})` with `M:F/A` at the top of the stack; implementation is pending |
| `declined` | Retains stub behavior with a recorded reason for exclusion, such as distribution or code loading |

Stub behavior follows the [`erlang` stubs](../builtins.md#erlang-module). Declined features are
tracked in [step 71](../../.agents/11-plan.md#step-71). Functions directly supported by existing
runtime services are implemented without a separate classification decision.

A function implemented by a catalog builtin retains its export and the placeholder body
`F(_, ...) -> clause_builtin_placeholder.`. Registration binds the builtin, and qualified calls
`M:F(...)` compile to the bridge. An unqualified call within the module would execute the
placeholder, so internal calls to these functions must be qualified. `library_inventory` rejects
placeholders without matching catalog builtins, catalog builtins represented by `not_implemented`
stubs, and unqualified calls to placeholder functions.

<a id="which-module-a-name-resolves-to"></a>

### Module name resolution

A host OTP installation is optional and must not affect module selection. Resolution order is:

1. **Explicit inputs:** sources listed through `sources`, `source_dirs` or positional arguments.
   An input with the name of a Clause module replaces it and produces a warning naming both files.
2. **Clause library modules:** including provided runtime replacements.
3. **Project `source_search_paths`:** see [project discovery](../projects.md#paths-and-discovery).

Missing module names are reserved. A matching search-path file, such as OTP's
`erts/preloaded/src/prim_file.erl`, is excluded, and the diagnostic states that Clause does not
provide the module. An explicit input can provide the module, with a replacement warning.

`ignore_modules` also applies to Clause modules. An ignored module is excluded from the batch;
an explicit input can then provide that name without a replacement warning.

### Files

- Library modules are stored under `library/<application>/`, using the owning OTP application's
  name, such as `erts` for preloaded counterparts and `erlang`, `kernel`, or `stdlib`. Private helpers
  such as `clause_socket`, `clause_re`, `clause_unicode`, `clause_zlib`, `clause_receive`,
  `clause_maps`, `clause_hashed`, `clause_array`, `clause_features`, `clause_io_lib`,
  `clause_disk_log` and `clause_dets` are stored beside the modules they support. Discovery
  searches every application directory; module names must be unique across those directories.
- The library catalog, `compiler/src/driver/library_modules.inc`, records module classifications
  as `PROVIDED(Module, Application)` or `MISSING(Module, Application, "reason")`, one entry per
  line. It is compiled into `clau`, so it changes with the library sources and `clau` together and
  is not read from disk; a changed catalog changes `clau`'s identity and thereby the library and
  transform host caches. The compiler uses it to identify Clause module names and reserved missing
  names, and `library_modules.py check` compares it with the library's files.
- Each provided module has an adjacent `<module>.terms` inventory containing one
  `{Name, Arity, Group, Status}.` entry per OTP 29 export, sorted by name and arity. `Group`
  identifies the function group, such as `stdlib`, `distribution` or `beam`. `erlang.terms` uses
  `{Name, Arity, Flags, Status}` instead; see [erlang module](../builtins.md#erlang-module).
- Export lists are observed from a host OTP 29 through `M:module_info(exports)` without copying
  OTP source. Implemented results and error terms match OTP 29; deliberate differences are
  recorded in [differences](../differences.md).

### Documented functions

`library/documented.terms` lists public functions documented by OTP 29 for `erts`, `kernel`,
`stdlib`, `crypto`, `public_key`, `ssl`, `syntax_tools` and `compile`. Each `{Module, Application, Name, Arity}.` entry identifies
a non-deprecated function whose module and function documentation are not hidden. The file is
generated explicitly from host OTP documentation chunks by
`tests/compiler/library/observe_docs.escript`. Undocumented exports remain in the inventories
and may remain stubs.

- `library_inventory` requires every documented function of a provided module to appear in its
  inventory with a non-stub status. Exceptions are modules listed as partial in `library_modules.py`,
  with the completion step identified in [plan 21](../../.agents/21-lib.md).
- `python tests/compiler/library/library_modules.py coverage .` reports implemented and
  documented function counts for each documented module.

### Types

Clause modules take precedence over OTP modules, so references such as `file:filename()` and
`erlang:time_unit()` must resolve to types in the Clause modules. Each provided module whose OTP
counterpart exports types includes an adjacent `<module>.types.hrl`. This header contains the
`-export_type` list, exported type definitions, and required local types and records matching OTP.
Definitions are extracted from BEAM debug information or the supplied source of a preloaded module
such as `erlang`; installed documentation chunks do not contain type definitions.

`escript tests/compiler/library/observe_types.escript LIBRARY APP MODULE` generates the header
from the host OTP. `--check` compares its forms against a new observation as part of CTest
`library_freshness`. `library_modules.py check` requires each module to include its type header.
References to types in modules outside the program, such as `unicode:chardata()` and
`calendar:datetime()`, resolve to `term()` without warnings. The same rule applies to unions
exceeding the type graph's capacity, such as `file:posix()`. These library type limitations do not
produce diagnostics for the user's program.

### Kernel services at startup

OTP's `kernel` boot sequence starts servers used by OTP's `file`, `io` and `code`, including
`file_server_2`, `code_server` and a group leader's I/O server. Clause executables do not start
these OTP services. Clause's `file` and `io` implementations do not require these boot servers;
`logger` starts its own server on first use. An OTP module requiring an absent server fails with
`noproc`, as it does in OTP when that server is absent. OTP code requiring `code_server` therefore
cannot use that service at run time. Starting the complete OTP `kernel` application is not planned.

### Applications

- **Resource compilation:** application specifications are compiled from program resources rather
  than discovered on a runtime code path. `clau` includes every `*.app` and `*.app.src` beside
  batch sources, every `*.app` in their adjacent `ebin` directory, and `ebin/Name.app` or
  `src/Name.app.src` for each `[targets.options.applications]` entry. Resources are compiled into
  `clause_applications`, a generated module added whenever the batch includes
  `application_controller`. The first file for each application name takes precedence. A resource
  that lists no modules is assigned the batch modules beside it. Diagnostics identify locations in
  the resource file.
- **Loading:** `application:load/1` also accepts `{application, Name, Keys}` terms. An application
  name without a resource returns `{error, {"no such file or directory", "Name.app"}}`, matching
  OTP's missing-file result.
- **Initial state:** `kernel` (11.0.4) and `stdlib` (8.1) are loaded and running at startup,
  matching OTP 29.1.1's application listing, without application masters. `kernel` uses the
  default environment from `kernel.app`.
- **Lifecycle:** an application with a start module receives an application master as its process
  group leader. This supports `get_application/0`, `get_env/1` and `start_type/0` within the
  application. The master relays I/O requests to standard I/O. On stop, it calls `prep_stop/1`,
  shuts down the top process, calls `stop/1` and terminates the remaining group processes.
  Termination of a permanent application, or abnormal termination of a transient application,
  halts the program with OTP's termination message and exit status 1.
- **Logger dependency:** `logger` and `clause_logger` reference `application` through a runtime
  module name. These references do not cause application support to be linked unless the program
  otherwise references `application`.

### Ledger

[Plan 21](../../.agents/21-lib.md) schedules implementation of every documented `erts`, `kernel`
and `stdlib` module. Real program requirements, initially luerl, determine priorities. Compile and
execute each target program, classify diagnostics as `unknown module`,
`undefined function M:F/A`, unsupported syntax or feature, or other errors, and assign each issue
to a plan step, backlog item or recorded decline.

`python tests/compiler/library/library_modules.py report FILE` classifies saved stderr into
tables. The report lists unknown modules with error counts, calling modules and called functions
with arities; undefined functions grouped by module; and other messages with occurrence counts.

### Tools

The following tools are stored in `tests/compiler/library/`:

- `escript observe_module.escript LIBRARY APP MODULE` writes `LIBRARY/APP/MODULE.terms` from the
  host OTP's `MODULE:module_info(exports)`. Existing groups and statuses are preserved; new exports
  receive `{Name, Arity, default, stub}`.
- `python library_modules.py generate LIBRARY APP MODULE` creates a module with a stub for every
  export. Subsequent edits are maintained manually.
- `python library_modules.py check ROOT`, run by CTest `library_inventory` without OTP, validates
  consistency among module classifications, files, inventories, exports, function bodies and
  catalog builtins. It rejects provided modules with missing files, missing modules with files,
  inventories for modules not classified as provided, exports absent from inventories and the
  reverse, `stub` or `declined` entries without `not_implemented` stubs, and `implemented` or
  `refused` entries without an Erlang body or catalog builtin. `library_rejects` verifies rejection
  using an intentionally inconsistent fixture library.
- CTest `library_freshness`, labeled `host_otp` and `full_only`, repeats inventory observations and
  requires unchanged results. It then observes, generates, checks and compiles a new module
  end to end. `library_report` runs `clau` on a fixture program and checks the generated report rows.
