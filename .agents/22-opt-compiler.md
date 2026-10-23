# Plan 22: Compiler Performance, Type Inference First

Find where `clau` spends its time on real multi-module projects, starting with
whole-program type inference (`compiler/src/semantic/types/`). The first goal is
that inference either **converges or gives up within a bounded, small amount of
work**, instead of repeating whole-batch passes and recursive-component solves
that keep almost the same facts. The next goal is a lower cost per walk. This
file plans only. No compiler code has changed yet.

## Working rules for plan 22 steps

- Another agent works in `runtime/` at the same time. Plan 22 touches only
  `compiler/src/semantic/types/`, `compiler/src/driver/` (timing output), docs,
  tests and benchmark scripts. It never edits `runtime/` or `abi/`.
- Do not take timings while another build or benchmark is running.
  `compile_times.py` (step B.1) checks for this and refuses to time when one is.
- Commit each step on its own. The subject starts with `[plan22]`, then the step
  code and title, e.g. `[plan22] INS.1. Pass-level inference counters`. Plan
  11's gate applies: fast CTest, `check-quality`, clang-format, docs
  (`docs/semantic.md` `#inference` for any change to passes, rounds or
  widening).
- Instrumentation is opt-in. With the profile pointer null (no
  `--print-stage-times`), the hot paths gain at most one well-predicted branch.
  An instrumentation step must show that the uninstrumented inference phase did
  not slow down and that `--print-types` output did not change (B.3).
- Tag new code with `Added for --print-stage-times.` (instrumentation) or with
  the step's feature name (optimizations).
- Temporary investigation code, such as a global `operator new` counter like the
  runtime's untracked `zz_alloc_count.cpp`, stays out of git. Keep it in the
  scratchpad or name it `zz_*` and never stage it. OTP sources and copies of
  them, including reduced copies, stay in the scratchpad (AGENTS.md); committed
  fixtures are written from scratch.
- When a step is done, move it to `00-finished.md` (Plan 22) with its commit
  hash and headline numbers.

## Focus workload: `beam_core_to_ssa` (W5a)

Scratch project, not in git: `<scratchpad>/b2s/b2s.toml` with `sources =
[".../compiler-10.0.6/src/beam_core_to_ssa.erl",
"b2s_main.erl"]` against the installed OTP (`C:/Program Files/Erlang
OTP/lib/**/src` search paths and includes, `ignore_dirs` crypto,
`ignore_modules` prim_tty and prim_inet, defines `COMPILER_VSN=1` and
`NEW_STACKTRACE`, the same as `test-luerl-euler.toml`). `b2s_main:main/0` calls
`beam_core_to_ssa:module/2` and exists only to give `--entry` a 0-arity
function. Recreate the project from this description if the scratchpad is gone.
- Against `references/otp`, `compile.erl` fails with
  `unknown module core_parse`, because `core_parse.erl` is generated from
  `.yrl`. The installed OTP ships it.
- The run pulls in **133 modules** (the OTP compiler, stdlib and kernel parts it
  reaches).
- **Liveness does not trim it.** With `--entry b2s_main:main --print-ir`, every
  module keeps all its exports. `--verbose` gives the reason: `a dynamic call in
  file:io_request/3 and the atom-producing erlang:list_to_atom/1 in
  file:reply/1`, from the host OTP `file.erl`. So `--print-types` and the
  executable path do the same inference work (within 281 steps). That finding
  goes into plan 22 as LIV.1 below.

Quiet re-run 2026-10-23 (B.1, `benchmarks/compile_times.py`, 3 runs after a
warm-up, P-cores, HEAD ab635294 Release `build/bench` `clau`): `--project
<b2s.toml> --no-cache --print-stage-times --print-types`. Medians; spread is
(max - min) / median. The earlier loaded-machine numbers were within 3%.

| Phase | W5a median | spread | W1 median | spread |
|---|---|---|---|---|
| index | 0.90 s | 4.5% | 1.21 s | 4.7% |
| inference | **100.4 s** (98% of compile) | 0.5% | **117.0 s** | 1.2% |
| contracts | 0.31 s | 1.4% | 0.32 s | 0.6% |
| wall | 103.9 s | 0.4% | 121.1 s | 1.3% |

W5a `--print-ir` (earlier, loaded): lower 10.4 s, then failed `LLVM
serialization failed: compilation artifact byte limit exceeded`.

Inference passes (W5a): work steps 1.18M → 2.48M per pass (budget 8M, never
reached), time per pass 0.66 s → 8.4 s. W1: 1.42M → 3.29M steps, 0.84 s → 10.6
s.

