# Plan 20 completed work (2026-10-12 to 2026-10-17)

Compact record of the runtime and code generation optimizations, one bullet per step;
[the index](00-finished.md) lists the other plans.

Optimizations from OTP T2-Full ideas; open steps are in [20-opt.md](20-opt.md). Speedups are against
the previous step unless noted; the luerl euler run went from 21 s (O2, before plan 20) to 0.59 s
(OTP 0.35 s).

- **Plan** `.agents/20-opt.md` from garazdawi/otp PR #855 plans — `4438fde`
- **OPT0.1** inline frame transfers, `FrameState` in every context (ABI 9) — `3c6cecc`
- **OPT0.1A** leading arguments and results in registers, `run_code` (ABI 10) — `11439308`
- **OPT0.1A** `preserve_none` Code on x86-64/AArch64 with Clang (ABI 16); prologue 8 pushes to 1 —
  `21ba013d`
- **OPT0.1A** six register arguments on every target (ABI 17); euler O2 4 s to 3 s — `a89ce357`
- **OPT0.5A** maps over 32 entries are off-heap B+ trees, O(log n) updates — `ed3ccc2`
- **OPT0.5B** compiler-proven in-place updates of fresh single-read maps (`semantic/map_ownership`)
  — `04043339`
- **OPT0.2A** fixed `false`/`true` atom words, inline exact equality and tuple/cons match shapes
  (ABI 11); euler O0 74 s to 12 s — `6897efd4`
- **OPT0.2B** inline type tests, comparisons, `element/2`, `hd/tl`, small `band`/`bsr`/`div`; 69A
  1.13x vs `31b20d73` — `fd5d18df`
- **OPT0.3** inline bump allocation of tuples and lists up to 64 words (ABI 12); euler O2 8 s to 6
  s, 69A 1.58x — `46642856`
- **OPT0.8** cached atom words, fixed `ok`/`error`/`undefined` (ABI 13); euler O2 6 s to 4 s —
  `1286ae77`
- **OPT0.9** word-sized integer bit segments, no multiprecision; `lists:sum/1` — `fdd8a7e3`
- **OPT0.2C** inline `setelement/3`; euler O2 problem_009 3.6 s to 2.35 s — `52ad1da4`
- **OPT0.4** per-module literal pool of read-only heap cells (ABI 14); 69A 1.05x, 9.0x behind OTP
  JIT — `ece1ee6b`
- **OPT0.6** appendable binaries, spare room 2x; 100k one-byte appends quadratic to 190 ms —
  `8bb121d5`
- **OPT0.7** reductions at comprehension loop heads, inline `slow_collect` test — `816e8150`, fix
  `8525b3e9`
- **OPT0.5** word-level small-map updates and lookups, no `Term` staging; 20-entry update 2.1x
  (shared-keys layout moved to OPT1.6) — `1cd369c7`
- **OPT0.2D** inline `is_binary/1`, `bsl`, `self/0` (ABI 15) — `4c6e9223`
- **OPT0.3A** inline `#{...}` construction with atom/small-integer literal keys — `e563d60d`
- **OPT1.2 (part)** no eager root stores; only values crossing calls get a slot; sum loop 60 to 37
  ms (OTP 26 ms) — `3f74f220`
- **OPT1.4A** inline binary match start/finish and constant whole-byte reads; 400 KB sum 1.42 to
  1.16 s — `2c04f3d5`
- **OPT1.4B** sub-binary cells, inline `Rest` (ABI 18); sum loop 1.16 s to 58 ms (OTP 2.7 ms) —
  `22e8baca`
- **OPT0.10** inline list generators, one-reservation comprehension reverse; comprehension 88 to 3.2
  ms — `cb38203c`
- **OPT0.11A** inline heap binaries for constant integer segments; `<<N:32>>` 151 to 8 ns —
  `a8c01c37`
- **OPT0.11B** inline appends, append state in the refc cell (ABI 19); append 277 to 16 ns —
  `4b755cf6`
- **OPT0.12** cheap term access in builtins; length of 10 cells 1122 to 111 ns, 69A 1.27x —
  `52925127`
- **OPT0.13** inline map generators, cheaper tree-map updates (ABI 20); 1M tree updates 1446 to 398
  ms — `08414232`
- **OPT0.14** inline container inspections, word-level float/binary ordering; euler O2 2.08 to 1.13
  s — `927416b8`
- **OPT0.15** raw dictionary builtins and inline float arithmetic; euler O2 1.13 to 0.96 s —
  `8e2b4223`
