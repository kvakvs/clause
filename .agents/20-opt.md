# Optimization Candidates from the OTP T2-Full JIT Work

This file lists optimizations taken from Lukas Larsson's (garazdawi) **T2-Full**
optimizing-tier work for BeamAsm. Each one is restated for Clause's ahead-of-time
LLVM code generation. The plans are planning input only. This file does not
copy any OTP source.

## Working rules for plan 20 steps

- Commit each step as soon as it is done (gate green). Don't batch several steps
  into one commit (user direction 2026-10-12).
- Prefix the commit subject with `[plan20]`, like plan 11's `[plan11]`, then
  the step code and title. Example:
  `[plan20] OPT0.1. Inline frame transfers`.
- The plan 11 gate applies:
  - fast CTest
  - `check-quality` with clang-tidy and Lizard, no suppressions
  - clang-format
  - docs updated (`docs/`, `.agents/arch.md`, `.agents/files.md`,
    `docs/differences.md` for observable differences)
- Mark the step done here, with its date, in the same commit.
- Completed steps should have their instructions compacted and focus on the 
  "what was done in this task" part

## Sources

- PR: <https://github.com/garazdawi/otp/pull/855>, "Implement T2 JIT". It is
  open, branch `lukas/erts/beamjit2`, with 293 commits. Its head is `d80e30e29d`
  (2026-07-18).
- Planning commit named by the user: `a7fafb9567` (2026-07-05, "G-M0 resolved").
  The PR head is newer and holds the complete plan, so this file is based on the
  head:
  - `PLAN/T2FULL/00`–`22` plus `census/*`
  - `PLAN/T2/*`, `PLAN/verification/*`
  - `erts/emulator/internal_doc/BeamAsmTier2.md`, the final architecture guide
- The PR text also cites `24_deepening_campaign.md` and `25_tmc_technique_a.md`.
  Neither file is reachable on any fetched ref. The TMC design is available as
  `lib/compiler/internal_doc/tail_modulo_cons.md` on branch `lukas/erts/tmc`.
- Local refs, fetched only and never checked out, so the `maint-29` pin is
  unchanged. Read them with `git -C references/otp show <ref>:<path>`:
  - `refs/remotes/garazdawi/pr855`
  - `refs/remotes/garazdawi/tmc`

## How to read T2 evidence for Clause

1. **Every T2 speedup is measured against BeamAsm T1.** T1 already does the
   following:
   - inline guard BIFs, tag tests and small-int arithmetic
   - inline bump allocation with one `test_heap` per block
   - native direct calls (roughly 20 predicted cycles)
   - register-cached X registers
   - flatmaps that share a keys tuple
   - writable/appendable binaries
   - a literal pool

   Clause has almost none of these yet. Step 69A measured Clause O2 at
   **20.2× slower than OTP JIT** (geometric mean). The worst cases:

   | Kernel | Slowdown vs OTP JIT |
   | --- | --- |
   | `map_put` | 618× |
   | `binary_match` | 492× |
   | `binary_append` | 462× |
   | `map_update` | 418× |
   | `sort` | 126× |
   | `list_to_tuple` | 125× |
   | `comprehension` | 115× |

   The substrate items in tier OPT0 below are therefore prerequisites. T2 treats them
   as given and does not measure them.
2. **Null results depend on the baseline and do not transfer automatically.**
   Example: T2 found that inlining for call overhead alone gives 0 ± 1 %
   (experiment G3-2). That holds only because a T1 call is about 20 cycles. In
   Clause, every Erlang call goes through `CLAUSE_enter_v1` and a trampoline, so
   call overhead is a real cost until direct calls exist (item OPT0.1).
3. **T2's deopt, profiling and tier-up machinery does not apply.** Clause has no
   T1 to fall back to. The usable content is:
   - the *transforms*
   - the *IR shapes* that make them legal, such as cursor IVs and raw values that
     stay out of GC slots
   - the *measurements* of where the wins are
   - the *correctness invariants*

   Where a T2 item depends on a runtime profile, a static fact from Clause's type
   inference, or optionally LLVM PGO, can take its place.