| Pass | 1 | 4 | 8 | 12 | 16 | 17 (final, inputs `term()`) |
|---|---|---|---|---|---|---|
| W5a ms | 665 | 4132 | 6522 | 7339 | 8405 | 6162 |
| W5a M work steps | 1.18 | 1.66 | 2.04 | 2.23 | 2.44 | 2.48 |
| ends with | inputs changed | ← | ← | ← | ← | forced |

Slowest components over the 17 passes (W5a; walk counts from the loaded run,
which are deterministic):

| Component | ms | Work steps | Rounds over 17 solves (cap per solve) | Member walks (skipped) | ms per walk |
|---|---|---|---|---|---|
| `beam_core_to_ssa:ubody_used_vars/2` + 6 | **27 160** | 0.48M | 197 (36) | 1097 (282) | **≈25** |
| `core_parse:yeccpars1/5` + 522 | 9 166 | 3.70M | 70 (2100) | 15 447 (21 163) | 0.6 |
| `erl_eval:exprs/6` + 53 | 5 404 | 1.29M | 82 (224) | 2 888 | 1.9 |
| `v3_core:clauses/2` + 52 | 3 851 | 1.39M | 125 (220) | 3 553 | 1.1 |
| `erl_expand_records:clauses/2` + 25 | 2 426 | 0.61M | 95 (112) | 1 692 | 1.4 |
| `beam_core_to_ssa:body/3` + 31 | 2 253 | 0.45M | 115 (136) | 1 933 | 1.2 |

W1 (luerl euler, 174 modules) shows the same 17-pass pattern and the same group
on top: `ubody_used_vars` 25.5 s, then `core_parse` 8.7 s, `luerl_parse` 6.4 s,
`erl_eval` 4.9 s, `luerl_scan:yystate/7` 4.6 s (73 walks, ≈63 ms per walk). So
fixing W5a fixes W1.

### Why it does not converge (from the code, to be confirmed by INS.8)

- **Passes:** `infer()` runs `PASSES = JOIN_ROUNDS + 2 * WIDENING_PASSES = 16`
  whole-batch passes plus the forced final one. After pass 8,
  `gather_inputs(widening=true)` calls `Lattice::widen`, and that only unbounds
  integer bounds that moved. Inputs that grow in **structure**, through new
  union members, new record and tuple shapes or deeper nesting up to the depth-4
  limit, are not widened. They creep for 8 more passes until the final pass sets
  every local input to `term()`. So the batch pays 16 full passes and then
  throws away the precision anyway.
- **Every pass re-walks everything** (`infer_pass`): all components, including
  ones whose inputs and read summaries did not change.
- **Every solve restarts from bottom** (`solve()` sets each member to
  `bottom()`). The `ubody_used_vars` group re-derives its fixed point in 17
  solves, 5-13 rounds each, at about 25 ms per member walk.
- **The cost per walk of this group is extreme.** `ubody/3` and `uexpr/3` thread
  a large `#kern{}` state record and return `{Expr, Used, St}` with `Expr` a
  union of many `#cg_*` and `#b_*` records. The facts are probably near the
  limits: 8 union members, depth 4, 16 tuple elements. Every join, meet and
  `within` on them is expensive, and work steps (syntax) do not count that.
- 58P's `failed` set gives up on components that hit their **round cap or work
  share**. This group hits neither: it converges each pass, just expensively and
  again and again.

## Convergence strategy (main track, steps CONV.*)

Target for W5a: **inference ≤ 10 s on a quiet machine (≈10×)**, ≤ 6 passes whose
work is mostly dirty components, and a measured, accepted precision loss (B.3).
Soundness invariant for every step: a fact may only get wider than today's
result, never narrower.

- **CONV.0 Widen nested integer bounds** (from INS.8 finding 2). Make
  `Lattice::widen` (or the input widening in `gather_inputs`) relax integer
  bounds that moved anywhere inside the fact, not only at the top. Smallest
  step; it should remove the 12-16-change slots on its own.
