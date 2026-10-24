# Plan 22: Compiler Performance, Type Inference First

Find where `clau` spends its time on real multi-module projects, starting with whole-program type
inference (`compiler/src/semantic/types/`): inference must converge or give up within a small,
bounded amount of work, then cost less per walk. Done steps, with commit hashes and numbers, are in
`00-finished.md` (Plan 22).

**State after CONV.6 (56490e0a):** inference W5a 100.4 → 12.9 s, W1 117.0 → 14.5 s; W5a, W1 and W3
settle in pass 50 (limit 64), so the final `term()` pass no longer runs. Target for W5a: **≤ 10 s**
with a measured, accepted precision loss. Soundness invariant: a fact may get wider, never narrower,
than a correct result.

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

1. **CONV.8 Member-granular warm solves** (new, from CONV.4 measurements). A warm solve still walks
   every member in its first round: `core_parse:yeccpars1/5` + 522 re-solves in passes 17-28 at
   330k-1.2M joins each (≈ 2-3 s of W5a) though most members read nothing that changed. Keep each
   member's last reads (58R `Worklist` reads plus external summary and input versions) across
   solves and mark dirty only members whose reads changed; keep the others' summaries and facts.
   Expect W5a passes 17-50 to cost tens of ms.
2. **B.0 owned fixture:** a Clause-written module reproducing the `ubody` shape (state record of
   more than 16 fields threaded through mutually recursive walkers, results that are unions of many
   record shapes, `ordsets` of variables) in `tests/compiler/inference/` as a budget test like
   `budgets.py`, asserting work steps, joins, passes and rounds, never time.
3. **Re-profile W5a** (sampling) at the current HEAD and rank OPTC.* by measured share.
4. **INS.2 rest:** joins are counted (`Graph::joins`); add `widen`, `meet`, `within`, `subtract`,
   `truncate`/`nesting` and repeated `(op, left, right)` pairs per pass (the bound on a memo's hit
   rate), one `[time] inference lattice ...` line. **INS.3:** joins and nodes created per
   component in `InferenceProfile::Cost`, an `inference costliest` list by time per work step.
5. **OPTC.B Lattice memo** if INS.2 shows repeated pairs: a per-graph cache for 2-member `join`,
   `meet`, `within`, `truncate` keyed by Ids (the graph is append-only); watch memory against the
   1M-node limit.
6. **OPTC.C rest, interning:** compact nodes (atoms and integers as ids or small values, not
   `std::string`), `intern(Node&&)`, `truncate` by reference unless it changes the node.
7. **INS.6 + OPTC.D walk containers:** count `Inference::expressions` inserts/erases, `BindingFacts`
   built, `Snapshot` comparisons; then replace pointer-keyed `std::map`s on per-walk paths with
   vectors indexed by `ExprId`/`BindingId` or hash maps (check `--print-types` order).
8. **INS.4 + OPTC.E outliers:** count `reanalyse` calls (limit hits, work) and `instantiate` calls
   per component; then `yystate` (W5b) and re-analysis fan-out.
9. **CONV.5 harder fact caps on recursive results:** deprioritized (INS.5: facts are small, mean
   tree size 4; the cost is interning and constant work per join).
10. **LLVM phases of W2:** `optimize` + `emit` took 196 s of W2's 328 s at B.4 (W2 build now 230 s
    in total); profile module size, per-function pass time, single-threaded `emit`. Outside
    `semantic/types/`.
11. **Side findings, whenever convenient:**
    - **LIV.1:** liveness keeps every export on W5a and W1 because of the host OTP `file.erl`
      (dynamic call in `io_request/3`, `list_to_atom/1` in `reply/1`): check why the `library/`
      copy or a catalog builtin does not stand in, and whether `--print-types` should run liveness
      under `--entry`.
    - **BUG.1:** `clau` (Release, ab635294) exited 0xC0000005 once in ~100 W5r runs. Suspect a
      `const Node &` from `Graph::get` held across `Graph::intern`; loop a Debug or ASan `clau`
      over W5r and review such uses.
12. **Later:** parallel inference of independent components at one call-graph level (thread-local
    or sharded graphs), only after CONV.8 and OPTC.B-D.
