# Optimization Candidates from the OTP T2-Full JIT Work

This file lists optimizations taken from Lukas Larsson's (garazdawi) **T2-Full** optimizing-tier
work for BeamAsm, restated for Clause's ahead-of-time LLVM code generation. The plans are planning
input only; no OTP source is copied.

Completed steps (all of tier OPT0 plus OPT1.1, OPT1.2, OPT1.3 parts 1-2, OPT1.4, OPT1.5, OPT1.6
inline caches, OPT1.7 parts 1-3 (with `maps:fold`), OPT1.8 and its remainder, OPT1.9 leaves, OPT1.14
parts 1-2, OPT3.1-3.3, OPT3.6, OPT3.7, OPT3.9 and its remainder, OPT3.10-3.12, OPT3.10B, OPT3.13A-C,
OPT3.14 service path, OPT3.15, OPT3.8, OPT3.16, OPT3.17, OPT3.4 and its remainder, OPT3.18, OPT3.19,
sorted tables of OPTM.1, OPTM.2A-E, OPTM.3, OPTM.4, OPTM.5, OPTM.6): see
[00-finished.md](00-finished.md). Rejected steps (OPTM.1 remainder): see
[00-reject.md](00-reject.md). This file keeps open work only.

**NAMING CONVENTIONS** The optimization steps use prefixes based on their area of concern
- OPT<id1>.<id2> - optimization tasks
- OPTM.<id2> - manual optimization tasks added by the human

## Working rules for plan 20 steps

- Commit each step as soon as it is done (gate green); don't batch steps (user direction
  2026-10-12).
- Prefix the commit subject with `[plan20]`, then the step code and title, e.g.
  `[plan20] OPT0.1. Inline frame transfers`.
- The plan 11 gate applies: `check-quality` first (clang-tidy and Lizard, no suppressions), then
  fast CTest, clang-format, and docs updated (`docs/`, `.agents/arch.md`, `.agents/files.md`,
  `docs/differences.md` for observable differences).
- When a step is done, remove it from this file and add a 1-2 line bullet with its commit hash and
  headline speedup to `00-finished.md` (Plan 20).

## Sources

- PR: <https://github.com/garazdawi/otp/pull/855>, "Implement T2 JIT" (open, branch
  `lukas/erts/beamjit2`, 293 commits, head `d80e30e29d`, 2026-07-18). Planning commit named by the
  user: `a7fafb9567` (2026-07-05); the head holds the complete plan, so this file is based on it:
  - `PLAN/T2FULL/00`–`22` plus `census/*`, `PLAN/T2/*`, `PLAN/verification/*`
  - `erts/emulator/internal_doc/BeamAsmTier2.md`, the final architecture guide
- The PR text also cites `24_deepening_campaign.md` and `25_tmc_technique_a.md`, which are not
  reachable on any fetched ref. The TMC design is `lib/compiler/internal_doc/tail_modulo_cons.md` on
  branch `lukas/erts/tmc`.
- Local refs, fetched only and never checked out (the `maint-29` pin is unchanged); read with
  `git -C references/otp show <ref>:<path>`: `refs/remotes/garazdawi/pr855`,
  `refs/remotes/garazdawi/tmc`.

## How to read T2 evidence for Clause

1. **Every T2 speedup is measured against BeamAsm T1**, which already has inline guard BIFs and
   small-int arithmetic, inline bump allocation, native direct calls (~20 cycles), register-cached X
   registers, shared-keys flatmaps, appendable binaries and a literal pool. Most of these are now
   done in Clause (tier OPT0); step 69A measured Clause O2 at 20.2x slower than OTP JIT at the start
   of plan 20 (worst kernels: `map_put` 618x, `binary_match` 492x, `binary_append` 462x).
2. **Null results depend on the baseline.** T2 found inlining for call overhead alone gives 0 +/- 1
   % because a T1 call is cheap; Clause's calls were costly until OPT0.1 made transfers inline.
3. **T2's deopt, profiling and tier-up machinery does not apply** (no T1 to fall back to). The
   usable content is the transforms, the IR shapes that make them legal (cursor IVs, raw values
   outside GC slots), the measurements and the correctness invariants; a runtime-profile item can be
   replaced by a static inference fact or optionally LLVM PGO.
4. **Clause can do what OTP cannot:** OTP never inlines across modules (hot code loading); Clause
   builds whole executables, so cross-module inlining of `lists`, `maps` and user helpers is open
   unless a module is marked upgradable.
