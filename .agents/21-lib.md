# Library functions luerl reaches at run time (2026-10-14)

This file lists the library functions a luerl program calls, in execution order,
that are either missing in Clause or that, when compiled from OTP source,
behave differently from OTP. Each one is a reason the luerl executable produces
no output or fails. Later steps reimplement them or decide what to do about
them.

## Why `test-luerl` prints nothing

- `test-luerl.toml` uses `--entry luerl:init`. `luerl:init/0` only builds a Lua
  state and returns it. It prints nothing on real OTP either, so the executable
  "doing nothing" is the expected behavior of that entry point.
- Once luerl runs real Lua code (`luerl:do/2`), the Clause build failed on the
  first token: `luerl_scan:yypre/2` calls the stub `lists:sublist/2`, which
  raises `{not_implemented,sublist,2}`. Every Lua chunk is scanned, so every
  `luerl:do` call failed before anything was printed.
- After the stand-ins below were added (scratch copy only, not the repository
  library), 30 of the driver's 35 Lua snippets printed the same output as on
  OTP. That includes `print`, string/table/utf8/pairs/load/pcall/metatables,
  `string.format('%g'/'%f'/...)` through OTP's `io_lib_format`, and the
  compiler. Of the other 5:
  - 2 differ only in float digits from the rough `math` stand-ins.
  - 3 are listed under "Still failing" below.

## Method (reproducible)

- **Runtime call tracer** (new, uncommitted):
  `runtime/src/process/call_trace.{hpp,cpp}`, hooked into
  `ProcessStack::enter/tail/leave` and `GeneratedCallState::fail`.
  - `CLAUSE_CALL_TRACE=FILE` logs each *boundary call*: a call from a program
    module into a library module. Program modules are those named by the
    prefixes in `CLAUSE_CALL_TRACE_QUIET`, default `luerl,ttdict,ttsets`. Each
    call is logged with its arguments and its result (`RET`) or failure
    (`EXC`), with bounded depth and width.
  - Every recorded failure is also logged as `RAISE`.
  - `CLAUSE_CALL_TRACE_FIRST=1` adds `FIRST M:F/A` at the first entry of every
    function, including calls inside OTP-compiled modules.
  - `CLAUSE_CALL_TRACE_ALL=1` logs every entry.
  - While a trace is on, generated code takes the slow transfer path (the
    `slow_profile` bit). With the variable unset, nothing changes.
- **OTP twin**: `tools/luerl-trace/ctrace.erl` runs the same driver on the host
  OTP 29 with call tracing (`exception_trace` + `{caller}`). It prints with the
  same rules, so the two traces can be diffed line by line.
- **Driver**: `tools/luerl-trace/luerl_drive.erl` runs 35 Lua snippets, one
  step each, catching errors. `drive.toml` builds it together with luerl.
- **Analysis**:
  - `norm.py` / `mismatch.py` align calls with GNU diff and report calls whose
    results differ.
  - `firsts.py` lists `FIRST` entries that are Clause stubs (from the `.terms`
    inventories), `erlang` builtins marked unavailable, or OTP NIF stubs
    (functions whose body is `erlang:nif_error`, found by `nifs.erl`).
- **Getting past failures**: a scratch copy of `clau.exe` + `clause_runtime.lib`
  sits next to a scratch `library/`:
  - `fill.escript` swaps Clause stubs for OTP's definitions. This was done for
    all `lists`/`maps` stubs, in scratch only, because the OTP code must not
    enter the repository.
  - `stand.py` + `stand/*.erl` add hand-written stand-ins.
  - `ovr/math.erl`, `ovr/binary.erl` are rough pure-Erlang stand-ins listed as
    project sources.
  - A library edit costs a full ~4.5 min rebuild of the luerl program.

## Ordered list: functions reached that are missing or wrong

Order is first entry during the driver run, from `luerl:init/0` through 35 Lua
snippets. "Kind" values:

- **stub**: Clause library stub raising `{not_implemented,F,A}`.
- **nif**: OTP module compiled from source whose function body is
  `erlang:nif_error(undef)`, so Clause raises `undef`.
- **otp-path**: OTP-compiled code that fails because a service it relies on is
  absent in Clause.

