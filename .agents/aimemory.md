# Project memory — compacted 2026-10-14

Plan history: .agents/11-plan.md (compact step records), .agents/00-finished.md
(archive), docs/validation.md (evidence history). This file keeps only durable
gotchas and user directions not recorded elsewhere.

## User directions (keep)
- Minimal first, iterate later; no defenses for impossible cases (classic ERTS
  trust model). Reject over-engineering.
- ERTS host model: no handle table; Term valid only in own process, read-only
  elsewhere. One heap block per process.
- Binaries > 64 B are std::shared_ptr buffers outside heaps; prefer C++ style
  designs.
- No hard memory cap by default (heap, stack, binaries, contexts); caps are
  opt-in options only. Atoms 2^20 default, `--max-atoms` <= 2^26.
- Ports must exist: sockets, file I/O, subprocess stdin/stdout are ports
  (backlog F35). Ports are not scheduled entities (I/O threads + synchronous
  port ops); ERTS-like port tasks only if user asks.
- Long-running builtins run in interruptible portions (call_builtin_portion,
  step 43A).
- Test iteration counts: just large enough to prove the property.
- Test/gate time: per step fast CTest + check-quality; full CTest only at
  phase/major completion, INSTEAD of fast. Fast gate must stay fast (user
  2026-10-17): slow workflows are full_only, check-quality tidies one unit per
  changed header (dependents in check-quality-all); fast CTest ~75 s at -j 16. Tidy default jobs = half the logical
  cores. Keep slow tests parallel. Keep 20-25% CPUs free.
- Tidy/Lizard run on the Windows dev host only; other platforms build+test.
- All ABI symbol/namespace versions collapse to v1 in plan step 78A (never
  released).
- 78B: rename runtime/src/builtins files to builtin_<name>.
- External Term Format code is shared (abi/include/clause/abi/external_term.hpp
  Writer/Reader); never reimplement it.
- Inference: guards/patterns define a function's entry domain; uses narrow
  operands after them. Specs are errors only when DISJOINT from inferred facts.
  Printing: `_` for term(), N..M ranges, argument relations as variable names /
  _argumentN, records as records; case/if/try-of values are dependent facts
  printed as $case_of_operator etc.
- OTP small-map iteration order follows atom index: leave Clause's key order
  (recorded in docs/differences.md).