5. **Methodology from the T2 record** (`08_v1_loop_tier.md` §2.3, `16_retrospective.md` §3–4):
   - Size a pool with cycle samples (perf/ETW), not call counts.
   - Eligible does not mean a win: each optimization must prove it removes work, with a negative
     control.
   - Run a cheap static census of applicability before building machinery.
   - Run a differential harness against OTP golden output (results, error terms, output ordering).

## Measured T2 results (reference numbers)

| Mechanism | Result vs T1 | Source |
|---|---|---|
| Loop recovery + fused-guard leaf inlining (`total/2`) | 1.97× min, 1.85× median | `mvp/OUTCOME.md` |
| Binary scan: match-context in registers + loop ownership + SWAR | 5.56× isolated (2.0 → 11.8 GB/s); 6–10 % end to end on `json:decode` | `GBIN_OUTCOME.md` |
| Byte scan-and-count kernels (shipped) | 2.5–3.1× on Apple Silicon; up to 16× digit scan on server ARM | `15_scope…`, `BeamAsmTier2.md` |
| Integer/float tail loops with register-homed accumulators | 1.3–3.8× (single accumulator ~2.4×) | PR text |
| Register-pressured high-arity loops (register homing) | from 1.6× slower to 1.6–1.8× faster | PR text |
| Flatmap shape guard + direct offset load | 1.64× (2 fields); codegen ceiling 2.0–13.6× | `G31_GMAP_OUTCOME.md`, `map_monomorphic_design.md` |
| `maps:find` `{ok,V}` fused into the consumer branch | ≈7× isolated, but no real call sites found | `BeamAsmTier2.md` |
| Inlining for call overhead only | null, 0 ± 1 % | `G3_OUTCOME.md` |
| Cold-arm pruning / branchy dispatch | null, 1.01–1.02× | `G31_GMAP_OUTCOME.md` |
| Body-recursion → loop (Y-homed) | parity; ≈1.37× only on 10-element lists | `BeamAsmTier2.md` |
| TMC (compiler level) | O(1) stack; front-end 2 uses 2–4× less heap; often ~2× faster in the 10k–1M range | `tail_modulo_cons.md` |

On OTP about ⅓ of real time is JIT'd Erlang; the rest is C BIFs and GC (`erts_maps_put`, `eq`,
`erts_cmp_compound`, `do_minor`). Clause's runtime services are the equivalent pool, so making them
fast is itself a target.

## Measured priorities (pinned 69A run 2026-10-21, HEAD 71ec52f8)

Pick work by benchmark impact (user direction 2026-10-16). The goal is to be faster than OTP on
every kernel (user direction 2026-10-20): a kernel already ahead still owns a step, ranked by its
ratio below the kernels behind. The 69A run of 2026-10-21 07:18 (every program pinned to the
P-cores, `--cpus performance` = logical 0-15; each kernel in its own process, kernels sized to 1 s
or more, `benchmarks/history/reports/pekarnya/2026-10-21-71ec52f8.txt`; it replaced the earlier
bce423f3 run of the same day) puts Clause O2 1.28x ahead of OTP JIT by geomean (20.2x slower at the
start of plan 20, 1.68x faster than baseline ffcdaf12). Every kernel by Clause / OTP time, worst
first, with the step that owns it (unprofiled ones say so):

