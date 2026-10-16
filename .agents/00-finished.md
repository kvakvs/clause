# Completed implementation work

Compact record of completed work, one bullet per step, in plan order. Full step
text, criteria and evidence are in Git at the listed commits; current behavior
lives in `docs/`, architecture and file ownership in [arch.md](arch.md) and
[files.md](files.md). Gate counts, per-step dates and measurements are dropped.

## Before plan 11 (2026-09-17 to 2026-10-03)

- **Foundations:** CLI/CMake scaffold; C++23, warnings as errors, Clang
  toolchain; `clau`, independent `clause_runtime`, shared `clause_abi`; stages
  exchange owned data, stage readers only reserved directories.
- **Preprocessor 1–3:** parser/oracle boundary, source buffers and locations,
  Erlang lexer, form dispatch and recovery
  ([preprocessor](../docs/preprocessor.md)).
- **Preprocessor 4–6:** object and parameterized macros, overloads, recursion
  checks, rescanning, stringification from original tokens.
- **Preprocessor 7–9:** nested conditionals, include/include_lib lookup,
  contextual module/file/line/function/machine/OTP macros.
- **Preprocessor 10–13:** restricted condition evaluator, feature macros,
  warning/error directives, CLI options, OTP golden fixtures; focused OTP
  comparisons only, no full upstream Common Test run.
- **Parser 1–4:** grammar/oracle inventory, shared token mechanics, move-only
  AST arenas with checked IDs, transactional forms
  ([parser](../docs/parser.md)).
- **Parser 5–12:** literals, operators, calls, clauses, patterns, guards, maps,
  records, binaries, blocks, branching, receive, funs, try/catch, maybe,
  comprehensions.
- **Parser 13–18:** attributes, type/spec/callback declarations, recovery,
  `--parse-check`/`--print-pp`/`--print-ast`, differential coverage; 344 grammar
  productions witnessed, ten-source OTP corpus (syntax only).
- **Projects 1–22:** toml++ manifests (schema 1), targets, globs, selection,
  planning, `--new-project`; platform matrix not closed
  ([projects](../docs/projects.md)).
- **Test migration:** 16 native project suites moved to CLI fixtures, frontend
  to CLI/OTP projections and 900 mutations, runtime to a separately built
  consumer; ledger audit still open (step 70).
- **Compiler/runtime 1–8:** LLVM SDK discovery, owned results, target policy,
  verification, object emission, ABI v1, feature reporting
  ([compile](../docs/compile.md)).
- **Compiler/runtime 9–14:** runtime/context lifecycle, immediate terms, frozen
  builtin registries, lazy memory, scheduler bookkeeping.
- **Compiler/runtime 15–28:** declarations, subset checks, acyclic batch calls,
  symbolic types, independent inference, target-width integers, calls,
  descriptors, native registration.
- **Compiler/runtime 29–32:** bounded specialization with guarded variants (3
  per function, 32 per module, 2x growth), LLVM O0/O2, text/bitcode output.
- **Compiler/runtime 33–46:** artifact publication, options, project
  integration, `--verbose`, IR/type inspection, native harnesses, OTP
  comparisons, cross-target inspection, examples.
- **Pattern matching 1–3:** semantic matrix (81 signatures), revision-2 failure
  channel, runtime atoms and module bindings ([patterns](../docs/patterns.md)).
- **Pattern matching 4–12:** clause-local bindings, normalization, immediate
  matching, guard resolution and control flow, RHS-first body matches, stable
  heaps, rooted tuples/lists/strings.
- **Pattern matching 13–20 and 15a:** bignums, floats, exact-key maps,
  project-owned goldens, bitstrings, records, audited guard catalog (77 of 81
  rows), stress; 1.08M golden comparisons.

## Plan 11 (2026-10-03 to 2026-10-15)

- **1** refreshed the OTP reference (`maint-29` pin `21776803`) and baseline —
  `2d27503`
