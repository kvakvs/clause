# Plan 22: Compiler Performance, Type Inference First

Find where `clau` spends its time on real multi-module projects, starting with whole-program type
inference (`compiler/src/semantic/types/`): inference must converge or give up within a small,
bounded amount of work, then cost less per walk. Done steps, with commit hashes and numbers, are in
`00-finished.md` (Plan 22).

**NAMING CONVENTION** Tasks in this file have prefixes based on what they are doing.
- CONV.<id> - convergence of the inference engine; 
- BUG.<id>
- LIV.<id> - liveness analysis
- INS.<id> - instrumentation

**State after CONV.8:** inference W5a 100.4 → 12.2 s, W1 117.0 → 13.0 s; W5a, W1 and W3 settle in
pass 50 (limit 64), so the final `term()` pass no longer runs. Passes 1-16 take ≈ 8.6 s of W5a's
9.7 s of passes (real input churn), passes 17-50 ≈ 1.0 s. Target for W5a: **≤ 10 s** with a
measured, accepted precision loss. Soundness invariant: a fact may get wider, never narrower, than a
correct result. W2 compile 214 → 57.8 s after LLVM.1 (parallel code generation).

## Working rules

- Another agent works in `runtime/`. Plan 22 touches only `compiler/src/semantic/types/`,
  `compiler/src/driver/` (timing output), docs, tests and benchmark scripts; never `runtime/` or
  `abi/`.
- One commit per step: `[plan22] <code>. <title>`, then a `Record commit hash` commit for the
  `00-finished.md` entry. Plan 11's gate applies: fast CTest, `check-quality`, clang-format, docs
  (`docs/semantic.md#inference` for passes, rounds, widening; `docs/compile.md` for every
  `--print-stage-times` line).
- Instrumentation is opt-in: with `Inference::profile` null the hot paths gain at most one
  well-predicted branch, the uninstrumented phase does not slow down and `--print-types` does not
  change. Counters are `size_t` increments; timers stay at pass and component level. Tag new code
  `Added for --print-stage-times.` or with the step's feature name.
- Temporary investigation code stays out of git (scratchpad, or `zz_*` never staged). OTP sources
  and reduced copies stay in the scratchpad; committed fixtures are written from scratch.

## Workloads

| Id | Command (`clau ... --no-cache --print-stage-times`) | Purpose |
|---|---|---|
| **W5a** | `--project <scratch>/b2s/b2s.toml --print-types` | Main target, 134 modules |
| W5r | `--project <scratch>/w5r/w5r.toml --print-types` | Fast loop for the `ubody` group (1.2 s) |
| W1 | `--project test-luerl-euler.toml --entry euler:run --print-types` | 174 modules |
| W2 | W1 with `-O2 -o <scratch>/euler.exe`, no `--print-types` | Executable path; run from `references/luerl/examples/euler` |
| W3 | `--project test-luerl-benchmark.toml --entry benchmarks:run/0 --print-types` | Second luerl entry |
| W4 | `--project test-emqx.toml --print-types` | Stress check, only if it gets through inference |
| W5b | Scratch project with only `references/luerl/src/luerl_scan.erl` | `yystate/7` outlier |
| W6 | `--project test-inference.toml --print-types` | Precision regression check |

- **b2s** (recreate if gone): `sources = [".../compiler-10.0.6/src/beam_core_to_ssa.erl",
  "b2s_main.erl"]` against the installed OTP (`C:/Program Files/Erlang OTP/lib/**/src` search
  paths and includes, `ignore_dirs` crypto, `ignore_modules` prim_tty and prim_inet, defines
  `COMPILER_VSN=1` and `NEW_STACKTRACE`, as `test-luerl-euler.toml`); `b2s_main:main/0` calls
  `beam_core_to_ssa:module/2`. `references/otp` lacks the generated `core_parse.erl`.
- **w5r** (recreate if gone): `beam_core_to_ssa.erl`'s second pass (`ubody_used_vars` …
  `pat_list_vars`, `make_fun`, `set_unused`, `get_free`/`store_free`, `internal_returns`,
  `pre_seq`, `force_atomic`/`force_variable`, `new_var*`, `new_fun_name`, `make_vars`) copied
  verbatim with its records (`#kern{}` all 11 fields, `ds = []`), a stub first pass
  (`gen_body`/`gen_expr`/`gen_match`/`gen_pat`/`gen_defs`) turning tagged tuples into every kernel
  record, a stub `make_ssa_function`; exported `module(Term)` runs `gen_body` then `ubody(B, return,
  St)`, `main/0` feeds it `binary_to_term` of an env var. `w5r.toml` has only `sources`.