- **OPT0.16** inline math (`sqrt` intrinsic, C-library calls for the rest); euler O2 0.96 to 0.91 s
  — `3a741dfd`
- **OPT0.17** inline closures and word-level apply (ABI 21); euler O2 0.91 to 0.70 s — `6437c193`
- **OPT0.18** literal binaries up to 64 bytes, inline `make_tuple/2`; euler O2 0.70 to 0.62 s —
  `b1a038b3`
- **OPT0.19** lazy worker wakeups; ping_pong 9.4x faster (= OTP), ring 4x — `95396c04`
- **OPT0.20** amortized binary collections; large_read 5.9x, map_update 3.9x — `f6fcb95a`
- **OPT1.1A** self tail calls keep their frame; counting loop 35% faster, euler O2 0.62 to 0.59 s —
  `cb5b54fb`
- **OPT1.1B** tail calls between members of a tail-call cycle keep the caller link and shared term
  slots; even/odd 16%, euler O2 0.565 to 0.540 s — `9dfdc244`
- **OPT1.1C** each tail-call cycle is one LLVM function dispatching on the frame's descriptor;
  member tail calls are branches; even/odd 23%, 3-state 18% — `[plan20] OPT1.1C ...`
- **OPT1.2 (part 2)** arguments stored into their slots only where they cross a call or safepoint;
  self loop 8%, list sum 7% — `3d23d818`
- **OPT1.2** raw floats kept in registers and float loops taking double parameters; 3M-step float
  loop 20.8 to 6.8 ms (OTP 2.7x slower) — `[plan20] OPT1.2. Float loops ...`
- **OPT1.3 (part)** inline `+`/`-` on tagged words with a sign-test overflow check, no double width;
  hash 22%, count_up 19%, fib 15% — `93981174`
- **OPT1.3 (part 2)** `+ - *` branch on the overflow flag with no double width; mul loop 418 to 328
  ms (OTP 322), 69A 1.05x geomean — `[plan20] OPT1.3 (part 2) ...`
- **OPT1.5** byte scans: a binary loop clause with a byte-only guard skips up to 16 accepted bytes
  per step with one vector test; digits scan 23 to 5 ms (OTP 30) — `[plan20] OPT1.5 ...`
- **OPT1.14 (part)** a self tail call reached only through a non-tail call spends no reduction of
  its own; `lists:foldl` 40 to 37 ms — `[plan20] OPT1.14 (part) ...`
- **OPT1.14 (part 2)** self tail calls branch back to the prologue, the reduction count lives in a
  register, likely-branch weights on fast paths; count_up 92 to 86 ms —
  `[plan20] OPT1.14 (part 2) ...`
- **OPT1.7 (part 2)** more list loops inlined: `filtermap`, `partition`, `search`, `takewhile`,
  `dropwhile`, `splitwith`; filtermap 3.1x, all now ahead of OTP JIT —
  `[plan20] OPT1.7 (part 2) ...`
- **OPT1.7 (part 3)** `zipwith/3` and `zipwith3/4` walk their lists in step, `lists` versions follow
  OTP's walk; zipwith 7.8 to 2.5 ms (OTP 4.7) — `[plan20] OPT1.7 (part 3) ...`
- **OPT1.8 remainder** sunk bound funs: `F = fun ... end` builds no closure when its value is unused
  and only calls read it; a 4-element `foldl` 97 to 88 ms — `[plan20] OPT1.8 remainder ...`
- **OPT1.9** leaf inlining: a local call of a small function that calls no Erlang code is lowered in
  place with the caller's argument facts; `total/diff` loop 81 to 23 ms (OTP 40) —
  `[plan20] OPT1.9 ...`
- **OPT1.9 remainder + OPT1.13** nested leaves, `?MODULE:f()` sites, and construct/deconstruct
  fusion (matches read the words of tuples and cons cells built in the same function); divmod loop
  53 to 33 ms (OTP 291); OPT1.13 closed — `[plan20] OPT1.9 remainder, OPT1.13 ...`
- **OPT1.10** cleanup across inlined boundaries: invariant loads for cache claims and runtime
  identity, TBAA tags for term and frame memory; 8-atom dispatch 55 to 37 ms (OTP 106) —
  `[plan20] OPT1.10 ...`
- **OPT3.24** libraries.qlc: ms_transform's translation in C++, called in place by qlc_pt so a
  transform walks once; libraries.qlc 4.17 s to 1.61 s (OTP 1.95 s) — `[plan20] OPT3.24 ...`
