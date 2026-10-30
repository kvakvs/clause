# Rejected Work

Steps investigated and dropped, with the measurement behind each decision.

- **OPTM.1 remainder** (hash or binary search instead of linear scans; rejected 2026-10-27). Measured
  pinned with clang `-O2` over the real 1351-entry catalog: `abi::v1::find_bridge_builtin` costs
  340-570 ns per lookup (hash 7-15 ns). Generated code never runs it: it uses compile-time indices.
  It costs about 0.5 ms per program start (`BuiltinRegistry::link_bridge`) and an estimated
  under 0.1% of a build (`semantic::bridge_builtin`, a few calls per call site). Dynamic calls hash
  `exports_` and fall back to `std::map` only for builtins (~100 ns). `BuiltinRegistry::modules()`
  costs about 6 µs, and only `ensure_loaded`/`is_loaded`/`module_loaded` call it, for modules that
  are not compiled code. The small tables (re flags, time units, port options, importer handlers)
  have 30 entries or fewer, where a linear scan is as fast as binary search. If revisited: a sorted
  or perfect-hash bridge index, and the builtin module names cached at registration.
- **OPT1.10 remainder: service memory effects** (measured null 2026-10-30). Non-allocating
  `CLAUSE_immediate_v1` operations (comparisons, `is_function/2`, `element`, `tuple_size`, `length`,
  `byte_size`, min/max, boolean checks; about half of the ~590 sites left at -O2 in the benchmark
  programs) were given `memory(read, argmem: readwrite, inaccessiblemem: readwrite)`, `nounwind`,
  `willreturn` and `captures(none)`. The optimized IR of all six benchmark programs kept every load
  (dataproc 18894). Alias analysis cannot separate a term address (formed from an integer) from the
  context and frame-slot arguments, and a call's TBAA tag would cover the heap reads too. A
  `noalias` body context removes 7-17 loads per program with or without the effects. Even an
  unsound read-only ceiling removed 1.9% (dataproc) and under 0.4% elsewhere, mostly at the entry
  guards of `lists`/`maps` library functions and partly through reloads it is not allowed to skip.
  The other two notes were never steps: claim checks of sibling clauses stay separate, and
  `select_val` on atoms cannot become a `switch` (atom words are runtime-assigned). If revisited:
  `is_function(F, N)` has no inline form and is that ceiling's main source; inlining it needs the
  arity of the runtime's `FunDefinition` at an ABI offset and removes the call itself.
- **LIB80E2/E3** (public_key otp form walk and name functions in C++; reverted 2026-10-27, user
  decision): in a Release benchmark the C++ was 3-8% slower than compiled Erlang, and neither is a
  big recursive cluster. E2's long user notice fix (table constraint exits raised raw) and its golden
  case stay. Revert `3a3bed22`.
- **LIB80E5 CRL revoked-entry loops in C++** (tried and reverted 2026-10-27): walks through the Term
  API were 1.7-4x slower than compiled Erlang matching. The `der_decode`/`der_encode` and
  `pkix_crls_validate` SCCs stay Erlang (generated module calls, funs, throws). Rule since then:
  port only hot inner loops with a measured win, or very complex recursive clusters.
- **LIB9 remainder: 20 `erlang` functions** (declined; moved out of plan 21 on 2026-10-28). They
  raise or return what OTP gives where the feature is unavailable, each recorded as
  `{unavailable, code_loading | distribution | tracing}` in `library/erts/erlang.terms`. Code
  loading (D01: `load_module/2`, `load_nif/2`, `purge_module/1`, `check_old_code/1`,
  `check_process_code/2,3`): programs are linked executables.
  Distribution (`dist_ctrl_get_data/1`, `dist_ctrl_get_data_notification/1`, `dist_ctrl_get_opt/2`,
  `dist_ctrl_input_handler/2`, `dist_ctrl_put_data/2`, `dist_ctrl_set_opt/3`): a Clause program is a
  non-distributed node. Tracing (`trace/3`, `trace_delivered/1`, `trace_info/2`,
  `trace_pattern/2,3`, `system_monitor/0,1,2`, `system_profile/0,2`) raises `notsup` (LIB71); real
  tracing stays a "Later" idea in `21-lib.md`.
- **LIB81D5 SRP suites** (declined 2026-10-28 by the user: libssl's SRP interface is deprecated).
  `srp_identity` is validated as OTP's but no SRP suite is negotiated (`docs/differences.md`); TLS
  1.2 PSK was done instead.
- **LIB78 `parsetools` (`yecc`/`leex`)** (cancelled 2026-10-28 by the user before any work: not needed). Projects
  that ship only `.yrl`/`.xrl` sources keep needing OTP's `yecc`/`leex` to generate their `.erl` files.
