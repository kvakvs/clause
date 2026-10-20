# Optimization Candidates from the OTP T2-Full JIT Work

This file lists optimizations taken from Lukas Larsson's (garazdawi) **T2-Full**
optimizing-tier work for BeamAsm, restated for Clause's ahead-of-time LLVM code
generation. The plans are planning input only; no OTP source is copied.

Completed steps (all of tier OPT0 plus OPT1.1, OPT1.2, part of OPT1.3,
OPT1.4, OPT1.7, OPT1.8, OPT3.1-3.3, OPT3.7, OPT3.9, OPT3.10-3.12, OPT3.10B, OPT3.13A-C, OPT3.14 service path,
read-ahead lines of OPT3.4, sorted tables of OPTM.1, OPTM.2A-E, OPTM.3,
OPTM.4): see [00-finished.md](00-finished.md). This file keeps open work only.

## Working rules for plan 20 steps

- Commit each step as soon as it is done (gate green); don't batch steps (user
  direction 2026-10-12).
- Prefix the commit subject with `[plan20]`, then the step code and title, e.g.
  `[plan20] OPT0.1. Inline frame transfers`.
- The plan 11 gate applies: fast CTest, `check-quality` (clang-tidy and Lizard,
  no suppressions), clang-format, and docs updated (`docs/`, `.agents/arch.md`,
  `.agents/files.md`, `docs/differences.md` for observable differences).
- When a step is done, remove it from this file and add a 1-2 line bullet with
  its commit hash and headline speedup to `00-finished.md` (Plan 20).

## Sources

- PR: <https://github.com/garazdawi/otp/pull/855>, "Implement T2 JIT" (open,
  branch `lukas/erts/beamjit2`, 293 commits, head `d80e30e29d`, 2026-07-18).
  Planning commit named by the user: `a7fafb9567` (2026-07-05); the head holds
  the complete plan, so this file is based on it:
  - `PLAN/T2FULL/00`–`22` plus `census/*`, `PLAN/T2/*`, `PLAN/verification/*`
  - `erts/emulator/internal_doc/BeamAsmTier2.md`, the final architecture guide
- The PR text also cites `24_deepening_campaign.md` and `25_tmc_technique_a.md`,
  which are not reachable on any fetched ref. The TMC design is
  `lib/compiler/internal_doc/tail_modulo_cons.md` on branch `lukas/erts/tmc`.
- Local refs, fetched only and never checked out (the `maint-29` pin is
  unchanged); read with `git -C references/otp show <ref>:<path>`:
  `refs/remotes/garazdawi/pr855`, `refs/remotes/garazdawi/tmc`.

## How to read T2 evidence for Clause

1. **Every T2 speedup is measured against BeamAsm T1**, which already has inline
   guard BIFs and small-int arithmetic, inline bump allocation, native direct
   calls (~20 cycles), register-cached X registers, shared-keys flatmaps,
   appendable binaries and a literal pool. Most of these are now done in Clause
   (tier OPT0); step 69A measured Clause O2 at 20.2x slower than OTP JIT at the
   start of plan 20 (worst kernels: `map_put` 618x, `binary_match` 492x,
   `binary_append` 462x).
2. **Null results depend on the baseline.** T2 found inlining for call overhead
   alone gives 0 +/- 1 % because a T1 call is cheap; Clause's calls were costly
   until OPT0.1 made transfers inline.
3. **T2's deopt, profiling and tier-up machinery does not apply** (no T1 to fall
   back to). The usable content is the transforms, the IR shapes that make them
   legal (cursor IVs, raw values outside GC slots), the measurements and the
   correctness invariants; a runtime-profile item can be replaced by a static
   inference fact or optionally LLVM PGO.
4. **Clause can do what OTP cannot:** OTP never inlines across modules (hot code
   loading); Clause builds whole executables, so cross-module inlining of
   `lists`, `maps` and user helpers is open unless a module is marked
   upgradable.
5. **Methodology from the T2 record** (`08_v1_loop_tier.md` §2.3,
   `16_retrospective.md` §3–4):
   - Size a pool with cycle samples (perf/ETW), not call counts.
   - Eligible does not mean a win: each optimization must prove it removes work,
     with a negative control.
   - Run a cheap static census of applicability before building machinery.
   - Run a differential harness against OTP golden output (results, error terms,
     output ordering).

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