- **1A** fast/full test modes — `07d2c38`
- **1B** changed-scope quality check — `3cdc743`
- **1C** documentation check run — `e381a86`
- **2** six OTP-goldened program fixtures with a feature map — `1a006ac`
- **3** entry, argv and exit-status contract
  ([executables](../docs/executables.md)) — `af55175`
- **3A** escript compile mode — `8bb5f89`
- **4** runtime term printing and `erlang:display/1` — `d63132b`
- **5** generated startup object with `halt/0,1` — `6a246ae`
- **6** Clang-linked executables from positional CLI inputs, staged publication
  — `8f68c18`
- **6A** single project target linked with explicit `-o` — `0c27f72`
- **7** executables from project targets — `bcaa23f`
- **8** executable golden test runner — `736a194`
- **8A** classic process heap contract ([runtime-heap](../docs/runtime-heap.md))
  — `02ddf8d`
- **8B** binary cells split, off-heap binary list — `34005ee`
- **8C** parseable heap areas and a heap walker — `ad0f810`
- **8D** heap words admitted by header, not object index — `a0dbc00`, `0c76a8a`
- **8E** host terms held as raw words between safe points — `e5aa219`
- **8F** generated root frames on a process stack — `e99e4a8`, `b4c99d3`,
  `9573722`, `7ac15fb`
- **8G** one contiguous heap block plus fragments instead of chunks — `c162308`
- **8H** copying Cheney collector with ERTS sizing on explicit request (also
  step 25) — `d0cf00b`
- **8I** heap rework closed; per-context footprint 66 KB to 2.4 KB — `dc6e349`
- **9** `begin`/`end` blocks and `case`, with `case_clause` — `60102a1`
- **10** `if` with `if_clause` — `1982697`
- **11** `error`/`exit`/`throw` from source — `80cd244`
- **12** `catch Expr` — `328a290`
- **13** `try ... of ... catch` — `27867ed`
- **14** `try ... after` — `9df4e63`
- **15** stack traces (8 frames), `Class:Reason:Stack`, `erlang:raise/3` —
  `c311ae7`
- **16** `maybe` with `else_clause` — `aecbff2`
- **17** frame and continuation model: explicit frames on a flat process stack,
  `musttail` transfers ([execution-model](../docs/execution-model.md)) —
  `5447a16`
- **18** recursive call graphs accepted in analysis — `0e6eb6d`
- **19** proper tail calls — `eff731f`
- **20** deep non-tail recursion — `26f8b27`
- **21** list comprehensions — `07d249a`
- **22** binary and map comprehensions with OTP generator/filter errors —
  `1f58ba4`
- **23** root inventory extended to the execution model — `a3ef043`
- **24** collection triggers and safepoints decided — `cf9d82b`
- **26** collection from generated code — `0a94ccf`
- **27** heap exhaustion reported as `out_of_memory` — `bcd1ed5`, `8e939f1`,
  `3a21bc0`
- **27A** runtime-wide memory limit; optional
  `--max-heap`/`--max-stack`/`--max-memory` — `6a87dae`
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
- **41A** `erlang` builtins get `runtime/src/builtins/erlang/`, one file per
  family ([builtins](../docs/builtins.md)) — `9e22ff1`
- **41B** `erlang` exports via generated stub `erlang.erl` and
  `library/erts/erlang.terms` (351 exports), OTP auto-import table — `8c7cb84`
- **41C** process dictionary — `3fec7f8`
- **41D** binary, atom and number conversions — `efa7d4c`
- **41E** external term format, byte-exact and fuzzed (found two decoder bugs) —
  `da8118c`
- **41F** time, unique values, timers (timer messages kept in a `MessageBuffer`)
  — `f9579d5`
- **41G1** process and system introspection — `12a2838`
- **41G2** spawn options, spawn requests, aliases, hibernation — `752c5fc`
- **41G3** fun information, hashing, MD5 — `a0a9bc9`
- **41H** non-distributed node and code-loading answers; phase gate left only
  non-`erlang` modules for luerl — `89d0c7d`
