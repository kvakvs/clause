# Plan 22 completed work (from 2026-10-23)

Compact record of the compiler optimizations, one bullet per step;
[the index](00-finished.md) lists the other plans.

Compiler performance, type inference first (`.agents/22-opt-compiler.md`).

- **B.1** (082dc057) `benchmarks/compile_times.py`: pinned, repeated compile-workload timer with
  per-phase medians and `--baseline` A/B ratios. Quiet baseline: W5a inference 100.4 s, W1 117.0 s.

- **B.0** W5r scratch repro: 11.39 s inference, dominated by `ubody_used_vars` member walks;
  settles in pass 11 (W5a never settles).
- **INS.8** (2e12b57f) per-pass input-churn report under `--print-stage-times`; showed inputs
  reach callees one call level per pass and nested integer bounds creep (led to CONV.0, CONV.7).
- **INS.1** (fc6e40c6) per-pass graph counters and clean-component potential; clean components
  reach 71% of late W5a passes, so incremental passes need dynamic reads (CONV.2).
- **INS.9** (c226b524) report of solves that repeat the previous fixed point (W5a: 1-6 of 17 per
  group); profile only.
- **INS.7** (b83e9756) sampling profile, no code change: `Gather` constants and `same_atoms` meets
  dominate W5r; led to OPTC.A.
- **INS.5** (b83e9756) fact-size report per pass; fact size is not the per-walk cost.

- **B.4** (cb94e8f3) quiet baseline at 2af72e57 kept in ignored `benchmarks/history/`:
  inference W5r 11.97 s, W5a 96.16 s, W1 114.73 s.

- **OPTC.A** (288133e0) `Graph` keeps the Ids of char() and improper list, so `Gather` no longer
  interns them per join. Inference W5a 95.8 to 55.2 s, W1 114.7 to 66.7 s; output identical.

- **OPTC.A2** (936a3852) `Meet::same_atoms` compares atom name sets directly instead of building
  a meet. W5r inference 0.817 of OPTC.A.

- **OPTC.A3** (f37bb441) `Gather::add` reads nodes in place instead of copying.
  W5a 56.6 to 50.8 s (1.89x against B.4 overall).

- **OPTC.C part 1** (fafd6d79) hashed node interning (`NodeHash`) in place of an ordered map.
  Inference W5a 50.6 to 31.8 s, W1 to 42.4 s.

- **OPTC.C part 2** (74f4b900) table-driven `family_rank`, `nesting` and atom inclusion without
  copies. Inference W5a 31.3 to 28.8 s, W1 to 37.5 s.

- **OPTC.C part 3** (33595da3) memoized per-node nesting so `truncate` stops re-walking facts.
  W5a 29.8 to 28.7 s.

- **CONV.0** (08384300) `Lattice::widen` also relaxes integer bounds nested in tuples, maps and
  lists. Output identical, time unchanged; fixture `tests/fixtures/inference/widening.erl`.

- **CONV.7** (tried, not kept) inputs gathered within the pass: no gain, W5a still 17 passes;
  folded into CONV.2.

- **CONV.1** (0bf86e74) per-input widening ladder (join, widen, coarsen, then `term()`), so no
  slot changes more than 8 times; output identical, the termination guarantee for CONV.6.

- **CONV.2** (4c2bd059) incremental passes: components whose reads are unchanged are kept and
  their work replayed. Inference W5a 27.7 to 19.6 s, W1 36.3 to 23.7 s.

- **CONV.3** (5c0e09b9) warm-start solves from a recursive component's last summaries.
  Inference W5a 19.6 to 13.6 s, W1 23.9 to 16.2 s.

- **CONV.4** (8927a4df) cost-aware give-up: members of a costly recursive component get
  `term()` inputs when they change again. Inference W5a 13.8 to 11.3 s, W1 16.1 to 13.4 s.

- **CONV.6** (56490e0a) pass limit 17 to 64 as a safety limit; W5a, W1, W3 settle in pass 50.
  Fewer `_` signatures; inference W5a 11.4 to 12.9 s, W1 13.4 to 14.5 s.

- **CONV.8** (44906d9d) member-granular warm solves: each recursive member keeps its last walk
  and replays it while nothing it read changed. Inference W5a 12.8 to 12.2 s, W1 13.9 to 13.0 s.

- **LLVM.1** (15181ec9) parallel code generation (`codegen/parallel`, `--jobs <n>`); objects
  byte-identical to `--jobs 1`. W2 compile 214 to 57.8 s.

- **LLVM.2 part 1** (a632e6c9) per-module compound-literal encoding decoded once per runtime into
  a persistent literal heap (ABI 25); `module_info` no longer rebuilds literals per call.
  W2 compile 54.9 to 39.2 s, executable 41.9 to 33.3 MB.

- **LIV.1** (c4c2378e) investigation, no code change: keep-every-export is triggered by
  `apply`/`list_to_atom` in live code across many modules; no sound fix found.

- **B.0** (9bd4b2e2) owned fixture `tests/fixtures/inference/budgets/walkers.erl` and CTest
  `inference_walkers`: reproduces W5r's cost and holds pass, work, join and solve budgets.

- **B.5** (8c58279b) W5a re-profile, no code change: lattice ops 41%, body walk 28%, walk
  containers 12% of inference. Plan reordered: OPTC.B, D, C, new OPTC.F.

- **INS.2** (a5c2b4ae) lattice operation counts per pass; 98% of W5a joins repeat within a
  pass, so a graph-lifetime memo (OPTC.B) serves nearly all.

- **INS.3** (f76f2d5f) per-component join and node costs (`inference costliest`); `ubody_used_vars`
  is costliest in W5a at 11 us per work step.

- **OPTC.B part 1** (b0ac717e) memoized 2-member `join`, `meet` and `within` in the graph
  (`Graph::answer`), counters unchanged. Inference W5a 12.89 to 7.30 s, W1 14.67 to 8.42 s.

- **B.6** (5a5d69c8) W5a re-profile after the memo, no code change: body walk 48% and lattice ops
  12% of inference. Plan reordered: OPTC.D, OPTC.F, then OPTC.B part 2 with OPTC.C.

- **OPTC.D** (a9216ae4) walk containers: variable facts in flat `BindingValues` slots with a
  once-built `BindingIndex`, hash maps for lookups. Inference W5a 7.17 to 4.83 s, W1 8.21 to 5.49 s.

- **OPTC.F** (3e325483) input gathering indexed per function and re-joined only for functions
  walked or called by a walked function. Inference W5a 4.93 to 4.22 s, W1 5.56 to 4.73 s.

- **OPTC.B part 2** (93bdc6ca) allocation-free joins: memo also keeps joins of one, none is
  `none()`, 16-byte answer slots. Inference W5a 4.21 to 3.93 s, W1 4.76 to 4.48 s.

- **OPTC.C part 4** (deda102d) identity map over the node vector (`Identities`, open-addressed
  8-byte slots) replaces the node-copying hash map. W5a 3.79 to 3.66 s, W1 4.26 to 4.16 s.