On OTP about ⅓ of real time is JIT'd Erlang; the rest is C BIFs and GC
(`erts_maps_put`, `eq`, `erts_cmp_compound`, `do_minor`). Clause's runtime
services are the equivalent pool, so making them fast is itself a target.

## Measured priorities (2026-10-16 profile of the 69A kernels)

Pick work by benchmark impact (user direction 2026-10-16). cdb stack samples of
the kernels furthest behind OTP JIT (Release runtime with symbols, `build/prof`)
group the cost into:

1. **Collection** (sort 65%, binary_append 50%, int_to_list 31%, map_put 25%,
   map_update 22%, string_build 21%, euler ~15%): every collection was a full
   sweep re-copying long-lived data; generational collection is the fix (done,
   see `00-finished.md`). Left: off-heap binary and map pressure still force
   full sweeps; dead old cells wait for the next full sweep.
2. **`Term`-object overhead in builtins** (term_to_binary 14x: 85% in
   encode/decode; string_build: `atom_to_list`, `++`; int_to_list:
   `integer_to_list`/`list_to_integer`): `TermAccess::admit`, `get_kind`,
   shared-pointer refcounts and `malloc` per call; word-level paths are the fix
   (the external term codec is done, OPT0.26; each `TermFactory` binding costs a
   lifetime check, so one per call).
3. **Message copy** (large_message 9.3x, 1.08x after OPT3.9): flat copies,
   compound-only walks, hand-off and spine-following walks are done
   (OPT0.28-OPT0.30, OPT3.9); small messages to busy receivers still copy twice.
4. **Map trees** (map_put 3-10x, map_update 9.5x): word-level updates, cursors
   and on-heap trees are done (OPT0.32, OPT0.33, OPT3.1); small-map reads and
   present-key updates are inline (OPT3.12).
5. **Lists with funs** (map_foldl 3.9x): known funs are inlined into list
   loops (OPT1.7, OPT1.8); `lists:map` and the other building loops still make
   their list reversed and reverse it (OPT1.12).

## Profiling on Windows

- **Runtime with symbols:** a runtime-only build dir, e.g. `build/prof`
  (`-DCLAUSE_BUILD_COMPILER=OFF`, `CMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG
  /Z7`); link the kernel with the Release `clau -O2 -g --runtime-library
  build/prof/lib/Release/clause_runtime.lib`. Time the same kernel on OTP
  (`erlc`, `erl -noshell -eval`) for the target.
