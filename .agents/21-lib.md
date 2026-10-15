# Library functions luerl reaches at run time

luerl is the reference real-world program for library coverage. Every library
gap its driver and examples found is fixed; the record is in
[00-finished.md](00-finished.md) (plan 11 steps 41T–41ZC, "Library gaps"). The
only open item is generated-code speed.

## Open: generated-code speed

The euler example (`clau.bat --project test-luerl-euler --entry euler:run`) is
correct but slower than OTP: `-O2` takes 0.62 s against OTP's 0.35 s (it took
21 s before plan 20). The work continues in plan 20 ([20-opt.md](20-opt.md),
tier OPT1.x); profile with the LLDB sampler on an `-O2 -g` build.

## Method (reproducible)

- **Runtime call tracer:** `runtime/src/process/call_trace.{hpp,cpp}`, hooked
  into `ProcessStack::enter/tail/leave` and `GeneratedCallState::fail`.
  - `CLAUSE_CALL_TRACE=FILE` logs each boundary call (program module into
    library module) with arguments and `RET`/`EXC`; program modules are the
    prefixes in `CLAUSE_CALL_TRACE_QUIET` (default `luerl,ttdict,ttsets`).
  - Recorded failures are also logged as `RAISE`. `CLAUSE_CALL_TRACE_FIRST=1`
    adds `FIRST M:F/A` at each function's first entry;
    `CLAUSE_CALL_TRACE_ALL=1` logs every entry.
  - While tracing, generated code takes the slow transfer path (`slow_profile`);
    with the variable unset nothing changes.
- **OTP twin:** `tools/luerl-trace/ctrace.erl` runs the same driver on host OTP
  with call tracing and prints by the same rules, so traces diff line by line.
- **Driver:** `tools/luerl-trace/luerl_drive.erl` runs 35 Lua snippets, catching
  errors; `drive.toml` builds it with luerl
  (`clau --project drive.toml --entry luerl_drive:main`).
- **Analysis:** `norm.py`/`mismatch.py` align calls with GNU diff and report
  differing results; `firsts.py` lists `FIRST` entries that are Clause stubs,
  unavailable `erlang` builtins or OTP NIF stubs (found by `nifs.erl`).
- **Getting past failures:** a scratch copy of `clau.exe` and
  `clause_runtime.lib` next to a scratch `library/`; `fill.escript` swaps stubs
  for OTP definitions and `stand.py` plus `stand/*.erl` add stand-ins, in
  scratch only because OTP code must not enter the repository. A library edit
  costs a full rebuild of the luerl program (about 4.5 min).
