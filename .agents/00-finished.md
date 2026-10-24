# Completed implementation work

Compact record of completed work, one bullet per step, in plan order. Full step text, criteria and
evidence are in Git at the listed commits; current behavior lives in `docs/`, architecture and file
ownership in [arch.md](arch.md) and [files.md](files.md). Gate counts, per-step dates and
measurements are dropped.

## Before plan 11 (2026-09-17 to 2026-10-03)

- **Foundations:** CLI/CMake scaffold; C++23, warnings as errors, Clang toolchain; `clau`,
  independent `clause_runtime`, shared `clause_abi`; stages exchange owned data, stage readers only
  reserved directories.
- **Preprocessor 1–3:** parser/oracle boundary, source buffers and locations, Erlang lexer, form
  dispatch and recovery ([preprocessor](../docs/preprocessor.md)).
- **Preprocessor 4–6:** object and parameterized macros, overloads, recursion checks, rescanning,
  stringification from original tokens.
- **Preprocessor 7–9:** nested conditionals, include/include_lib lookup, contextual
  module/file/line/function/machine/OTP macros.
- **Preprocessor 10–13:** restricted condition evaluator, feature macros, warning/error directives,
  CLI options, OTP golden fixtures; focused OTP comparisons only, no full upstream Common Test run.
- **Parser 1–4:** grammar/oracle inventory, shared token mechanics, move-only AST arenas with
  checked IDs, transactional forms ([parser](../docs/parser.md)).
- **Parser 5–12:** literals, operators, calls, clauses, patterns, guards, maps, records, binaries,
  blocks, branching, receive, funs, try/catch, maybe, comprehensions.
- **Parser 13–18:** attributes, type/spec/callback declarations, recovery,
  `--parse-check`/`--print-pp`/`--print-ast`, differential coverage; 344 grammar productions
  witnessed, ten-source OTP corpus (syntax only).
- **Projects 1–22:** toml++ manifests (schema 1), targets, globs, selection, planning,
  `--new-project`; platform matrix not closed ([projects](../docs/projects.md)).
- **Test migration:** 16 native project suites moved to CLI fixtures, frontend to CLI/OTP
  projections and 900 mutations, runtime to a separately built consumer; ledger audit still open
  (step 70).
- **Compiler/runtime 1–8:** LLVM SDK discovery, owned results, target policy, verification, object
  emission, ABI v1, feature reporting ([compile](../docs/compile.md)).
- **Compiler/runtime 9–14:** runtime/context lifecycle, immediate terms, frozen builtin registries,
  lazy memory, scheduler bookkeeping.
- **Compiler/runtime 15–28:** declarations, subset checks, acyclic batch calls, symbolic types,
  independent inference, target-width integers, calls, descriptors, native registration.
- **Compiler/runtime 29–32:** bounded specialization with guarded variants (3 per function, 32 per
  module, 2x growth), LLVM O0/O2, text/bitcode output.
- **Compiler/runtime 33–46:** artifact publication, options, project integration, `--verbose`,
  IR/type inspection, native harnesses, OTP comparisons, cross-target inspection, examples.
- **Pattern matching 1–3:** semantic matrix (81 signatures), revision-2 failure channel, runtime
  atoms and module bindings ([patterns](../docs/patterns.md)).
- **Pattern matching 4–12:** clause-local bindings, normalization, immediate matching, guard
  resolution and control flow, RHS-first body matches, stable heaps, rooted tuples/lists/strings.
- **Pattern matching 13–20 and 15a:** bignums, floats, exact-key maps, project-owned goldens,
  bitstrings, records, audited guard catalog (77 of 81 rows), stress; 1.08M golden comparisons.

## Plan 11 (2026-10-03 to 2026-10-15)

- **1** refreshed the OTP reference (`maint-29` pin `21776803`) and baseline — `2d27503`
- **1A** fast/full test modes — `07d2c38`
- **1B** changed-scope quality check — `3cdc743`
- **1C** documentation check run — `e381a86`
- **2** six OTP-goldened program fixtures with a feature map — `1a006ac`
- **3** entry, argv and exit-status contract ([executables](../docs/executables.md)) — `af55175`
- **3A** escript compile mode — `8bb5f89`
- **4** runtime term printing and `erlang:display/1` — `d63132b`
- **5** generated startup object with `halt/0,1` — `6a246ae`
- **6** Clang-linked executables from positional CLI inputs, staged publication — `8f68c18`
- **6A** single project target linked with explicit `-o` — `0c27f72`
- **7** executables from project targets — `bcaa23f`
- **8** executable golden test runner — `736a194`
- **8A** classic process heap contract ([runtime-heap](../docs/runtime-heap.md)) — `02ddf8d`
- **8B** binary cells split, off-heap binary list — `34005ee`
- **8C** parseable heap areas and a heap walker — `ad0f810`
- **8D** heap words admitted by header, not object index — `a0dbc00`, `0c76a8a`
- **8E** host terms held as raw words between safe points — `e5aa219`
- **8F** generated root frames on a process stack — `e99e4a8`, `b4c99d3`, `9573722`, `7ac15fb`
- **8G** one contiguous heap block plus fragments instead of chunks — `c162308`
- **8H** copying Cheney collector with ERTS sizing on explicit request (also step 25) — `d0cf00b`
- **8I** heap rework closed; per-context footprint 66 KB to 2.4 KB — `dc6e349`
- **9** `begin`/`end` blocks and `case`, with `case_clause` — `60102a1`
- **10** `if` with `if_clause` — `1982697`
- **11** `error`/`exit`/`throw` from source — `80cd244`
- **12** `catch Expr` — `328a290`
- **13** `try ... of ... catch` — `27867ed`
- **14** `try ... after` — `9df4e63`
- **15** stack traces (8 frames), `Class:Reason:Stack`, `erlang:raise/3` — `c311ae7`
- **16** `maybe` with `else_clause` — `aecbff2`
- **17** frame and continuation model: explicit frames on a flat process stack, `musttail` transfers
  ([execution-model](../docs/execution-model.md)) — `5447a16`
- **18** recursive call graphs accepted in analysis — `0e6eb6d`
- **19** proper tail calls — `eff731f`
- **20** deep non-tail recursion — `26f8b27`
- **21** list comprehensions — `07d249a`
- **22** binary and map comprehensions with OTP generator/filter errors — `1f58ba4`
- **23** root inventory extended to the execution model — `a3ef043`
- **24** collection triggers and safepoints decided — `cf9d82b`
- **26** collection from generated code — `0a94ccf`
- **27** heap exhaustion reported as `out_of_memory` — `bcd1ed5`, `8e939f1`, `3a21bc0`
- **27A** runtime-wide memory limit; optional `--max-heap`/`--max-stack`/`--max-memory` — `6a87dae`
- **27B** list length caps removed — `5701ab5`
- **27C** tuple arity limit as OTP — `12f772d`
- **27D** map size and key-work caps removed — `d4baa1f`
- **27E** big integer limit as OTP — `1c085f9`
- **28** sharing-preserving term copies between heaps — `4d707e5`
- **29** record updates — `ef7e2cc`
- **30** `record_info/2` — `7186f18`
- **31A** native record contract — `8e3e18b`
- **31B** native record cells and runtime services — `206afc1`
- **31C** local native records — `68252fc`
- **31D** qualified and imported native records — `595650c`
- **31E** anonymous native record forms — `a80d26b`
- **32** funs without captures — `bffcb27`
- **33** closures with captured variables — `76e4713`
- **34** named funs — `4ad9bce`
- **35** dynamic calls and `apply` — `919f32e`
- **36** generic production builtin bridge — `78cbb41`
- **37** term-access builtin family — `b29cbfc`
- **38** conversion builtin family — `cd03833`
- **39** project-owned `lists`/`maps` subsets — `21f1f77`
- **40** console output through `io:format`/`put_chars` — `f6a5c30`
- **41** typed native callables for builtins — `09bdac0`
- **41A** `erlang` builtins get `runtime/src/builtins/erlang/`, one file per family
  ([builtins](../docs/builtins.md)) — `9e22ff1`
- **41B** `erlang` exports via generated stub `erlang.erl` and `library/erts/erlang.terms` (351
  exports), OTP auto-import table — `8c7cb84`
- **41C** process dictionary — `3fec7f8`
- **41D** binary, atom and number conversions — `efa7d4c`
- **41E** external term format, byte-exact and fuzzed (found two decoder bugs) — `da8118c`
- **41F** time, unique values, timers (timer messages kept in a `MessageBuffer`) — `f9579d5`
- **41G1** process and system introspection — `12a2838`
- **41G2** spawn options, spawn requests, aliases, hibernation — `752c5fc`
- **41G3** fun information, hashing, MD5 — `a0a9bc9`
- **41H** non-distributed node and code-loading answers; phase gate left only non-`erlang` modules
  for luerl — `89d0c7d`