- **Sampler (no admin):** kernel ETW (`xperf -on`, `wpr -start`) is refused
  without elevation even with "Profile system performance" and Performance Log
  Users (the kernel logger is admin-only; UAC filters the token). The VS
  Standard Collector runs as a service and works:
  - `VSDiagnostics.exe start N /launch:<exe> /launchArgs:<args>
    /loadConfig:<Collector>\AgentConfigs\CpuUsageHigh.json`, then
    `VSDiagnostics.exe stop N /output:<file>.diagsession` (under
    `<VS>\Team Tools\DiagnosticsHub\Collector`).
  - The `.diagsession` is a zip holding `*/sc.user_aux.etl`; with
    `_NT_SYMBOL_PATH` at the exe's folder, `xperf -i sc.user_aux.etl -o out.txt
    -symbols -a profile -detail` lists weight per function; filter by the exe.
- **Fallback:** a cdb sampler (`cdb -pv -p PID`, `~* kc 50` per attach) gives
  stacks but only ~1 sample per 1.4 s; run it 90-150 s.

## Tier OPT3: runtime gaps of the 69A kernels

Steps by the gap to OTP JIT in the 2026-10-16 69A run (after OPT0.34). Profile
each kernel before building (see [Profiling on Windows](#profiling-on-windows)).

- **OPT3.4 file I/O:** read_line reads ahead (done); run alone, large_read is at
  parity with OTP (4.1 against 4.0 ms, two thirds in `ReadFile`), so its 69A gap
  and large_write's are heaps left by earlier kernels. read_line is
  still 41 against 29 ms per 20,000 lines: the I/O server round trip (monitor,
  send, receive, demonitor) per line.
- **OPT3.5 process creation and teardown (short_processes 2.5x):** profiled
  2026-10-20 (5000 spawns per round): the parent's spawn loop is ~90% of a
  round on both systems. One worker: Clause 159 ms against OTP 129; 32
  workers: Clause ~52 ms against OTP ~22, and Clause is best at 8 workers
  (36 ms). Lock timing at 32 workers: spawn waits 7 us and holds 1.75 us per
  spawn; the executor mutex is busy ~56% of the run. Each child's own cost is
  GC (7 collections, ~10k words copied, 65-70% of one-worker CPU).
  - **A (done):** spawns build and load the child outside the lock; ended
    contexts are freed after it. 8 workers 42 to 36 ms; 32 unchanged.
  - **B:** sends without the global lock: every message from another process
    goes into a buffer on the receiver's per-process inbox (own small lock,
    pid table sharded with its own locks); the receiver drains it at receive
    waits and before its slice-end lock; deliveries under the global lock drain
    it first (per-sender order); parking checks it under the inbox lock.
  - **C:** spawn and queueing without the global lock: per-queue locks and
    condvars, `Schedule` stored with the process (no `schedules_` map); picks
    stay under the global lock (it guards non-running heaps).
  - **D:** faster collection per word for the children (`Copier::evacuate`,
    `parse_cell`), also the 69A collection kernels.
  - Later: picks and slice ends under a per-process lock (full ERTS model).
- **OPT3.6 binary_append (3.6x):** `<<Bin/binary, N:32>>` 10,000 times: one
  service call per append on the appendable buffer, and collections of the
  growing binary.
- **OPT3.8 int_to_list:** done for list_to_integer's short text (OPT3.8); run
  alone the round trip is 835 against OTP's 444 us per 20,000, now the two
  bodies' digit work (`to_chars`, `from_chars`, a staging vector).
- **Small messages to busy receivers** (after OPT3.9, large_message at 1.08x):
  still copy twice (into a buffer, then into the receiver).
- **Heaps left by earlier kernels** (after OPT3.10B): kernels sharing one
  process still pay for heaps an earlier kernel grew or for a large shrunk
  block (the first sweep after list_of_tuples' 3M-word old area; large_read
  0.82x after OPT3.10B in sequence, unchanged run alone). ERTS's own sizing
  measured worse in Clause (see `docs/runtime-heap.md#generations`).

## Steps outside the 69A metric (review of parked OPT0 follow-ups, 2026-10-17)

Real Erlang programs lean on these, though no 69A kernel shows them. Run in this
order, one commit each.

- **OPT3.13 remainder** (13A-13C done): more portion builtins can become quick
  builtins (`lists:reverse/2`, `'--'/2`) and more binary-module builtins direct;
  list_to_atom stays at 70 ns against OTP 9 in `AtomStorage::intern` (a shared
  lock and a `std::map` of spellings).
- **OPT3.14 remainder** (service path done): atom `<` still pays the comparison
  service call (27 ns against OTP 10); an order rank per atom readable from
  generated code would make it a word compare, and compound terms holding atoms
  (tuple keys) still take `structural_order` with `Term`s.

## Invariants of finished tier OPT0 steps

- **OPT0.2:** a split fast/slow lowering (`length/1`, bignum comparison) is
  atomic; no pass may delete the slow tail.
- **OPT0.4:** the in-place map proof (`semantic/map_ownership`) treats every map
  expression as freshly built; constant maps moved into the literal pool must be
  excluded. Maps and binaries stay with their services.

## OPTM.1 remainder: binary search over static tables

Sorted tables done (see `00-finished.md`). Open: the small tables that would
need reordering first (re flags and newlines, time units, port options, the
importers' handler tables); not the index-bearing ones (`fixed_atom_spellings`,
preprocessor directive names, compile cache output kinds) or
process_info/system_info item lists.

## Tier OPT1: static compiler optimizations (directly applicable, no profile)

Ordered by measured T2 value and how well each fits an AOT compiler.

### OPT1.3 Speculation-free small-int fast paths, with guard fusion and range-proven overflow elimination