| Kernel | Clause / OTP (ms) | Ratio | Owner |
|---|---|---|---|
| file.small_files | 1256 / 1187 | 1.06x | done (OPT3.4 remainder: 0.81x pinned, OS-bound); file kernels |
| dataproc.small_map | 1544 / 1501 | 1.03x | OPT1.6 caches done (alone, pinned: 1500 to 1035, OTP 1240); OPT1.6 remainder |
| file.read_line | 1514 / 1545 | 0.98x | done (OPT3.4 remainder: 0.92x; was 1.05x); OPT3.23 |
| file.read_file | 1471 / 1514 | 0.97x | file kernels (page faults) |
| file.large_write | 1000 / 1029 | 0.97x | done (OPT3.4 remainder run: 0.88x) |
| datagen.map_put | 1589 / 1664 | 0.96x | OPT3.15 remainder |
| msg.ring | 1355 / 1472 | 0.92x | done (OPT3.9 remainder: 1324 to 1152 alone, ~0.80x); OPT3.23 |
| gc.long_lived_churn | 1195 / 1326 | 0.90x | OPT3.20 (collector, unprofiled) |
| datagen.list_to_tuple | 1408 / 1605 | 0.88x | OPT3.21 (unprofiled) |
| dataproc.binary_match | 1100 / 1277 | 0.86x | loop bookkeeping (a 32-bit stride sum, not a byte scan; OPT1.5 byte scans do not apply) |
| file.large_read | 1203 / 1455 | 0.83x | done (OPT3.4 remainder run: 0.74x) |
| gc.grow_shrink | 860 / 1068 | 0.81x | OPT3.20 |
| socket.udp_round_trip | 1212 / 1511 | 0.80x | OPT3.22 (sockets, unprofiled) |
| socket.tcp_echo | 1294 / 1636 | 0.79x | OPT3.22 |
| datagen.binary_append | 840 / 1098 | 0.76x | OPT3.6 remainder |
| socket.tcp_connect | 1151 / 1510 | 0.76x | OPT3.22 |
| dataproc.map_foldl | 1122 / 1465 | 0.77x | OPT1.12 (`lists:map` builds reversed; OPT3.18 run) |
| msg.ping_pong | 1110 / 1495 | 0.74x | done (OPT3.9 remainder: 1096 to 975 alone); OPT3.23 |
| msg.large_message | 813 / 1099 | 0.74x | done (OPT3.18 run; one copy per send, unprofiled since) |
| dataproc.term_to_binary | 1053 / 1496 | 0.70x | term_to_binary remainder (OPT3.18 run) |
| datagen.list_of_tuples | 1063 / 1517 | 0.70x | done (OPT3.18 run) |
| gc.short_processes | 1113 / 1665 | 0.67x | OPT3.5 remainder |
| dataproc.comprehension | 896 / 1424 | 0.63x | OPT1.3, OPT1.12 |
| datagen.string_build | 591 / 1047 | 0.56x | done (OPT3.8, OPT3.16) |
| dataproc.map_update | 649 / 1235 | 0.53x | done (OPT3.15, OPT1.7 `maps:fold`) |
| dataproc.int_to_list | 422 / 908 | 0.47x | done (OPT3.8) |
| dataproc.sort | 501 / 1113 | 0.45x | done (OPT3.17) |
| msg.fan_in | 576 / 1388 | 0.42x | done (OPT3.9 remainder: 461 to 354 alone); OPT3.23 |
| gc.explicit_collect | 594 / 1615 | 0.37x | OPT3.20 |
| datagen.bigint_factorial | 210 / 1145 | 0.18x | done (OPT3.19 run; was 1369) |

Repeatability: the bce423f3 run the same day matched within 5% on 20 of 30 kernels for both systems.
bigint_factorial's OTP time spreads 37% (1145 here, 1651 then); its OPT3.19 row is a datagen-only
run. Also noisy: file.small_files (Clause 39%), term_to_binary (OTP 19%), short_processes (OTP 36%),
map_put (1.02x then). Confirm a ratio near 1.0x with interleaved pinned runs of the kernel alone
before building. Rows marked "OPT3.18 run" come from the pinned run of the OPT3.18 working tree
(block cache eviction), which moved them by more than the noise; geomean there 1.36x ahead.

## Profiling on Windows

- **Runtime with symbols:** a runtime-only build dir, e.g. `build/prof`
  (`-DCLAUSE_BUILD_COMPILER=OFF`, `CMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG
  /Z7`); link the kernel with the Release `clau -O2 -g --runtime-library
  build/prof/lib/Release/clause_runtime.lib`. Time the same kernel on OTP (`erlc`,
  `erl -noshell -eval`) for the target.
- **Sampler (no admin):** kernel ETW (`xperf -on`, `wpr -start`) is refused without elevation even
  with "Profile system performance" and Performance Log Users (the kernel logger is admin-only; UAC
  filters the token). The VS Standard Collector runs as a service and works:
  - `VSDiagnostics.exe start N /launch:<exe> /launchArgs:<args>
    /loadConfig:<Collector>\AgentConfigs\CpuUsageHigh.json`, then
    `VSDiagnostics.exe stop N /output:<file>.diagsession` (under
    `<VS>\Team Tools\DiagnosticsHub\Collector`).
  - The `.diagsession` is a zip holding `*/sc.user_aux.etl`; with `_NT_SYMBOL_PATH` at the exe's
    folder, `xperf -i sc.user_aux.etl -o out.txt -symbols -a profile -detail` lists weight per
    function; filter by the exe.
- **Fallback:** a cdb sampler (`cdb -pv -p PID`, `~* kc 50` per attach) gives stacks but only ~1
  sample per 1.4 s; run it 90-150 s.
- **Matching CRT:** the profiling build must use the main build's CRT
  (`-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DCLAUSE_MSVC_ITERATOR_DEBUG_LEVEL=0`); with /MD the
  map kernels ran 2x slower.

## Timing on P-cores and E-cores

- The dev host is a hybrid i9-14900KF: 8 P-cores with hyperthreading (logical CPUs 0-15) and 16
  E-cores (logical 16-31). Windows may place an unpinned kernel on an E-core, and that alone swings
  its time up to 2x between runs.