4. **Clause is allowed to do something OTP structurally cannot.** OTP's static
   compiler never inlines across modules because of hot code loading. Clause
   builds whole executables, and dynamic upgrade is optional (see AGENTS.md
   "End Goal"). Cross-module inlining of `lists`, `maps` and user helpers is
   therefore open to Clause without guards, as long as the module is not marked
   upgradable.
5. **Methodology rules from the T2 record.** These were learned the hard way and
   are listed in `08_v1_loop_tier.md` §2.3 and `16_retrospective.md` §3–4:
   - Size a pool with cycle samples (perf/ETW), not call counts. `call_time`
     tracing over-weighted `lists:foldl/3` about 10×.
   - Eligible does not mean a win. Make each optimization prove that it removes
     work, as T2's install gate does. Every gate needs a negative control.
   - Run a cheap static census of applicability before building machinery.
   - Run a differential correctness harness: Clause against OTP golden output,
     like T2's `t2hammer` and `t2_parity_SUITE`. Check identical results, error
     terms and output ordering.

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

On OTP, about ⅓ of real time is JIT'd Erlang. The rest is C BIFs and GC:
`erts_maps_put`, `eq`, `erts_cmp_compound`, `do_minor` (see `22_where_time…`).
Clause's runtime services are the equivalent pool. Making them fast is itself an
optimization target, not only a matter of correctness.

---

## Tier OPT0: substrate prerequisites (T1-level, assumed by every T2 result)

These close the gap to BeamAsm. They come first because every later tier needs
LLVM to *see* the operations instead of opaque service calls.

### OPT0.1 Direct native calls for local and known remote targets

- **Now:** every call runs `CLAUSE_enter_v1` or `CLAUSE_tail_v1`, a `musttail`
  trampoline, an argument memcpy into X registers, and a `CallFailed` check.
- **Target:** direct `call`/`musttail` to known callees, with arguments in
  registers. Reductions and the safepoint stay in the callee prologue.
- **Effect:** removes the per-call tax. This is the precondition for LLVM's own
  inliner and for items OPT1.2 and OPT1.5.
- **Status: done 2026-10-12** (ABI revision 9), as inline transfers inside the
  explicit-frame model rather than native calls:
  - Every context starts with `abi::v1::FrameState`: stack, top, limit, frame,
    reductions, registers, live, slow bits and `failed`.
  - Body prologues read the frame and registers from it, with no service calls.
  - Enter, tail and return push, pop and spend reductions inline. They fall back
    to `CLAUSE_enter/tail/return_v1` only on a wanted collection, profiling, no
    reductions left, stack growth or limit, or a builtin.
  - The per-call `CLAUSE_call_failed_v2` became a load of `failed`.
  - Still open:
    - Arguments still travel through the memory X-register array (OPT0.1A).
    - Comprehension loop-head safepoints still call the service
      unconditionally; they could test `slow_collect` inline first.
    - Benchmarks were not rerun; that is manual (`make-benchmark.bat`).

### OPT0.1A Register-passed arguments and results

User question (2026-10-12): "X registers are a memory array; can LLVM keep them
in machine registers and spill the extras automatically?"

- **Now:**
  - X registers are `std::array<Word, 256>` in `ProcessStack`.
  - A call stores its arguments there, the entry copies them into frame slots,
    and the body reloads them from the slots.
  - The result travels back through `x[0]` in memory.
  - So every argument takes three memory trips, even for a leaf that never
    needs a frame.
- **Target:** one uniform body signature
  `void body(ptr ctx, word x0, …, word x(K-1))`.
  - The first K X registers are LLVM parameters; any further ones stay in the
    memory array.
  - Every transfer is `musttail` with identical prototypes, so the existing
    guaranteed-tail-call model is unchanged.
  - A return passes the result as `x0` to the caller's body.
  - The calling convention should make K large and leave no callee-saved
    registers:
    - `preserve_nonecc` (LLVM 19+: x86-64, AArch64), designed for musttail
      interpreter dispatch
    - or `ghccc` (x86-32/64, AArch64)
    - otherwise the C convention with K = the target's integer argument
      registers
  - Pick K per target. K = 0 keeps today's scheme for targets where the
    convention or `musttail` is refused. Prove each required target with a
    prototype like the step-17 `musttail` table in `execution-model.md`.