Part done 2026-10-16: `+`/`-` add the tagged words (`a + (b ^ 15)`, the
one-untag trick) and test word overflow with a sign test; both tag tests already
fuse into one mask. Open: the flag form (`llvm.s*.with.overflow`, `add; jo`, a
further 5-15% on the arithmetic kernels) is blocked only by a clang-analyzer
false positive in LLVM's `ExtractValueInst` headers; `*` still uses double
width; range-proven elimination beyond `codegen/proofs`.

- **Overflow check:** compute with flag-setting instructions and branch on
  overflow to the bignum service ("check before commit").
- **One-untag trick:** untag one operand and add it to the tagged other.
- **Guard fusion:** test the tags of several operands in one mask. Use **AND,
  not OR**: the MVP's `(A|F)&0xF==0xF` bug accepted mixed types.
- **Overflow-guard elimination:** drop the per-iteration overflow guard when a
  range proves no overflow, e.g. a loop counter bounded by list length or
  `bit_size`. Also drop redundant tag guards.
- **Clause:** inline `+ - *` already exist, and `codegen/proofs` drops checks
  for proven smalls. Extend this to:
  - comparisons and `band`/`bsl`
  - fused multi-operand guards
  - integer range facts from F34 inference, the equivalent of `ssa_opt_ranges`

### OPT1.5 Unrolling plus SWAR byte-class recipes for scan loops

Builds on OPT1.4.

- **Unroll:** byte scans ×8 by default (`word_bits/stride`):
  - one strengthened `BsEnsure(N·S)` per chunk
  - one 64-bit load
  - a scalar remainder loop
- **Lane-combining recipes:** about five cover the predicates that occur in
  practice: `==C`, `<C`, a range, and small-set membership, each with
  OR-reduction. Use the classic `(w - 0x0101..) & ~w & 0x8080..` family. A
  scalar tail finds the exact stop byte.
- **Variants:**
  - *roll-back skip-count*: one checked `acc + N·C` before the cursor advance
  - *read-and-sum* (`SwarByteSum`)
  - *born-8-wide classifier*: a source-unrolled `select_val` chain on 8 bytes,
    as in `json:string_ascii`, becomes one wide load plus a branchless test
- **Why legal:** pure work inside an effect-free window is safe to re-execute.
- **Limits:**
  - per-byte loop-carried value dependences (`Acc*10+D`) stay scalar
  - UTF-8 does not unroll, except speculating an ASCII run
- **Measured:** about 2× on top of fused bytewise scanning. 5.6× total.

### OPT1.6 Static map-shape specialization

- **What:**
  - When type inference, or the literal/record origin of a map, proves a flatmap
    with a known keys tuple, `#{k := V}` becomes a pointer compare plus a
    fixed-offset load.
  - Otherwise use a per-site **inline cache** `{keys_ptr → index}`: a hit is a
    direct load; a miss runs the generic lookup and refills the cache. This is
    OTP's track-A2 idea and needs no deopt.
  - Shape-preserving single-key updates do the same on write: copy the values,
    overwrite the slot, keep the keys pointer.
- **Measured:**
  - 1.64× on a 2-field fold; flat ~0.67 ns per access at the ceiling.
  - Wrong shape assumptions made it 1.45× *slower*. Never assume key positions
    at codegen; guard on the keys pointer.
  - The guard cost amortizes across several fields read from one map.
  - A pointer-guarded hit also removes the key comparisons (`eq`), which are 12
    % of compiler time on OTP.