- **CONV.1 Per-input widening ladder.** Count the changes of each local input
  slot (`Inference::inputs[f][i]`) across passes, then climb a ladder: join for
  K₁ changes, then integer widening (today's `widen`), then structural widening
  (`within_depth`/`truncate` to depth 1, unions collapsed to their categories),
  then `term()`. Each slot changes a bounded number of times, so the passes
  terminate without a global pass count. Inputs that are already stable keep
  their precision, where today the forced final pass widens every input. K₁ and
  the ladder come from INS.8's per-slot change histogram.
- **CONV.2 Incremental passes.** Turn the pass loop into a worklist across
  passes. A component is dirty when one of its local inputs changed, a summary
  it read changed (58R `reads` already lists them), or a `fun_reads` entry
  changed. Clean components keep their summaries and expression facts (do not
  reset `expressions` for them). This needs INS.1's check to show 0
  "clean-but-changed" components over W1, W3, W5a and W6 first.
- **CONV.3 Warm-start solves.** When a recursive component is dirty only because
  its inputs or callees grew, start its next solve from its previous fixed point
  instead of `bottom()`. Joins are not monotone at the depth limit (58Q), so a
  member that would drop below its previous summary keeps the join of the two.
  That costs some precision and keeps soundness; measure the rounds saved.
- **CONV.4 Cost-aware give-up.** Extend 58P's `failed` set: a component whose
  solve cost (lattice ops from INS.2, not syntax steps) is over its share in a
  pass, and that is dirty again in a later pass, gives up at once (`term()`
  members) instead of solving again. The cost share comes from INS.3's
  distribution (for example, more than 1/16 of the pass's lattice ops).
- **CONV.5 Fact size cap on recursive results.** If INS.5 shows the
  `ubody_used_vars` facts at the union, depth or element limits, cap the size of
  recursive-member summaries harder than plain facts (fewer union members, or
  depth 3) during the join rounds.
- **CONV.7 Inputs within the pass** (from INS.8 finding 1). Let local inputs
  reach callees in the same pass instead of one call level per pass: for example
  gather a non-recursive component's inputs from its callers' facts just before
  walking it, walking components caller-first for inputs in between callee-first
  passes, or a separate cheap input-propagation sweep. Measure passes until
  `first` changes stop on W5a.
- **CONV.6 Remove the fixed pass count.** Once CONV.1 bounds input changes and
  CONV.2 makes passes cheap, `PASSES` is a safety limit only. Its final `term()`
  pass should become rare. INS.1 reports when it still fires.

CONV.1 and CONV.2 are independent and both needed. CONV.3 and CONV.4 target the
per-component repetition. CONV.5 targets the per-walk cost.

## Instrumentation steps (INS.*)

All of these record into `InferenceProfile` (`semantic/types/profile.hpp`) only
when `Inference::profile` is set, and print through `driver/compile_times.cpp`
under `--print-stage-times`. Counters are plain `size_t` increments. Timers stay
at pass and component level, because calling `steady_clock` inside lattice ops
would distort them; the sampling profiler (INS.7) covers per-op time. Document
every new line in `docs/compile.md` (`--print-stage-times` row).

### INS.8 Input churn (done, `00-finished.md`)

Lines: `; inputs: ...` on each `inference pass N`, `inference churn`, `inference
churned` (`docs/compile.md`). The code lives in `inference_inputs.cpp`, not
`inference.cpp`: a first version that restructured `infer()` there made the
**uninstrumented** W5r 3.9% slower (spread 1%) by code layout alone; moved out,
the ratio is 1.000.

**W5a findings** (18 156 local input slots, 547 never changed):

| Pass | 1 | 2 | 4 | 8 | 12 | 16 |
|---|---|---|---|---|---|---|
| slots changed | 4715 | 4078 | 3574 | 1477 | 439 | 418 |
| first (none → fact) | 2372 | 1825 | 1350 | 356 | 115 | 29 |
| integer bound | 0 | 14 | 135 | 171 | 26 | 41 |
| union member | 0 | 162 | 59 | 24 | 1 | 2 |
| shape | 0 | 153 | 445 | 389 | 74 | 257 |
| deeper | 0 | 48 | 254 | 116 | 137 | 63 |
| function types | 0 | 17 | 17 | 7 | 2 | 2 |
| term() | 2343 | 1859 | 1314 | 414 | 84 | 24 |
| fun reads not final | 0 | 59 | 14 | 3 | 1 | 0 |

Slots by changes over the batch: 1×: 11 279, 2×: 4 565, 3×: 1 100, 4×: 342,
5-8×: 222, 9-16×: 101. W5r: 149 slots, at most 8 changes, settles in pass 11.

1. **Inputs travel one call level per pass.** `first` changes (a slot leaving
   none) never stop: 29 in pass 16. Components run callees first, so a callee's
   inputs from this pass's callers are only gathered after the pass; a chain of
   k local calls needs k passes before its last callee sees any input. Every one
   of those passes re-walks the whole batch. → new **CONV.7**.
2. **Nested integer bounds creep by one per pass.** The 10 most changed slots
   (12-16 changes, still changing in pass 16) are all `integer bound` inside a
   record or map: `application_master` `#state{}` field `-11..0 → -12..0`,
   `io_lib_format:count_small/2` map `p := 0..14 → 0..15`; W6 shows the same
   (`parsers:count_up/2` `[0..14, ...] → [0..15, ...]`). `Lattice::widen`
   relaxes only the fact's top-level integer family (`lattice.cpp`
   `Lattice::widen`), never integers inside tuples, maps or lists. → CONV.1 must
   widen nested bounds first.
3. **Structure keeps moving late:** `shape` and `deeper` stay 60-260 per pass at
   passes 12-16; union-member growth is over by pass 10.
4. `fun_reads` that were not final drop to 0-1 after pass 10: a minor cause.
5. `term()` is the most common change (half the changes of passes 1-4): many
   inputs go from none straight to term() or reach it. Those slots cannot change
   again.

### INS.1 Pass counters and incremental potential (done, `00-finished.md`)

`; graph: ...; clean: ...` on each `inference pass N` and `inference
clean-changed` lines (`docs/compile.md`), from `inference_profile.cpp`, which
`gather_inputs` calls before gathering. Fingerprints are exact (every summary as
numbers); a component is clean when its members' local inputs and its direct
callees' summaries (static `calls.calls`, not the 58R reads) equal the previous
pass's. `intern` calls and hits were measured once with temporary counters in
`Graph::intern` and not committed: the counters alone moved W5r by 3.8% (layout,
see B.2).

**Findings (W5a; W1 alike):**

| Pass | 2 | 4 | 8 | 11 | 13 | 16 |
|---|---|---|---|---|---|---|
| graph nodes (+created) | 44.3k (+6.5k) | 55.9k (+5.4k) | 68.7k (+2.6k) | 73.0k (+1.4k) | 74.2k (+463) | 75.1k (+279) |
| `intern` calls (temporary) | 6.5M | 20.1M | 32.5M | 35.7M | 38.8M | 43.5M |
| clean components of 8911 | 4521 | 6369 | 7855 | 8284 | 8608 | 8763 |
| time on clean ones | 15% | 13% | 26% | 41% | 71% | 51% |
| clean but changed | 1 | 30 | 4 | 1 | 1 | 0 |

1. **`intern` is called ~20 times per work step** and almost always finds an
   existing node (≥ 99.9% hits from pass 2; W5r: 11.8M calls per pass on 5k
   nodes). Every hit is a `std::map<Node>` lookup comparing strings and child
   vectors. → OPTC.B/OPTC.C move up; INS.7 should confirm the share.
2. **CONV.2 potential ≈ one third of W5a**: clean components took ≈ 31.5 s of
   the ≈ 94 s of passes 2-16, rising to half of each late pass. W5r gains
   nothing (197 of 199 clean, 6% of the time: the `ubody` group is dirty in
   every pass).
3. **Clean is not yet safe:** 0-30 clean components per pass still changed (W5a:
   `compile:binary_passes/0` 3 times, `v3_core:map_sort_key/2`,
   `rand:handler/1`, ...; W1: `luerl_comp:chunk_passes/0` 6 times and `cerl`
   constructors in pass 2). They depend on something besides local inputs and
   direct callees: fun F/A values (`*_passes/0` return lists of funs),
   re-analysis of callees' bodies, `apply` to exported functions. CONV.2 must
   fingerprint the dynamic reads (58R `reads` for every walk, `fun_reads`,
   re-analysis) and re-measure until clean-changed is 0.
4. The graph grows little after pass 8 (≤ 2.6k nodes per pass) and widens 3
   times in total: the late passes re-derive existing facts.

### INS.9 Solve repetition (done, `00-finished.md`)

`; N solves repeated the previous fixed point, M reached it after R rounds in
total` on `inference rounds` (`docs/compile.md`), from a `note_round` call per
round in `iterate()` (`inference.cpp`, profile only; aligned A/B 1.006-1.013,
paired median ≈ 0). "Reached" means every member's summary **equals** the
previous pass's: checking that the members grew past it (`Lattice::within`) can
intern nodes, which a measurement must not do. Lattice-op cost per solve waits
for INS.2.

**Findings (W5a, 17 solves each):**

| Component | ms | rounds | repeated | rounds until reached |
|---|---|---|---|---|
| `ubody_used_vars` + 6 | 27 191 | 197 | 4 | 48 |
| `core_parse:yeccpars1/5` + 522 | 8 987 | 70 | 1 | 1 |
| `erl_eval:exprs/6` + 53 | 5 364 | 82 | 6 | 24 |
| `v3_core:clauses/2` + 52 | 3 788 | 125 | 3 | 24 |
| `erl_expand_records:clauses/2` + 25 | 2 419 | 95 | 5 | 30 |
| `beam_core_to_ssa:body/3` + 31 | 2 257 | 115 | 3 | 18 |
| `erl_pp:body/2` + 6 | 1 040 | 78 | 11 | 44 |

W1 adds `luerl_scan:yystate/7` (4.5 s, 73 rounds): 14 repeated, 54 rounds until
reached.

1. **Exact repeats are rare for the heavy groups:** the `ubody` group ends on
   the previous fixed point in 4 of 17 solves, so an equal-start warm start
   saves 48 of its 197 rounds (24%). Most of its solves end above the previous
   fixed point because its inputs keep growing (INS.8), so CONV.3 must start
   from a previous fixed point that is *below* the new one; how many rounds that
   saves needs a non-interning `within` (INS.9b, measure with CONV.3).
2. Repeated solves are mostly components whose inputs did not change: CONV.2
   skips them whole, so CONV.3 matters only for dirty ones.
3. `yystate/7` repeats 14 of 17 times: CONV.2 alone removes most of its cost.

### INS.2 Lattice operation counters

Count calls to `Lattice::join` (with the member count), `widen`, `meet`,
`within`, `subtract`, `truncate`/`nesting` (with the recursion count) and
`Graph::join`. Keep a hash set of `(op, left, right)` per pass and count
repeated pairs, which bounds a memo's hit rate from above. Build the pair set
only under the profile. Print one line per batch:
`[time] inference lattice ...`.

### INS.3 Cost per step ranking

Add the lattice-op count and the graph nodes created to
`InferenceProfile::Cost`. Add a third top-10 list, `inference costliest`:
components ranked by time per work step, with time, steps, lattice ops and nodes
created.

### INS.4 Re-analysis and fun instantiation

Count `reanalyse` calls, the ones that hit the depth or work limit, and their
work (`reanalysis_work` already exists per pass). Count `instantiate` calls (an
anonymous fun evaluated again at its call). Put these per component in `Cost`,
and the totals in the pass line. `ubody` folds over `#iletrec{}` defs through
anonymous funs, so this may be part of its cost.

### INS.5 Fact size statistics (done, `00-finished.md`)

`inference facts N` per noted pass and `inference largest` for the 3 costliest
components (`docs/compile.md`), from per-node tree size, container depth,
widest union and element-budget flags filled in one pass over the graph
(`inference_profile.cpp`). Aligned A/B 1.005 (paired median +0.5%).

**Findings:** facts are small. W5a pass 16: 152 488 facts, mean tree size 4.0,
max 4236; 1888 (1.2%) at the depth budget, 442 with a full 8-member union, 249
at the element budget, 47 summaries at 32 function types, 1808 dependent
facts. W1 alike (max 10 935). The `ubody` group's largest fact has tree size
1509 (a union of `#cg_*`/`#b_*` record shapes nested to the depth budget);
`core_parse` and `erl_eval`'s are under 30. **CONV.5 (harder size caps) has
little to gain**: per-walk cost comes from interning and constant work per
join (INS.7), not from fact size.

### INS.6 Container statistics

Per pass: the size of `Inference::expressions` and the inserts and erases on it,
the `BindingFacts` built and their `events` sizes, and the `Snapshot`
comparisons in `next_summary`/`classify`.

### INS.7 Sampling profile of clau (done, `00-finished.md`)

`build/prof-compiler`: compiler-only Release, `/O2 /Ob2 /DNDEBUG /Z7`,
`/MT`, iterator debug 0, linker `/DEBUG /INCREMENTAL:NO /OPT:REF /OPT:ICF`
(without `/DEBUG` there is no PDB). Collected with `VSDiagnostics start
/launch` (CpuUsageHigh, 4 kHz; a 500 Hz copy for W5a), processed with `xperf -i
... -a profile -detail` (exclusive) and `-a stack -butterfly` (inclusive,
callers). Scratch scripts: `prof-build.cmd`, `prof-run.cmd`,
`xperf-profile.cmd`, `butterfly.py`. **The collector keeps only about the
first 25-30 s of a run**, so W5a covered ≈ 13 s of inference; its shares match
W5r.

**W5r exclusive time by group:** intern and node map 52% (`Graph::intern`
25%, `Node::operator<=>` 18%, `memcmp` 6%, `Graph::get` 4%), allocation 17%
(`malloc`/`free` 13%), lattice ops 21% (`fact_of` 4%, `family_rank` 3.5%,
`nesting` 2.5%, `Meet::*`, `Lattice::*`), `std::set<std::string>` of atoms
and integers in `Families` 6%, the inference walk itself under 1%.

**Inclusive (W5r, share of clau):**

| Function | Share | Note |
|---|---|---|
| `Lattice::join` | 95% | nearly all work is joins |
| `fact_of` | 91% | |
| `Graph::intern` | 51% | 48% of it from `Lattice::range`, 34% from `Lattice::improper`, 9% `Lattice::tuple` |
| `Gather::Gather` | **38%** | **constructor interns two constants per join** |
| `Lattice::within` | 39% | `fact_of` → `Meet::contained` → `Meet::within` |
| `Meet::same_atoms` | 30% | a full meet + `Lattice::join` to test atom inclusion |

1. **`Gather`'s constructor** (`lattice.cpp`) computes `characters_ =
   lattice.range({"0", CHAR_LIMIT})` and `any_improper_ =
   lattice.improper(top, top)` for every `Gather`, i.e. every join: two
   `Node` constructions (strings, vectors), two map lookups and frees, each
   time yielding the same two Ids. ≈ 38% of W5r and ≈ 30% of sampled W5a
   inference. → **OPTC.A**.
2. **`Meet::same_atoms`** decides atom inclusion by building the meet (atom
   facts, `Lattice::join`, `Gather`) and comparing Ids. → OPTC.A2.
3. Every `intern` builds a `Node` by value even on a hit, and the identity map
   compares nodes field by field with strings: `intern` ≈ 50% after OPTC.A is
   still the next target (OPTC.C, OPTC.B).
4. `Gather::add` copies each `Node` (`const auto node = graph_.get(fact)`),
   part of the allocation share.

### BUG.1 Intermittent clau crash (side finding, investigate)

`clau-base.exe` (Release, ab635294) exited with 0xC0000005 once in about 100 W5r
`--print-types --no-cache` runs, during an A/B; 4 immediate reruns were clean.
Suspect a `const Node &` from `Graph::get` held across `Graph::intern` (the node
vector reallocates; `domain.hpp` warns about it). Check with a Debug or ASan
`clau` looping over W5r, and review `get` uses that intern while holding the
reference.

### LIV.1 Liveness blocked by host `file.erl` (side finding, investigate only)

On W5a and W1, liveness keeps every export because of the host OTP `file.erl`:
the dynamic call in `io_request/3` and `list_to_atom/1` in `reply/1`. Check why
the Clause `library/` copy (or a catalog builtin) does not stand in for `file`,
and whether `--print-types` should run liveness under `--entry`. A working
liveness would shrink the inference input of every executable build. It is not a
substitute for CONV.*, because `--print-types` and IR inspections without an
entry still see the whole batch.

## Benchmarks

### Workloads

| Id | Command | Purpose |
|---|---|---|
| **W5r** | Reduced scratch repro of `ubody`/`uexpr`/`umatch` (B.0) | **Fast loop** for CONV.3-5 and INS.7: seconds, not minutes |
| **W5a** | `clau --project <scratch>/b2s/b2s.toml --no-cache --print-stage-times --print-types` | **Main target**, 133 modules, about 100 s today |
| W1 | `clau --project test-luerl-euler.toml --entry euler:run --no-cache --print-stage-times --print-types` | 174 modules, confirms W5a's fixes carry over |
| W2 | W1 with `-O2 -o <scratch>/euler.exe` and without `--print-types` | Executable path. Read only the `inference` phase line. |
| W3 | `test-luerl-benchmark.toml`, `--entry benchmarks:run/0`, `--print-types` | Second luerl entry |
| W4 | `test-emqx.toml`, `--print-types` | Large app, a stress check, run only if it gets through inference |
| W5b | Scratch project with only `references/luerl/src/luerl_scan.erl` | `yystate/7` single-function outlier (≈71 ms per walk) |
| W6 | `test-inference.toml` (`tests/fixtures/inference/*.erl`) | Small, precision regression check, with expectations below |

### B.0 Reduced repro and owned fixture

- **W5r (scratch only):** copy `beam_core_to_ssa.erl`'s second pass, meaning
  `ubody_used_vars`, `ubody`, `uexpr`, `umatch`, `iletrec_funs*` and their local
  helpers, plus the `#kern{}`, `#ilet{}`, `#iletrec{}`, `#ivalues{}`, `#ifun{}`,
  `#cg_*` and `#b_*` records, into a scratch module. Replace the calls into
  `cerl`, `beam_ssa` and others with local stubs that return the same shapes,
  and give it a 0-arity entry. Aim for the group to cost 1-5 s under the current
  `clau`, with the same round pattern (5-13 rounds per solve, growing facts).
  - **Built 2026-10-23** (`<scratch>/w5r/w5r.erl` + `w5r.toml` with only
    `sources = ["w5r.erl"]`; recreate from this description). The second pass
    (`ubody_used_vars` … `pat_list_vars`, `make_fun`, `set_unused`,
    `get_free`/`store_free`, `internal_returns`, …) and `pre_seq`,
    `force_atomic`/`force_variable`, `new_var*`, `new_fun_name`, `make_vars` are
    copied verbatim. The first pass is a stub `gen_body`/`gen_expr`/
    `gen_match`/`gen_pat`/`gen_defs` that turns tagged tuples into every kernel
    record the second pass matches. `make_ssa_function` is a stub returning a
    one-block `#b_function{}`. `#kern{}` keeps all 11 fields (`ds = []`: the
    library has no `sets`). Exported `module(Term)` runs `gen_body` then
    `ubody(B, return, St)`, so the input is `term()`; `main/0` feeds it
    `binary_to_term` of an env var.
  - **Result** (`compile_times.py --label W5r -- --project
    <scratch>/w5r/w5r.toml --print-types`, 5 runs, HEAD 6dc21c42): inference
    **11.39 s** (spread 0.7%), 11 passes, then **settles** (W5a never does: its
    inputs keep changing from callers W5r does not have). `ubody_used_vars` + 6
    takes 10.47 s: 11 solves, 120 rounds (cap 36), 658 member walks (182
    skipped), last solve 13 rounds, **≈22 ms per walk** (W5a ≈25). Passes 6-11
    cost 1.5 s each at an identical 57 336 work steps. Above the 1-5 s aim, but
    kept: 9× faster than W5a with the same per-walk cost, and long enough to
    profile (INS.7). Use it for CONV.3-5 and the per-walk work, W5a for
    CONV.1/CONV.2 (input churn).
- **Owned fixture (committed):** a Clause-written module that reproduces the
  shape without OTP code: a state record of more than 16 fields threaded through
  mutually recursive walkers, results that are unions of many record shapes, and
  `ordsets` union/subtract of variables. Add it to `tests/compiler/inference/`
  as a budget test in the style of `budgets.py`. The test asserts work steps,
  pass count and rounds from `--print-stage-times`, which are deterministic,
  never wall time. Add it once CONV.1 or CONV.2 lands, so it guards the
  improvement.

### B.1 Benchmark script (done, `00-finished.md`)

`benchmarks/compile_times.py --help`. Example: `--runs 3 -- --project
<scratch>/b2s/b2s.toml --print-types`; add `--baseline <clau.exe>` for A/B (keep
the baseline copy next to a `clau.exe` in `build/*/bin`).

### B.2 Conditions

- Machine quiet (see B.1). P-core affinity. AC power. The same `clau` build
  flavor (Release from `build/bench`, or `make-benchmark.bat --baseline <rev>`
  output).
- **Code layout moves `clau` by ±4%.** Adding unrelated code to an inference
  translation unit (or changing what `inference.cpp` instantiates) shifted the
  uninstrumented W5r by 2-4% with no change on the hot path; the same INS.1
  change measured 1.001-1.010 when both builds aligned every function of
  `clause_semantic` to 64 bytes (`target_compile_options(clause_semantic PRIVATE
  -mllvm -align-all-functions=6)`, local only; the alignment itself costs 3.7%).
  For pure performance and instrumentation steps, an unaligned gap under 5% is
  checked with aligned builds of both sides before it counts. A null A/B (one
  build against a copy of itself) measured 0.999.
- **The deterministic counters are the primary A/B metrics:** work steps per
  pass, pass count, rounds, member walks, graph nodes and lattice ops. A pure
  performance step must leave them unchanged. A convergence step reports them
  before and after. Wall-time medians are secondary and need at least 3% to
  count, after measuring run-to-run spread on W5a.

### B.3 Correctness, precision and overhead guards

- **Pure performance steps** (INS.*, data-structure changes): `--print-types`
  stdout for W5a, W1, W3 and W6 stays byte-identical. Keep the reference outputs
  in the scratchpad, not in git.
- **Convergence steps** (CONV.*) change precision on purpose:
  - Diff `--print-types` against the reference. Count the changed facts per
    module and split them into wider and narrower; narrower must be 0 unless the
    step explains why.
  - Report how many W5a/W1 facts became `term()` that were not before.
  - Re-record `tests/compiler/inference/expectations.py` with `--record` only
    after reviewing the diff by hand, in the same commit.
- **Runtime effect:** codegen depends on facts. After a CONV step, rebuild W2
  (luerl euler executable) and check that `benchmarks/run.py` kernels show no
  slowdown. Specialization lost to wider facts would show up there.
- **Instrumentation overhead:** the W5a inference phase without
  `--print-stage-times` stays within noise of the commit before the INS step.
- Inference expectations, `budgets.py` and fast CTest pass at every step.

### B.4 Baseline record (done, `00-finished.md`)

Quiet baseline at 2af72e57 (code b83e9756), `benchmarks/history/compile-times/
<W>-2af72e57.json` (ignored, local), `build/bench` Release, P-cores; medians
(spread):

| Workload | runs | inference | lower | optimize | emit | wall |
|---|---|---|---|---|---|---|
| W5r | 5 | 11.97 s (2.6%) | | | | 12.05 s |
| W5a | 3 | 96.16 s (0.7%) | | | | 99.54 s |
| W1 | 3 | 114.73 s (0.9%) | | | | 118.82 s |
| W2 (`-O2 -o`) | 3 | 115.83 s (0.9%) | 10.85 s | **75.59 s** | **120.85 s** | 327.70 s |

**On the executable path LLVM takes longer than inference:** `optimize` +
`emit` are 196 s of W2's 328 s. A later plan step should profile them (module
size, per-function pass time, `emit` single-threaded?) once inference is
fixed; it is outside `semantic/types/`.

## Order

1. ~~B.1 script, quiet re-run of W5a and W1~~ (done).
2. ~~B.0 W5r reduced repro (scratch)~~ (done; owned fixture stays in step 6).
3. ~~INS.8 input churn, INS.1 incremental potential, INS.9 solve repetition~~
   (done): the data that sizes CONV.1-CONV.4.
4. ~~INS.7 sampling profile on W5r and W5a, INS.5 fact sizes~~ (done).
5. ~~B.4 baseline~~ (done).
6. OPTC.A → OPTC.A2 → OPTC.A3 (measured 38% and 30% of W5r, small and pure),
   then CONV.0 → CONV.7 → CONV.1 → CONV.2 → CONV.3/CONV.4 → CONV.6, each measured on
   W5a, W1 and W6. Then the owned fixture (B.0).
7. INS.2-INS.4 and INS.6 as the profile calls for them, then OPTC.* ranked by
   measured share.
8. LIV.1 investigation and BUG.1, whenever convenient.

## Per-walk cost optimizations (OPTC.*, rank after measurement)

**After OPTC.A-A3 (f37bb441), against B.4:** inference W5r 11.97 → 5.6 s
(2.1×), W5a 96.2 → 50.8 s (1.89×); the `ubody_used_vars` group 27.2 → 10.7 s.
Pass counts and work steps unchanged. Re-profile (INS.7 recipe) before the
next OPTC step: interning should still lead.

Not started. Each comes with the hypothesis that would justify it.

- **OPTC.A Gather constants (done, `00-finished.md`; W5r 1.71×, W5a 1.73×):** `Gather::Gather` interns
  `range(0..CHAR_LIMIT)` and `improper(term(), term())` on every join; 38% of
  W5r (INS.7). Intern them once per `Graph` (or cache their Ids lazily) so a
  `Gather` only copies two Ids. Pure performance: counters and `--print-types`
  unchanged. Expected W5r ≈ 1.5×.
- **OPTC.A2 Atom inclusion without a meet (done; W5r 0.817):** `Meet::same_atoms` builds the atom
  meet (atom facts and a join) to compare Ids; compare the atom name sets
  directly. 30% of W5r inclusive, much of it the OPTC.A constants.
- **OPTC.A3 No node copies in `Gather::add` (done; W5r 0.976, A2+A3 on W5a 0.897):** read the node by reference where
  no interning can happen in between, else copy only its children.

- **OPTC.B Lattice memo:** many repeated `(op, a, b)` pairs (INS.2). A per-graph
  cache for `join` (2-member), `meet`, `within` and `truncate`, keyed by Ids.
  The cache stays valid because the graph is append-only. Watch the memory
  against the 1M-node limit.
- **OPTC.C Interning:** the profile shows `std::map<Node>` compares and
  allocation. A hash table with a precomputed `Node` hash, or a compact node
  (atoms and integers as interned ids or small values, not `std::string`), and
  `intern(Node&&)`. `truncate` reads a node by reference unless it must change
  it.
- **OPTC.D Walk containers:** map churn (INS.6). Replace the pointer-keyed
  `std::map`s on per-walk paths (`Inference::expressions`, `BindingFacts`) with
  vectors indexed by `ExprId`/`BindingId`, or hash maps. Check `--print-types`
  identity, because iteration order matters where results are printed.
- **OPTC.E Outliers:** `yystate` (W5b) and others that INS.3-INS.5 expose, for
  example re-analysis fan-out.
- **Later:** parallel inference of independent components at the same call-graph
  level. This needs thread-local or sharded graphs, so it comes only after
  CONV.* and OPTC.B-D.
