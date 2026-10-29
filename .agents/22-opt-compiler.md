# Plan 22: Compiler Performance, Type Inference First

Find where `clau` spends its time on real multi-module projects, starting with whole-program type
inference (`compiler/src/semantic/types/`): inference must converge or give up within a small,
bounded amount of work, then cost less per walk. Done steps, with commit hashes and numbers, are in
`00-finished.md` (Plan 22).

**NAMING CONVENTION** Tasks in this file have prefixes based on what they are doing.
- CONV.<id> - convergence of the inference engine
- OPTC.<id> - cheaper inference work (same results)
- INS.<id> - instrumentation
- B.<id> - benchmarks, fixtures and profiles
- LLVM.<id> - code generation time
- LIV.<id> - liveness analysis
- BUG.<id>

**State after OPTC.C part 4 (identity map over the node vector):** inference W5a 100.4 → 3.7 s, W1
117.0 → 4.2 s (profiled runs); W5a, W1 and W3 settle in pass 50 (limit 64). Target for W5a (≤ 10 s)
reached without precision loss. Shares of W5a inference (OPTC.B part 2 profile, pinned): body walk
60% (`walk` 12.2%, `Module::expression` 4.7%, `refine` 3.8%), lattice ops 12% (`Answers::find` 2.7%,
`fact_of` 1.2%; its `Order::visit` 1.3% is `inference_scopes`), pass driver 8.7% (`unchanged` 2.9%),
`BindingFacts` 7.7%, interning 6.0% (≈ 4.7% after OPTC.C part 4), `gather_all` 4.0%. Soundness
invariant: a fact may get wider, never narrower, than a correct result. W2 compile 214 → 57.8 s
after LLVM.1 (parallel code generation), 54.9 → 39.2 s (16 P-core threads) after LLVM.2 part 1
(literal heap).

## Working rules

- Another agent works in `runtime/`. Plan 22 touches only `compiler/src/semantic/types/`,
  `compiler/src/driver/` (timing output), docs, tests and benchmark scripts; never `runtime/` or
  `abi/`.
- One commit per step: `[plan22] <code>. <title>`, then a `Record commit hash` commit for the
  `00-finished.md` entry. Plan 11's gate applies: `check-quality` first, then fast CTest,
  clang-format, docs (`docs/semantic.md#inference` for passes, rounds, widening; `docs/compile.md`
  for every `--print-stage-times` line).
- Instrumentation is opt-in: with `Inference::profile` null the hot paths gain at most one
  well-predicted branch, the uninstrumented phase does not slow down and `--print-types` does not
  change. Counters are `size_t` increments; timers stay at pass and component level. Tag new code
  `Added for --print-stage-times.` or with the step's feature name.
- The owned fixture `inference_walkers` (B.0) keeps passes, work steps, joins, solves, rounds and
  member walks under budgets; lower them when a step cuts these counters.
- Temporary investigation code stays out of git (scratchpad, or `zz_*` never staged). OTP sources
  and reduced copies stay in the scratchpad; committed fixtures are written from scratch.

## Workloads

| Id | Command (`clau ... --no-cache --print-stage-times`) | Purpose |
|---|---|---|
| **W5a** | `--project <scratch>/b2s/b2s.toml --print-types` | Main target, 134 modules |
| W5r | `--project <scratch>/w5r/w5r.toml --print-types` | Fast loop for the `ubody` group (0.08 s since the memo) |
| W1 | `--project test-luerl-euler.toml --entry euler:run --print-types` | 97 modules (174 before LIB76 dropped OTP sources) |
| W2 | W1 with `-O2 -o <scratch>/euler.exe`, no `--print-types` | Executable path; run from `references/luerl/examples/euler` |
| W3 | `--project test-luerl-benchmark.toml --entry benchmarks:run/0 --print-types` | Second luerl entry |
| W4 | `--project test-emqx.toml --print-types` | Stress check, only if it gets through inference |
| W5b | Scratch project with only `references/luerl/src/luerl_scan.erl` | `yystate/7` outlier |
| W6 | `--project test-inference.toml --print-types` | Precision regression check |