- **Needs:** OPT0.4 and the shared-keys flatmap layout (moved here from OPT0.5;
  it touches every map consumer): small maps (≤ 32, OTP's `MAP_SMALL_MAP_LIMIT`)
  become `header, size, keys→tuple, v0..vn`; the current sorted-pairs cell stays
  as the **classic map** for literals. Shape-preserving updates (`M#{k := V}`,
  `maps:put` on an existing key) copy only values and keep the keys pointer,
  whose identity is the shape token (same literal, constructors and chained
  updates share it; only `from_list` and new-key puts make a fresh one).

### OPT1.7 remainder: more higher-order intrinsics

List loops of `map`, `filter`, `foreach`, `all`, `any`, `flatmap`, `foldl`,
`foldr`, `mapfoldl` and `mapfoldr` are done (see `00-finished.md`). Open:

- Other fun-taking `lists` functions (`filtermap`, `partition`, `takewhile`,
  `dropwhile`, `zipwith`, `keymap`, ...), by census of real programs.
- **`maps:fold` with a literal fun on a flatmap:**
  - turns into an indexed loop over the value slots
  - DCE removes key loads the fun does not use
  - the map/keys base untag and the index untag are hoisted (IV strength
    reduction, pointer `+= 8`)
- Generating the loops from the real `lists` source by cross-module inlining
  (OPT2.1) instead of hand-written loops.

### OPT1.8 remainder: sinking bound closures

Funs written in an inlined call are built only on fallback paths (done). Open:
`F = fun ... end` still builds its closure where it is bound even when every
read of `F` is an inlined call; sinking it needs the match's value to be unused
and every read to inline (static decisions only).

### OPT1.9 Leaf inlining scored by eliminated work

- **What:** inline small call-free local callees at most ≤24 IR ops, depth ≤3,
  and ≤5× total growth, only when a score shows something is eliminated:
  - callee entry guards subsumed by caller facts
  - constant arguments, with literal funs scoring highest
  - a construct/deconstruct pair (`{ok,X}` returned and immediately matched)
  - opening up loop recovery
- **Measured:** T2's MVP fused `diff/2` into the `total/2` loop.
- **Clause caveat:** with OPT0.1 done (OPT0.1A dropped), follow T2's rule: never
  inline for call overhead alone. LLVM's inliner can take over once calls are
  direct and services are inline.

### OPT1.10 Cleanup suite across inlined boundaries

- **Passes:** DCE (including dead phis), constant folding, copy propagation,
  CSE/GVN of pure term ops, and LICM of invariant guards, loads and pure
  computation into the preheader.
- **Immutability:** BEAM terms are immutable. Loads from term memory never alias
  a store; only GC moves them, and GC happens only at safepoints.
- **Clause:** mostly free from LLVM once OPT0.2 and OPT0.3 expose the ops. Emit:
  - `!invariant.load`, or TBAA plus `noalias`, on term-field loads
  - `memory(none/read)`, `nounwind` and `willreturn` attributes on pure services
  - GC/safepoint calls as the only clobbers
- **Related T2 fusions that come for free:**
  - `GetTupleElement` shared base: the tuple untag is CSE'd across consecutive
    reads
  - a wide `select_val` becomes a binary search or jump table: emit an LLVM
    `switch` on raw atom/small words

### OPT1.11 Float unboxing and FP-register residency

- **What:** keep float-typed values in FP registers within a function, and box
  only at escapes.
- **Measured:** float loops 2.4–3.8×. OTP's AOT already does this
  (`ssa_opt_float`), which is why T2 lists it as "compiler already does it".
- **Clause:** OPT1.2 did the main part: float literals and float-only results
  keep their double (`raw_floats`), cells only where a word is read, float
  parameters of self tail loops pass as double bits. Open: comparisons of a
  raw float with an unknown word (they make a cell), `float/1` and unary
  minus, `is_float/1` on raw floats, and floats crossing calls between
  functions (arguments, results).

### OPT1.12 Tail-modulo-cons and in-place list building

- **What:** a compiler-level transform (`tail_modulo_cons.md`, default-on in
  `lukas/erts/tmc`):
  - **Front-end 1:** `[E | self(...)]` body recursion (map, filter, append,
    comprehensions) becomes a destination-passing tail loop. A helper threads
    `Root` and `Dest`, and `set_cons_tail(Dest, New)` fills the hole.
  - **Front-end 2:** `f([H|T], Acc) -> f(T, [g(H)|Acc]); f([], Acc) ->
    lists:reverse(Acc)` builds the list forward and seals it with
    `lists:reverse(Acc, Root)`. This fires only when `Acc` is used solely as the
    prepend tail and the reverse argument.
- **GC rule:** if the hole cell is not young (`[high_water, HTOP)`), flag a full
  sweep. Otherwise, without a remembered set, the write creates an old→young
  edge.
- **Measured:**
  - O(1) stack (~5800× less peak stack at 200k elements)
  - front-end 2 builds n cells instead of 2n
  - often ~2× faster than body recursion in the 10k–1M range
  - lists past ~1M elements can regress slightly (repeated full sweeps)
- **Related:** an in-place watermark-guarded `lists:reverse` on a uniquely owned
  accumulator (`erts_internal:rev_inplace/2`).
- **Clause:**
  - Body recursion costs heap frames in Clause's stackless model, so the win is
    likely larger than on BEAM.
  - The cost is observable: shallower stacktraces and `stack_size`. Record it in
    `docs/differences.md`.

### OPT1.13 Construct/deconstruct fusion, then allocation sinking

- **What:** when a tuple or cons is built and destructured in the same region,
  do not build it. Sink an allocation that is dead on every exit path. The
  `maps:find` → `{ok,V}` → `case` fusion measured ≈7× in isolation.
- **T2 caveat:** on OTP this pool is ≈0, because `beam_ssa` already cancels
  same-function pairs and every allocation that survives escapes.
- **Clause:**
  - First check whether our frontend already cancels these pairs. It has no
    mid-end of its own.
  - Inlining (OPT1.7, OPT1.9, OPT2.1) manufactures new pairs.
  - LLVM SROA does not apply to service-allocated cells, so this needs OPT0.3
    plus a small escape check on our side.

## Tier OPT2: possible because Clause compiles whole programs

### OPT2.1 Cross-module inlining

- **What:** this is T2's "single most important" missing lever
  (`census/opt_landscape.md` P1): monomorphic call-site inlining across modules.
  - OTP can reach it only with profiling and deopt.
  - Clause sees every module of a non-upgradable executable at compile time.
- **Options:**
  - inline, or LTO-visible bodies for, `lists`/`maps`/`proplists` helpers and
    user wrappers
  - turn on `--lto` with the inline transfers (OPT0.1) to get this mostly for
    free
- **Limits:**
  - mark modules that need code upgrade as opaque
  - preserve observable stack-trace and `function_clause` semantics; record any
    divergence in `docs/differences.md`
- **Unlocks OPT0.5B:** in-place updates of maps that arrive from callers
  (`maps:put/3`, accumulator parameters) need interprocedural uniqueness or this
  inlining.

### OPT2.2 Optional: profile-guided specialization through LLVM PGO

- **What:** T2's runtime-profile items, with a static substitute where possible:
  - map-shape monomorphism
  - entry type classes
  - branch weights for block layout
  - monomorphic fun targets
- **Clause:** without a runtime profile, use type inference first, and LLVM
  instrumentation PGO (`-fprofile-generate/use`) for branch weights. This is low
  priority: T2 measured entry type-class speculation as cost-only (827 guards,
  none of which eliminated anything).

## Measured to have no value (do not build unless new evidence appears)

| Idea | T2 finding | Clause note |
|---|---|---|
| Cold-arm pruning / branch-counter dispatch specialization | 1.01–1.02×; a 12-clause dispatch costs ~4 ns in T1 | Use branch weights for layout only |
| Inlining for call overhead alone | 0 ± 1 % | Holds now that OPT0.1 is done |
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

## Suggested order for Clause

1. **Substrate (tier OPT0):** done (open follow-ups above); it made services
   visible to LLVM and cut the 100–600× kernels.
2. **Loops:** OPT1.1 and OPT1.2 done; OPT1.3 next.
3. **Binaries:** OPT1.5 (OPT1.4 done).
4. **Maps:** OPT1.6, then the `maps:fold` part of OPT1.7.
5. **Lists:** OPT1.12, then the OPT1.7 remainder together with OPT2.1.
6. **Funs and floats:** OPT1.11, the OPT1.8 remainder.
7. **Inlining and cleanup:** OPT1.9, OPT1.10 and OPT1.13, mostly through LLVM
   once the IR is transparent.
8. **Outside 69A:** done (OPT3.12-3.14, 2026-10-17 review); next by 69A impact:
   OPT3.5, a term_to_binary profile.

**Gate for every item:**

- Run a step-69A benchmark kernel before and after the change.
- Run the differential correctness check against OTP golden output.
- Measure a negative control: a branchy workload must not regress.