- **41I** replacement-module contract: Clause modules win over `source_search_paths`, missing
  modules are reserved names ([library](../docs/library.md#runtime-module-replacements)) —
  `8c6a94f1`
- **41J** library split into `library/{erts,kernel,stdlib}/` with `modules.terms`;
  `{Name, Arity, Group, Status}` inventories checked against OTP 29 — `b284a034`
- **41K** provided runtime modules discoverable, all-stub replacements of preloaded modules
  (`atomics`, `zlib`, `init`, ...); stubs raise `{not_implemented, F, A}` — `347ae0ad`
- **41K1** OTP's exported types in `<module>.types.hrl` headers — `4810de56`
- **41K2** remote types of ignored modules warn and are `term()`; luerl compiled and linked —
  `4810de56`
- **41L** `init` without a boot script ([builtins](../docs/builtins.md#init-module)): argv/home once
  at startup, `stop`/`reboot` halt, `boot`/`restart` `notsup`; also `lists:filtermap`, `uniq` —
  `377c2228`
- **41N** handle references (off-heap buffer in a refc layout); `atomics`, `counters`,
  `erts_internal`, `erts_code_purger` — `f7d42c4c`
- **41O1** `unicode_util` tables generated from host OTP answers (573 KB C++), all 1.1M code points
  match — `07a945b4`
- **41O2** `unicode_util:gc/1` UAX #29 clusters and NFD/NFC/NFKD/NFKC — `84ec8037`
- **41P** files: Clause `file` is the file layer, OTP `filelib`/`filename` run on it;
  `erl_prim_loader` over `file`; link, cwd, copy operations — `92980354`
- **41Q** `zlib` 1.3.2 linked, streams as handle references, compressed `term_to_binary` —
  `dece5234`
- **41R** networking: Clause `gen_tcp`/`gen_udp`/`inet` are the layer; `inet` address parsing,
  `ntoa`, `getaddr`, `gethostname` — `a99595e8`
- **41ZA** PCRE2 10.47 dependency (`cmake/Pcre2Dependencies.cmake`), provided `re` stub module —
  `26a04085`
- **41ZB** `re` over PCRE2: C++ `compile`/`run` plus Erlang `re.erl` loop, replace and split; luerl
  `os.date` — `b17b2a09`
- **41S** `prim_eval:'receive'/2` over `clause_receive` builtins; fixed receive scan position after
  a bad timeout; luerl driver prints OTP output with no stand-ins — `9d3f3c17`
- **41T** `lists` data functions as runtime builtins (`sublist`, `flatten`, `usort`, `keymember`,
  ...); fun-taking ones stay Erlang; tracer fix of `6a7d402a` — `f6a53456`
- **41U** `io:format/3`, `fwrite`, `put_chars/2`, `nl` builtins; pid or name devices go through
  `file:write/2` ([io](../docs/io.md#calls)) — `cd08f1a8`
- **41V** `math` as builtins over the C library (`library/stdlib/math.erl` stubs) — `42acff89`
- **41W** `binary` search, split, parts, hex builtins; `replace` in Erlang over them — `82292d0a`
- **41X** `os` env/getpid builtins and `maps` get/merge/take/with builtins; Erlang `os:timestamp`,
  `maps:filter`/`map` — `c8de2662`
- **41M1** runtime-wide persistent term table; reads copy into the reader's heap
  ([runtime](../docs/runtime.md#persistent-terms)) — `dd5b4b3c`
- **41M2** `persistent_term` `put/get/erase/info` builtins keyed by `phash2` hash — `65bde218`
- **41Y1** `unicode:characters_to_binary/list` for latin1/utf8/utf16/utf32 — `e942687b`
- **41Y2** `file:read_file_info/1,2`, `write_file/3`; `write` plus `append` no longer truncates —
  `b0030e9b`
- **41Z** `code` answers for the linked program without a code server — `d911b78d`
- **41ZC** OTP I/O protocol on stdin and file servers (`get_until`, `io:request/1,2`, `requests`);
  luerl euler prints OTP output ([ports](../docs/ports.md#standard-io-and-files)) — `31b20d73`
- **42** pid and reference identities — `59064bf`
- **43** cooperative executor for spawned processes — `f092504`
- **43A** interruptible long-running builtins — `0682b54`
- **44** process exit and crash reports — `98ad97a`
- **45** signal inbox and message send — `dbad5db`
- **46** selective receive without timeout — `45b424c`
- **47** `receive ... after` timeouts — `e404460`
- **48** links, exit signals, `trap_exit` — `82d8187`
- **49** monitors — `e111740`
- **50** registered process names — `d881d4b`
- **51** collection with mailboxes and suspended processes — `55347de`
- **52** identity-dependent guards — `dd6b8f2`
- **53** port identity scope decided (superseded by 57A) — `583e779`
- **54** synchronized atom table — `25215cf`
- **55** synchronized code-server publication and lookup — `8b8a699`
- **56** several scheduler workers — `c1ff627`
- **57** cross-worker wakeups, timers, shutdown — `fa54da5`
- **57A** port contract ([ports](../docs/ports.md)) — `d39a9e1`
- **57B** port identities and table — `21a5199`
- **57C** I/O poller with scheduler wakeups — `d1dc2f2`
- **57D** subprocess ports — `6b04929`
- **57E** file and standard I/O ports — `b1bc5d7`
- **57F** socket ports — `8c99374`
- **57G1** every port kind served by one event-driven I/O thread — `fbacc98`
- **57G2** port tasks on scheduler workers — `171b7f9`
- **57G3** senders suspended on busy ports, unread input bounded — `616e7b2`
- **58** six fixture projects match OTP at O0/O2 on 1 and 4 workers — `3ec5737`
- **58A** inference fact domain ([semantic](../docs/semantic.md#inference)) — `e52277c`
- **58B** facts of all literals — `a6c01b5`
- **58C** operator and builtin results — `e92650f`
- **58D** container facts — `6adc311`
- **58E** fun facts — `ebac255`
- **58F** local function inputs from callers — `8b1e998`
- **58F1** consecutive integers printed as ranges — `bfab7a8`
- **58G** narrowing by patterns and guards, entry domains — `22dff64`
- **58H** narrowing by uses — `c56d515`
- **58H1** integer ranges narrowed by guard comparisons — `16fb58e`
- **58I** specs contradicting inferred types rejected — `be76267`
- **58I1** `term()` printed as `_` — `579e605`
- **58I2** argument relations printed as variable names — `6d02bd1`
- **58J** wide known containers kept, records printed — `bc94017`
- **58J1** try and maybe values — `2607172`
- **58K** per-clause function types, printed by `--print-types` — `c7f52b6`
- **58L** calls matched against function types — `01fbba5`
- **58M** callees re-analysed per call — `06042fa`
- **58N1** dependent facts of `case`/`if` ([semantic](../docs/semantic.md#dependent-facts)) —
  `e63e14c`
- **58N2** dependent facts through operations and bindings — `60a5dd4`
- **58N3** dependent facts of tuple scrutinees and `try ... of` — `175b545`
- **58O** fixed-point rounds measured (`--print-stage-times`) — `c712d605`
- **58P** non-converging groups remembered, given `term()` at once — `1ebaca6d`
- **58Q** monotone rounds; a member flipping back to a recent summary gives up alone — `fa55a8d5`
- **58R** a round re-walks only members whose inputs changed — `2a3059ba`
- **58S** joins monotone at the container depth limit; luerl inference 330 s to 107 s — `b5aec11f`
- **58U** `cache_inference_in_dirs` option, implicit `*/stdlib`, `*/kernel`, ...
  ([projects](../docs/projects.md#inference-cache)) — `5490832e`
- **58V** a module's inference results as bytes — `d8b26d1b`
- **58W** cached results of covered modules reused; luerl warm recompile inference 180 s to 40.5 s —
  `ff31997f`
- **59** O2 specialization removes real tag/shape checks — `8a5ace2`
- **60** source-level debug information (`-g`) — `116cff1`
- **61** opt-in profiling (`--profile`) — `ecb8de5`
- **62** link-time optimization (`--lto`, MSVC and ELF only) — `5ec3645`
- **62A** hashed code-server lookups — `aa0198b`
- **62B** receive timers in a timer wheel — `eccd7c4`
- **62C** export liveness: executables drop functions the entry cannot reach; `--keep-exports` opts
  out ([executables](../docs/executables.md#export-liveness)) — `fbfe99c`
- **62D1** `build_dir`/`--build-dir`, Clause temp files below `<build_dir>/tmp` — `0cd45db`
- **62D2** cache store for parse transform replies and compile outcomes, `--no-cache` — `8e71e4d`
- **62D3** directory scan cache measured, no code — `d5a655f`
- **62D4** whole-batch compile result cache — `176c4dd`
- **62D5** SQLite 3.54 cache backend (2,000 puts 12 s to 250 ms)
  ([compile](../docs/compile.md#compile-cache)) — `f4d29b9`
- **62E** in-memory index of searched directories (15,215 probes to 0; luerl warm build 3.8 s to
  1.65 s) — `0e8a6c5`
- **63** Linux x86-64 validation (WSL2) — `5948a44`
- **64** 32-bit x86 runtime validation (Windows x86, Linux i386), cross-linked goldens — `9eef9cc`
- **65** Linux AArch64 and 32-bit ARM under qemu-user — `ab5943b`
- **65A** `-behaviour` with OTP callback warnings, generated `behaviour_info/1` — `e3f7a89`
- **65B** generated `module_info/0,1`, informational attributes kept — `86d84aa`
- **65C** `-import` of any module, compile hints ignored, rejected attributes named — `12dc2d5`
- **67** compiler and frontend sanitizers — `58b3243`
- **68** ThreadSanitizer on the multi-worker runtime; fixed a shutdown use-after-free and a
  lock-order inversion — `9679aea`
- **69A** manual benchmarks against OTP (`benchmarks/`, 6 programs, 29 kernels); first run Clause O2
  20.2x slower than OTP JIT ([validation](../docs/validation.md)) — `0777b693`
- **73A** parse transform contract ([transforms](../docs/transforms.md)) — `57d4193`
- **73B** owned abstract term model with ETF and `.abstr` codecs — `a3f66ab`
- **73C** modules exported as abstract forms (`--print-abstr`) — `9a9f9aa`
- **73D** abstract forms imported into the AST — `0ebf90e`
- **73E** transforms run through the host OTP loader — `13a5e20`
- **73F** transforms applied before lint in the compile pipeline — `1c92c5c`
- **73G** precompiled and project transform modules — `d1ce36a`

## Plan 20 (2026-10-12 to 2026-10-17)

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
- **OPT1.2** raw floats (doubles kept in registers, cells only where a word is read) and float loops
  (a `<sym>.float` variant takes float parameters as double bits); 3M-step float loop 20.8 to 6.8 ms
  (at the fmul+fadd latency, OTP 2.7x slower on a growing value), 1M float sum 9.1 to 2.6 ms (OTP
  4.3); 69A and luerl euler neutral — `[plan20] OPT1.2. Float loops ...`
- **OPT1.3 (part)** inline `+`/`-` on tagged words with a sign-test overflow check, no double width;
  hash 22%, count_up 19%, fib 15% — `93981174`
- **OPT1.4C** binary loops: a `<fun>.loop` variant updates one fresh sub-binary cell in place across
  self tail calls; binary_match 8.5x — `c45402af`
- **OPT1.4C (part 2)** the variant reads its parameter's view once per step and skips its bitstring
  test; sum_binary 5.2 to 3.7 ms (OTP 2.7) — `5fa0c26c`
- **OPT1.4D** integer segments of any constant width up to the word less 8 bits read inline at any
  bit position (whole-byte aligned: one load), 32-bit targets included; 1-bit loop 182 to 6.5 ms
  (OTP 3.2), 12-bit fields 16.3 to 0.40 ms (OTP 0.41), unaligned 16-bit 11.8 to 0.27 ms — `8e3f483d`
- **OPT1.4E** escaping rests: binary loops whose rests (or whole binary) are also kept, returned,
  sent or captured stay loops; those reads get a copy cell; JSON-like string scan 2.6 to 1.25 ms
  (OTP 1.2-1.6), comma fields kept 5.3 to 2.0 ms (OTP 2.3) — `428520c0`
- **OPT1.4F** SSA binary cursors: TBAA memory kinds, invariant view loads in binary loop variants
  that call nothing else, branch-free binary views and a late GVN on them; 69A binary_match 1.64x
  (2.28x faster than OTP JIT), byte sum 14.4 to 8.0 ms (OTP 12.4), 1-bit loop 4.7 to 1.67 ms (OTP
  3.2) — `29c03617`
- **OPT3.10B** young heap sizing: shrinks after minor and full collections lower the block's
  capacity in place instead of copying the live words again; ERTS's sizing (minor `young + stack`,
  larger old area, F_HEAP_GROW) measured worse. Kernels alone 1.04x geomean (string_build 1.27x,
  list_of_tuples 1.16x, sort 1.12x); 69A sequence 1.00x — `1618e0f0`
- **OPT1.7 + OPT1.8** known anonymous funs inlined at O2 (written in the call, or `F = fun` bound
  once): clauses in place, the closure built only where no clause matches; `lists`
  map/filter/foreach/all/any/flatmap/foldl/foldr/ mapfoldl/mapfoldr become loops in the caller that
  collect only at yields; results and error reasons as `lists`, only stack traces differ. 69A
  map_foldl 12.2 to 7.8 ms (OTP 3.4); foldl alone ~11x, map min 1.35x; luerl euler unchanged —
  `6da94982`
- **OPT0.20B** generational collection: safepoints run minor collections that promote objects below
  the high-water mark into an old area and leave it in place (ERTS sizing rules, Debug old→young
  check); 69A geomean 1.15x (sort 1.29x, binary_append 1.6x, map_put 1.25x, comprehension 3.3x) —
  `4635bb64`
- **OPT0.20C** `min_heap_size` applied (`spawn_opt`, `process_flag`, `--min-heap-size` as `+hms`):
  the first block and every collection floor, so a process trades memory for fewer collections the
  OTP way — `0afd29b1`
- **OPT0.26** word-level `term_to_binary`/`binary_to_term`: small integers, atoms (cached), lists
  and tuples as words, one factory per decoding; encode 3,066 to 558 us, decode 1,084 to 550 us on
  the 69A term — `4fe2ec7f`
- **OPT0.27** short builtin lists (`++` up to 64 cells, character lists of
  `integer_to_list`/`atom_to_list`) bumped into the heap block's free words; collector copies skip
  zero-filling; string_build 1.8x, int_to_list 1.6x — `a996c552`
- **OPT0.28** flat message copies as OTP (`size_object`/`copy_struct`): a shared subterm is copied
  once per reach, no address map or per-object records; large_message 1.5x; runtime tests get a 120
  s timeout — `3a50cd77`
- **OPT0.29** message copies queue only compound slots (the copy carries immediates), compound
  tested by its primary tag; large_message 39 to ~25 ms — `ef64ba45`
- **OPT0.30** message hand-off: a message of 512 words or more is built before the executor lock and
  its block becomes a fragment of the receiver (`HeapStorage::adopt`), with no second copy for busy
  receivers; adopted fragments collect only past the young block — `97295c84`
- **OPT0.31** file reads copy once: `read_file` sizes its reply by `fstat` and reads straight into
  it, `read` reads into its reply, and `port_control` takes a large reply over as the binary's
  buffer (`TermFactory::owned_binary`); read_file 23.8 to 6.2 ms (OTP 4.4), large_read 9.2 to 7.8 ms
  — `0a8410db`
- **OPT0.32** word-level map update at a small-integer key on an off-heap map
  (`MapAccess::update_small_key`, no `Term` per argument); map_update and map_put 13-15% —
  `289336a8`
- **OPT0.33** map cursors (`terms/map_cursor`): one word-level walk over flat and tree maps;
  `clause_maps:entries/1` gives `maps:fold` an entries tuple instead of a list of pairs, and
  `map_next` stops building every entry per step; map_update 15% — `d7f90ad9`
- **OPT0.34** `atom_to_list` of an atom literal folded at compile time into a literal string, as
  OTP's `sys_core_fold`; string_build ~25% — `54f12529`
- **OPT3.1** on-heap map trees: tree nodes are `map_node` heap cells (fanout 16) that collections
  move and free like any cell, with no reference counts, list sweeps or off-heap charge forcing full
  sweeps; map_update 3.6x (4.2 to 1.19 ms), 69A geomean +3.6% (heap-sizing side effects in OPT3.10)
  — `bca659ab`
- **OPT3.2** inline `++` (`codegen/lowering_append`: a proper left list of up to 32 cells copied in
  generated code, 145 to 13 ns, OTP 12) and direct builtins (`CLAUSE_builtin_call_v1`, ABI 22: pure
  conversions run with no frame transfer); string_build 1.81x, int_to_list 1.08x, 69A geomean +2.8%
  — `91cc2e10`
- **OPT3.10** heap block reuse (`memory/heap_blocks`): released heap blocks stay with their thread
  for its next collections instead of fresh OS pages; 69A geomean +9.3% (large_write 2.98x,
  grow_shrink 2.06x, binary_append 1.42x), 1.45x behind OTP JIT — `58d545cb`
- **OPT3.3** codec walks and binary pressure: the encoder reads cells in place, the decoder claims
  each container's words once from one children stack, and binary pressure collects the young
  generation first (a full sweep only after a minor that freed less than half); 69A geomean +7.2%
  (comprehension 2.13x, large_read 1.90x, term_to_binary 1.43x, string_build 1.36x), 1.42x behind
  OTP JIT — `708d9414`
- **OPT3.4** read-ahead lines: the file driver's read_line returns lines from a 64 KB read-ahead
  buffer instead of a read and a seek per line, and every other operation first seeks back over what
  it has not returned; read_line 75 to 41 ms per 20,000 lines (OTP 29) — `301e1bc4`
- **OPT3.7** `lists:sort/1` as an alternating merge sort from sorted pairs: no merge reverses a run,
  so a pass allocates one copy instead of two (results identical to OTP's, ties included); sort 5.25
  to 3.4 ms (OTP 2.1). Also a real `lists:merge/3` — `d514e6f4`
- **OPT3.11** fragment windows: once the heap block is nearly full, generated code bump-allocates in
  the process's newest own fragment, which grows geometrically, instead of calling the construct
  service per cell between safepoints; map_foldl 1.52x (`lists:map` returns), term_to_binary 1.13x,
  binary_append 1.10x — `c86bb60a`
- **OPT3.8** `list_to_integer/1` reads a short list in place into a fixed buffer (trusted walk)
  instead of admitting a `Term` and walking it; the int_to_list round trip run alone 1688 to 835 us
  (OTP 444) — `6823705f`
- **OPT3.9** message copies cache the source area of the last cell (one range check instead of a
  storage lookup per cell) and copy cons cells without a parse or copy loop; list builtins' `walk`
  reads cells in place (trusted walks); large_message echo run alone 4.37 to 3.62 ms (OTP 1.95) —
  `a8c3f8f6`
- **OPT3.9 (second pass)** after OPTM.2A put a literal lookup on every cell (6.4 ms per echo): copy
  walks follow list tails in place with an inline literal-span test, buffers skip zeroing, collector
  scans cache the from-space area and skip cons parses, adopted messages count toward collection
  only once received (as ERTS); large_message 3.5x (1.08x of OTP), sort 1.7x, list_of_tuples 1.7x,
  string_build 1.8x, gc kernels 1.3-1.8x, geomean of 22 kernels 1.26x — `5c836ba7`
- **OPT3.5A** spawns build and load the child outside the executor lock (it is only published and
  queued under it); ended contexts are freed after the lock; short_processes at 8 workers 42 to 36
  ms per 5000 spawns, 32 workers unchanged (lock traffic, see OPT3.5B-C) — `53c3f0db`
- **OPT3.5B** sends to a receiver running on another worker take no lock: a lock-free process table
  (open addressing, tombstones), a message node pushed onto the receiver's lock-free arrivals and
  taken 64 at a time when its receive would wait, quiescent reclamation of arrivals and table
  arrays; `Schedule` kept with the process (no hash lookups). short_processes 1.79x, ring 1.12x,
  ping_pong 1.07x, fan_in 0.94x — `606ac9ce`
- **OPT3.5C** run queues get their own lock (idle workers sleep on it); a spawn without link or
  monitor publishes and queues its child without the executor mutex; workers free ended contexts
  after leaving the mutex for the next slice instead of relocking; short_processes 2.26x of the base
  (32 workers 25 ms, OTP 22), ring 1.15x, fan_in 1.09x — `9bea9033`
- **OPT3.5D** collector scans take slot layouts from the header's kind, copy small objects without
  `memmove` and test immediates inline; short_processes one worker 159 to 104 ms (OTP 129), 32
  workers 22 ms (OTP 22); against 662e5c1b: short_processes 2.37x, explicit_collect 1.78x,
  grow_shrink 1.22x, list_of_tuples 1.21x, sort 1.09x — `eb47dc06`
- **OPT3.5E** term_to_binary was not slower after OPT3.5D (same profile); its kernel time (18-26% of
  samples, page faults) came from the codec's large per-call buffers: stacks and encoded bytes are
  now per-thread scratch kept between calls, small integers skip the item path; isolated kernel 3.43
  to 2.26 s (OTP 2.38), 69A term_to_binary 1.50x — `f241c6f7`
- **OPT3.6** binary append growth: every whole-byte accumulator takes the word path, a grown buffer
  copies its bytes once (power-of-two capacities, no zeroed copy), freed buffers are reused per
  thread (no page faults); binary_append 1.61x (1359 to 845 ms, OTP ~1080 in the 69A history) —
  `165878d8`
- **OPT3.15 + OPT1.7 `maps:fold`** map trees: small-key inserts search with no virtual probe and
  copy node runs; `maps:fold/3` with a known fun is a loop over the entries, and fold funs returning
  `Acc#{...}` (`maps:fold`, `lists:foldl/foldr`) own the accumulator from step 2 on (update op
  chosen at run time; owned small-key updates reuse nodes and stub in place). Pinned: map_update
  4.4x (OTP 0.52x), map_put 1.39x (OTP 0.92x) — `fdc7ab3c`
- **OPT3.8 + OPT3.16** inline integer text (`codegen/lowering_integer_text`): `integer_to_list/1` of
  a small integer writes its digit cells in generated code, `list_to_integer/1` of a short digit
  list reads it in place, and `integer_to_list(N) ++ R` writes the digits onto R (N checked where
  the call stands); `atom_to_list` of a literal was already folded (OPT0.34). Pinned: int_to_list
  1790 to 520 ms (OTP ~880), string_build 1370 to 645 ms (OTP ~1110) — `43a7cf87`
- **OPT3.17** `lists:sort/1` is a runtime builtin sorting element words (all small integers by word,
  all atoms by spelling read once, otherwise stable by word order), shared with `usort/1`;
  `word_order` now also orders atoms, bignums, small/float and float/bignum pairs, and tuples and
  lists of these, so generated comparisons of such compounds skip `structural_order` too. Pinned,
  600 sorts of 20,000: integers 1555 to ~535 ms (OTP 1045), 2-tuples 8450 to ~520 (OTP 549), mixed
  numbers 3850 to ~610 (OTP 653), atoms 735 to ~250 (OTP 373); `small_order` compares signed at the
  word's width (32-bit negatives) — `09db532a`
- **OPT3.4** file I/O: off-heap buffers and driver replies are `ByteBuffer`s that grow unzeroed (a
  16 MB read_file was 27% memset), `port_control/3` passes a binary to the driver in place (no
  iodata copy), and message copies keep their stacks inline (a small message allocated 3-4 times per
  copy). Pinned, Clause / OTP ms: large_write 1150 to 850 (OTP 960), read_file 1840 to 1390 (1510),
  read_line 1740 to 1485 (1550), large_read 1320 to 1180 (1460), small_files 1180 to 1120 (1300) —
  `7dfe7dbc`
- **OPT3.18** heap block cache eviction: a released block that does not fit frees the oldest kept
  ones, and a thread keeps up to 16M words (was 4M, with no eviction: early small blocks filled the
  cache, so every young block, old area and full sweep of a growing heap took fresh pages, ~31 MB
  faulted per list_of_tuples iteration). Pinned 69A: list_of_tuples 1995 to 1063 ms (OTP 1517),
  map_foldl 1571 to 1122, large_message 1062 to 813, term_to_binary 1227 to 1053; geomean 1.28x to
  1.36x ahead of OTP JIT — `880e567a`
- **OPT3.19** bignum scaling: `*` with one small operand multiplies the other's heap limbs into the
  new cell (was cpp_int import, multiply and export: 56% of samples were export bit extraction with
  MSVC's 32-bit limbs). bigint_factorial 1369 to 210 ms (OTP 1145-1694), 1.20x behind to ~5x ahead —
  `a19fbed0`
- **OPT1.6** map-shape inline caches: a small-map search for a literal key first compares the key
  word at the index its site found last (a shared `[2 x word]` global with 8 rewrites, so
  polymorphic sites in many processes stop writing it); variable keys only scan. small_map alone,
  pinned, 1500 to 1035 ms (OTP 1240); luerl unchanged — `7ceed350`
- **OPT3.4 remainder** file kernels: path operations read a path's status only after a failed open
  or delete (delete unlinks), read_file reads to the reported size with no end probe, `=:=` of two
  references compares their numbers from the cells (`word_exact`), and mailbox lists reuse spare
  nodes. Pinned medians, HEAD / new / OTP ms: read_line 1633 / 1427 / 1554 (1.05x to 0.92x),
  small_files 1050 / 1043 / 1294; ping_pong 1118 to 1025, ring 1341 to 1274, fan_in 560 to 456 —
  `389636c5`
- **OPT3.9 remainder** small messages copied once: a send walks its message once before any lock
  (`OutgoingMessage`; the bounded `large()` walk is gone, persistent terms are copied), copies it
  straight into a parked receiver with an unzeroed claim (no reservation), and a receiver takes a
  small arrival by memcpy plus pointer relocation instead of a second walk and copy; senders read
  `parked_` before claiming it. Pinned A/B medians (alone, 7 rounds): ping_pong 1096 to 975 ms, ring
  1324 to 1152, fan_in 461 to 354; large_message, short_processes unchanged — `da39d0a0`
- **OPT3.23 part 1** lighter process creation: 26 allocations per process lifecycle to 6 (monitor
  maps made by the first monitor, flat registries for heap buffers and contexts, persistent term
  epochs kept in the context, first 64 stack words and 4 mailbox nodes embedded, `Schedule` inside
  `Impl`, keyed `make_shared` lifetime). Ring's gap was setup (3.9 us a process against OTP 1.45),
  not hops (315 ns against 407). Pinned 0-15 medians, base / new: ring_setup 260 / 208 ms (OTP 113),
  ring 1134 / 1089, short_processes 1150 / 1049; ping_pong, fan_in unchanged. Batch stealing
  measured null. Fixed the erlang_gaps fixture race behind its occasional hang — `1b14da68`
- **OPT3.12** small-map lookups: map patterns, `map_get`/`is_map_key`/ `map_size` and present-key
  updates with atom/small-int keys scan and copy classic map cells inline; 4 reads 929 to 12 ns (OTP
  6), a 2-field update 576 to 11 ns (OTP 13); 69A kernel `dataproc.small_map` added — `9be0f412`
- **OPT3.13A** word-level builtin arguments: `tuple_to_list/1` 84 to 26 ns (OTP 8), `phash2/1` 92 to
  26 ns (13), short `list_to_atom/1` 143 to 70 ns (9), hash values unchanged — `b29a23d8`
- **OPT3.13B** quick builtins (ABI 23): `length/1`, `binary_to_list/1`, `list_to_binary/1`,
  `iolist_to_binary/1` run a bounded word-level fast path through `CLAUSE_builtin_call_v1`, 0
  declining to the frame entry; length 92 to 18 ns (OTP 17), list_to_binary 654 to 34 (9),
  iolist_to_binary 840 to 37 (43), binary_to_list 270 to 31 (9) — `361f00da`
- **OPT3.13C** inline `bit_size/1`/`byte_size/1` (45 to 11 ns, OTP 8); `binary:part/2,3` direct and
  word-level, short results claimed straight into heap words (175 to 31 ns, OTP 20) — `24588568`
- **OPT3.14** atom ordering: two atoms compare by their spellings read from the atom table's
  lock-free index in the comparison service, with no `Term`; atom `<` 142 to 27 ns (OTP 10), sorting
  1000 atoms 1.56 to 0.26 ms (OTP 0.06) — `267ae39d`
- **OPTM.1** binary search with a `static_assert` on sortedness over sorted static tables (reserved
  words, type names, paired attributes, normal forms); one reserved-word table for compiler and
  runtime — `d98cafa4`, `fb53eba9`
- **OPTM.2A** one-word `Term` (`static_assert`, trivially copyable): spellings from the process-wide
  `terms/atom_table`, compound words trusted pointers, `stale_term` gone, copies read source cells
  by pointer; 69A geomean 1.06x (fan_in 2.09x, grow_shrink 1.46x) — `b4924c01`
- **OPTM.2B** persistent terms read in place: `get` returns the stored word, sends and re-puts share
  it — `fa795339`
- **OPTM.2C** `TermAccess::admit` range, shape and owner checks in Debug only (`CHECKED_ADMISSION`);
  Release trusts compound, atom, pid and port words — `ef467893`
- **OPTM.2D** `term_to_binary`, atom comparison and fun-name hashes read `atom_table::spelling`
  directly — `aa056b93`
- **OPTM.3** `*_unchecked` term accessors (`terms/term_unchecked.cpp`, Debug aborts on a wrong kind)
  at 769 call sites with a dominating kind check; no `pid_value` mirror, semantic-failure accessors
  not mirrored — `8889f450`
- **OPTM.2E** persistent term reclamation: per-term areas in a reserved range
  (`memory/persistent_region`), replaced/erased areas retire under an epoch, processes copy them out
  at an evacuating sweep (slice start, parked ones under the executor lock), then the area is freed;
  2,000 replacements of 16 KB grow 0.3 MB instead of 32 MB — `24f053bb`
- **OPTM.4** cached literal atoms: `"name"_atom` (`atom_names.hpp`) keeps one static word per
  spelling, checked against the issuing runtime by one lock-free index read; ~700 runtime call
  sites, one shared builtin `atom` helper; `gb_trees:lookup` 1.23x, `lists:keysearch` 1.26x,
  scan+parse 1.15x, 69A geomean unchanged (+0.7%)
- **OPTM.5** pinned benchmarks: `--scheduler-cpus performance|all|<list>` binds workers, the default
  worker count follows the affinity mask, and `benchmarks/run.py --cpus` (default `performance`)
  pins OTP and Clause alike through the job object; pinned 69A: Clause 1.30x ahead of OTP, 20 of 30
  kernels repeat within 5% — `bce423f3`
- **OPTM.6** opportunistic `reserve` on string accumulators whose output is usually large: sized
  from the input where known (uri codecs, base64, binary escapes, `pretty_bin`'s flat length),
  otherwise 256 for printed containers (`format_term`), source printers and reports, 4096 for
  abstract-code text and transform requests; names, atoms, digits and short joins stay unreserved
  (a reserve under ~30 bytes gives up the small-string buffer: 2.5-8x slower). Microbench: 1.2-2.6x
  on the concatenation above ~50 bytes; no 69A kernel formats text, so none was rerun — `ed21dfca`
- **OPT0.21** literal list prefixes, raw `integer_to_list`/`list_to_integer`/`atom_to_list`;
  int_to_list 2.1x — `e6d4fa94`
- **OPT0.22** word-level `list_to_tuple`/`tuple_to_list`; 1000 elements 16.7 to 9.6 us — `b564c56b`
- **OPT0.23** flat hash index for message copies; 100k-element echo 34 to 17.5 ms — `b192769b`
- **OPT0.24** buffered sends to running processes; spawn plus send 21 to 13 us per process —
  `a36ed84a`
- **OPT0.25** per-worker run queues with stealing and neighbor balancing; fan_in 1.7-3.8x —
  `904e9cd1`

## Plan 21 (from 2026-10-17)

Clause's own erts, kernel and stdlib; open steps are in [21-lib.md](21-lib.md).

- **LIB0** `library/documented.terms`: 2,888 documented functions observed from OTP's doc chunks
  (`observe_docs.escript`); `library_inventory` rejects a documented stub outside the partial
  modules; `library_modules.py coverage` — `e3e44460`
- **LIB1** `ordsets`, `orddict`, `proplists`: list walks as C++ builtins (`builtins/collections/`)
  mirroring OTP's clauses, fun-taking functions Erlang; `Named<Role>` typed arguments — `04329156`
- **LIB2** `dict`, `sets`: OTP's linear hash records (`collections/linear_hash`) and version 2 maps;
  `phash/2`, `phash2/1,2` now OTP's `make_hash`/`make_hash2` values — `162eab2f`
- **LIB3** `gb_trees`, `gb_sets`, `queue`: OTP's shapes from a shared balanced tree engine
  (`collections/balanced_tree`) and `{Rear, Front}` moves — `66e0ab0c`
- **LIB3B** `array`: OTP 29's cached tuple tree record in C++ (`collections/array`), folds and
  mapfolds Erlang over `clause_array` helpers — `5f197abb`
- **LIB5A** `base64`: C++ encoders and OTP's strict and mime decoding state machines, errors
  included (`encoding/base64`) — `547ae828`
- **LIB5B** `calendar`: Gregorian arithmetic in C++ (`builtins/calendar.cpp`), local time and RFC
  3339 Erlang — `a70297a6`
- **LIB5C** `filename`: Erlang walks by OTP's Unix and Windows rules, golden checks both families'
  tables (`fixtures/executables/filenames`) — `fa6d4a3c`
- **LIB5D** `uri_string`: Erlang walker over RFC 3986 driven by a delimiter table, OTP's error
  fragments and retries (`fixtures/executables/uri_strings`) — `f91bbcb6`
- **LIB5E** `json`: C++ `decode/1` and escapers with OTP's iodata; encoders, formatters and callback
  decoders Erlang over `clause_json` scanners — `28f84686`
- **LIB6** `rand`, `random`: OTP's algorithms and generic functions in C++ (`builtins/rand/`),
  plug-in handlers Erlang; ziggurat and jump constants observed from OTP (`tools/rand/`) —
  `a543519e`
- **LIB5F** `json` default `encode/1`, `format/1,2` in C++ with OTP's iodata
  (`encoding/json_encode.cpp`), Erlang fallback past 256 levels — `084ed9f9`
- **LIB5G** `filename` in C++ (`builtins/filename/`), OTP's clause walks and shapes; `absname`,
  `basedir` Erlang — `19896937`; `uri_string` in C++ (`builtins/uri/`, grapheme clusters in
  `unicode/graphemes.cpp`) — `17cf492d`
- **LIB4A** `string`: every function in C++ (`builtins/string/`) over `unicode/code_points.cpp`
  (cp/gc in OTP's shapes) and `unicode/normalize.cpp`; `strings` golden — `fc6022ce`
- **LIB4B** `unicode` forms and properties, `unicode_util` walks as builtins (`unicode/forms.cpp`);
  `unicode_forms` golden — `c25f8153`
- **LIB4C** `io_lib` in C++ (`builtins/io_lib/`): writers, checks, collect functions and the format
  engine in OTP's deep shapes, `io:format` on it; `io_lib_text` golden — `d1c64893`
- **LIB4D** `io_lib_pretty` in C++ (`io_lib/pretty*.cpp`): intermediate format, `chars_limit`
  deepening, `cind`/`pp` in OTP's deep shapes and `pp_bin` for `bformat`; `print/1,4`, `~p`/`~P`,
  limited `write`/`bwrite`, crash reports; `pretty_print` golden — `c8d01d0d`
- **LIB4E** `io_lib:fread/2,3` in C++ (`io_lib/fread.cpp`): OTP's scanner over the line as a list,
  line collection into OTP's continuation, `~#`'s `try` covering the rest; `io:fread/2,3` as a
  `get_until` request; `fread_scanning` golden — `6bc5b45f`
- **LIB8A** `erl_internal` and `erl_anno` in C++ (`builtins/front_end/`): OTP's tables (BIFs
  generated from `erlang.terms`), predefined functions, annotation reads and updates with OTP's
  errors; `compiler_tables` golden — `6c9447df`
- **LIB8B** `records` and `erl_features` (ABI 24): defaults folded at compile time
  (`semantic/record_definitions.cpp`, also OTP's legality rule) into `RecordDescriptor::definition`
  ETF bytes, `ModuleDescriptor::features`; run-time definitions interned by the code server for
  `create/4`; `records_reflection` golden — `3847ccb6`
- **LIB9A** `erlang` gaps: checksums and `iolist_to_iovec/1` (OTP's 512-byte copy threshold), tuple
  edits, `is_integer/3` builtin, cookies, `halt/2`, option-taking sends and signals, node-naming
  spawns (useless pids end with `noconnection`), `process_flag/3`, process iterator, `system_flag/2`
  (`backtrace_depth` now bounds traces, up to 64), `hibernate/0`; `erlang_gaps` golden — `d24cdb75`
- **LIB9C** `decode_packet/3` in C++ (`erlang/packets.cpp` length rules as a table per type,
  `erlang/packet_http.cpp` OTP's http line parser and terms); `packet_decoding` golden — `e4f1081c`
- **LIB9B** `suspend_process/1,2`, `resume_process/1` as executor holds (`scheduler/suspension.cpp`:
  suspend counts per suspender, released when it ends, `suspended` status), `process_display/2`;
  `suspension` golden — `e1d597d2`
- **LIB7B** `graph`: queries and traversals in C++ (`builtins/graph/`, OTP's walks with explicit
  stacks and OTP's list orders), updates, subgraphs and condensation in Erlang; `graph_module`
  golden — `424e0a2a`
- **LIB7A** `sofs`: typed sets, relations, products, selectors and families in C++
  (`builtins/sofs/`, OTP's sorted merges), fun-taking functions in Erlang over `clause_sofs`
  builtins, digraph conversions stubbed until LIB12; bridge catalog built with `std::to_array` (over
  1024 entries); `sofs_sets` golden — `67ee4fe9`
- **LIB10A–E** `ets` in C++ (`runtime/src/ets/`, `builtins/ets/`): objects as detached blocks
  (`memory/detached_term`), own hash index with tombstones while fixed and a `std::set` tree, 64
  locked parts with write_concurrency, registry of names, owners and fixers, owner end and heir
  transfer in the executor before links and monitors; folds and `i/0..3` Erlang; match specs and
  files stubbed (LIB11, LIB12); `ets_tables`, `ets_owner`, `ets_update`, `ets_walk`,
  `ets_concurrency` goldens — `8a734394`
- **LIB4F** `maps_order` comparison funs: a write that meets a map ordered by a fun records it and
  continues in `clause_io_lib:sort_maps/6`, which sorts the keys and reruns the call through
  `apply_ordered/4` (`io:format` writes only then); `semantic/builtin_continuations.hpp` brings
  continuation modules into the batch; `io_lib_text` golden — `595c9574`
- **LIB11** match specifications (`runtime/src/match_spec/`): compiled to pattern and expression
  nodes with OTP's checks and lint messages, heads bound and guards read in place, bodies copied out
  ('EXIT' for a raising call); ets `select*`, `match*`, `select_count`, `select_delete`,
  `select_replace` (OTP's keeps-key rule), `match_spec_*`, `is_compiled_ms`,
  `erlang:match_spec_test/3` (trace dialect, fake actions) as builtins; continuations in OTP's
  shapes; `test_ms`, `repair_continuation`, `match_delete`, `init_table` Erlang; `ets_match_specs`
  golden — `2998c62e`
- **LIB12A** `digraph`, `digraph_utils` over OTP's three ETS tables: updates and reads Erlang over
  `ets`, path searches and digraph_utils' traversals C++ (`builtins/digraph/`: `DigraphView` reads
  the tables in place for module graph's walks, now over `graph::View`); `subgraph`, `condensation`
  and `sofs:family_to_digraph/digraph_to_family` Erlang; `digraph_tables` golden — `663405e0`
- **LIB12B** `ets:tab2file/2,3`, `file2tab/1,2`, `tabfile_info/1`: disk_log halt logs in OTP's
  format written with `file` and split by `clause_disk_log:items/1` (C++, read-only chunk rules),
  OTP's options, verification and errors; files pass both ways; `ets_table_files` golden —
  `4bf10136`
- **LIB10F** ETS in portions: `tab2list/1` and whole-table selects walk in portions (hash tables
  fixed from the first trap); `delete/1` and `delete_all_objects/1` take the indexes out and free
  them in portions (`Index::shed`); an ended owner's tables are freed outside the executor lock;
  small `update_counter` values change in place; `ets_portions` golden (one and four workers); Linux
  TSan over the ETS goldens clean — `d4d63f2c`
- **LIB72** `ms_transform`: `parse_transform/2`, `transform_from_shell/3`, `format_error/1` in
  Erlang over the abstract format, OTP's match specifications, warnings and error codes; state
  threaded instead of the process dictionary; `ms_transform_forms` golden over literal forms —
  `81589524`
- **LIB53** `dets` with files byte for byte as OTP writes them: a C++ engine (`builtins/dets/`:
  header and MD5, buddy system with frozen lists while fixed, linear hashing and segment splits, the
  write cache's flush in OTP's order, 8 KB scans and bchunks, bulk layouts of init_table, repair and
  compaction) under OTP's `dets` server and table processes in Erlang (users, write cache, fixes,
  timers); `ets:to_dets/2`, `from_dets/2`; `dets_tables` golden compares file MD5s with OTP
- **LIB30** `erl_scan` (first step of the qlc chain): a runtime scanner of its own
  (`builtins/front_end/scanner*.cpp`) clause for clause with OTP's `scan1/5`, re-entrant by
  rescanning the unfinished token; options and callbacks in `erl_scan.erl`; `erl_scan_tokens` golden
- **LIB31** `erl_parse` (second step of the qlc chain): a recursive-descent parser of OTP's grammar
  over token terms (`builtins/front_end/parser*.cpp`) with OTP's abstract format, annotations, error
  tokens and action errors (default-reduction order included); `normalise/1` with `eval_bits`'
  rules, `abstract`, `tokens`, annotation walks; `erl_parse_forms` golden, plus a scratch comparison
  with OTP over 765 OTP sources and token mutations
- **LIB22** `timer` without a timer server: local timed sends over `erlang:send_after/3`, other
  timers as processes reached through an alias in the `TRef`, conversions in C++
  (`builtins/timer.cpp`); `erlang:apply/2,3` builtins for dynamic calls; `timers` golden —
  `b0562967`
- **LIB25** `pg`/`data_publisher`, `global`, `global_group`, `rpc`/`erpc` on one node (other nodes down),
  `net_adm`/`net_kernel`/`auth` non-alive answers, `seq_trace` per-process tokens; behaviours call `global`
  literally; `process_groups`, `rpcs` goldens — `9ae39f56`
- **LIB24** `application`, `application_controller` (lazy controller, OTP's `ac_tab`, errors, reports,
  restart types, permanent ends halt) and `clause_application_master`; clau compiles `.app`/`.app.src`
  resources into a generated `clause_applications`; kernel/stdlib virtual and running; `applications`
  golden, `project_applications` test — `5945f7f3`
- **LIB23** `logger`, `logger_formatter`, `logger_std_h`, `logger_filters`, `logger_handler` over a
  `clause_logger` server started at first use (default handler on `standard_error`, synchronous
  writes); `error_logger`, `error_logger_file_h`, `log_mf_h`, `error_handler`; behaviour reports as
  `?LOG_*` events; `kernel/include/logger.hrl` for `include_lib`; `file:sync/1`, `datasync/1`;
  `loggers` golden — `f2895564`
- **LIB20, LIB21** `proc_lib`, `sys`, `gen`, `gen_server`, `gen_statem`, `gen_event`, `gen_fsm`,
  `supervisor`, `supervisor_bridge` in Erlang with OTP's messages, results, error terms, debug
  shapes and report texts (through `clause_logger` until `logger`); `gen_fsm` keeps its own loop;
  `proc_libs`, `gen_servers`, `gen_statems`, `gen_events`, `gen_fsms`, `supervisors` goldens —
  `37a9c223`
- **LIB32** `erl_eval` (third step of the qlc chain): an Erlang interpreter of the abstract format
  with OTP's handlers, errors, bindings (orddicts or maps), interpreted funs over OTP's env tuple,
  strict and zip generators, native records; bit segments as `clause_erl_eval` builtins over the
  `bit_types` settling shared with `normalise/1`; `clause_erl_lint` (first-error command checks,
  used vars, guard tests); `erlang:make_fun/3`; `erl_eval_exprs` golden — `428ac91b`
- **LIB33** readers: `io:read/1..4`, `scan_erl_*`, `parse_erl_*` as `get_until` requests;
  `file:consult/1`, `eval/1,2`, `script/1,2`, `path_*` and `path_open/3` read the file whole, decode
  it by its coding comment (`clause_epp`, as `epp:set_encoding/1`) and scan form by form with OTP's
  errors, invalid UTF-8 included; `file_readers` golden — `0a1a0679`
- **LIB34 part 1** `erl_error`: `format_exception/3,4` and the internal formatters in Erlang over
  `io_lib` with OTP's explanations, layouts, `error_info` callbacks, trim, column, format fun and
  characters-limit options; `erl_error_formats` golden over synthetic stack traces — `04a5844e`
- **LIB34 part 2** `erl_pp`: OTP's layout algorithm (items, then a layout pass) in Clause's own
  code; options, hooks, `legalize_vars/1`; `erl_pp_forms` golden, and a scratch comparison over the
  forms of 357 OTP sources found no differences — `a8d88410`
- **LIB34 part 3** `erl_expand_records:module/2` in Erlang with OTP's traversal and variable
  numbering (guard context in the state, not the process dictionary); `clause_erl_lint:is_guard_test/3`;
  `erl_expand_records_forms` golden, and a scratch comparison over 396 OTP sources and suites under
  four option sets found no differences — `f07983e8`
- **LIB34 part 4** `epp` in Erlang (server per source, replies queued per step, files read whole and
  decoded by coding comment through `clause_epp`, shared with `file:consult/1`), `erl_features`'
  parse state; `file` servers monitor their opener instead of linking (no `EXIT` on close, as OTP);
  `epp_files` golden, and a scratch comparison over 1,110 epp calls on OTP sources and suites found
  no differences — `4a3e5b7b`

## Plan 22 (from 2026-10-23)

Compiler performance, type inference first (`.agents/22-opt-compiler.md`).

- **B.1** (082dc057) `benchmarks/compile_times.py`: runs a compile workload (W1-W4, W6 built in;
  scratch W5a/W5r after `--`) pinned to P-cores with `--no-cache --print-stage-times`, 1 warm-up + 5
  (3) timed runs, median/min/spread per phase and pass, slowest components with rounds, counter and
  stdout agreement; `--baseline` alternates A B and prints ratios; refuses while builds, tests or
  another `clau` run (`--force-noisy`, `--wait`). Quiet HEAD ab635294: W5a inference 100.4 s (spread
  0.5%), W1 117.0 s (1.2%), both 17 passes; `ubody_used_vars` + 6 is 27.2 s / 25.5 s of them.

- **B.0** W5r scratch repro (`<scratch>/w5r/w5r.erl`, plan 22 B.0): 11.39 s inference (spread 0.7%),
  `ubody_used_vars` + 6 10.47 s at ≈22 ms per member walk, 5-13 rounds per solve; settles in pass 11
  (W5a never settles).
- **INS.8** (2e12b57f) input churn under `--print-stage-times`: per pass the functions and local
  input slots `gather_inputs` changed, by kind (first, integer bound, union member, shape, deeper,
  function types, term()), and fun reads not final; `inference churn` histogram and the 10 most
  changed slots with their last facts (`inference_inputs.cpp`, printed by
  `driver/compile_times.cpp`). Uninstrumented W5r A/B ratio 1.000; `--print-types` identical on W5a,
  W1, W6, W5r. Findings: inputs reach callees one call level per pass (`first` changes until pass
  16) and nested integer bounds creep by one per pass because `Lattice::widen` only relaxes
  top-level integers; plan 22 CONV.0, CONV.7. `compile_times.py` gained `--no-stage-times` and
  splits the pass outcome from the churn note.

- **INS.1** (fc6e40c6) pass counters and incremental potential: `; graph: N nodes (+M), W widenings;
  clean: C of K components (...), X changed, T ms (P% of pass)` per pass and
  `inference clean-changed` lines, from exact summary fingerprints compared between passes
  (`semantic/types/inference_profile.cpp`). W5a: clean components take 15% → 71% of late passes
  (≈31.5 s of ≈94 s), but 0-30 per pass change anyway (fun F/A, re-analysis), so CONV.2 needs
  dynamic reads. Temporary `intern` counters: up to 43.5M calls per pass at ≥99.9% hits on a
  75k-node graph (OPTC.B/C). Uninstrumented A/B 1.02-1.04 unaligned but 1.001-1.010 with 64-byte
  function alignment on both sides: layout, recorded in plan 22 B.2. `--print-types` identical on
  W5a, W1, W6, W5r.

- **INS.9** (c226b524) solve repetition: `; N solves repeated the previous fixed point, M reached it
  after R rounds in total` on `inference rounds`, from a per-round `note_round` in `iterate()`
  (profile only; aligned A/B within noise). W5a: the `ubody_used_vars` group repeats 4 of 17 solves
  (48 of 197 rounds), the heavy groups 1-6 of 17; `luerl_scan:yystate/7` (W1) 14 of 17. Equality
  only: a growth check would intern nodes. `--print-types` identical on W5a, W1, W6, W5r.

- **INS.7** (b83e9756) sampling profile (no code change; `build/prof-compiler`, VSDiagnostics +
  xperf): W5r exclusive intern/node map 52%, allocation 17%, lattice ops 21%, the walk under 1%.
  `Gather::Gather` interns two constants per join (38% of W5r, ≈30% of sampled W5a inference) and
  `Meet::same_atoms` builds a meet to test atom inclusion (30%): plan 22 OPTC.A-A3, ahead of CONV.*.
- **INS.5** (b83e9756) fact sizes: `inference facts N` per pass and `inference largest` for the 3
  costliest components. W5a facts mean tree size 4, max 4236; the `ubody` group's largest 1509. Fact
  size is not the per-walk cost (CONV.5 deprioritized). Aligned A/B 1.005; `--print-types`
  identical.

- **B.4** (cb94e8f3) quiet baseline at 2af72e57 in ignored `benchmarks/history/compile-times/`:
  inference W5r 11.97 s, W5a 96.16 s, W1 114.73 s; W2 executable 327.7 s of which LLVM optimize 75.6
  s + emit 120.8 s. `compile_times.py` no longer compares pass-note times between runs.

- **OPTC.A** (288133e0) Gather constants: the graph keeps the Ids of char() and any improper list
  from the first join (`Graph::JoinConstants`), so `Gather` no longer interns them per join; same
  interning order, so every Id and `--print-types` stay identical (W5r, W5a, W1, W6) and pass
  counters equal. Inference: W5r 11.88 → 6.94 s, W5a 95.8 → 55.2 s, W1 114.7 → 66.7 s, W6 0.43 →
  0.29 s.

- **OPTC.A2** (936a3852) atom inclusion without a meet: `Meet::same_atoms` checks the atom name sets
  directly instead of joining their meet. W5r pass counters (graph nodes included) and
  `--print-types` of W5r, W5a, W1, W6 unchanged. W5r inference 0.817 of OPTC.A.

- **OPTC.A3** (f37bb441) no node copies in `Gather::add`: the node is read in place and a union's
  members by index (only `plain()` interns). `--print-types` of W5r, W5a, W1, W6 unchanged. W5r
  0.976 of A2; A2+A3 on W5a 56.6 → 50.8 s (1.89× against B.4 overall).

- **OPTC.C part 1** (fafd6d79) hashed node interning: `Graph`'s identity map is an
  `std::unordered_map` with `NodeHash` (was `std::map<Node>` comparing nodes field by field) and
  `Graph::get` is inline. Ids, counters and `--print-types` unchanged (W5r, W5a, W1, W6). Inference
  W5r 5.54 → 3.81 s, W5a 50.6 → 31.8 s, W1 → 42.4 s (against B.4: W5a 3.0×, W1 2.7×).

- **OPTC.C part 2** (74f4b900) family ranks and fewer allocations: `family_rank` from a kind table
  and a hashed category map, `nesting` without child copies, atom inclusion over in-place names.
  Output identical (W5r, W5a, W1, W6). Inference W5r 3.67 → 2.92 s, W5a 31.3 → 28.8 s, W1 → 37.5 s.

- **OPTC.C part 3** (33595da3) memoized nesting: the graph keeps each node's nesting per limit
  (nodes never change), so `truncate` stops re-walking facts; `Lattice::list` reuses the kept char()
  Id. Output identical (W5r, W5a, W1, W6). W5r 0.922, W5a 29.8 → 28.7 s.

- **CONV.0** (08384300) nested integer widening: `Lattice::widen` also relaxes integer bounds that
  moved inside tuples, maps and lists of the previous fact's shape, in recursive rounds and input
  widening. W5a output identical and still 17 passes, but integer-bound input changes end in pass 14
  (most changed slot 11 changes, was 16); W1/W3 only gain precision (round-capped groups such as
  `luerl_comp_cg:stmt/3` converge, 0 new `term()`). Time unchanged (W5a 0.994, W1 1.004). Fixture
  `tests/fixtures/inference/widening.erl`.

- **CONV.7** (tried 2026-10-24, not kept) inputs within the pass: every other pass walked components
  callers first, gathering inputs from call sites walked earlier. W5a still 17 passes, output and
  time the same: inputs wait on result → input round trips (`{ok, X} = f(..), g(X)`), not on one
  call level per pass. Folded into CONV.2.

- **CONV.1** (0bf86e74) per-input widening ladder: each local input slot counts its changes; changes
  1-4 join, 5-6 widen, 7-8 widen and coarsen (containers inside a container become `term()`), the
  next gives `term()`. Output identical on W5a, W1, W3, W6, still 17 passes; no slot changes more
  than 8 times (W5a 9-16x: 87 before). Passes stay bound by late first arrivals, so the ladder is
  CONV.6's termination guarantee and CONV.2 must bring the speed.

- **CONV.2** (4c2bd059) incremental passes: a pass keeps every component whose last walk read the same summary
  and input versions and that the pass and re-analysis budgets would answer alike
  (`semantic/types/inference_incremental`); its work is replayed into the pass budget. Output and
  work steps identical (W5a, W1, W3, W6, W5r). Inference W5a 27.7 → 19.6 s, W1 36.3 → 23.7 s, W5r
  0.925; late W5a passes skip about 8 700 of 8 911 components.

- **CONV.3** (5c0e09b9) warm-start solves: a recursive component solved before starts from its last summaries,
  and each member's first walk replaces (not joins) its summary. Inference W5a 19.6 → 13.6 s, W1
  23.9 → 16.2 s, W5r 2.61 → 1.18 s (output identical); W6 identical; about 1% of W5a/W1/W3
  signatures change, both wider and narrower (converging before widening rounds), none newly
  `term()`.

- **CONV.4** (8927a4df) cost-aware give-up: lattice joins counted per graph; a recursive component whose solve
  ran more than 2^17 joins and 1/16 of the largest pass is costly, and its members' local inputs
  become `term()` when they change again (giving up results instead lost 460-600 W5a signatures to
  `_`). CONV.2's re-analysis check no longer re-solves components the budget answers alike. Output
  identical on W5a, W1, W3, W6 (W5r: 9 lines wider, 1.065 slower); inference W5a 13.8 → 11.3 s,
  W1 16.1 → 13.4 s.

- **CONV.6** (56490e0a) pass limit 17 → 64, a safety limit: W5a, W1 and W3 settle in pass 50, W6 in 24, W5r
  in 11. Signatures returning `_`: W5a 2355 → 2148, W1/W3 2479 → 2288, W6 26 → 17; 10 W5a/W1
  signatures newly `-> _` are clause sets that lost unreachable clauses. Inference vs CONV.4 (3
  runs): W5a 11.4 → 12.9 s (1.136), W1 13.4 → 14.5 s (1.087), both below CONV.3. W2 (luerl euler
  `-O2`): build 234 → 230 s, run 660 → 644 ms, same output. `parsers.erl` today lines re-recorded.

- **CONV.8** (44906d9d) member-granular warm solves: each recursive member keeps its last walk across
  solves (body, recorded facts, work, re-analysis answers, joins; members read by result and
  function types, everything else by version, `MemberWalk`), and a solve replays it while nothing it
  read changed; replayed joins still count for cost-aware give-up. Output identical on W5a, W5r, W1,
  W3, W6 and the same pass counters. Inference vs CONV.6 (3 runs): W5a 12.8 → 12.2 s (0.957), W1
  13.9 → 13.0 s (0.935). W2: build 214 → 216 s (LLVM phases), run 613 → 617 ms, same output. The
  plan's premise (passes 17-50 ≈ 2-3 s) was CONV.4's; at CONV.6 they took 1.15 s and passes 1-16
  (real input churn) 9.4 s. Tried and not kept: replayed members keeping their summary instead of
  refreshing it (cuts round-0 cascades): W5a 11.87 → 11.73 s, 102 lines of `--print-types` differ.

- **LLVM.1** (15181ec9) parallel code generation (`codegen/parallel`, `--jobs <n>`, default every hardware
  thread). W2 profile (serial, 175 modules): optimize 67 s, emit 113 s, IR verification 5 times
  over the batch 14.7 s; the largest module (`core_parse`) only 12%. For object output each worker
  writes a module's bitcode (use lists kept: without them one luerl object differed) under a lock on
  the shared context, reads it into its own `LLVMContext`, optimizes and verifies it; emission runs
  on those contexts; largest modules first; diagnostics and objects merge in module order; repeated
  `lower_frames` is free. Objects byte-identical to `--jobs 1` (W2's 175, W6 at -O0/-Os/-O2, new
  `codegen_options` check). W2 compile 214 → 57.8 s (optimize 11.1 s, emit 19.7 s), same run
  output. IR/asm inspections stay on one context (source scopes key on its metadata).

- **LLVM.2 part 1** (a632e6c9) literal heap. Finding: `module_info/0,1` rebuilt yecc's ~900 `-dialyzer`
  attributes per call (atoms are runtime words, so atom-holding literals were built inline): 256K of `core_parse`'s
  511K IR instructions; llc -O2 on `core_parse` alone: module_info 8.7 s, `yeccpars1/5` cycle 6.2 s, rest 0.7 s.
  Compound literals are now encoded per module (`abi/literals.hpp`, BEAM compact term encoding + Clause extended tags)
  and decoded once per runtime at registration (scratch context, pid number 0) into a never-retired persistent-range
  area that `LiteralAreas` owns and registers as a module literal area; code reads `literal_cache` after the claim
  check, other runtimes call `CLAUSE_literal_v1`; OPT0.4 cell area removed; ABI 25. W2 (3 interleaved runs, 16
  threads): compile 54.9 → 39.2 s (lower 10.8 → 9.6, optimize 13.3 → 9.3, emit 23.9 → 13.7), executable 41.9 → 33.3
  MB, same output. Tried and not kept: `optnone` on module_info (emit 19.6 → 31 s, O0 IR larger); a writable
  atom-patched literal area (reverted for this design). Fusing optimize+emit gains nothing while `core_parse` leads
  both phases.

- **LIV.1** (c4c2378e) investigation, no code change. The keep-every-export trigger on W1/W2 and W5a (with
  `--entry b2s_main:main -o`) is Clause's own `library/kernel/file.erl` (`apply(M, F, A)` in
  `io_request/3`, `list_to_atom/1` in `reply/1`), not the host OTP copy; fixing it would not help:
  W1's live code has 138 runtime-module calls in 40 modules and 206 atom-producing calls in 41
  (`gen_server`, `erl_eval`, `qlc`, `luerl_scan:binary_to_atom/2` for Lua names, ...). A
  `hello.erl` with `io:format` and `file:read_file` batches 4 modules and keeps none whole. With the
  trigger disabled (unsound, measured only): W1 still keeps 102 of 174 modules whole (1061 exports
  dropped), W5a 82 of 134 (1019); `compile`, `core_parse`, `erl_lint`, `v3_core`, `qlc`, `erl_eval`
  stay whole, mostly through `?MODULE` in their own error tuples (`{Loc, ?MODULE, Reason}` for
  `format_error/1`). Generated `module_info/0,1` escape their keys (`compile`) and function names
  (`{init, 1}`, `{file, 2}`) as module atoms; ignoring them gains 2 modules (35-38 exports) on
  top, and attribute values must keep escaping (user attributes may name modules called later), so
  not changed. A name-granular escape (keep only the `F/A` called at runtime-module sites) is
  blocked by live `apply`/spawn calls with a runtime function name (`erl_eval`, `proc_lib`,
  `file:io_request/3`). `--print-types` keeps inferring the whole batch under `--entry`: it is the
  precision guard, and W1/W3 liveness keeps every export anyway (W2 inference 13.2 s, W1 13.0 s).

- **B.0** (9bd4b2e2) owned fixture `tests/fixtures/inference/budgets/walkers.erl`: a free-variable pass over an
  invented tree language (state record of 18 fields, 7 mutually recursive walkers, results unions of
  many records, ordsets), written from scratch in W5r's shape; CTest `inference_walkers` compiles it
  with `-O2`, keeps passes (13), work steps (397 795), lattice joins (4 950 111) and the walker
  group's solves (11), rounds (55) and member walks (236) under budgets ≈1.25× those, then runs it.
  Reproduces W5r's cost (group 1.0 s Release, ≈1M joins per pass, one costly give-up); counters are
  identical in Debug and Release, with or without `--print-types`.

- **B.5** (8c58279b) W5a re-profile at 0beec0a8, no code change (`build/prof-compiler`, VSDiagnostics
  with `clau` pinned to P-cores, raw stacks from `xperf -a dumper`, each sample attributed to its
  innermost category frame). Pinned inference 11.85 s (spread 1.3%), 78% of 57 200 samples; print
  types 6%, parse and preprocess 6%, the rest of analysis 7%. Inside inference: lattice ops 41%
  (`fact_of` union assembly 14%, `Gather::add` 5%, `Lattice::join` 4%, `~Families` 2%,
  `family_rank` 2%; a third is allocation: `std::vector<Id>` growth and `std::set<std::string>` atom
  names), body walk 28% (`walk` 4%, `Module::expression` 3%), walk containers 12% (scope `restore`
  copying `std::map<BindingId, Fact>` 6%, `BindingFacts` 5%, `set_aside`/`put_back` 1%), interning
  10% (`Node::operator==` 4%, `NodeHash` 3%, `Graph::intern` 2%), `gather_all` 6% (rebuilds all
  call arguments every pass), pass driver 2%. Plan 22 reordered: OPTC.B, D, C, new OPTC.F.

- **INS.2** (a5c2b4ae) lattice operation counts: `inference lattice` line (`semantic/types/lattice_trace`, graph
  points at it only while a profiled inference runs) with calls of 2-member `join`, `widen`, `meet`, `within`,
  `subtract`, `truncate`, those repeating a call of their pass and of any pass, other joins and nesting lookups.
  W5a: join 22.58M (98.2% repeat in pass, 99.7% ever: 77K distinct), meet 6.30M (50K distinct), within 9.60M (26K),
  truncate 46.97M (47K), widen 761, subtract 5 505; W1 alike. A graph-lifetime memo of a few 100K entries serves
  nearly all: OPTC.B starts with it. Profiled inference +7% (two hash sets); uninstrumented W5r 1.004, W5a 1.019
  (min 1.006, spread 3%). `--print-types` identical on W5a, W1, W3, W6, W5r.

- **INS.3** (f76f2d5f) per-component lattice costs: `InferenceProfile::Cost` sums the lattice joins and graph nodes
  of each component's walks (on `inference slowest`), and `inference costliest` lists the 10 components of at least
  10 ms by time per work step. W5a: `ubody_used_vars` + 6 7.2M joins over 165K work steps (43.6 per step, 11 us per
  step), `core_parse:yeccpars1` + 522 1.1M joins over 1.8M steps; W1's costliest `euler:run/0` 37 us per step (59
  joins per step). Output identical on W5a, W1.

- **OPTC.B part 1** (b0ac717e) memoized lattice operations: the graph keeps the answer of every 2-member `join`
  (by depth), `meet` and `within` (`Graph::answer`, an open-addressed table keyed by packed operation, depth and
  operand indices), with the joins each ran nested, added again on a hit so cost-aware give-up and every counter
  stay the same; answers computed while the node budget widened are not kept. `--print-types` and pass counters
  (work, joins, nodes) identical on W5r, W6, W5a, W1, W3. Inference (3 runs, profiled): W5a 12.89 → 7.30 s
  (0.567), W1 14.67 → 8.42 s (0.574), W5r 1.23 → 0.08 s.

- **B.6** (5a5d69c8) W5a re-profile after the OPTC.B memo, no code change (`build/prof-compiler`, pinned, raw
  stacks by innermost category; inference 66% of 38 700 samples, parse and print types 9% each). Inside
  inference: body walk 48%, lattice ops 12% (was 41%: `fact_of` 2.6%, `Answers::find` 1.5%, `Gather::add` 1.4%),
  `gather_all` 11%, scope `restore` 11%, `BindingFacts` 8%, pass driver 4%, interning 4%, `set_aside`/`put_back`
  2%. Plan 22 reordered: OPTC.D, OPTC.F, then OPTC.B part 2 with OPTC.C.

- **OPTC.D** (a9216ae4) walk containers: variable facts are `BindingValues`, one flat slot per definition in
  `BindingId` order (scopes, clause joins, guard narrowing and fun instances copied `std::map<BindingId, Fact>`
  nodes); each function's binding events, shared names and first facts are a `BindingIndex` built once per
  inference (every definition has an event, so slots equal the old keys; the work spent per binding is unchanged);
  `Inference::expressions`, `reused_expressions` and `callees` are hash maps (only looked up). `--print-types` and
  pass counters identical on W5r, W6, W5a, W1, W3. Inference (3 runs, profiled): W5a 7.17 → 4.83 s (0.673), W1
  8.21 → 5.49 s (0.668). Pinned W5a profile: scope `restore` 11% → 0.05%, `BindingFacts` 8% → 6.7% (`publish`
  2.1%), `set_aside` 0.6%; now body walk 50% (`Module::expression` 6.8%, outside plan 22's files), lattice ops
  17%, `gather_all` 13%, pass driver 6.6%, interning 6.5%.

- **OPTC.F** (3e325483) input gathering: each local function's calls are indexed once (`Inference::input_calls`),
  and a gather joins again only the inputs of functions walked in the pass or called by a walked function
  (`Inference::walked`); the others read the same argument facts into the same inputs (a function whose inputs
  changed is walked in the next pass). `--print-types` and pass counters identical on W5r, W6, W5a, W1, W3.
  Gathering (profiled `inference churn` line): W5a 936 → 255 ms, W1 1162 → 297 ms. Inference (3 runs, profiled):
  W5a 4.93 → 4.22 s (0.857), W1 5.56 → 4.73 s (0.850).

- **OPTC.B part 2** (93bdc6ca) allocation-free joins. Joins by member count on W5a (after the depth budget): 7.83M of
  two, 1.14M of one (98% repeated), 158K of none, 112K of more; the memo served only joins of two, so joins of one
  were most of `fact_of`'s work. Now the graph also keeps joins of one (`Graph::Operation::join_one`), a join of none
  is `none()` without gathering, the answer table holds a key and its answer in one 16-byte slot (32-bit result index
  and nested joins; was two arrays of 8 and 24 bytes), and `without_subsumed` keeps members with fewer than two exact
  tuples without its pairwise `within`. `--print-types` and pass counters identical on W5r, W6, W5a, W1, W3.
  Inference (3 runs, profiled, both builds function-aligned): W5a 4.21 → 3.93 s (0.934), W1 4.76 → 4.48 s (0.943).
  Pinned W5a profile: inference samples −8.4%, lattice ops 16% → 12% of inference (`fact_of` 3.7% → 1.2%,
  `Answers::find` 2.1% → 2.7% with more lookups). Not done: `Families`' `std::set<std::string>` names (≤ 1%, left
  to OPTC.C's compact nodes) and memoizing joins of more than two (112K calls).

- **OPTC.C part 4** (deda102d) identity map over the node vector: `Graph`'s `std::unordered_map<Node, size_t>` (a full
  node copy per list entry) became `Identities`, open-addressed 8-byte slots of a 32-bit node hash and the node's
  index in `nodes_`; a probe compares hashes before nodes. Most interns are hits that rebuild existing nodes
  (`constructed_fact` 37%, `fact_of` 15%, `Shape::of` 11% of interning samples). `--print-types` and pass counters
  identical on W5a, W1, W3, W6, W5r. Inference (3 runs, profiled, both builds function-aligned): W5a 3.79 → 3.66 s
  (0.967), W1 4.26 → 4.16 s (0.977); interning samples −21%. Compact names (atoms, integers, labels as ids) measured
  at ≈ 1% left: deprioritized in the plan.

## Library gaps (luerl, 2026-10-14 to 2026-10-15)

Compiling and running luerl (`tools/luerl-trace/` driver, 35 Lua snippets) found library functions
that were missing or wrong; all are fixed.

- **Ledger of 25 rows** (in order of first use) closed by plan 11 steps 41T–41ZC, each step's luerl
  reason below.
- **41T** `lists` stubs (`sublist` for every token, `droplast`, `mapfoldl`, `splitwith`, `nthtail`,
  `flatten`, `duplicate`, `max/min`, `umerge`, `usort`, `partition`, `join`, `keymember`,
  `keysearch`).
- **41U** `io:nl/0`, `fwrite` (`print` crashed before its newline).
- **41V** `math` NIFs (`pow` for `1e20` exponents and `^`, `sqrt`, `sin`, `exp`, `log`, `acos`,
  ...).
- **41W** `binary:match/part/split` (`string.find`/`sub`/`format`).
- **41X** `os:timestamp` (`os.clock`), `maps:get/3`, `is_key`, `remove`.
- **41M1/41M2** `persistent_term:get/put` (reached via `erl_features` from `io_lib` atom quoting in
  Lua syntax errors).
- **41Z** `code:ensure_modules_loaded` instead of the absent `code_server` on `io_lib:format`'s
  error path.
- **41L** `init:get_arguments/0`, `lists:filtermap`, `uniq` (`erl_features:init_features`); Lua
  syntax-error text now equals OTP's.
- **41Y1/41Y2** `unicode:characters_to_binary/2`, `file:read_file_info`, `write_file/3`
  (`dofile`/`loadfile`).
- **41ZA/41ZB** `re:run/2` over PCRE2 (`os.date`), `re:replace`.
- **41ZC** `io:request/2` `get_until` on file pids (every `luerl:loadfile` failed with
  `{not_implemented,request,2}`).
- **Driver result** after 41S: 34 of 35 snippets print OTP's output; `goto` has the same
  `function_clause` with frames lacking arguments and locations
  ([differences](../docs/differences.md)); `coroutine` is nil on both.
- **`examples/euler`**: all 10 problems print OTP's output; speed went 78 s (O0) / 21 s (O2) to 0.62
  s (O2, OPT0.18) against OTP 0.35 s; sampling found inspection, boolean and atom lookups,
  `bits_service` and `get/1` hot (OPT0.2–0.18).
- **`examples/benchmark`**: `--entry benchmarks:run` took `run/1`; the `/ARITY` suffix
  (`--entry M:F/0`, [executables](../docs/executables.md)) fixed it; all 22 suites print OTP's 158
  lines (Clause O0 1.8 s, OTP 0.55 s).

## Outstanding work to finish

[The backlog](01-todo.md) expands each gap; [plan 11](11-plan.md) orders it. Done: everything above.

- [ ] **Native platform validation:** macOS Apple Silicon (step 66) and native ARM hardware; Linux
  x86-64, 32-bit x86 and ARM under emulation done.
- [ ] **OTP compatibility:** upstream Common Test suites, wider differential comparisons (step 69).
- [ ] **Test migration closure:** retire adapters after equivalent public coverage (step 70).
- [ ] **Parse transform closure:** real projects and the user guide (step 73H).

Deferred, not required: dynamic modules (D01), atom collection (D02), behavior-changing attributes
beyond the selected slices (D03), public interchange (D04), stage readers (D05), C/FFI (D06),
project-schema extensions (D07). Every change keeps formatting, behavioral tests and the fresh
combined Debug quality gate without weakened thresholds or suppressions.