| # | Function | Kind | Reached from (Lua feature) | Effect without it |
| --- | --- | --- | --- | --- |
| 1 | `lists:foreach/2` | stub | the driver; also `luerl_lib_basic:eprint/3`, `luerl_lib_io` (`io.write`) | crash |
| 2 | `lists:sublist/2` | stub | `luerl_scan:yypre/2`, every token | **every `luerl:do` fails** |
| 3 | `lists:droplast/1` | stub | `luerl_scan:string_token/3`, string literals | scan fails |
| 4 | `lists:mapfoldl/3` | stub | `luerl_comp_*` passes, `luerl:encode_list` | compile fails |
| 5 | `io:nl/0` | stub | `luerl_lib_basic:print/3` (`print`) | prints the text, then crashes before the newline |
| 6 | `lists:splitwith/2` | stub | `luerl_scan` number tokens | any numeric literal fails |
| 7 | `math:pow/2` | nif | `luerl_scan` exponents (`1e20`), `luerl_emul` `^`, `math.pow`, `bit32` | `^` gives `badarith`; `1e2`, `tonumber('1e2')` crash |
| 8 | `lists:nthtail/2` | stub | `luerl_scan:yysuf/2`, `luerl_emul` (multiple assignment), `select` | scan/assignment fail |
| 9 | `lists:flatten/1` | stub | OTP `io_lib_format` (`string.format('%g')`) | `string.format` fails |
| 10 | `lists:duplicate/2` | stub | OTP `io_lib_format` (`%f` padding) | `string.format` fails |
| 11 | `binary:match/2`, `binary:match/3` | nif | `luerl_lib_string` (`find`), `luerl_lib_string_format` (`%s`) | `string.find`/`format` crash |
| 12 | `binary:part/3` | nif | `luerl_lib_string:sub`, `luerl_lib_string_format` | `string.sub` crashes |
| 13 | `math:sqrt/1`, `math:sin/1`, `math:cos/1`, `math:exp/1`, `math:log/1` | nif | `luerl_lib_math` | `math.*` crash |
| 14 | `lists:max/1`, `lists:min/1` | stub | `luerl_lib_math` (`math.max`/`min`) | crash |
| 15 | `lists:umerge/1` | stub | OTP `ordsets:union/1` in the luerl compiler (variable sets) | compile fails for some chunks |
| 16 | `os:timestamp/0` | stub | `luerl_lib_os` (`os.clock`/`os.time`) | crash |
| 17 | `re:run/2` | nif | `luerl_lib_os_date:replace_pattern/3` (`os.date`) | **still failing** (no regex engine) |
| 18 | `persistent_term:get/2` | stub | `io_lib:write_atom` → `erl_scan:reserved_word` → `erl_features:keywords` | Lua syntax-error message fails |
| 19 | `persistent_term:put/2` | stub | `erl_features` initializes its cache (same chain as 18) | **still failing** |
| 20 | `maps:get/3` | stub | `erl_features` (same chain) | — |
| 21 | `lists:usort/1`, `lists:partition/2` | stub | `code:ensure_modules_loaded` from `io_lib:test_modules_loaded` (the error path of `io_lib:format`) | — |
| 22 | `code_server:call/1` | otp-path | same error path: `code` sends to the registered `code_server`, which Clause never starts → `badarg` | any `io_lib:format` error becomes `badarg` |

Builtins the trace showed as fine: `io:format/1,2`, `io:put_chars/1` and
`os:getenv/1` are runtime builtins, even though `library/*.erl` keeps stubs
for them.

The OTP-compiled modules entered were `array`, `calendar`, `epp`, `erl_anno`,
`erl_features`, `erl_scan`, `io_lib`, `io_lib_format`, `io_lib_pretty`,
`ordsets`, `random`, `string`, `code` and `code_server`. Wherever the two traces
line up, their results equal OTP's, apart from time values. The only
exceptions are the rows above and the rough `math` stand-ins. For example,
`1e100` scanned as `1.0000000000000006e100` with the stand-in `pow`.

## Still failing with the stand-ins

- `os.date` (row 17): `luerl_lib_os_date` matches format directives with
  `re:run/2`. Clause has no `re` NIF.
- Lua syntax errors (rows 18–22): `luerl_parse:yeccerror` formats the token
  with `io_lib:format("~tp")`. That reaches `persistent_term:put/2`. The
  failure then takes `io_lib`'s error path into `code_server`. On OTP the
  result is `{error,[{1,luerl_parse,["syntax error before: ","')'"]}],[]}`. On
  Clause it is `error:badarg`.
- `goto` gives `function_clause` on both. That is luerl's own limitation, not a
  library gap. The stack frames differ (Clause has no arguments, file or line);
  that belongs in `docs/differences.md`, not here.
- `coroutine` is nil on both (luerl does not provide it).

## Referenced by luerl but not reached by this driver

These are stubs or NIF stubs that luerl source calls directly. Expect them next:

- `binary:split/2,3`
- `file:read_file_info/1,2`, `file:write_file/3` (`dofile`/`loadfile`)
- `io:fwrite/1,2,3`, `io:request/2` (`io.read`)
- `lists:join/2`, `lists:keymember/3`, `lists:keysearch/3`
- `maps:is_key/2`, `maps:remove/2`
- `math:acos/1`, `asin/1`, `atan/1`, `atan2/2`, `cosh/1`, `log10/1`, `sinh/1`,
  `tan/1`, `tanh/1`
- `unicode:characters_to_binary/2`
- Indirectly: `re:replace` (pure Erlang over `re:run`), and `math:floor`/`ceil`
  if luerl reaches them

## Suggested grouping for the follow-up work

1. **`lists` stubs** (rows 1–4, 6, 8–10, 14, 15, 21; also `join`, `keymember`,
   `keysearch`): small and pure. They unblock the scanner, the compiler and
   `io_lib_format`.
2. **`io:nl/0,1`, `io:fwrite/1,2,3`** (and `format/3`, `put_chars/2`) as thin
   wrappers over the runtime's `io:format`/`put_chars` builtins.
3. **`math` NIFs** as runtime builtins (libm), plus OTP's float printing rules
   for exact `pow`. This is required for numeric literals with exponents.
4. **`binary` NIFs**: `match/2,3`, `part/2,3`, `split/2,3`, `replace` as
   runtime builtins.
5. **`os:timestamp/0`** (over `erlang:timestamp/0`), **`maps:get/3`,
   `is_key/2`, `remove/2`**.
6. **`persistent_term:get/1,2`, `put/2`**: a runtime-wide term table (plan step
   41M). Required by `erl_features`, so by any `io_lib` atom quoting.
7. **`code_server` absence on `io_lib:format`'s error path**: either provide a
   minimal `code:ensure_modules_loaded/1` (all modules are linked in), or shadow
   `io_lib:test_modules_loaded/3`.
8. **`re`**: decide between a regex engine in the runtime and declining it.
   luerl needs it only for `os.date`, which could also be shadowed.