- Time every before/after and OTP comparison pinned to P-cores, Clause and OTP alike:
  `cmd /c start "" /b /wait /affinity 5554 kernel.exe` and the same prefix for
  `erl.exe -noshell -eval ...`. Mask `0x5554` is logical 2, 4, ..., 14: one hyperthread on each of
  P-cores 1-7, leaving core 0 and the sibling threads free.
- `benchmarks/run.py` pins every program to the P-cores by default (`--cpus performance`, logical
  0-15) through its job object; 69A runs before 2026-10-21 were unpinned and mix core placements.
  Programs take `--scheduler-cpus` to bind their own workers. Keep other heavy jobs (builds, CTest,
  tidy) off the machine while timing.
- Run file kernels in a directory under `F:\CLAUSE` (as `benchmarks/run.py` does,
  `build/bench/runs`): in the user's Temp folder file creation is ~5x slower for Clause programs
  (small_files 6.1 s against 1.2 s; OTP's `erl.exe` is not slowed there, likely real-time scanning
  of the unsigned program).

## Tier OPT3: runtime gaps of the 69A kernels

Steps by their kernels' ratio to OTP JIT in the pinned 2026-10-21 69A run (see
[Measured priorities](#measured-priorities-pinned-69a-run-2026-10-21-head-71ec52f8)), largest first;
steps whose kernels are already ahead stay, ranked lower. Profile each kernel before building (see
[Profiling on Windows](#profiling-on-windows)).

- **OPT3.23 message signals (ring 0.80x, ping_pong 0.61x, fan_in 0.27x after part 1, pinned 0-15
  against OTP 1359/1476/1402 ms):** part 1 (lighter process creation) is done, see `00-finished.md`.
  Ring's gap was process lifecycle, not hops: `ring(1000, 500)` runs 315 ns a hop against OTP 407,
  while `ring(1000, 1)` setup took 3.9 us a process against OTP 1.45 (2.3x; 1.8x after part 1). Next
  for ring and short_processes: the NT heap still decommits and refaults the context (3.8 KB) and
  first heap block (1.9 KB), ~25% of setup samples, as the spawner allocates them and other workers
  free them; a shared recycler of those blocks needs a multi-worker negative control (a global lock
  on every small heap block). Batch stealing (half the victim's queue) measured null. Messages are
  copied once now; a send to a receiver that does not run still takes the executor mutex to copy and
  wake it. fan_in (unpinned profile) spends 31% of samples contending for it in `send_locked`;
  ping_pong ~15% in SRW acquire/release (send, push, next_process). The file.read_line profile (a
  call round trip per line) shows monitor plus demonitor of the call ~14% (a map node and a
  reference each). Candidates: per-process signal locks as ERTS, or posting to a parked receiver's
  arrivals with only the waking sender locking (sends that buffered every message regressed fan_in
  in OPT3.5B: allocator contention).
- **OPT3.15 remainder (map_put 0.96x):** `maps:put/3` in a self tail loop
  (`map_put(N, M) -> map_put(N - 1, maps:put(N, V, M))`) copies the path per insert, since the map
  arrives as a parameter. A map-loop variant owning that parameter from the second step on (as
  binary loops own their rest, OPT1.4C), with `maps:put/3` lowered to an update, would insert in
  place.
- **OPT3.20 collector (long_lived_churn 0.90x, grow_shrink 0.81x, explicit_collect 0.37x, unprofiled
  since OPT3.5D):** generational collection and per-kind scans are done. Programs whose phases share
  one process still pay for heaps an earlier phase grew or for a large shrunk block (69A no longer
  shows it: each kernel runs in its own process since a4659943); ERTS's own sizing measured worse in
  Clause (see `docs/runtime-heap.md#generations`).
- **File kernels (read_file 0.98x, small_files 0.81x after the OPT3.4 remainder):** read_file is
  kernel copying plus faults on the fresh pages of its 16 MB reply (~25%); a cache of large freed
  binary buffers would spare them. small_files is file creation and deletion in the OS (~115 and ~50
  us a file, both systems; runs swing bimodally).
- **OPT3.21 list_to_tuple (0.88x, unprofiled):** `list_to_tuple(lists:seq(1, 1000))`; both builtins.
- **term_to_binary remainder (0.70x after OPT3.18):** before OPT3.18 kernel page faults were ~13% of
  samples (the off-heap result binary and the decoded term's heap fragments each call); the codec
  itself is ~65% (decode loop, `Writer::integer`, `Reader::next_integer` not inlined). A
  single-append `tag + value` write measured slower on MSVC.
- **OPT3.22 sockets (udp_round_trip 0.80x, tcp_echo 0.79x, tcp_connect 0.76x, unprofiled):** port
  operations on the Asio thread.
- **OPT3.6 remainder (binary_append 0.76x):** per 4-byte append the inline path allocates a 4-word
  sub-binary (as OTP does); collection of those cells is ~13% and the growth service ~24% of the
  kernel after OPT3.6 (bits_service, `ProcessHeap::reserve` making a fragment when the young heap is
  full).
- **OPT3.5 remainder (short_processes 0.67x):** A-D done (see `00-finished.md`); picks and slice
  ends under a per-process lock (full ERTS model). Each child's own collections were ~53% of CPU at
  32 workers after C.

## Steps outside the 69A metric (review of parked OPT0 follow-ups, 2026-10-17)

Real Erlang programs lean on these, though no 69A kernel shows them. Run in this order, one commit
each.

- **OPT3.13 remainder** (13A-13C done): more portion builtins can become quick builtins
  (`lists:reverse/2`, `'--'/2`) and more binary-module builtins direct; list_to_atom stays at 70 ns
  against OTP 9 in `AtomStorage::intern` (a shared lock and a `std::map` of spellings).
- **OPT3.14 remainder** (service path done): atom `<` still pays the comparison service call (27 ns
  against OTP 10); an order rank per atom readable from generated code would make it a word compare,
  and compound terms holding atoms (tuple keys) still take `structural_order` with `Term`s.

## Invariants of finished tier OPT0 steps

- **OPT0.2:** a split fast/slow lowering (`length/1`, bignum comparison) is atomic; no pass may
  delete the slow tail.
- **OPT0.4:** the in-place map proof (`semantic/map_ownership`) treats every map expression as
  freshly built; constant maps moved into the literal pool must be excluded. Maps and binaries stay
  with their services.

## Tier OPT1: static compiler optimizations (directly applicable, no profile)

Ordered by measured T2 value and how well each fits an AOT compiler. 69A weight on 2026-10-21
(pinned): OPT1.6's inline caches put small_map ahead (~0.84x run alone); OPT1.6 remainder, OPT1.12
(map_foldl 0.77x after OPT3.18), OPT1.5 (binary_match 0.86x) and OPT1.3 (comprehension 0.63x) target
kernels already ahead, so they rank lower but stay (the goal is beating OTP everywhere), and also
pay off in real programs (euler, luerl, json).

### OPT1.14 Batched loop reductions (user idea 2026-10-29)

Every self tail call loads, tests, decrements and stores `FrameState::reductions` and tests `slow`; with
the frame-header memset, the `live` store and the stack/frame reloads that is ~8 memory operations per
step around a 5-instruction body. Experiment (self calls spending no reduction, unpinned, same-round
ratios): count_up -18%, mul_loop -17%, sum_list -19% (OTP still 0.83x of sum_list after that; the
rest is the header reset and reloads). Reductions are not observable (`process_info` reports 0), so
only scheduling fairness constrains this.

- **Loops whose every step enters a call:** done (see `00-finished.md`). The self call still tests
  `slow` (profiling and tracing must see each step). A tail call to a peer of a merged cycle could
  skip its reduction the same way.
- **Leaf loops:** done (see `00-finished.md`). Notes: branching to the body start instead of the
  prologue kept every frame address live across the loop and spilled (mul_loop +11%); without
  likely-branch weights the budget register pushed reloads into the hot path (binary_match +8%).
  mul_loop stays ~8% slower pinned with a clean, spill-free hot path (layout); map_update 69A -5%.
- **Header reset (open):** a body with no continuations and no handlers never sets the resume or
  handler word, so its self call can skip the memset (an experiment dropping it in a body with a call
  looped forever: the resume word must stay reset there); likewise the `live` store when no service
  sets it. Merged cycles (OPT1.1C) still spend through the field.
- Gate: count_up, sum_list, mul_loop kernels vs OTP; 69A; a spinning leaf loop must still let other
  processes run (msg ring with a busy process); `--max-reductions`-style fairness tests if any.

### OPT1.3 remainder: range-proven overflow elimination and wider guard fusion

Parts 1 and 2 are done (see `00-finished.md`): `+ - *` on tagged words branching on the overflow
flag, two-operand tag tests fused into one AND mask. LLVM speculates the checked add above the tag
test (costs ~1% on float-only luerl euler). Open:

- **Overflow-guard elimination:** drop the per-iteration overflow guard when a range proves no
  overflow, e.g. a loop counter bounded by list length or `bit_size`. Also drop redundant tag
  guards.
- **Guard fusion:** test the tags of more than two operands in one mask. Use **AND, not OR**: the
  MVP's `(A|F)&0xF==0xF` bug accepted mixed types.
- **Integer-typed operands:** `integer()` operands still carry the float fallback (code size, spills
  in loops such as `mul_loop`).
- **Clause:** `codegen/proofs` drops checks for proven smalls. Extend this to:
  - comparisons and `band`/`bsl`
  - integer range facts from F34 inference, the equivalent of `ssa_opt_ranges`

### OPT1.5 remainder: more byte-scan shapes

Byte-class scans are done (see `00-finished.md`): a binary loop's first `<<C, R/binary>>` clause with
a byte-only guard and constant-step counters skips up to 16 accepted bytes per step with one vector
test (after earlier literal-byte clauses). Census 2026-10-29: about 15 such loops in stdlib, luerl
and emqx; OTP's own `json` already scans 7 bytes at a time in source (`<<W:56, ...>>` with a SWAR
guard). Open:

- Worst case (the scan clause alternates with another clause every byte): +18% from the probe; an
  adaptive probe could skip it after short runs.
- Several 16-byte steps per Erlang step (spending reductions per step) for long runs.
- Literal-byte scan clauses (`f(<<$a, R/binary>>, N) -> f(R, N - 2)`), guards reading other
  unchanged parameters, `Acc*10 + D`-style dependent loops (stay scalar), the *born-8-wide
  classifier* (`<<W:56, ...>>` guards as in `json:string_ascii`) and *read-and-sum*.

### OPT1.6 remainder: static map shapes and the shared-keys layout

Per-site inline caches over classic cells are done (see `00-finished.md`): a search for a literal
key compares the key word at the index its site found last; variable keys (maps as dictionaries,
luerl's tables) only scan. Open:

- **Static shapes:** when inference proves a map with exactly known atom keys (the literal or record
  origin, `#{k := V}` facts), the key index follows from the keys' term order: a fixed-offset load
  with no probe. Inference keeps only `map()` for loop parameters such as small_map's `State`
  (callers are joined).
- **One guard per map:** several reads and an update of one map each repeat the boxed-tag and header
  test and probe their own cache.
- **Shared-keys flatmap layout** (moved here from OPT0.5; it touches every map consumer): small maps
  (≤ 32) as `header, size, keys→tuple, v0..vn`, the sorted-pairs cell kept as the classic map for
  literals. Shape-preserving updates would copy only the values (9 words instead of 13 for 6 keys)
  and a keys-pointer guard would cover every field. T2 measured 1.64× on a 2-field fold; wrong
  position guesses were 1.45× slower, so always guard.

### OPT1.7 remainder: more higher-order intrinsics

List loops of `map`, `filter`, `filtermap`/`zf`, `partition`, `foreach`, `all`, `any`, `search`,
`takewhile`, `dropwhile`, `splitwith`, `flatmap`, `foldl`, `foldr`, `mapfoldl`, `mapfoldr`,
`zipwith/3` and `zipwith3/4` are done (see `00-finished.md`). Census 2026-10-29 (`lists:F(fun`
written in the call, otp/luerl/emqx): foreach 1082, map 979, foldl 669, filter 294, all/any 233,
flatmap 205, dropwhile 78, splitwith 71, mapfoldl 61, filtermap 48, search 47, partition 44,
takewhile 24, zipwith 13, uniq 4, keymap 1. Open:

- `zipwith/4` and `zipwith3/5` (the `trim` and `pad` modes; rare), `uniq/2`, `keymap/3`, by census.
- Generating the loops from the real `lists` source by cross-module inlining (OPT2.1) instead of
  hand-written loops.

### OPT1.9 remainder: leaf inlining beyond call-free leaves

Call-free leaves are done (see `00-finished.md`). T2's "never inline for call overhead alone" does not
hold in Clause: a call pushes a heap frame and returns through it (~2.8 ns, OTP ~0.7), and a loop
that calls a leaf is no leaf loop. Open:

- Callees that call other leaves (depth up to 3), a growth budget per caller instead of the fixed
  24-expression leaf size, and `?MODULE:f()` sites.
- Leaves that raise (`error/1`, `throw/1`): inlining loses their stack-trace frame, so they stay calls.
- The tuple a leaf returns and the caller matches at once is still built (`{Q, R} = divmod(X, 7)`
  47 ms against 43 hand-inlined; `{ok, V}` 40 against 32): OPT1.13.

### OPT1.10 Cleanup suite across inlined boundaries

- **Passes:** DCE (including dead phis), constant folding, copy propagation, CSE/GVN of pure term
  ops, and LICM of invariant guards, loads and pure computation into the preheader.
- **Immutability:** BEAM terms are immutable. Loads from term memory never alias a store; only GC
  moves them, and GC happens only at safepoints.
- **Clause:** mostly free from LLVM once OPT0.2 and OPT0.3 expose the ops. Emit:
  - `!invariant.load`, or TBAA plus `noalias`, on term-field loads
  - `memory(none/read)`, `nounwind` and `willreturn` attributes on pure services
  - GC/safepoint calls as the only clobbers
- **Related T2 fusions that come for free:**
  - `GetTupleElement` shared base: the tuple untag is CSE'd across consecutive reads
  - a wide `select_val` becomes a binary search or jump table: emit an LLVM `switch` on raw
    atom/small words

### OPT1.11 Float unboxing and FP-register residency

- **What:** keep float-typed values in FP registers within a function, and box only at escapes.
- **Measured:** float loops 2.4–3.8×. OTP's AOT already does this (`ssa_opt_float`), which is why T2
  lists it as "compiler already does it".
- **Clause:** OPT1.2 did the main part: float literals and float-only results keep their double
  (`raw_floats`), cells only where a word is read, float parameters of self tail loops pass as
  double bits. Open: comparisons of a raw float with an unknown word (they make a cell), `float/1`
  and unary minus, `is_float/1` on raw floats, and floats crossing calls between functions
  (arguments, results).

### OPT1.12 Tail-modulo-cons and in-place list building

- **What:** a compiler-level transform (`tail_modulo_cons.md`, default-on in `lukas/erts/tmc`):
  - **Front-end 1:** `[E | self(...)]` body recursion (map, filter, append, comprehensions) becomes
    a destination-passing tail loop. A helper threads `Root` and `Dest`, and
    `set_cons_tail(Dest, New)` fills the hole.
  - **Front-end 2:** `f([H|T], Acc) -> f(T, [g(H)|Acc]); f([], Acc) -> lists:reverse(Acc)` builds
    the list forward and seals it with `lists:reverse(Acc, Root)`. This fires only when `Acc` is
    used solely as the prepend tail and the reverse argument.
- **GC rule:** if the hole cell is not young (`[high_water, HTOP)`), flag a full sweep. Otherwise,
  without a remembered set, the write creates an old→young edge.
- **Measured:**
  - O(1) stack (~5800× less peak stack at 200k elements)
  - front-end 2 builds n cells instead of 2n
  - often ~2× faster than body recursion in the 10k–1M range
  - lists past ~1M elements can regress slightly (repeated full sweeps)
- **Related:** an in-place watermark-guarded `lists:reverse` on a uniquely owned accumulator
  (`erts_internal:rev_inplace/2`).
- **Clause:**
  - Body recursion costs heap frames in Clause's stackless model, so the win is likely larger than
    on BEAM.
  - The cost is observable: shallower stacktraces and `stack_size`. Record it in
    `docs/differences.md`.

### OPT1.13 Construct/deconstruct fusion, then allocation sinking

- **What:** when a tuple or cons is built and destructured in the same region, do not build it. Sink
  an allocation that is dead on every exit path. The `maps:find` → `{ok,V}` → `case` fusion measured
  ≈7× in isolation.
- **T2 caveat:** on OTP this pool is ≈0, because `beam_ssa` already cancels same-function pairs and
  every allocation that survives escapes.
- **Clause:**
  - First check whether our frontend already cancels these pairs. It has no mid-end of its own.
  - Inlining (OPT1.7, OPT1.9, OPT2.1) manufactures new pairs.
  - LLVM SROA does not apply to service-allocated cells, so this needs OPT0.3 plus a small escape
    check on our side.

## Tier OPT2: possible because Clause compiles whole programs

### OPT2.1 Cross-module inlining

- **What:** this is T2's "single most important" missing lever (`census/opt_landscape.md` P1):
  monomorphic call-site inlining across modules.
  - OTP can reach it only with profiling and deopt.
  - Clause sees every module of a non-upgradable executable at compile time.
- **Options:**
  - inline, or LTO-visible bodies for, `lists`/`maps`/`proplists` helpers and user wrappers
  - turn on `--lto` with the inline transfers (OPT0.1) to get this mostly for free
- **Limits:**
  - mark modules that need code upgrade as opaque
  - preserve observable stack-trace and `function_clause` semantics; record any divergence in
    `docs/differences.md`
- **Unlocks OPT0.5B:** in-place updates of maps that arrive from callers (`maps:put/3`, accumulator
  parameters) need interprocedural uniqueness or this inlining.

### OPT2.2 Optional: profile-guided specialization through LLVM PGO

- **What:** T2's runtime-profile items, with a static substitute where possible:
  - map-shape monomorphism
  - entry type classes
  - branch weights for block layout
  - monomorphic fun targets
- **Clause:** without a runtime profile, use type inference first, and LLVM instrumentation PGO
  (`-fprofile-generate/use`) for branch weights. This is low priority: T2 measured entry type-class
  speculation as cost-only (827 guards, none of which eliminated anything).

## Measured to have no value (do not build unless new evidence appears)

| Idea | T2 finding | Clause note |
|---|---|---|
| Cold-arm pruning / branch-counter dispatch specialization | 1.01–1.02×; a 12-clause dispatch costs ~4 ns in T1 | Use branch weights for layout only |
| Inlining for call overhead alone | 0 ± 1 % | Does not hold for Clause: a call pushes a heap frame (~2.8 ns); leaf inlining (OPT1.9) made a loop calling a leaf 3.5x faster |
| Polymorphic inline caches wider than 2 | rarely pays; compiler sites see 20–130+ shapes | Monomorphic map cache only |
| General interning of map shapes | measured unnecessary: literal shapes already share the pointer | — |
| Escape analysis at scale / PEA | survivors escape; top allocators are inside C BIFs | Revisit only after OPT2.1 creates pairs |
| Induction-variable strength reduction (beyond binaries and the `maps:fold` pointer walk) | cons walking has no IV | — |
| Auto-vectorization beyond SWAR recipes | no measured pool | — |
| Unrolling body-recursion loops | flat; serial `get_tl` chain dominates | — |
| Reimplementing map/binary BIFs in Erlang to inline them | moves the cost, does not remove it | Make the C++ services fast instead |
| Software prefetch, "home traffic" elimination, GcTest coalescing across unrolled iterations, broad register-pool widening | measured and dropped (PR text; doc 24 unavailable) | Recheck before attempting |
| Straight-line GVN outside loops | pool ≈0 on real code | Leave to LLVM |
| Frameless leaves (OPT0.1A) | — | Saves a few stores per call; loses the stack-trace entry and `process_info(current_function)` (dropped 2026-10-17) |
| One heap check per basic block (OPT0.3) | — | Measured negligible in Clause (dropped 2026-10-17) |
| String builder: pieces in a list, one `reserve`, then join (OPTM.6) | — | Dropped 2026-10-24: every `+=` site appends to one growing buffer (nothing quadratic), and in a pinned microbench of 2-65536 pieces the builder is 2-10x slower than `+=`. Opportunistic `reserve` was done instead (see `00-finished.md`) |

## Suggested order for Clause

0. **Measurement:** done (OPTM.5: 69A runs pinned to the P-cores).
1. **Substrate (tier OPT0):** done (open follow-ups above); it made services visible to LLVM and cut
   the 100–600× kernels.
2. **Loops:** OPT1.1, OPT1.2 and OPT1.3 parts 1-2 done; OPT1.14 next (count_up 1.13x and sum_list
   1.30x behind OTP, unpinned 2026-10-29), then OPT1.3 remainder.
3. **Binaries:** OPT1.5 byte scans done; remainder above.
4. **Maps:** OPT1.6 remainder (inline caches and the `maps:fold` part of OPT1.7 are done).
5. **Lists:** OPT1.12, then the OPT1.7 remainder together with OPT2.1.
6. **Funs and floats:** OPT1.11 (OPT1.8 remainder done).
7. **Inlining and cleanup:** OPT1.9 call-free leaves done; its remainder, OPT1.10 and OPT1.13.
8. **Outside 69A:** done (OPT3.12-3.14, 2026-10-17 review); next by 69A impact (pinned 2026-10-21
   71ec52f8 run, worst kernel of each step; OPT3.18 done): OPT3.19 (1.20x), OPT1.6 caches (1.03x),
   both done; then the kernels already ahead: OPT3.23 (ring ~0.80x), OPT1.6 remainder (~0.84x),
   OPT3.15 remainder (0.96x), OPT3.20 (0.90x), OPT3.21 (0.88x), OPT1.5 (0.86x), OPT3.22 (0.80x),
   OPT1.12 (0.77x), OPT3.6 remainder (0.76x), term_to_binary remainder (0.70x), OPT3.5 remainder
   (0.67x), OPT1.3 remainder (0.63x). Steps with no 69A kernel (OPT3.13/3.14 remainders,
   OPT1.7-1.11, OPT1.13, OPT2.x) follow; `--lto` (an OPT2.1 proxy) is 1.00x overall.

**Gate for every item:**

- Run a step-69A benchmark kernel before and after the change.
- Run the differential correctness check against OTP golden output.
- Measure a negative control: a branchy workload must not regress.
