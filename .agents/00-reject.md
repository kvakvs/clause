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
- **LIB80E2/E3** (public_key otp form walk and name functions in C++; reverted 2026-10-27, user
  decision): in a Release benchmark the C++ was 3-8% slower than compiled Erlang, and neither is a
  big recursive cluster. E2's long user notice fix (table constraint exits raised raw) and its golden
  case stay. Revert `3a3bed22`.
- **LIB80E5 CRL revoked-entry loops in C++** (tried and reverted 2026-10-27): walks through the Term
  API were 1.7-4x slower than compiled Erlang matching. The `der_decode`/`der_encode` and
  `pkix_crls_validate` SCCs stay Erlang (generated module calls, funs, throws). Rule since then:
  port only hot inner loops with a measured win, or very complex recursive clusters.