- **OPT1.11** wider float loops and raw-float operations (unary minus, `float/1`, float parameters
  found by fixpoint, inline comparisons); new `benchmarks/bench_float`, limit 4.13 s to 0.44 (OTP
  1.23) — `d28eb5a7`
- **OPT1.11 remainder** floats crossing calls enter a raw-double variant of other functions at
  `-O2`; calls 1.13 s to 0.53 (OTP 1.29); closed, left: float results still return as cells —
  `[plan20] OPT1.11 remainder ...`
- **OPT1.12 part 1** forward-built lists: comprehensions and `lists` loops append each new cell to
  the last one's tail, no reverse (ABI 26); map_foldl 1109 to 607 ms (OTP 1456) —
  `[plan20] OPT1.12 part 1 ...`
- **OPT1.4C** binary loops: a `<fun>.loop` variant updates one fresh sub-binary cell in place across
  self tail calls; binary_match 8.5x — `c45402af`
- **OPT1.4C (part 2)** the variant reads its parameter's view once per step and skips its bitstring
  test; sum_binary 5.2 to 3.7 ms (OTP 2.7) — `5fa0c26c`
- **OPT1.4D** integer segments of any constant width up to the word less 8 bits read inline at any
  bit position; 1-bit loop 182 to 6.5 ms (OTP 3.2) — `8e3f483d`
- **OPT1.4E** escaping rests: binary loops whose rests are also kept, returned, sent or captured
  stay loops; JSON-like string scan 2.6 to 1.25 ms (OTP 1.2-1.6) — `428520c0`
- **OPT1.4F** SSA binary cursors: invariant view loads and a late GVN in binary loop variants;
  69A binary_match 1.64x (2.28x faster than OTP JIT) — `29c03617`
