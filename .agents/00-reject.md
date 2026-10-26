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