- **b2s** (recreate if gone): `sources = [".../compiler-10.0.6/src/beam_core_to_ssa.erl",
  "b2s_main.erl"]` against the installed OTP (`C:/Program Files/Erlang OTP/lib/**/src` search paths
  and includes, `ignore_dirs` crypto, `ignore_modules` prim_tty and prim_inet, defines
  `COMPILER_VSN=1` and `NEW_STACKTRACE`, as `test-luerl-euler.toml`); `b2s_main:main/0` calls
  `beam_core_to_ssa:module/2`. `references/otp` lacks the generated `core_parse.erl`.
- **w5r** (recreate if gone): `beam_core_to_ssa.erl`'s second pass (`ubody_used_vars` …
  `pat_list_vars`, `make_fun`, `set_unused`, `get_free`/`store_free`, `internal_returns`, `pre_seq`,
  `force_atomic`/`force_variable`, `new_var*`, `new_fun_name`, `make_vars`) copied verbatim with its
  records (`#kern{}` all 11 fields, `ds = []`), a stub first pass
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
  `profiling-recipe.md`; the collector keeps only the first 25-30 s of a run, so pin `clau` to
  P-cores after launch (unpinned W5a takes 25 s). Butterfly inclusives double-count recursion
  (`join` ↔ `fact_of`) and `/OPT:ICF` folds identical map code under one name: attribute raw stacks
  (`xperf -a dumper`) instead, each sample to its innermost category frame.
- **Guards.** Pure performance steps: `--print-types` byte-identical on W5a, W1, W3, W6 (references
  in the scratchpad). Convergence steps: diff against the reference and count signatures returning
  `_` and facts newly `_` per module (a text heuristic counting `_` misreads `number()` →
  `integer()` as wider); review, then re-record `tests/compiler/inference/expectations.py` with
  `--record` in the same commit. Then W2: build and run with both `clau`s, no slowdown.

## Steps, in suggested order

1. **OPTC.F part 2, per-call arguments (`gather_all` 4.0%):** early passes walk nearly everything,
   and a function walked because its inputs changed re-reads every call's argument facts (hash
   lookups through out-of-line `Module::expression`): keep each call's argument facts and read again
   only calls whose caller was walked.
2. **INS.4 + OPTC.E outliers:** count `reanalyse` calls (limit hits, work) and `instantiate` calls
   per component; then `yystate` (W5b) and re-analysis fan-out.
3. **CONV.5 harder fact caps on recursive results:** deprioritized (INS.5: facts are small, mean
   tree size 4; the cost is interning and constant work per join).
4. **LLVM.2 rest of W2.** Part 1 (literal heap) done: compound literals are encoded per module and
   decoded once per runtime at registration; the per-call building of `module_info` data (yecc's
   ~900 `-dialyzer` attributes, half of `core_parse`'s IR) is gone. Profile after it (W2, 32
   threads): lower 9.7, optimize 8.4, emit 10.3 s. `core_parse` still bounds optimize and emit: its
   `yeccpars1/5` tail-call cycle is one 191K-instruction LLVM function (OPT1.1C), which
   `llvm::SplitModule` cannot split, so fusing optimize and emit per module gains little while one
   module leads both phases. Next: cap the size of a tail-call cycle function (fall back to frame
   transfers for huge cycles), then fuse optimize+emit and destroy moved-out modules off the main
   thread; `lower` (≈ 10 s) is serial.
5. **Side findings, whenever convenient:**
    - **BUG.1:** `clau` (Release, ab635294) exited 0xC0000005 once in ~100 W5r runs. Suspect a
      `const Node &` from `Graph::get` held across `Graph::intern`; loop a Debug or ASan `clau` over
      W5r and review such uses.
    - `ast::Module::expression` is 6.8% of W5a inference (out of line, with a moved-from check),
      outside plan 22's files: inline it if the scope widens.
    - **OPTC.C compact names** (deprioritized after part 4): atoms, integers and labels as ids, not
      `std::string`, would cut only `Node::operator==` and `NodeHash` (≈ 1.9% of profiled W5a
      inference, ≈ 1% to gain) and `Families`' `std::set<std::string>` (≈ 0.5%), across ≈ 400 uses
      of `Node::name`/`module`/`labels`. `Answers::find` misses the cache (≈ 180K answers in 8 MB).
6. **Later:** parallel inference of independent components at one call-graph level (thread-local or
   sharded graphs).
