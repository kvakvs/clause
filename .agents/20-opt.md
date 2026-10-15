# Optimization Candidates from the OTP T2-Full JIT Work

This file lists optimizations taken from Lukas Larsson's (garazdawi) **T2-Full**
optimizing-tier work for BeamAsm, restated for Clause's ahead-of-time LLVM code
generation. The plans are planning input only; no OTP source is copied.

Completed steps (all of tier OPT0 plus OPT1.1A, part of OPT1.2, OPT1.4A and
OPT1.4B): see [00-finished.md](00-finished.md). This file keeps open work only.

## Working rules for plan 20 steps

- Commit each step as soon as it is done (gate green); don't batch steps
  (user direction 2026-10-12).
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
   `lists`, `maps` and user helpers is open unless a module is marked upgradable.
5. **Methodology from the T2 record** (`08_v1_loop_tier.md` §2.3,
   `16_retrospective.md` §3–4):
   - Size a pool with cycle samples (perf/ETW), not call counts.
   - Eligible does not mean a win: each optimization must prove it removes work,
     with a negative control.
   - Run a cheap static census of applicability before building machinery.
   - Run a differential harness against OTP golden output (results, error
     terms, output ordering).

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

## Open follow-ups of finished tier OPT0 steps

- **OPT0.1A frameless leaves** (no push for a function with no non-tail call and
  nothing live across a safepoint): **deferred 2026-10-15.** A frameless leaf has
  no stack-trace entry, so `erlang:raise`/error traces and
  `process_info(current_function)` would differ from OTP, and the saved push is
  a few stores. Revisit only with a profile where leaf entry dominates.
- **OPT0.2 invariant:** a split fast/slow lowering (`length/1`, bignum
  comparison) is atomic; no pass may delete the slow tail. Atom ordering still
  calls the service.
- **OPT0.3 / 0.3A:** one heap check per basic block (low value, measured
  negligible) and inline floats, which come with float unboxing (OPT1.11).
- **OPT0.4 caveat:** the in-place map proof (`semantic/map_ownership`) treats
  every map expression as freshly built; constant maps moved into the literal
  pool must be excluded. Maps and binaries stay with their services.
- **OPT0.5 shared-keys flatmap layout** (deferred to OPT1.6, where its shape
  token is used; it touches every map consumer): small maps (≤ 32, OTP's
  `MAP_SMALL_MAP_LIMIT`) become `header, size, keys→tuple, v0..vn`; the current
  sorted-pairs cell stays as the **classic map** for literals. Shape-preserving
  updates (`M#{k := V}`, `maps:put` on an existing key) copy only values and
  keep the keys pointer, whose identity is the shape token (same literal,
  constructors and chained updates share it; only `from_list` and new-key puts
  make a fresh one).
- **OPT0.5B:** in-place updates of maps that arrive from callers (`maps:put/3`,
  accumulator parameters) need interprocedural uniqueness or inlining (OPT2.1).
- **OPT0.12:** the remaining ~200 ns of a list-building builtin (typed argument
  conversion, `Term` copies with their weak pointer, per-element admission in
  `list_words`).
- **OPT0.17:** closure sinking and closure inlining remain in OPT1.8.
- **OPT0.20:** generational collection, so a large live heap is not copied by
  every collection (~15% of luerl euler).
- **OPT0.24 / OPT0.25:** cheaper process creation and teardown; queues still
  share one executor mutex, so a scheduler without one global lock.

## Tier OPT1: static compiler optimizations (directly applicable, no profile)

Ordered by measured T2 value and how well each fits an AOT compiler.

### OPT1.1 Loop recovery for self- and mutual tail recursion

Status 2026-10-15: self tail calls already become LLVM loops and, since
OPT1.1A, keep their frame instead of re-pushing it (done). Mutual recursion
remains.

- **What:** rewrite a self tail call into a branch back to a synthesized loop
  header, with phis for the parameters. Mutually tail-recursive SCCs (Tarjan)
  become one loop with a dispatch. Loop structure stays side data (`LoopInfo`).
  LLVM then sees real loops for LICM, unrolling and the vectorizer.
- **Measured:** this is the core of the MVP's 1.97×. A tail call to an internal
  `loop_top` instead of the public entry added another ~6 %, because it skips
  the prologue and yield check.
- **Clause:**
  - Today a self tail call is `CLAUSE_tail_v1`, a full trampoline round trip.
  - Keep a reduction check plus safepoint on the back-edge (OPT0.7).
  - Collection happens only at the safepoint.

### OPT1.2 Keep loop-carried values in registers, untagged where proven

Part done 2026-10-15: no eager root-slot stores. Untagged raw loop values open.

- **What:** T2 calls this "regalloc R3 register homing" and "RAW-IN-HOME":
  - keep the accumulator, induction variable and list cursor as raw machine
    words in SSA registers across the back-edge
  - tag only at exits and safepoints
- **Measured:** 1.3–3.8× on integer/float tail loops. Pressured loops went from
  1.6× slower to 1.6–1.8× faster.
- **Clause:**
  - Today every value crossing a call or safepoint is demoted to a frame root
    slot (`DemoteRegToStack`), and each service result is stored to a root slot.
  - Only values that are live *at a safepoint* need a root slot.
  - Raw values must never occupy a GC-scanned slot: retagging to spill them
    gives back the win (`14_bs_cursor_iv.md` §6b).
  - Item OPT1.1 must come first.