- --entry may name FUNCTION/0. source_search_paths take */** patterns, searched
  before the library.
- Testing-off wrappers (clau.bat, make-build.bat, run-macos.sh) build in
  build/tool so build/debug keeps tests ON.
- Don't stage user's test1.toml. Preserve user's uncommitted AGENTS.md edits.
  Stale IDE buffers may revert plan edits.

## Host and toolchain
- Repo F:/CLAUSE/clau (uppercase F: matters for programs.py <fixture>
  substitution and expectation runners: use absolute
  F:/CLAUSE/clau/build/debug/bin/Debug/clau.exe). build/ is wiped by
  make-clean.bat: helper scripts live in the session scratchpad.
- Toolchain LLVM 23.1.2 SDK, VS 18 x64, SDK 10.0.26100, /MT, Ninja, Boost 1.90,
  toml 3.4, Lizard 1.24, tidy 22.1.8. Fresh configure MUST pass
  %CLAUSE_CONFIGURE_ARGS% from tools/windows-toolchain.cmd (BUILD_DIR set): else
  VS generator + cl (C2220) or GNU clang++ (nested test caches break,
  Unicode-path tests fail). Never pass LLVM_DIR. Switching compiler needs rm -rf
  of the build dir. A cmake run without vcvars can wipe the cache.
- Check `grep BUILD_TESTING build/debug/CMakeCache.txt` before trusting a fast
  run (IDE CMake Tools may reconfigure OFF; ctest then runs stale exes). First
  run after runtime edits may time out while nested consumers recompile. Don't
  edit runtime sources while a gate runs. A killed session can leave a gate
  running: check ctest/cmake/ninja.
- cmd scripts: never pass `|` in arguments (pipe): use env vars (TESTRE) or
  ctest -I numbers. cmd /c needs a full .bat path. Write .cmd files with Write
  (printf/heredoc eat backslashes).
- Bash heredocs mangle `\`, `\n` (become real newlines in C++/Python literals),
  non-ASCII, and many quotes/apostrophes ("unexpected EOF"). `cat > file`
  without heredoc hangs. Write scripts with Write into scratchpad; use Edit for
  lines with escapes. Edit tool strips a trailing space at end of new_string:
  never end old/new mid-line before a quoted argument. Edit may write CRLF into
  .py: normalize. Many worktree files are CRLF (autocrlf=input): keep endings.
- Python on Windows: read_text defaults cp1252 (pass encoding="utf-8");
  write_text writes CRLF (use write_bytes); console capture doubles CR; 3.14
  needs sys.set_int_max_str_digits(0) for huge ints; difflib hangs on huge diffs
  (use GNU diff). No make; gmake at C:/Strawberry/c/bin. Strawberry cmake/ninja
  first on PATH is fine.
- Never `git checkout <file>` to drop temporary instrumentation from a file with
  uncommitted work.
- LLDB (C:/Program Files/LLVM/bin/lldb.exe) works with our PDB:
  `expr -- clause::runtime::debug_erlang_stack()`.
- WSL Ubuntu 25.04: passwordless sudo, cmake/ninja/qemu-user-static/debootstrap; Debian trixie chroots
  /srv/clause-{i386,arm64,armhf}; sync via Windows git.exe diff + rsync (Linux git over /mnt/f stalls); SDK ld.lld
  needs Ubuntu 22.04 ICU70 libs (~/clau-hostlibs); SDK lldb needs libpython3.14 -> use GDB. ARM: host clang --target +
  sysroot, run via binfmt qemu (compiling under qemu is 20x slow). qemu-user posix_spawn never reports ENOENT.
  POSIX syntax check: clang++ -std=c++23 -fsyntax-only -Wall -Wextra -Werror -Iruntime/include -Iruntime/src
  -Iabi/include -isystem thirdparty/boost_1_90_0. Sanitizers: abort on impossible allocations; TSan defines operator new
  (lifecycle_failure, heap_measurements don't link); CLAUSE_TEST_TIMEOUT_SCALE 10/20.
- Windows x86 goldens: runtime-only build/x86 under vcvarsamd64_x86 with clang-cl,
  -DCMAKE_{C,CXX}_COMPILER_TARGET=i686-pc-windows-msvc (not -m32 flags: they drop /EHsc) and
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded (default MDd fails every exe with exit 3 and pops CRT assert
  dialogs); then cross.py --target-triple i686-pc-windows-msvc --suffix .exe. 2026-10-19: bit_map_comprehensions
  (negative map key order) and ets_match_specs fail on x86 at HEAD 64b19ee9 already.
- erlang_gaps O0 hangs ~1/40 runs with the Debug runtime (also with a0f77e08 codegen; Release runtime 0/80):
  an existing runtime race, seen as a 60 s fast-gate timeout under load (2026-10-20).
- MSVC cl (non-clang-cl): Boost.Parser C4702 and LLVM-header C4244/C4267
  disabled on clause_parser_dependency / clause_llvm_sdk for cl only; C4554
  false positive on static_cast<Word>(n - 1) << shift: hoist to a local.

## Quality (tidy/Lizard)
- clang-tidy may crash (0xC0000005/0xC0000409) or exit 1 silently with 2 jobs:
  rerun with one job. Check targets do not forward QUALITY_JOBS: `cmake
  -DQUALITY_SCOPE=all -DQUALITY_BUILD_DIR=<b> -DQUALITY_NINJA=<n>
  -DQUALITY_JOBS=1 -P cmake/CheckClangTidy.cmake`; shard with
  -DQUALITY_SHARD=K/N, batches QUALITY_BATCH. Root/runtime/compiler CMakeLists
  edits select all units (~20 min). Single-file tidy -p build/debug/quality
  works for runtime units only. CLAUSE_QUALITY_BASE overrides HEAD for changed
  scope. Lizard: .venv-quality python -m lizard -C 10.
- Windows tidy HeaderFilterRegex never matched backslash paths; libstdc++
  exposes unchecked-optional findings MSVC hides.
- Recurring tidy findings and fixes:
  - bugprone-easily-swappable-parameters: adjacent same-type params (Word/size_t
    are one type; Term/Term; Id/Id) not used in one call -> wrap in a small
    struct (+ Argument<> specialization for builtins) or use them together.
  - bugprone-unchecked-optional-access: `opt->` and `.value()` flagged; test
    first.
  - bugprone-exception-escape: never move/return structs holding std::map by
    value (MSVC move may throw): emplace in place, extract node handles, or
    copy.
  - bugprone-unused-return-value: discarded std::expected (static_cast<void>
    too).
  - clang-analyzer ArrayBound false positives in LLVM headers: avoid
    getCalledFunction, PHI getIncomingBlock / incoming_values,
    LoadInst::getPointerOperand, StoreInst::getValueOperand (walk use lists);
    IRBuilder::CreateICmp (use builder.Insert(CmpInst::Create(...))); Boost
    cpp_int `x + var` (fixed +1/-1 helpers).
  - EnumCastOutOfRange from MSVC <filesystem> file_size/last_write_time: use
    llvm::sys::fs::status.
  - string_view::data() to C APIs flagged: take const std::string&.
    `new GlobalVariable` in a local = leak: use getOrInsertGlobal. No int-to-ptr
    casts: typed pointer fields. -Wmissing-designated-field-initializers: static
    factory helpers. Lizard/tidy complexity: dispatch tables for op switches.
- ADL traps: local `advance`/`quoted`/`accumulate` resolve to std::/llvm:: ->
  rename.
- clang-analyzer (OPT3.5B): a move-only owning handle passed BY VALUE trips
  NewDelete "use after release" (MSVC callee-destroyed args): pass `T&&`. A
  pointer pushed into a std::atomic list by CAS reads as a leak: do the CAS in
  an out-of-line function (opaque to callers' TUs). bugprone-easily-swappable
  fires when two same-type params stop appearing in one expression.
- check-quality's changed scope is `git diff HEAD`: `git add` new files first or
  they are skipped. abi/ header edits do not pull their runtime dependents into
  the tidy scope; CMakeLists edits select all units.

## OTP oracle, goldens and fixtures
- OTP pin and refresh: docs/otp-reference.md. Pin d199a49a (29.1.2), oracle
  install 29.1.1 / ERTS 17.1. Pin change needs regenerate.py of
  executables/lint/programs/printing (diff = revision lines only).
- Escript: "C:/Program Files/Erlang OTP/erts-17.1/bin/escript.exe" is reliable
  (bin/escript.exe once segfaulted; later worked). Probes: erl.exe -noshell
  -eval 'c:c(m), m:main()' in scratchpad; delete erl_crash.dump. Windows re-sets
  ProgramFiles for children: tests use --erl none. erlc needs absolute -pa.
- Normal build/test is OTP-free; OTP source never committed (observations only).
  Fixtures written as UTF-8 LF bytes.
- Fixture layout: project.toml, src, fixture.json (entry+argv, env, stdin, data files), expected/{stdout.txt,
  golden.json}, compile.txt. Golden runs may be `"authored": true`, may mark word_bits 64. Runner runs each
  combination in work/<label>. programs.py --update-diagnostics rewrites compile.txt.
- Corpus edits: change fragments + generated calls, then fixtures.tsv/manifest
  hashes (corpus.json row sha256 = LF digest; records input_manifest_sha256 =
  digest(corpus.json)), then regenerate.py --corpus X --check.
- Oracle uses warnings_as_errors: needs -compile(nowarn_deprecated_catch /
  nowarn_shadow_vars); constant folding of literal calls (apply(erlang, abs,
  [a]), pid_to_list(self)) and statically wrong calls fail -> route through
  id()/helper; unexported own `fun m:f/1` rejected; unused named-fun names warn.
- OTP printing: display = erl_printf_term.c (not ~w), latin-1 bytes for chars
  128-255 (avoid such strings; keep fixtures ASCII, erlfmt rewrites UTF-8
  literals); maps print/iterate in atom-index order (unstable: use integer keys,
  ~kw, display fields or single-field native records); uncaught-exception report
  prints ~w. OTP crash reports go through async logger: oracle usually halts
  first, author stderr. phash2 differs: digest with md5(term_to_binary).
- OTP race quirks: monitor/link after spawn of a short process races -> spawn_monitor; check goldens with --check 3x.
  Busy-port senders resume in any order. -eval process traps exits: probe inside spawned processes. Oracle runs
  entries from an escript while init still boots (shape-only checks for init status/arguments). init:request/1
  takes any pending {init,_} message.
- OTP compiler quirks: drops code after spawn(fun/1) (route fun via remote
  id/1); turns calls to never-returning functions into tail calls (trace
  fixtures need returning functions, non-tail sites); auto-imports setelement/3,
  tuple_to_list/1, list_to_tuple/1 but not make_tuple. OTP lint rejects
  `X =:= 0.0`. Never probe huge atomics:new arities; ~.*c with -1 hangs OTP.
- Reserved words unusable as fixture atoms/modules/functions: after, receive,
  case.
- erlfmt: thirdparty/tools/erlfmt/_build/local; helper escript (scratchpad
  fmt.escript) does code:add_path + erlfmt:format_file and writes
  unicode:characters_to_binary back. Cannot parse `-record #r{a, b}.`, `#div`,
  `#m:r`, `f(...)(2)` (parenthesize); crashes on non-ASCII atoms (format by
  hand).
- "Unsupported feature" placeholders in tests: currently `fun erlang:apply/2`
  ([dynamic calls]); swap when admitted.

## Compiler internals
- Fast matrix: O0 positional + O2 (spec on) project; full = 8 combinations.
  Project rule: link iff not frontend action AND (manifest output|entry or CLI
  -o|--entry). -Os = O2 pipeline + optsize. -g implies annotate_source.
- Bindings: exported names share one BindingId across clauses; BindingIds are
  per-function, so Facts leaving a function must be `erased`. lower_match_plan's
  bind uses emplace: restore state.bindings before rematching. Named fun name
  lives only in FunScope::inside. A clause with neither pattern nor guard needs
  an explicit br. Module atoms must include atoms codegen lowers implicitly
  (infinity/true for receive).
- maybe keeps ast::MaybeMatch (use semantic::maybe_operands). record_info/2 is a
  compile-time pseudo-function; a local record_info/2 definition is still
  indexed. behaviour_info/1 hidden from own source (semantic::source_function).
  module_info/0 must not call module_info/1 locally; predefined functions get no
  debug scope.
- Frames: lower_frames post-pass (idempotent); a native function without
  `erlang-arity` stays a native call. Inline heap reads must be stored into
  root_slot (raw spills go stale after GC). Proofs read Inference::inputs, never
  Summary entry domains. ExpressionLowering positional init: 7th field is
  `values`.
- Liveness only for executables; dead functions emit name-only exports. Builtin
  tail-continuations must be listed in semantic/liveness.cpp
  BUILTIN_CONTINUATIONS. referenced_modules skips catalog builtin calls.
- Inference: solve 16 rounds then widened; expression facts erased per round;
  per-call re-analysis has its own work pool. Expectations:
  tests/compiler/inference/expectations.py <abs clau> <fixture> [--record].
- Adding a FeatureId needs tests/abi/features.cpp names snapshot +
  codegen_placeholders CASE for deferred features. ABI version bump needs
  cross_targets.py and linking/startup.cmake descriptor strings.
- Build cache: SQLite build/cache/v2 (lazy open; only put creates the db). Tests
  running manifests inside tests/fixtures pass --build-dir into their work dir;
  --verbose compile checks use --no-cache; a rebuilt clau misses every entry
  (warm up Release timing runs).
- Inline codegen (OPT0.2/0.3): join values with `join_values` (SSAUpdater; PHI
  addIncoming trips clang-analyzer). Block/value names must avoid float-flag
  words (`fast`, `nnan`, ...: patternmatch_floats greps IR). Every inttoptr must
  be an instruction named `proven.cell*` (facts.py): never form pointers from
  constant words (they fold into constant expressions); constant words are
  immediates anyway. Inline pointer results go through a root-slot store.
- OPT1.7/1.8 (2026-10-20): loops building a reversed accumulator lose to body
  recursion when a heap-check safepoint collects mid-loop (the live accumulator
  is copied); body recursion allocates its result with no safepoint. List loops
  use loop_reduction only (collect at yields); comprehensions still check. A/B
  codegen with one clau via a temporary env switch; time each kernel in its own
  process (heap state leaks between kernels), min and median of 300 runs.
- `codegen_service_*` fault tests reach services only where codegen still calls
  them: their fixtures route type tests through `abs/1` and the consumer runs
  under a heap budget (inline allocation off).

## Runtime internals
- Term::bit_slice, is_function, list_elements, with_tuple_element,
  without_map_entry, with_existing_map_entry are declared but NOT defined (link
  errors): walk lists by hand, rebuild via tuple()/map_entries +
  TermFactory::map, slice via CLAUSE_bits_v1. bits service `part` writes TWO
  output words (1-word buffer = /GS fail 0xC0000409).
- GC: host Terms held across generated calls go stale once any call collects: native consumers keeping Terms use
  HeapOptions{1 << 16 / 1 << 20} min heap. Two blocks can share an address after shrink: test staleness, not word
  inequality. Never compare shared-subterm towers across heaps (2^depth walk). Copier ctor ++collections_.
- RuntimeMemory unlimited account returns UNLIMITED_WORDS from available().
  RuntimeOptions: use designated initializers.
- Pids: immediate 0x3, never reused. Refs: BoxedKind::reference cell; handle
  references use RefcBinaryCell layout (layout::off_heap_cell covers both
  everywhere). Weak magic-ref registry in terms/identities.cpp.
- Scheduler: one shared queue + executor mutex; after() places the process
  BEFORE resuming blockers (else double- queued). Program default schedulers =
  hardware_concurrency; host RuntimeOptions default 1. Exit signals act at once;
  CallError::exited uncatchable. Asio: one thread; objects die before io_context
  (reactor_ declared first, services die after Executor::clear, outside the
  lock). Never redefine NOMINMAX; BOOST_ASIO_NO_DEPRECATED is a CMake
  definition. PortDriver::attach for every port. Close sockets only after queued
  writes drain (Windows resets peer). Windows ConnectEx under load:
  ERROR_DUP_NAME -> eaddrinuse; socket fixtures let the server close first and
  retry.
- Locks: AtomStorage/CodeServer shared_mutex; locked public lookups call private
  unlocked helpers (never call a locking public method under the lock). Thread
  stress lives in tests/runtime/concurrency.cpp.
- Runtime C++ literal atoms: write `"name"_atom` (OPTM.4), helpers that build
  atoms take `AtomName`. Raw word 0 reads as fixed `false` in
  `atom_table::find`; never treat 0 as "no atom" through find.
- Debug speeds: service 0.1-0.5 us, cons ~2 us, map construction ~2.5 us,
  library call with guards ~2 us. Build long strings by binary doubling.
  Pretty-print recursion capped at 256 levels (1 MiB main stack).
- Call tracing: CLAUSE_CALL_TRACE=FILE (+ _FIRST=1, _ALL=1, _QUIET=prefixes);
  OTP twin tools/luerl-trace/ctrace.erl; sink allocated only when set
  (lifecycle_failure counts allocations). See .agents/21-lib.md.
- Profiling: runtime-only `--profile FILE`.
- false/true are fixed atom words 0x0b/0x4b in every runtime (ABI 11);
  AtomStorage::size()/max_atoms exclude them, so cross-runtime atom tests must
  use another atom. HeapStorage::absorb/expose keep FrameState
  heap_top/heap_limit in sync (ABI 12): any new runtime path reading
  heap_.top_/used_words_ of a running process must absorb first.

## Library and builtins
- Bridge catalog abi::v1::bridge_builtins (index = CLAUSE_builtin_v1 arg) may be
  reordered freely: no release, no users, only own tests (user direction
  2026-10-14); keep groups by module; BuiltinRegistry names are static
  string_views. Helpers in builtins/support.hpp; typed_entry<F> derives arity.
  Builtin can tail-continue in Erlang via code_server().function_frame +
  stack().trap.
- Builtins cannot call funs and get results: fun-taking functions stay Erlang in
  library/*.erl, calling sibling builtins qualified (a local call hits the
  stub).
- New library module = library/modules.terms + observe_module.escript +
  observe_types.escript + library_modules.py generate. erlang.terms statuses
  hand-maintained: regenerate erlang.erl via
  tests/compiler/builtins/generate_erlang.py. Any library edit = ~4.5 min luerl
  rebuild; luerl timing: clau --project test-luerl-euler --entry luerl:init.
- luerl manifests (old test-luerl.toml split 2026-10-15, both share build_dir
  build-clau-luerl, run from the example dir): test-luerl-euler.toml (--entry
  euler:run, cwd references/luerl/examples/euler) and test-luerl-benchmark.toml
  (--entry benchmarks:run/0, cwd references/luerl/examples/benchmark; plain
  benchmarks:run picks run/1).
- unicode_util tables: observe host OTP (observe.escript dump, ~15 MB, scratch)
  + UCD files -> generate.py -> clang-format tables.cpp. Full 1.1M-codepoint
  checks ~40 s Release: tests sample.
- Benchmarks: make-benchmark.bat -> build/bench Release + benchmarks/run.py;
  first results ~20x slower than OTP JIT geomean, maps:put / binary append look
  quadratic, binary matching ~1000x.
- Known pre-existing: codegen_resource_cli uses 1025 inputs vs module limit
  16384 (user commit 33481a7). OTP loader literal-record-guard and core_to_ssa
  legacy-import crashes documented; not executed OTP goldens.
- 41P (2026-10-14): MSVC std::filesystem::read_symlink trips clang-analyzer
  EnumCastOutOfRange (like file_size): file_paths.cpp reads links with readlink
  / FSCTL_GET_REPARSE_POINT. Symlink creation needs a privilege on this Windows
  host (eperm), so reading a real link on Windows is untested here. OTP filelib
  checked by hand with a project over host OTP sources (needs COMPILER_VSN
  define + luerl's ignores, ~3.5 min build).
- 41ZA (2026-10-14): PCRE2 10.47 objects (clause_pcre2 OBJECT lib) are merged
  into clause_runtime.lib via $<TARGET_OBJECTS>, so clau/link lines and the
  export set need nothing; an installed PCRE2 instead lands in <runtime>.link
  (one linker input per line) that clau appends (runtime_link_inputs). Untested
  here: the installed- library path (no system pcre2 on Windows). modules.terms
  entries must stay one line each after erlfmt (keep reasons short); a new
  provided module's .erl must -include its .types.hrl.
- 41Q (2026-10-14): zlib streams = handle refs with finalizer; leftover input after need_dictionary and
  deflateParams output are kept by zlib.erl in the process dictionary ({zlib, input|pending, Z}). OTP's
  safeInflate "finished" = queued input consumed (not end of stream). A full-scope check-quality (428 units, after a
  CMake change) once failed a tidy batch with no diagnostic; a rerun passed. Heredoc python with '\0' writes a real
  NUL into C++: use static_cast<char>(0) or the Write tool. include_guard(GLOBAL) cmake files: file-level variables
  are invisible to later includers in other directories - keep data inside functions.
- 41ZB (2026-10-14): re.erl mirrors OTP's re.erl split (urun / loopexec /
  precomp_repl / do_split) over clause_re:compile/2 and clause_re:run/4;
  re:inspect/2 is a builtin, so re.erl calls it qualified. OTP's re source
  (references/otp/lib/stdlib/src/re.erl) is the spec for edge cases:
  retry-after-empty uses the original offset, duplicate matches are dropped.
  lists:all/any/dropwhile/takewhile became implemented for it.
- LIB5E (2026-10-17): builtins reading big binaries repeatedly use
  detail::trusted_binary_bytes in place only above layout::heap_binary_bytes
  (off-heap, stable); copy heap binaries, since TermFactory allocation may move
  the heap (json.cpp Input). Term::binary_bytes() copies every call. OTP's
  json shapes (iodata nesting, continuation tuples with #decode{}) are matched
  exactly; the json_codec golden prints ~w shapes.
- LIB5G (2026-10-17): user directions: prefer C++ builtins whenever viable
  (no switch cost); C++ must reproduce OTP's exact result shapes for every
  input form (deep chardata, embedded binaries). Builtins may raise OTP's
  escaping throws via ErrorReason::raised_throw. Lizard CCN<=10 and tidy
  cognitive complexity<=10 bite on clause-table ports: write small helpers
  from the start; Named<Role> args for same-typed builtin parameters;
  thrown structs need nothrow copy (string_view, not std::string).
  heredoc python mangles backslashes in C++ string literals: use Write.
- LIB4A (2026-10-17): porting an OTP module clause-for-clause: write the
  golden fixture first (OTP decides shapes), strip -doc/-spec from the OTP
  source into scratch for reading (regexes can eat functions whose spec
  precedes them: grep the original when one is missing), then C++ over
  term-level cp/gc (unicode/code_points.cpp). Body-recursive OTP walks become
  a loop + frame vector folded at the end. OTP raises badarg where it calls
  binary:compile_pattern([]) (empty separator sets): mirror the call sites.
  Erlang-implemented lists:reverse/1 gives function_clause for a non-list;
  the BIFs (length, ++, member) give badarg. Only two OTP quirks needed
  probing; everything else came from the golden diff.
- LIB4D (2026-10-17): io_lib_pretty ported clause-for-clause (io_lib/pretty*.cpp):
  arena of intermediate nodes, More funs as {kind, term, index, depth, depth0}
  reruns, if_list ends as NONE/dots/improper node. Golden passed first try by
  porting OTP's quirks verbatim (record fields subtract 1 on the LAST field,
  cind_fields_tail Col + FW, pp_map W = PW, map pairs rerun keep D0, chars/2
  of 1 is [$\s, []], cind map W + 2 vs pp map W + 1). A fixture module must
  not be named io_lib_pretty (it replaces OTP's module in the oracle). Codex
  CLI subagents use gpt-6.1-sol (CLI 0.151 hid it; 0.163 lists it); its sandbox
  cannot write build/ (ACL) or .agents/, so regenerate goldens and edit plan
  files yourself.
- OPTM.2 (one-word Term): a Term held past its heap's next collection or its
  process's exit is undefined (no stale_term); tests must not dereference such
  handles. A runtime test counting allocations must warm the process-wide atom
  index first (its chunks live for the process). clang-tidy flags std::move of
  Term (trivially copyable) as performance-move-const-arg.
- Persistent term reclamation (OPTM.2E): anything that copies a term must keep
  "share or copy" decisions stable (RetiringRanges snapshot per copy); copies
  outside processes (message buffers, persistent areas) never share persistent
  words; direct copies into a process heap must run under the executor lock with
  the receiver not running. A process not scheduled (parked, port-suspended)
  must be evacuated by the executor. Budget arithmetic uses words_left (live
  words may exceed a limit after an evacuation).
- OPT3.5 (2026-10-20): fair A/B = runtime-only builds of base and new with the
  same /O2 /Ob2 /Z7 flags (git worktree in scratch + `mklink /J thirdparty`;
  rmdir the junction BEFORE `git worktree remove --force`), link one clau
  against both libs, run kernels interleaved; build/bench's Release lib is
  not comparable. Symbolized ntdll/kernel: xperf -i with _NT_SYMBOL_PATH
  srv*<cache>*https://msdl.microsoft.com/download/symbols (slow first time).
  short_processes S1 vs S32 separates CPU cost from lock contention. Lock-free
  sends that buffer every message regress fan_in (heap allocator contention,
  receiver drains huge backlogs): only running receivers get arrival nodes,
  taken in batches of 64.
- OPT3.10B (2026-10-20): 69A timings on this host swing 20-100% per kernel;
  judge GC policy by words copied + collection counts (temporary stderr trace
  in copy_generation/copy_live, env-switched variants) and by kernels run
  alone (scratch bench_util with ONLY=<kernel>), then time min-of-N
  interleaved. ERTS minor `next(young + stack)` never grows (stack is inside
  its block); ERTS old heap = next_heap_size(young, 1); both measured worse in
  Clause than the base rules. OTP gc trace: erlang:trace(Pid, true,
  [garbage_collection]) gives heap_block_size/old_heap_block_size per GC.
- OPT3.5E (2026-10-20): ntoskrnl share in an xperf profile (page faults,
  MiDecommit*) points at large per-call temporaries (>~64 KB vectors/strings):
  the NT heap decommits freed big blocks, so reuse them (builtins/scratch.hpp).
  MSVC: one std::string::append of tag+value (5 bytes) measured 2x slower
  than push_back + append(4). VSDiagnostics rejects an empty /launchArgs.
  Python edits of UTF-8 files on Windows need encoding="utf-8" (em dashes).
- OPT3.6 (2026-10-20): the VS collector's sc.user_aux.etl carries stacks:
  `xperf -i <etl> -o s.html -symbols -a stack -butterfly` gives inclusive
  callers/callees per function (HTML; one <tbody> per function, rows marked
  --> callee / <-- caller); this found the growth path's page faults under
  memset/memcpy. Appendable capacities are powers of two so freed buffers
  are reused exactly (off_heap.cpp BufferCache, thread_local like heap_blocks).
- OPT3.15 (2026-10-20): this host is an i9-14900KF (8 P-cores = logical 0-15,
  16 E-cores): an unpinned kernel swings 2x when Windows puts it on an E-core.
  Time kernels pinned: `cmd /c start "" /b /wait /affinity 5554 X.exe` (OTP
  too: same with erl.exe -noshell -eval). A runtime-only profiling build dir
  must match the main build's CRT (`-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
  -DCLAUSE_MSVC_ITERATOR_DEBUG_LEVEL=0`); /MD made map kernels 2x slower.
  Ownership of fold accumulators is decided at run time (a frame slot, false
  on step 1): the initial accumulator may be shared, e.g. maps:fold(F, M, M).