- **41I** replacement-module contract: Clause modules win over
  `source_search_paths`, missing modules are reserved names
  ([library](../docs/library.md#runtime-module-replacements)) — `8c6a94f1`
- **41J** library split into `library/{erts,kernel,stdlib}/` with
  `modules.terms`; `{Name, Arity, Group, Status}` inventories checked against
  OTP 29 — `b284a034`
- **41K** provided runtime modules discoverable, all-stub replacements of
  preloaded modules (`atomics`, `zlib`, `init`, ...); stubs raise
  `{not_implemented, F, A}` — `347ae0ad`
- **41K1** OTP's exported types in `<module>.types.hrl` headers — `4810de56`
- **41K2** remote types of ignored modules warn and are `term()`; luerl compiled
  and linked — `4810de56`
- **41L** `init` without a boot script
  ([builtins](../docs/builtins.md#init-module)): argv/home once at startup,
  `stop`/`reboot` halt, `boot`/`restart` `notsup`; also `lists:filtermap`,
  `uniq` — `377c2228`
- **41N** handle references (off-heap buffer in a refc layout); `atomics`,
  `counters`, `erts_internal`, `erts_code_purger` — `f7d42c4c`
- **41O1** `unicode_util` tables generated from host OTP answers (573 KB C++),
  all 1.1M code points match — `07a945b4`
- **41O2** `unicode_util:gc/1` UAX #29 clusters and NFD/NFC/NFKD/NFKC —
  `84ec8037`
- **41P** files: Clause `file` is the file layer, OTP `filelib`/`filename` run
  on it; `erl_prim_loader` over `file`; link, cwd, copy operations — `92980354`
- **41Q** `zlib` 1.3.2 linked, streams as handle references, compressed
  `term_to_binary` — `dece5234`
- **41R** networking: Clause `gen_tcp`/`gen_udp`/`inet` are the layer; `inet`
  address parsing, `ntoa`, `getaddr`, `gethostname` — `a99595e8`
- **41ZA** PCRE2 10.47 dependency (`cmake/Pcre2Dependencies.cmake`), provided
  `re` stub module — `26a04085`
- **41ZB** `re` over PCRE2: C++ `compile`/`run` plus Erlang `re.erl` loop,
  replace and split; luerl `os.date` — `b17b2a09`
- **41S** `prim_eval:'receive'/2` over `clause_receive` builtins; fixed receive
  scan position after a bad timeout; luerl driver prints OTP output with no
  stand-ins — `9d3f3c17`
- **41T** `lists` data functions as runtime builtins (`sublist`, `flatten`,
  `usort`, `keymember`, ...); fun-taking ones stay Erlang; tracer fix of
  `6a7d402a` — `f6a53456`
- **41U** `io:format/3`, `fwrite`, `put_chars/2`, `nl` builtins; pid or name
  devices go through `file:write/2` ([io](../docs/io.md#calls)) — `cd08f1a8`
- **41V** `math` as builtins over the C library (`library/stdlib/math.erl`
  stubs) — `42acff89`
- **41W** `binary` search, split, parts, hex builtins; `replace` in Erlang over
  them — `82292d0a`
- **41X** `os` env/getpid builtins and `maps` get/merge/take/with builtins;
  Erlang `os:timestamp`, `maps:filter`/`map` — `c8de2662`
- **41M1** runtime-wide persistent term table; reads copy into the reader's heap
  ([runtime](../docs/runtime.md#persistent-terms)) — `dd5b4b3c`
- **41M2** `persistent_term` `put/get/erase/info` builtins keyed by `phash2`
  hash — `65bde218`
- **41Y1** `unicode:characters_to_binary/list` for latin1/utf8/utf16/utf32 —
  `e942687b`
- **41Y2** `file:read_file_info/1,2`, `write_file/3`; `write` plus `append` no
  longer truncates — `b0030e9b`
- **41Z** `code` answers for the linked program without a code server —
  `d911b78d`
- **41ZC** OTP I/O protocol on stdin and file servers (`get_until`,
  `io:request/1,2`, `requests`); luerl euler prints OTP output
  ([ports](../docs/ports.md#standard-io-and-files)) — `31b20d73`
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
- **58A** inference fact domain ([semantic](../docs/semantic.md#inference)) —
  `e52277c`
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
- **58N1** dependent facts of `case`/`if`
  ([semantic](../docs/semantic.md#dependent-facts)) — `e63e14c`
- **58N2** dependent facts through operations and bindings — `60a5dd4`
- **58N3** dependent facts of tuple scrutinees and `try ... of` — `175b545`
- **58O** fixed-point rounds measured (`--print-stage-times`) — `c712d605`
- **58P** non-converging groups remembered, given `term()` at once — `1ebaca6d`
- **58Q** monotone rounds; a member flipping back to a recent summary gives up
  alone — `fa55a8d5`
- **58R** a round re-walks only members whose inputs changed — `2a3059ba`
- **58S** joins monotone at the container depth limit; luerl inference 330 s to
  107 s — `b5aec11f`
- **58U** `cache_inference_in_dirs` option, implicit `*/stdlib`, `*/kernel`, ...
  ([projects](../docs/projects.md#inference-cache)) — `5490832e`
- **58V** a module's inference results as bytes — `d8b26d1b`
- **58W** cached results of covered modules reused; luerl warm recompile
  inference 180 s to 40.5 s — `ff31997f`
- **59** O2 specialization removes real tag/shape checks — `8a5ace2`
- **60** source-level debug information (`-g`) — `116cff1`
- **61** opt-in profiling (`--profile`) — `ecb8de5`
- **62** link-time optimization (`--lto`, MSVC and ELF only) — `5ec3645`
- **62A** hashed code-server lookups — `aa0198b`
- **62B** receive timers in a timer wheel — `eccd7c4`
- **62C** export liveness: executables drop functions the entry cannot reach;
  `--keep-exports` opts out
  ([executables](../docs/executables.md#export-liveness)) — `fbfe99c`
- **62D1** `build_dir`/`--build-dir`, Clause temp files below `<build_dir>/tmp`
  — `0cd45db`
- **62D2** cache store for parse transform replies and compile outcomes,
  `--no-cache` — `8e71e4d`
- **62D3** directory scan cache measured, no code — `d5a655f`
- **62D4** whole-batch compile result cache — `176c4dd`
- **62D5** SQLite 3.54 cache backend (2,000 puts 12 s to 250 ms)
  ([compile](../docs/compile.md#compile-cache)) — `f4d29b9`
- **62E** in-memory index of searched directories (15,215 probes to 0; luerl
  warm build 3.8 s to 1.65 s) — `0e8a6c5`
- **63** Linux x86-64 validation (WSL2) — `5948a44`
- **64** 32-bit x86 runtime validation (Windows x86, Linux i386), cross-linked
  goldens — `9eef9cc`
- **65** Linux AArch64 and 32-bit ARM under qemu-user — `ab5943b`
- **65A** `-behaviour` with OTP callback warnings, generated `behaviour_info/1`
  — `e3f7a89`
- **65B** generated `module_info/0,1`, informational attributes kept — `86d84aa`
- **65C** `-import` of any module, compile hints ignored, rejected attributes
  named — `12dc2d5`
- **67** compiler and frontend sanitizers — `58b3243`
- **68** ThreadSanitizer on the multi-worker runtime; fixed a shutdown
  use-after-free and a lock-order inversion — `9679aea`
- **69A** manual benchmarks against OTP (`benchmarks/`, 6 programs, 29 kernels);
  first run Clause O2 20.2x slower than OTP JIT
  ([validation](../docs/validation.md)) — `0777b693`
- **73A** parse transform contract ([transforms](../docs/transforms.md)) —
  `57d4193`
- **73B** owned abstract term model with ETF and `.abstr` codecs — `a3f66ab`
- **73C** modules exported as abstract forms (`--print-abstr`) — `9a9f9aa`
- **73D** abstract forms imported into the AST — `0ebf90e`
- **73E** transforms run through the host OTP loader — `13a5e20`
- **73F** transforms applied before lint in the compile pipeline — `1c92c5c`
- **73G** precompiled and project transform modules — `d1ce36a`

## Plan 20 (2026-10-12 to 2026-10-15)

Optimizations from OTP T2-Full ideas; open steps are in [20-opt.md](20-opt.md).
Speedups are against the previous step unless noted; the luerl euler run went
from 21 s (O2, before plan 20) to 0.59 s (OTP 0.35 s).

- **Plan** `.agents/20-opt.md` from garazdawi/otp PR #855 plans — `4438fde`
- **OPT0.1** inline frame transfers, `FrameState` in every context (ABI 9) —
  `3c6cecc`
- **OPT0.1A** leading arguments and results in registers, `run_code` (ABI 10) —
  `11439308`
- **OPT0.1A** `preserve_none` Code on x86-64/AArch64 with Clang (ABI 16);
  prologue 8 pushes to 1 — `21ba013d`
- **OPT0.1A** six register arguments on every target (ABI 17); euler O2 4 s to 3
  s — `a89ce357`
- **OPT0.5A** maps over 32 entries are off-heap B+ trees, O(log n) updates —
  `ed3ccc2`
- **OPT0.5B** compiler-proven in-place updates of fresh single-read maps
  (`semantic/map_ownership`) — `04043339`
- **OPT0.2A** fixed `false`/`true` atom words, inline exact equality and
  tuple/cons match shapes (ABI 11); euler O0 74 s to 12 s — `6897efd4`
- **OPT0.2B** inline type tests, comparisons, `element/2`, `hd/tl`, small
  `band`/`bsr`/`div`; 69A 1.13x vs `31b20d73` — `fd5d18df`
- **OPT0.3** inline bump allocation of tuples and lists up to 64 words (ABI 12);
  euler O2 8 s to 6 s, 69A 1.58x — `46642856`
- **OPT0.8** cached atom words, fixed `ok`/`error`/`undefined` (ABI 13); euler
  O2 6 s to 4 s — `1286ae77`
- **OPT0.9** word-sized integer bit segments, no multiprecision; `lists:sum/1` —
  `fdd8a7e3`
- **OPT0.2C** inline `setelement/3`; euler O2 problem_009 3.6 s to 2.35 s —
  `52ad1da4`
- **OPT0.4** per-module literal pool of read-only heap cells (ABI 14); 69A
  1.05x, 9.0x behind OTP JIT — `ece1ee6b`
- **OPT0.6** appendable binaries, spare room 2x; 100k one-byte appends quadratic
  to 190 ms — `8bb121d5`
- **OPT0.7** reductions at comprehension loop heads, inline `slow_collect` test
  — `816e8150`, fix `8525b3e9`
- **OPT0.5** word-level small-map updates and lookups, no `Term` staging;
  20-entry update 2.1x (shared-keys layout moved to OPT1.6) — `1cd369c7`
- **OPT0.2D** inline `is_binary/1`, `bsl`, `self/0` (ABI 15) — `4c6e9223`
- **OPT0.3A** inline `#{...}` construction with atom/small-integer literal keys
  — `e563d60d`
- **OPT1.2 (part)** no eager root stores; only values crossing calls get a slot;
  sum loop 60 to 37 ms (OTP 26 ms) — `3f74f220`
- **OPT1.4A** inline binary match start/finish and constant whole-byte reads;
  400 KB sum 1.42 to 1.16 s — `2c04f3d5`
- **OPT1.4B** sub-binary cells, inline `Rest` (ABI 18); sum loop 1.16 s to 58 ms
  (OTP 2.7 ms) — `22e8baca`
- **OPT0.10** inline list generators, one-reservation comprehension reverse;
  comprehension 88 to 3.2 ms — `cb38203c`
- **OPT0.11A** inline heap binaries for constant integer segments; `<<N:32>>`
  151 to 8 ns — `a8c01c37`
- **OPT0.11B** inline appends, append state in the refc cell (ABI 19); append
  277 to 16 ns — `4b755cf6`
- **OPT0.12** cheap term access in builtins; length of 10 cells 1122 to 111 ns,
  69A 1.27x — `52925127`
- **OPT0.13** inline map generators, cheaper tree-map updates (ABI 20); 1M tree
  updates 1446 to 398 ms — `08414232`
- **OPT0.14** inline container inspections, word-level float/binary ordering;
  euler O2 2.08 to 1.13 s — `927416b8`
- **OPT0.15** raw dictionary builtins and inline float arithmetic; euler O2 1.13
  to 0.96 s — `8e2b4223`
- **OPT0.16** inline math (`sqrt` intrinsic, C-library calls for the rest);
  euler O2 0.96 to 0.91 s — `3a741dfd`
- **OPT0.17** inline closures and word-level apply (ABI 21); euler O2 0.91 to
  0.70 s — `6437c193`
- **OPT0.18** literal binaries up to 64 bytes, inline `make_tuple/2`; euler O2
  0.70 to 0.62 s — `b1a038b3`
- **OPT0.19** lazy worker wakeups; ping_pong 9.4x faster (= OTP), ring 4x —
  `95396c04`
- **OPT0.20** amortized binary collections; large_read 5.9x, map_update 3.9x —
  `f6fcb95a`
- **OPT1.1A** self tail calls keep their frame; counting loop 35% faster, euler
  O2 0.62 to 0.59 s — `cb5b54fb`
- **OPT1.1B** tail calls between members of a tail-call cycle keep the caller
  link and shared term slots; even/odd 16%, euler O2 0.565 to 0.540 s —
  `9dfdc244`
- **OPT1.2 (part 2)** arguments stored into their slots only where they cross
  a call or safepoint; self loop 8%, list sum 7% — `3d23d818`
- **OPT1.3 (part)** inline `+`/`-` on tagged words with a sign-test overflow
  check, no double width; hash 22%, count_up 19%, fib 15% —
  `93981174`
- **OPT1.4C** binary loops: a `<fun>.loop` variant updates one fresh
  sub-binary cell in place across self tail calls; binary_match 8.5x —
  `c45402af`
- **OPT1.4C (part 2)** the variant reads its parameter's view once per step and
  skips its bitstring test; sum_binary 5.2 to 3.7 ms (OTP 2.7) —
  `5fa0c26c`
- **OPT0.20B** generational collection: safepoints run minor collections that
  promote objects below the high-water mark into an old area and leave it in
  place (ERTS sizing rules, Debug old→young check); 69A geomean 1.15x (sort
  1.29x, binary_append 1.6x, map_put 1.25x, comprehension 3.3x) —
  `4635bb64`
- **OPT0.20C** `min_heap_size` applied (`spawn_opt`, `process_flag`,
  `--min-heap-size` as `+hms`): the first block and every collection floor, so
  a process trades memory for fewer collections the OTP way —
  `0afd29b1`
- **OPT0.26** word-level `term_to_binary`/`binary_to_term`: small integers,
  atoms (cached), lists and tuples as words, one factory per decoding; encode
  3,066 to 558 us, decode 1,084 to 550 us on the 69A term —
  `[plan20] OPT0.26 ...`
- **OPT0.21** literal list prefixes, raw
  `integer_to_list`/`list_to_integer`/`atom_to_list`; int_to_list 2.1x —
  `e6d4fa94`
- **OPT0.22** word-level `list_to_tuple`/`tuple_to_list`; 1000 elements 16.7 to
  9.6 us — `b564c56b`
- **OPT0.23** flat hash index for message copies; 100k-element echo 34 to 17.5
  ms — `b192769b`
- **OPT0.24** buffered sends to running processes; spawn plus send 21 to 13 us
  per process — `a36ed84a`
- **OPT0.25** per-worker run queues with stealing and neighbor balancing; fan_in
  1.7-3.8x — `904e9cd1`

## Library gaps (luerl, 2026-10-14 to 2026-10-15)

Compiling and running luerl (`tools/luerl-trace/` driver, 35 Lua snippets) found
library functions that were missing or wrong; all are fixed.

- **Ledger of 25 rows** (in order of first use) closed by plan 11 steps
  41T–41ZC, each step's luerl reason below.
- **41T** `lists` stubs (`sublist` for every token, `droplast`, `mapfoldl`,
  `splitwith`, `nthtail`, `flatten`, `duplicate`, `max/min`, `umerge`, `usort`,
  `partition`, `join`, `keymember`, `keysearch`).
- **41U** `io:nl/0`, `fwrite` (`print` crashed before its newline).
- **41V** `math` NIFs (`pow` for `1e20` exponents and `^`, `sqrt`, `sin`, `exp`,
  `log`, `acos`, ...).
- **41W** `binary:match/part/split` (`string.find`/`sub`/`format`).
- **41X** `os:timestamp` (`os.clock`), `maps:get/3`, `is_key`, `remove`.
- **41M1/41M2** `persistent_term:get/put` (reached via `erl_features` from
  `io_lib` atom quoting in Lua syntax errors).
- **41Z** `code:ensure_modules_loaded` instead of the absent `code_server` on
  `io_lib:format`'s error path.
- **41L** `init:get_arguments/0`, `lists:filtermap`, `uniq`
  (`erl_features:init_features`); Lua syntax-error text now equals OTP's.
- **41Y1/41Y2** `unicode:characters_to_binary/2`, `file:read_file_info`,
  `write_file/3` (`dofile`/`loadfile`).
- **41ZA/41ZB** `re:run/2` over PCRE2 (`os.date`), `re:replace`.
- **41ZC** `io:request/2` `get_until` on file pids (every `luerl:loadfile`
  failed with `{not_implemented,request,2}`).
- **Driver result** after 41S: 34 of 35 snippets print OTP's output; `goto` has
  the same `function_clause` with frames lacking arguments and locations
  ([differences](../docs/differences.md)); `coroutine` is nil on both.
- **`examples/euler`**: all 10 problems print OTP's output; speed went 78 s (O0)
  / 21 s (O2) to 0.62 s (O2, OPT0.18) against OTP 0.35 s; sampling found
  inspection, boolean and atom lookups, `bits_service` and `get/1` hot
  (OPT0.2–0.18).
- **`examples/benchmark`**: `--entry benchmarks:run` took `run/1`; the `/ARITY`
  suffix (`--entry M:F/0`, [executables](../docs/executables.md)) fixed it; all
  22 suites print OTP's 158 lines (Clause O0 1.8 s, OTP 0.55 s).

## Outstanding work to finish

[The backlog](01-todo.md) expands each gap; [plan 11](11-plan.md) orders it.
Done: everything above.

- [ ] **Native platform validation:** macOS Apple Silicon (step 66) and native
  ARM hardware; Linux x86-64, 32-bit x86 and ARM under emulation done.
- [ ] **OTP compatibility:** upstream Common Test suites, wider differential
  comparisons (step 69).
- [ ] **Test migration closure:** retire adapters after equivalent public
  coverage (step 70).
- [ ] **Parse transform closure:** real projects and the user guide (step 73H).

Deferred, not required: dynamic modules (D01), atom collection (D02),
behavior-changing attributes beyond the selected slices (D03), public
interchange (D04), stage readers (D05), C/FFI (D06), project-schema extensions
(D07). Every change keeps formatting, behavioral tests and the fresh combined
Debug quality gate without weakened thresholds or suppressions.