### OPT1.3 Speculation-free small-int fast paths, with guard fusion and range-proven overflow elimination

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

### OPT1.4 Binary matching as a cursor induction variable

OPT1.4A and OPT1.4B done 2026-10-15 (inline start/finish, aligned whole-byte
reads, sub-binary cells, inline `Rest`). Open: **OPT1.4C** the raw cursor across iterations (no `Rest` cell per loop
trip), unaligned and non-byte segments inline.

This is the largest measured T2 win class (`14_bs_cursor_iv.md`).

- **Context:** keep a match context as loop-invariant `base` and `limit` plus a
  raw **bit cursor** SSA value.
- **Ops:**

  | Op | Role |
  | --- | --- |
  | `BsEnsure(cursor, limit, need)` | Bounds check, separate from extraction, so it can be hoisted and widened |
  | `BsRead(base, cursor, size)` | Pure, non-allocating extraction of an integer up to a word, or a skip |
  | `BsSync` | Writes the cursor back to the boxed context only at sync points or exits |

- **Positions:** `bs_get_position` and `bs_set_position` become plain SSA
  values. Backtracking becomes a phi instead of heap traffic.
- **GC:** `base` is a raw pointer that GC can move, so reload it after any
  allocating op.
- **Clause:** today every segment calls `CLAUSE_bits_v1` (test/extract/finish),
  and the cursor is a tagged small in a root slot (`binary_match` is 492×
  slower). Inline `BsEnsure`/`BsRead` LLVM IR is enough for LLVM's
  LICM/GVN/unroll to start working.

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
- **Needs:** OPT0.4 and OPT0.5.

### OPT1.7 Higher-order `lists` intrinsics and literal-fun inlining

- **What:** for a call to `lists:foldl/3`, `foreach/2`, `all/2`, `any/2`
  (tail-recursive) or `map/2`, `filter/2`, `foldr/3`, `flatmap/2`, `mapfoldl/3`,
  `mapfoldr/3` (body-recursive) with a **literal fun**, expand the helper inline
  and splice in the fun body. The result is a flat loop (OPT1.1–OPT1.3 then
  apply).
- **Source of the set:** OTP's `sys_core_fold_lists.erl` set, driven by the
  `inline_list_funcs` option, which Clause currently accepts and ignores.
- **Clause:** without hot code loading, the expansion can be generated from the
  real `lists` source by cross-module inlining (OPT2.1) instead of from
  hand-ported templates.
- **Related item, `maps:fold` with a literal fun on a flatmap:**
  - turns into an indexed loop over the value slots
  - DCE removes key loads the fun does not use
  - the map/keys base untag and the index untag are hoisted (IV strength
    reduction, pointer `+= 8`)

### OPT1.8 Fun/closure handling: `make_fun` sinking and closure inlining

- **What:**
  - Sink a closure allocation into the only path that needs it (the slow
    fallback call), so the fast path neither allocates nor needs a heap check
    for it (T2 Stage 3d).
  - Where the target of a fun call is SSA-constant, inline the fun body, append
    the free variables as arguments, and remove the closure allocation.
- **Clause:** today `CLAUSE_apply_v1` plus a frame entry runs for every fun
  call.

### OPT1.9 Leaf inlining scored by eliminated work

- **What:** inline small call-free local callees at most ≤24 IR ops, depth ≤3,
  and ≤5× total growth, only when a score shows something is eliminated:
  - callee entry guards subsumed by caller facts
  - constant arguments, with literal funs scoring highest
  - a construct/deconstruct pair (`{ok,X}` returned and immediately matched)
  - opening up loop recovery
- **Measured:** T2's MVP fused `diff/2` into the `total/2` loop.
- **Clause caveat:** with OPT0.1/OPT0.1A done, follow T2's rule: never inline
  for call overhead alone. LLVM's inliner can take over once calls are direct
  and services are inline.

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

- **What:** keep float-typed values and loop accumulators in FP registers, and
  box only at escapes.
- **Measured:** float loops 2.4–3.8×. OTP's AOT already does this
  (`ssa_opt_float`), which is why T2 lists it as "compiler already does it".
- **Clause:** floats are boxed, and every float operation is a service call.
  Port the `ssa_opt_float` idea, a float block between `fclearerror` and
  `fcheckerror`, on top of OPT0.3 and OPT0.4.

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

## Suggested order for Clause

1. **Substrate (tier OPT0):** done (open follow-ups above); it made services
   visible to LLVM and cut the 100–600× kernels.
2. **Loops:** OPT1.1, then OPT1.2, then OPT1.3.
3. **Binaries:** OPT1.4, then OPT1.5.
4. **Maps:** OPT1.6, then the `maps:fold` part of OPT1.7.
5. **Lists:** OPT1.12, then OPT1.7 together with OPT2.1.
6. **Funs and floats:** OPT1.8 and OPT1.11.
7. **Inlining and cleanup:** OPT1.9, OPT1.10 and OPT1.13, mostly through LLVM
   once the IR is transparent.

**Gate for every item:**

- Run a step-69A benchmark kernel before and after the change.
- Run the differential correctness check against OTP golden output.
- Measure a negative control: a branchy workload must not regress.