- **What LLVM then does for free:**
  - Arguments stay in SSA values. `lower_frames` already spills exactly the
    values that cross a call or safepoint, so an argument gets a frame slot only
    if it lives across one.
  - Frames shrink, the entry memcpy disappears, and register allocation and
    spilling are LLVM's.
- **Runtime boundary:** the runtime calls code as C `void(ctx)` (start/resume,
  `invoke`), and generated code tail-calls runtime code (`pause`, `finish`, the
  services' returned bodies).
  - The compiler emits small adapters with the uniform signature into the
    startup object:
    - an entry adapter `void(ctx)` that loads `x0..x(K-1)` from the array and
      tail-calls a body
    - `pause`/`finish` stubs that store the parameters back to the array and
      return to the runtime
  - `FrameDescriptor::body` stays the C-callable code, or gains a second pointer.
  - Clang's `preserve_none` attribute could also serve, but an MSVC-built
    runtime cannot declare it.
- **GC and services:** register parameters are not roots.
  - Every slow path (`CLAUSE_enter_v1` and the others, entry safepoints,
    suspension) first stores `x0..x(K-1)` to the array, which is what the
    services already read and the collector already roots via `live`.
  - It reloads them after the service before the `musttail`.
  - Inline fast paths never touch the array.
- **Follow-on it enables:** frameless leaves. A function with no non-tail call
  and no value live across a safepoint skips its push entirely.
  `execution-model.md` already allows this; it needs the reduction check and
  entry safepoint kept on the path. Together they give near-native leaf calls.
- **Status: done 2026-10-13** (ABI revision 10):
  - Kept the C calling convention with K = 3 on every target, which fits the
    Win64 and armv7 argument registers. The runtime can then call and define
    code directly, so no adapters are needed: `ProcessStack::run_code`, and
    `finish` stores `x0`.
  - `musttail` with the new prototype passes on all cross targets (i686 and
    armv7 included) at O0/O2/Os.
  - Direct calls pass the values the native form stored into the argument
    array; those stores are removed. Fun calls load their parameters from the
    registers.
  - Bodies read leading arguments from `freeze`d parameters, spilled to their
    slot only when live across a call. Call results come from `x0`.
  - A specialization dispatcher stores its parameters into its slots first,
    because it forwards the slots.
  - With one shared prototype, LLVM turns self tail calls with an inline push
    into loops at O2 (partly OPT1.1).
  - Not done:
    - frameless leaves
    - a larger K on SysV and AArch64 (per-target K)
    - `preserve_none`
  - Benchmarks were not rerun (manual).
- **Gate:**
  - every target's `musttail` with the chosen convention at O0/O2/Os
    (cross-target test)
  - full suite and goldens unchanged
  - deep-recursion, yield and GC-stress tests
  - step-69A call-heavy kernels compared manually

### OPT0.2 Guard BIFs and term ops as inline primitives

T2 calls this "DOMJIT-style" (`T2/04_optimization.md` §10.7). Lower these as
inline IR with pure/`readnone` semantics so LLVM can CSE, DCE and hoist them:

- **Type tests:** all `is_*` predicates.
- **Access:** `element/2`, `tuple_size/1`, `hd/1`, `tl/1`, `map_size/1`,
  `byte_size/1`, `bit_size/1`.
- **Comparisons:** `=:=`, `<`, and so on on immediates.
- **Arithmetic:** `band`/`bor`/`bsl`/`bsr`, `div`/`rem` on smalls.
- **Process info:** `self/0`, `node/0`.

Today all of these go through `CLAUSE_immediate_v1`, `CLAUSE_exact_v1` or
`CLAUSE_map_v1` services.

- **Invariant:** a split fast/slow lowering (`length/1`, bignum comparison) is
  atomic. No pass may delete the slow tail because it believes the fast path
  always succeeds.

### OPT0.3 Inline bump allocation with one heap check per block

- Emit `HTOP += n` inline, with a single reservation per basic block or
  straight-line region, the equivalent of `test_heap`. Today every
  tuple/cons/float/map construction is its own service call.
- T2 found that **allocation is not an effect**. A partial iteration that is
  abandoned leaves only garbage. Allocation can therefore move freely inside a
  re-execution window.

### OPT0.4 Literal pool

- Build compound literals (strings, tuples, maps, floats) once per module, in
  constant or literal-area memory. Today a float literal calls `CLAUSE_float_v1`
  on every evaluation, and tuple literals are rebuilt each time.
- This is also the precondition for OPT0.5: map literals then share one keys tuple.

### OPT0.5 Flatmap layout with a shared keys tuple

- **Scope:** small maps only, up to a size threshold (OTP's
  `MAP_SMALL_MAP_LIMIT` is 32). Larger maps move to OPT0.5A.
- **Naming:** the current sorted-pairs `map` cell becomes the **classic map**.
  Classic maps stay the representation of small maps and literals.
- **Layout:** `header, size, keys→tuple, v0..vn`. This replaces the
  sorted-pairs-array layout.
- **Shape-preserving updates** (`M#{k := V}`, `maps:update`, `maps:put` on an
  existing key) copy only the values and keep the keys pointer.
- **Shape identity:** keys-pointer identity becomes a cheap shape token. T2
  measured that the same literal anywhere in a module, constructor functions and
  chained updates all share the pointer. Only `from_list` and new-key puts
  produce a fresh one.
- **Fixes:** the host `std::vector` staging on every put. A put stays O(n),
  which is fine at n ≤ 32.
- **Does not fix:** the quadratic growth of building or updating a large map
  (the 618× `map_put` kernel builds 2000 keys). That is OPT0.5A.

### OPT0.5A Off-heap large maps, updated in place while unshared

User idea (2026-10-12). Store a large map the way a large binary is stored: a
small stub cell on the owner's heap points to a faster structure allocated off
the heap.

- **Why:**
  - A classic map put copies all n entries, so building a map key by key is
    O(n²). `map_put` is 618× and `map_update` 418× slower than OTP.
  - A copying GC moves a large classic map on every collection. An off-heap
    body stays where it is; only the stub moves.
  - The cost is memory allocated differently: off-heap nodes, charged to the
    process like off-heap binaries.
- **Representations:**
  - **classic map:** the current cell, plus OPT0.5's shared keys tuple. Used for
    small maps and literals.
  - **off-heap map:** used above the threshold.
  - Puts and removes that cross the threshold convert between the two. Add
    hysteresis so a map near the limit does not convert back and forth.
- **Stub cell:** on the process heap. It holds:
  - a pointer to the off-heap body
  - the size
  - an off-heap list link, like `refc_binary`
- **Body:** a persistent **ordered** tree, for example a B+tree with fanout
  around 16–32.
  - Ordered, not a HAMT. Clause's documented contract is canonical key order for
    iteration, printing and `term_to_binary` (see `docs/differences.md`, Maps
    rows), and an ordered tree keeps those rows unchanged.
  - Lookup is O(log n). An update that copies its path is O(log n) and shares
    every other node with the old version.
  - Nodes are reference-counted, so several versions held by the same process
    share structure.
- **Keys and values stay on the process heap.** This is the difference from
  binaries:
  - The tree's leaves hold term words that point into the owner's heap.
  - The GC treats every live stub's tree as a root set. It scans each node's
    term slots once per collection (use an epoch mark, because versions share
    nodes) and rewrites moved pointers in place.
  - This is legal because the body is process-private. It is never shared
    between processes.
  - Deep-copying keys and values off the heap (ETS-style) is rejected: each put
    would cost O(size of the value), and read values would need lifetimes of
    their own.
- **Freeing:**
  - The off-heap list sweep releases the root reference of each dead stub, which
    frees unreachable nodes.
  - Path-copy garbage is charged as off-heap words, the virtual-heap analog, so
    a heavy update loop triggers a collection that frees old versions.
- **In-place update while unshared** (user refinement):
  - If no other live value can observe the old version, update the body in
    place. There is no path copy and no garbage, so a put is O(log n) with no
    allocation.
  - "Unshared" means exactly one stub references the body *and* that stub's old
    value is dead after the update.
  - A reference count of 1 is necessary but not enough. In
    `M2 = M#{k => v}, {M, M2}` the single stub is still read as `M`, so
    immutability requires a copy there.
  - Two sources of the proof:
    1. **Compiler uniqueness and liveness:** the old map variable dies at the
       update and never escaped into another term, the process dictionary, a
       closure or a message. This is the same analysis as OPT0.6
       `private_append` and OTP's `beam_ssa_alias`. It covers the common
       builders: a tail-recursive accumulator, a fold fun's `Acc`, map
       comprehensions, chains of `#{... => ...}`.
       - The update op then carries a "may mutate" flag.
       - At run time the op still checks that the body's reference count is 1,
         and copies its path otherwise. This guards against sharing between
         versions.
    2. **Runtime-owned fresh bodies:** `maps:from_list/1`, `maps:map/2`,
       `maps:merge/2`, `maps:filter/2` and map construction mutate a body that
       no term references yet, then publish the stub once.
  - **Escapes copy:** send, ETS and `term_to_binary` copy the contents, so a
    receiver gets its own body. Sending therefore never creates sharing between
    processes. A dead stub left in a frame root slot is harmless, because no
    Erlang code reads it.
- **Semantics must not change:**
  - Classic and off-heap maps with equal contents are indistinguishable under
    `=:=`, `==`, term order, `phash2`, `erts_debug:flat_size` (if provided),
    printing and the external term format.
  - Every service walks in key order through one iterator interface.
- **GC caveat:** in-place writes are safe while the collector is
  non-generational, which is today's state (`runtime-heap.md`: "Old heap.
  None"). A future generational GC needs a remembered set for nodes written
  after tenuring. OTP's TMC branch solved the same problem with a forced full
  sweep.
- **Observable differences:** memory reporting such as
  `process_info(_, memory)` and heap sizes will differ from OTP. Add a row to
  `docs/differences.md` when this lands.
- **Docs to update:**
  - `docs/runtime-heap.md`: the cell table and off-heap sections
  - `docs/terms.md`: Maps
  - `.agents/arch.md`
- **Status: done 2026-10-12** (runtime only, no ABI change):
  - `BoxedKind::tree_map` stubs and `terms/map_tree` (B+ tree, fanout 32,
    subtree sizes, refcounted nodes).
  - Updates copy their path, so building a map is O(n log n). Nodes created by
    the running update change in place under its edit token.
  - Runtime-built maps (construction, `from_list`, comprehensions, decoding) are
    bulk-built in O(n).
  - GC rewrites the tree words through the stubs; the sweep frees dead trees.
  - Copies between heaps rebuild the tree in the destination.
  - Tests: `executables_large_maps` (OTP golden) and `runtime_maps` `tree_maps`
    (sharing, rollback, freeing).
  - Benchmarks are manual and not rerun.
  - In-place update of an already published map, proven unshared by the
    compiler, is split out as OPT0.5B.
- **Gate:**
  - `map_put` and `map_update` become O(n log n) (step-69A kernels).
  - Differential checks against OTP on order, printing, `term_to_binary` and
    errors.
  - GC stress with large maps alive across many collections.
  - Aliasing tests where the old version stays live (it must not be mutated).

### OPT0.5B Compiler-proven in-place map updates

Split from OPT0.5A.

- **What:** let `CLAUSE_map_v1` update change a *published* off-heap map in
  place, with no path copy and no garbage, when the compiler proves its old value
  dead and unshared.
- **Conditions:**
  - The old map variable dies at the update.
  - It never escaped into another term, the process dictionary, a closure or a
    message.
  - At run time, every node on the path still has one reference.
- **Mechanism:**
  - A "may mutate" flag on the update op.
  - The runtime then hands the stub's root a fresh edit token instead of
    copying.
  - This shares the uniqueness analysis with OPT0.6 `private_append`.
- **Limits:** most hot builders pass the map through a library call
  (`maps:put/3`), so the proof needs inlining (OPT2.1) or a summary of
  library functions to fire often.
- **Gate:** aliasing tests where the old version stays live and must not
  change, plus step-69A `map_put`/`map_update` compared manually.

### OPT0.6 Writable/appendable binaries

- OTP's `private_append` and `beam_ssa_private_append`. `<<Acc/binary, X>>` on a
  uniquely owned accumulator must append in place, not copy.
  `BitWriter::append` copies every segment today (`binary_append` is 462× slower).

### OPT0.7 Reduction accounting at loop back-edges

- Comprehension heads call only `CLAUSE_safepoint_v1` and charge nothing, which
  is a fairness gap.
- T2 practice:
  - charge per back-edge
  - or batch per unrolled chunk (`subs FCALLS, K` is observably equivalent to K
    single decrements)
  - yield only at chunk granularity

---

## Tier OPT1: static compiler optimizations (directly applicable, no profile)

Ordered by measured T2 value and how well each fits an AOT compiler.

### OPT1.1 Loop recovery for self- and mutual tail recursion

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
  - Raw values must never occupy a GC-scanned slot: retagging to spill them gives
    back the win (`14_bs_cursor_iv.md` §6b).
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

This is the largest measured T2 win class (`14_bs_cursor_iv.md`).

- **Context:** keep a match context as loop-invariant `base` and `limit` plus a
  raw **bit cursor** SSA value.
- **Ops:**

  | Op | Role |
  | --- | --- |
  | `BsEnsure(cursor, limit, need)` | Bounds check, separate from extraction, so it can be hoisted and widened |
  | `BsRead(base, cursor, size)` | Pure, non-allocating extraction of an integer up to a word, or a skip |
  | `BsSync` | Writes the cursor back to the boxed context only at sync points or exits |

- **Positions:** `bs_get_position` and `bs_set_position` become plain SSA values.
  Backtracking becomes a phi instead of heap traffic.
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
  - When type inference, or the literal/record origin of a map, proves a
    flatmap with a known keys tuple, `#{k := V}` becomes a pointer compare plus
    a fixed-offset load.
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
  - A pointer-guarded hit also removes the key comparisons (`eq`), which are
    12 % of compiler time on OTP.
- **Needs:** OPT0.4 and OPT0.5.

### OPT1.7 Higher-order `lists` intrinsics and literal-fun inlining

- **What:** for a call to `lists:foldl/3`, `foreach/2`, `all/2`, `any/2`
  (tail-recursive) or `map/2`, `filter/2`, `foldr/3`, `flatmap/2`,
  `mapfoldl/3`, `mapfoldr/3` (body-recursive) with a **literal fun**, expand the
  helper inline and splice in the fun body. The result is a flat loop
  (OPT1.1–OPT1.3 then apply).
- **Source of the set:** OTP's `sys_core_fold_lists.erl` set, driven by the
  `inline_list_funcs` option, which Clause currently accepts and ignores.
- **Clause:** without hot code loading, the expansion can be generated from the
  real `lists` source by cross-module inlining (OPT2.1) instead of from hand-ported
  templates.
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
- **Clause:** today `CLAUSE_apply_v1` plus a frame entry runs for every fun call.

### OPT1.9 Leaf inlining scored by eliminated work

- **What:** inline small call-free local callees at most ≤24 IR ops, depth ≤3,
  and ≤5× total growth, only when a score shows something is eliminated:
  - callee entry guards subsumed by caller facts
  - constant arguments, with literal funs scoring highest
  - a construct/deconstruct pair (`{ok,X}` returned and immediately matched)
  - opening up loop recovery
- **Measured:** T2's MVP fused `diff/2` into the `total/2` loop.
- **Clause caveat:** until OPT0.1 lands, plain call overhead is also worth removing.
  After OPT0.1, follow T2's rule: never inline for call overhead alone. LLVM's
  inliner can take over once calls are direct and services are inline.

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
- **Clause:** floats are boxed, and every float operation is a service call. Port
  the `ssa_opt_float` idea, a float block between `fclearerror` and
  `fcheckerror`, on top of OPT0.3 and OPT0.4.

### OPT1.12 Tail-modulo-cons and in-place list building

- **What:** a compiler-level transform (`tail_modulo_cons.md`, default-on in
  `lukas/erts/tmc`):
  - **Front-end 1:** `[E | self(...)]` body recursion (map, filter, append,
    comprehensions) becomes a destination-passing tail loop. A helper threads
    `Root` and `Dest`, and `set_cons_tail(Dest, New)` fills the hole.
  - **Front-end 2:** `f([H|T], Acc) -> f(T, [g(H)|Acc]); f([], Acc) ->
    lists:reverse(Acc)` builds the list forward and seals it with
    `lists:reverse(Acc, Root)`. This fires only when `Acc` is used solely as
    the prepend tail and the reverse argument.
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
  - LLVM SROA does not apply to service-allocated cells, so this needs OPT0.3 plus
    a small escape check on our side.

---

## Tier OPT2: possible because Clause compiles whole programs

### OPT2.1 Cross-module inlining

- **What:** this is T2's "single most important" missing lever
  (`census/opt_landscape.md` P1): monomorphic call-site inlining across modules.
  - OTP can reach it only with profiling and deopt.
  - Clause sees every module of a non-upgradable executable at compile time.
- **Options:**
  - inline, or LTO-visible bodies for, `lists`/`maps`/`proplists` helpers and
    user wrappers
  - turn on `--lto` with direct calls (OPT0.1) to get this mostly for free
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

---

## Measured to have no value (do not build unless new evidence appears)

| Idea | T2 finding | Clause note |
|---|---|---|
| Cold-arm pruning / branch-counter dispatch specialization | 1.01–1.02×; a 12-clause dispatch costs ~4 ns in T1 | Use branch weights for layout only |
| Inlining for call overhead alone | 0 ± 1 % | Holds only after OPT0.1 |
| Polymorphic inline caches wider than 2 | rarely pays; compiler sites see 20–130+ shapes | Monomorphic map cache only |
| General interning of map shapes | measured unnecessary: literal shapes already share the pointer | — |
| Escape analysis at scale / PEA | survivors escape; top allocators are inside C BIFs | Revisit only after OPT2.1 creates pairs |
| Induction-variable strength reduction (beyond binaries and the `maps:fold` pointer walk) | cons walking has no IV | — |
| Auto-vectorization beyond SWAR recipes | no measured pool | — |
| Unrolling body-recursion loops | flat; serial `get_tl` chain dominates | — |
| Reimplementing map/binary BIFs in Erlang to inline them | moves the cost, does not remove it | Make the C++ services fast instead |
| Software prefetch, "home traffic" elimination, GcTest coalescing across unrolled iterations, broad register-pool widening | measured and dropped (PR text; doc 24 unavailable) | Recheck before attempting |
| Straight-line GVN outside loops | pool ≈0 on real code | Leave to LLVM |

---

## Suggested order for Clause

1. **Substrate (tier OPT0):** OPT0.1 (done), OPT0.1A (done), OPT0.2, OPT0.3, then
   OPT0.4, OPT0.5, OPT0.5A (done) and OPT0.5B, then OPT0.6, then OPT0.7. OPT0.5A has no dependency on the others
   and can go first if the map kernels are the priority. This attacks the measured 100–600× kernels and makes
   everything below visible to LLVM.
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