## Measuring

- `benchmarks/compile_times.py` (`--help`): `--baseline <clau.exe> --runs 3 -- <args>` interleaves
  runs; keep copies such as `clau-base.exe` next to `clau.exe` in `build/bench/bin/Release`. Quiet
  machine, P-cores, Release `build/bench`.
- **Deterministic counters are the primary metrics**: work steps, lattice joins, passes, rounds,
  member walks, graph nodes. Wall-time medians need ≥ 3% to count. **Code layout moves `clau` by
  ±4%**: check an unaligned gap under 5% with both builds aligned (local
  `target_compile_options(clause_semantic PRIVATE -mllvm -align-all-functions=6)`); counters in
  `Graph::intern` (~40M calls per W5a pass) are layout-costly, keep them temporary.
- Sampling profile: compiler-only `build/prof-compiler` (`/O2 /Z7`, `/DEBUG /OPT:REF`), memory
  `profiling-recipe.md`; the collector keeps only the first 25-30 s of a run.
- **Guards.** Pure performance steps: `--print-types` byte-identical on W5a, W1, W3, W6 (references
  in the scratchpad). Convergence steps: diff against the reference and count signatures returning
  `_` and facts newly `_` per module (a text heuristic counting `_` misreads `number()` →
  `integer()` as wider); review, then re-record `tests/compiler/inference/expectations.py` with
  `--record` in the same commit. Then W2: build and run with both `clau`s, no slowdown.

## Steps, in suggested order

1. **B.0 owned fixture:** done (`inference_walkers`); lower its budgets when a step cuts them.
2. **Re-profile W5a** (sampling) at the current HEAD and rank OPTC.* by measured share.
3. **INS.2 rest:** joins are counted (`Graph::joins`); add `widen`, `meet`, `within`, `subtract`,
   `truncate`/`nesting` and repeated `(op, left, right)` pairs per pass (the bound on a memo's hit
   rate), one `[time] inference lattice ...` line. **INS.3:** joins and nodes created per
   component in `InferenceProfile::Cost`, an `inference costliest` list by time per work step.
4. **OPTC.B Lattice memo** if INS.2 shows repeated pairs: a per-graph cache for 2-member `join`,
   `meet`, `within`, `truncate` keyed by Ids (the graph is append-only); watch memory against the
   1M-node limit.
5. **OPTC.C rest, interning:** compact nodes (atoms and integers as ids or small values, not
   `std::string`), `intern(Node&&)`, `truncate` by reference unless it changes the node.
6. **INS.6 + OPTC.D walk containers:** count `Inference::expressions` inserts/erases, `BindingFacts`
   built, `Snapshot` comparisons; then replace pointer-keyed `std::map`s on per-walk paths with
   vectors indexed by `ExprId`/`BindingId` or hash maps (check `--print-types` order).
7. **INS.4 + OPTC.E outliers:** count `reanalyse` calls (limit hits, work) and `instantiate` calls
   per component; then `yystate` (W5b) and re-analysis fan-out.
8. **CONV.5 harder fact caps on recursive results:** deprioritized (INS.5: facts are small, mean
   tree size 4; the cost is interning and constant work per join).
9. **LLVM.2 rest of W2** (after LLVM.1, W2 compile 57.8 s: inference 13.2, lower 10.9, optimize
   11.1, emit 19.7 s on 32 threads): emission is bound by `core_parse` alone (≈ 14 s of one
   thread); emit each module right after its optimization in the same job (≈ 10 s), destroy the
   moved-out modules off the main thread, and split very large modules (`llvm::SplitModule`):
   liveness keeps `core_parse`, `erl_lint`, `v3_core` and `qlc` whole (LIV.1). `lower` (10.9 s) is
   serial.
10. **Side findings, whenever convenient:**
    - **BUG.1:** `clau` (Release, ab635294) exited 0xC0000005 once in ~100 W5r runs. Suspect a
      `const Node &` from `Graph::get` held across `Graph::intern`; loop a Debug or ASan `clau`
      over W5r and review such uses.
11. **Later:** parallel inference of independent components at one call-graph level (thread-local
    or sharded graphs), only after OPTC.B-D.