- **OPT3.10B** young heap sizing: shrinks lower the block's capacity in place instead of copying
  live words again; kernels 1.04x geomean (ERTS's sizing measured worse) — `1618e0f0`
- **OPT1.7 + OPT1.8** known anonymous funs inlined at O2, `lists` map/filter/foreach/all/any/
  flatmap/foldl/foldr/mapfoldl/mapfoldr become loops in the caller; 69A map_foldl 12.2 to 7.8 ms
  (OTP 3.4) — `6da94982`
- **OPT0.20B** generational collection: safepoints run minor collections that promote into an old
  area; 69A geomean 1.15x (comprehension 3.3x) — `4635bb64`
- **OPT0.20C** `min_heap_size` applied (`spawn_opt`, `process_flag`, `--min-heap-size` as `+hms`):
  the first block and every collection floor, so a process trades memory for fewer collections the
  OTP way — `0afd29b1`
- **OPT0.26** word-level `term_to_binary`/`binary_to_term`, one factory per decoding; encode 3,066
  to 558 us, decode 1,084 to 550 us on the 69A term — `4fe2ec7f`
- **OPT0.27** short builtin lists (`++` up to 64 cells, `integer_to_list`/`atom_to_list`) bumped
  into the heap block's free words; string_build 1.8x, int_to_list 1.6x — `a996c552`
- **OPT0.28** flat message copies as OTP (`size_object`/`copy_struct`), no address map or per-object
  records; large_message 1.5x; runtime tests get a 120 s timeout — `3a50cd77`
- **OPT0.29** message copies queue only compound slots, tested by primary tag; large_message 39 to
  ~25 ms — `ef64ba45`
- **OPT0.30** message hand-off: a message of 512 words or more is built before the executor lock
  and adopted as a fragment of the receiver, with no second copy — `97295c84`
- **OPT0.31** file reads copy once (`read_file`, `read`, `port_control` read straight into the
  reply); read_file 23.8 to 6.2 ms (OTP 4.4) — `0a8410db`
- **OPT0.32** word-level map update at a small-integer key on an off-heap map; map_update and
  map_put 13-15% — `289336a8`
- **OPT0.33** map cursors: one word-level walk over flat and tree maps, `maps:fold` gets an entries
  tuple; map_update 15% — `d7f90ad9`
- **OPT0.34** `atom_to_list` of an atom literal folded at compile time, as OTP's `sys_core_fold`;
  string_build ~25% — `54f12529`
- **OPT3.1** on-heap map trees: tree nodes are heap cells (fanout 16) that collections move and
  free like any cell; map_update 3.6x (4.2 to 1.19 ms), 69A geomean +3.6% — `bca659ab`
- **OPT3.2** inline `++` of a short proper left list and direct builtins (ABI 22, pure conversions
  with no frame transfer); `++` 145 to 13 ns (OTP 12), 69A geomean +2.8% — `91cc2e10`
- **OPT3.10** heap block reuse: released heap blocks stay with their thread for its next
  collections; 69A geomean +9.3%, 1.45x behind OTP JIT — `58d545cb`
- **OPT3.3** codec walks and binary pressure: encoder reads cells in place, decoder claims words
  once, binary pressure collects the young generation first; 69A geomean +7.2% — `708d9414`
- **OPT3.4** read-ahead lines: the file driver's read_line serves lines from a 64 KB read-ahead
  buffer; read_line 75 to 41 ms per 20,000 lines (OTP 29) — `301e1bc4`
- **OPT3.7** `lists:sort/1` as an alternating merge sort from sorted pairs (results identical to
  OTP's), plus a real `lists:merge/3`; sort 5.25 to 3.4 ms (OTP 2.1) — `d514e6f4`
- **OPT3.11** fragment windows: once the heap block is nearly full, generated code bump-allocates
  in the newest own fragment; map_foldl 1.52x — `c86bb60a`
- **OPT3.8** `list_to_integer/1` reads a short list in place; int_to_list round trip 1688 to 835 us
  (OTP 444) — `6823705f`
- **OPT3.9** message copies cache the source area of the last cell and copy cons cells without a
  parse; large_message echo 4.37 to 3.62 ms (OTP 1.95) — `a8c3f8f6`
- **OPT3.9 (second pass)** copy walks follow list tails in place, collector scans cache the
  from-space area; large_message 3.5x (1.08x of OTP), geomean of 22 kernels 1.26x — `5c836ba7`
- **OPT3.5A** spawns build and load the child outside the executor lock; short_processes at 8
  workers 42 to 36 ms per 5000 spawns — `53c3f0db`
- **OPT3.5B** sends to a receiver on another worker take no lock (lock-free process table and
  arrivals queue); short_processes 1.79x, ring 1.12x — `606ac9ce`
- **OPT3.5C** run queues get their own lock, a spawn without link or monitor skips the executor
  mutex; short_processes 2.26x (32 workers 25 ms, OTP 22) — `9bea9033`
- **OPT3.5D** collector scans take slot layouts from the header's kind and copy small objects
  without `memmove`; short_processes one worker 159 to 104 ms (OTP 129) — `eb47dc06`
- **OPT3.5E** term_to_binary codec stacks and encoded bytes are per-thread scratch kept between
  calls; kernel 3.43 to 2.26 s (OTP 2.38) — `f241c6f7`
- **OPT3.6** binary append growth: every whole-byte accumulator takes the word path, freed buffers
  are reused per thread; binary_append 1359 to 845 ms (OTP ~1080) — `165878d8`
- **OPT3.15 + OPT1.7 `maps:fold`** faster small-key map-tree inserts, `maps:fold/3` with a known
  fun as a loop, and fold funs returning `Acc#{...}` own the accumulator; map_update 4.4x (OTP
  0.52x) — `fdc7ab3c`
- **OPT3.8 + OPT3.16** inline integer text: `integer_to_list/1`, `list_to_integer/1` and
  `integer_to_list(N) ++ R` in generated code; int_to_list 1790 to 520 ms (OTP ~880) — `43a7cf87`
- **OPT3.17** `lists:sort/1` as a runtime builtin sorting element words, shared with `usort/1`, and
  `word_order` extended to atoms, bignums and compounds; 2-tuple sorts 8450 to ~520 ms (OTP 549) —
  `09db532a`
- **OPT3.4** file I/O: unzeroed growing buffers, `port_control/3` passes a binary in place, message
  copies keep their stacks inline; large_write 1150 to 850 ms (OTP 960) — `7dfe7dbc`
- **OPT3.18** heap block cache eviction: a released block that does not fit frees the oldest kept
  ones, up to 16M words per thread; list_of_tuples 1995 to 1063 ms (OTP 1517) — `880e567a`
- **OPT3.19** bignum scaling: `*` with one small operand multiplies the other's limbs into the new
  cell; bigint_factorial 1369 to 210 ms (OTP 1145-1694) — `a19fbed0`
- **OPT1.6** map-shape inline caches: a small-map search for a literal key first compares the key
  at the index its site found last; small_map 1500 to 1035 ms (OTP 1240) — `7ceed350`
- **OPT3.4 remainder** file kernels: fewer status reads, no end probe in `read_file`, references
  compared from cells, spare mailbox nodes reused; read_line 1633 to 1427 ms (OTP 1554) —
  `389636c5`
- **OPT3.9 remainder** small messages copied once: a send walks the message before any lock and
  copies it straight into a parked receiver; ping_pong 1096 to 975 ms, fan_in 461 to 354 —
  `da39d0a0`
- **OPT3.23 part 1** lighter process creation: 26 allocations per process lifecycle to 6; ring_setup
  260 to 208 ms (OTP 113); also fixed the erlang_gaps fixture race behind its hang — `1b14da68`
- **OPT3.23 part 2** lock-free posts: a send to any receiver that is not parked pushes onto its
  arrivals without the executor mutex; fan_in 370 to 269 ms — `f80aab9f`
- **OPT3.23 part 3** handed-over wakes: a process a worker wakes for itself is taken at its slice
  end without the run queues' lock; ping_pong 903 to 764 ms (OTP 1499) — `70a5b30f`
- **OPT3.12** small-map lookups: map patterns, `map_get`, `is_map_key`, `map_size` and present-key
  updates with atom/small-int keys run inline; 4 reads 929 to 12 ns (OTP 6) — `9be0f412`
- **OPT3.13A** word-level builtin arguments for `tuple_to_list/1`, `phash2/1`, `list_to_atom/1`;
  `tuple_to_list/1` 84 to 26 ns (OTP 8) — `b29a23d8`
- **OPT3.13B** quick builtins (ABI 23): `length/1`, `binary_to_list/1`, `list_to_binary/1`,
  `iolist_to_binary/1` take a bounded word-level fast path; length 92 to 18 ns (OTP 17) —
  `361f00da`
- **OPT3.13C** inline `bit_size/1`/`byte_size/1`, direct word-level `binary:part/2,3`;
  `binary:part` 175 to 31 ns (OTP 20) — `24588568`
- **OPT3.14** atom ordering: two atoms compare by their spellings in the comparison service, with
  no `Term`; atom `<` 142 to 27 ns (OTP 10) — `267ae39d`
- **OPTM.1** binary search with a `static_assert` on sortedness over sorted static tables (reserved
  words, type names, paired attributes, normal forms); one reserved-word table for compiler and
  runtime — `d98cafa4`, `fb53eba9`
- **OPTM.2A** one-word `Term` (`static_assert`, trivially copyable): spellings from the process-wide
  `terms/atom_table`, compound words trusted pointers; 69A geomean 1.06x — `b4924c01`
- **OPTM.2B** persistent terms read in place: `get` returns the stored word, sends and re-puts share
  it — `fa795339`
- **OPTM.2C** `TermAccess::admit` range, shape and owner checks in Debug only (`CHECKED_ADMISSION`);
  Release trusts compound, atom, pid and port words — `ef467893`
- **OPTM.2D** `term_to_binary`, atom comparison and fun-name hashes read `atom_table::spelling`
  directly — `aa056b93`
- **OPTM.3** `*_unchecked` term accessors (Debug aborts on a wrong kind) at 769 call sites with a
  dominating kind check — `8889f450`
- **OPTM.2E** persistent term reclamation: per-term areas retire under an epoch and processes copy
  them out at an evacuating sweep; 2,000 replacements of 16 KB grow 0.3 MB instead of 32 MB —
  `24f053bb`
- **OPTM.4** cached literal atoms: `"name"_atom` keeps one static word per spelling, used at ~700
  runtime call sites; `gb_trees:lookup` 1.23x, `lists:keysearch` 1.26x
- **OPTM.5** pinned benchmarks: `--scheduler-cpus` binds workers and `benchmarks/run.py --cpus`
  pins OTP and Clause alike; pinned 69A: Clause 1.30x ahead of OTP — `bce423f3`
- **OPTM.6** opportunistic `reserve` on string accumulators whose output is usually large (uri
  codecs, base64, printers, reports); concatenation above ~50 bytes 1.2-2.6x — `ed21dfca`
- **OPT0.21** literal list prefixes, raw `integer_to_list`/`list_to_integer`/`atom_to_list`;
  int_to_list 2.1x — `e6d4fa94`
- **OPT0.22** word-level `list_to_tuple`/`tuple_to_list`; 1000 elements 16.7 to 9.6 us — `b564c56b`
- **OPT0.23** flat hash index for message copies; 100k-element echo 34 to 17.5 ms — `b192769b`
- **OPT0.24** buffered sends to running processes; spawn plus send 21 to 13 us per process —
  `a36ed84a`
- **OPT0.25** per-worker run queues with stealing and neighbor balancing; fan_in 1.7-3.8x —
  `904e9cd1`
