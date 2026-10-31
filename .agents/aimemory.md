# Project memory — compacted 2026-10-14

Plan history: .agents/11-plan.md (compact step records), .agents/00-finished.md (archive index of
the per-plan NN-finished.md), docs/validation.md (evidence history). This file keeps only durable
gotchas and user directions not recorded elsewhere.

## User directions (keep)
- Minimal first, iterate later; no defenses for impossible cases (classic ERTS trust model). Reject
  over-engineering.
- ERTS host model: no handle table; Term valid only in own process, read-only elsewhere. One heap
  block per process.
- Binaries > 64 B are std::shared_ptr buffers outside heaps; prefer C++ style designs.
- No hard memory cap by default (heap, stack, binaries, contexts); caps are opt-in options only.
  Atoms 2^20 default, `--max-atoms` <= 2^26.
- Ports must exist: sockets, file I/O, subprocess stdin/stdout are ports (backlog F35). Ports are
  not scheduled entities (I/O threads + synchronous port ops); ERTS-like port tasks only if user
  asks.
- Long-running builtins run in interruptible portions (call_builtin_portion, step 43A).
- Test iteration counts: just large enough to prove the property.
- Test/gate time: per step check-quality FIRST, then fast CTest (user 2026-10-29); full CTest only
  at phase/major completion, INSTEAD of fast. After a gate run, rank build/debug/Testing/Temporary/
  CTestCostData.txt: cases of ~70 s or more alone go full_only (executables/CMakeLists.txt
  full_only_cases; ssl connection cases and shell_expand/session moved 2026-10-29). Fast gate must stay fast (user 2026-10-17): slow workflows are full_only,
  check-quality tidies one unit per changed header (dependents in check-quality-all); fast CTest ~75
  s at -j 16. Tidy default jobs = half the logical cores. Keep slow tests parallel. Keep 20-25% CPUs
  free.
- Tidy/Lizard run on the Windows dev host only; other platforms build+test.
- All ABI symbol/namespace versions collapse to v1 in plan step RT4 (never released).
- RT5: rename runtime/src/builtins files to builtin_<name>.
- External Term Format code is shared (abi/include/clause/abi/external_term.hpp Writer/Reader);
  never reimplement it.
- Inference: guards/patterns define a function's entry domain; uses narrow operands after them.
  Specs are errors only when DISJOINT from inferred facts. Printing: `_` for term(), N..M ranges,
  argument relations as variable names / _argumentN, records as records; case/if/try-of values are
  dependent facts printed as $case_of_operator etc.
- OTP small-map iteration order follows atom index: leave Clause's key order (recorded in
  docs/differences.md).
- --entry may name FUNCTION/0. source_search_paths take */** patterns, searched before the library.
- Testing-off wrappers (clau.bat, make-build.bat, run-macos.sh) build in build/tool so build/debug
  keeps tests ON.
- Don't stage user's test1.toml. Preserve user's uncommitted AGENTS.md edits. Stale IDE buffers may
  revert plan edits.

## Host and toolchain
- macOS host (2026-10-27): 8 logical cores (budget 6: build --parallel 6, ctest -j 4 at nice).
  ~/.zprofile exports CXXFLAGS=-I/opt/homebrew/include; ~/.tool-versions pins asdf erlang 28 ahead
  of Homebrew OTP 29 on PATH: configure -DCLAUSE_TEST_ERL=/opt/homebrew/bin/erl, regenerate goldens
  with /opt/homebrew/bin/escript; rebar3 needs ASDF_REBAR_VERSION=3.25.0. clang-tidy (pip 22) cannot
  parse the Apple SDK: tidy stays on Windows; Lizard runs. LLDB attach needs Developer Mode
  (linking_debugger skips). libc++ traps seen: missing <algorithm>, filesystem::equivalent reports a
  missing path as not_supported, istream>>double fails on subnormals, sigemptyset is a macro,
  uint64_t != Word (unsigned long). Unix Makefiles nested builds run serially.
- Code layout alone moves GC-heavy kernels ~5% (OPT3.4: list_of_tuples +7%, list_to_tuple +4% with
  byte-identical GC/generated code, only other TUs changed). Before blaming a change, diff object
  code (llvm-objdump -d with symbol names stripped) between builds.
- Repo F:/CLAUSE/clau (uppercase F: matters for programs.py <fixture> substitution and expectation
  runners: use absolute F:/CLAUSE/clau/build/debug/bin/Debug/clau.exe). build/ is wiped by
  make-clean.bat: host-local history lives in the gitignored `.state/` (user 2026-10-27): helper
  scripts `.state/scripts/` (README there), gate run log with timings `.state/runs.md` (append
  after every gate run), tidy records `.state/quality/`. Committed lessons stay in this file.
- Toolchain LLVM 23.1.2 SDK, VS 18 x64, SDK 10.0.26100, /MT, Ninja, Boost 1.90, toml 3.4, Lizard
  1.24, tidy 22.1.8. Fresh configure MUST pass %CLAUSE_CONFIGURE_ARGS% from
  tools/windows-toolchain.cmd (BUILD_DIR set): else VS generator + cl (C2220) or GNU clang++ (nested
  test caches break, Unicode-path tests fail). Never pass LLVM_DIR. Switching compiler needs rm -rf
  of the build dir. A cmake run without vcvars can wipe the cache.
- Check `grep BUILD_TESTING build/debug/CMakeCache.txt` before trusting a fast run (IDE CMake Tools
  may reconfigure OFF; ctest then runs stale exes). First run after runtime edits may time out while
  nested consumers recompile. Don't edit runtime sources while a gate runs. A killed session can
  leave a gate running: check ctest/cmake/ninja.
- cmd scripts: never pass `|` in arguments (pipe): use env vars (TESTRE) or ctest -I numbers. cmd /c
  needs a full .bat path. Write .cmd files with Write (printf/heredoc eat backslashes).
- Bash heredocs mangle `\`, `\n` (become real newlines in C++/Python literals), non-ASCII, and many
  quotes/apostrophes ("unexpected EOF"). `cat > file` without heredoc hangs. Write scripts with
  Write into scratchpad; use Edit for lines with escapes. Edit tool strips a trailing space at end
  of new_string: never end old/new mid-line before a quoted argument. Edit may write CRLF into .py:
  normalize. Many worktree files are CRLF (autocrlf=input): keep endings.
- Python on Windows: read_text defaults cp1252 (pass encoding="utf-8"); write_text writes CRLF (use
  write_bytes); console capture doubles CR; 3.14 needs sys.set_int_max_str_digits(0) for huge ints;
  difflib hangs on huge diffs (use GNU diff). No make; gmake at C:/Strawberry/c/bin. Strawberry
  cmake/ninja first on PATH is fine.
- Never `git checkout <file>` to drop temporary instrumentation from a file with uncommitted work.
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
  (negative map key order) and ets_match_specs fail on x86 at HEAD 64b19ee9 already; OPT3.17 fixed small_order
  (widened 32-bit words unsigned: negatives sorted last), a likely cause (x86 rerun not done).
- erlang_gaps "hang" was a fixture race (fixed in OPT3.23 part 1): `receive {spawned, P}` also matched
  P4's {spawned, linked}. Hunt hangs with scratch hang.ps1 (loop + cdb `~* kc` on timeout; build the
  fixture with clau -g, test programs have no PDB), then bisect phases in a scratch copy.
- MSVC cl (non-clang-cl): Boost.Parser C4702 and LLVM-header C4244/C4267 disabled on
  clause_parser_dependency / clause_llvm_sdk for cl only; C4554 false positive on
  static_cast<Word>(n - 1) << shift: hoist to a local.

- Plan 22 (2026-10-23): compiler timing = `benchmarks/compile_times.py` (W5r scratch repro for a 11
  s loop). Editing `semantic/types/inference.cpp` (holds the hot body walk) moved uninstrumented W5r
  3.9% by code layout alone, and so did code added to other inference TUs (2-4%). Tell layout from
  code with both builds aligned: local `target_compile_options(clause_semantic PRIVATE -mllvm
  -align-all-functions=6)`. Counters in `Graph::intern` (~40M calls per W5a pass) are layout-costly:
  keep them temporary.

- Plan 21 Tier F (2026-10-25): WSL check = rsync sources into ~/clau (exclude
  build/thirdparty/references/.git), then `bash ~/gate.sh configure|build|test -R <re>` (gate.sh not
  executable: call via bash; full log in build/plan11-wsl/logs/ctest-debug.log). OTP probes in
  escript: output piped through grep buffers until exit, so a hang looks like no output; add a
  watchdog process (halt after N s) and write to a file. taskkill of escript is denied: stop
  background probes with TaskStop. OTP on Windows: socket connect needs bind first (not_bound),
  nowait answers completion messages, recvmsg/sendmsg/sendfile raise notsup; inet_res ids count from
  1 without crypto; io_ansi has no terminfo there. frontend_cli/codegen_examples time out under the
  full fast suite at -j24 and pass alone.

- Plan 21 Tier G (2026-10-25): the PowerShell tool's environment sets NO_COLOR=1 (Git Bash does not); io_ansi
  honours it, so the executable runner and regenerate.py now drop NO_COLOR. Builtins needing funs (argparse) return
  a request term and Erlang re-calls them with the answers so far (replay): no state serialization. Calling other
  builtins from C++: registry find + call_builtin; a 0 result leaves the failure recorded (rethrow as a marker,
  return 0). Tidy's easily-swappable check passes when both Terms meet in one call; else role structs or Named<>.
  Under -j21 gen_statems/gen_fsms once failed (timer order, `gen_statem:state_callback/7 not exported`) and passed
  alone. library_freshness fails on socket.types.hrl (drift from LIB62, eb51924e).
## Quality (tidy/Lizard)
- clang-tidy may crash (0xC0000005/0xC0000409) or exit 1 silently with 2 jobs: rerun with one job.
  Check targets do not forward QUALITY_JOBS: `cmake -DQUALITY_SCOPE=all -DQUALITY_BUILD_DIR=<b>
  -DQUALITY_NINJA=<n> -DQUALITY_JOBS=1 -P cmake/CheckClangTidy.cmake`; shard with
  -DQUALITY_SHARD=K/N, batches QUALITY_BATCH. CMakeLists edits other than source lists select all
  units (~25 min, 710 units at 16 jobs) when no record exists; passing batches merge into the record
  (a65e1ac4). Iterate on findings with `.state/scripts/tidy_files.py FILE...` (-p build/debug).
  CLAUSE_QUALITY_BASE overrides HEAD for changed scope. Lizard: .venv-quality python -m lizard -C
  10.
- Windows tidy HeaderFilterRegex never matched backslash paths; libstdc++ exposes unchecked-optional
  findings MSVC hides.
- Recurring tidy findings and fixes:
  - bugprone-easily-swappable-parameters: adjacent same-type params (Word/size_t are one type;
    Term/Term; Id/Id) not used in one call -> wrap in a small struct (+ Argument<> specialization
    for builtins) or use them together.
  - bugprone-unchecked-optional-access: `opt->` and `.value()` flagged; test first.
  - bugprone-exception-escape: never move/return structs holding std::map by value (MSVC move may
    throw): emplace in place, extract node handles, or copy.
  - bugprone-unused-return-value: discarded std::expected (static_cast<void> too).
  - clang-analyzer ArrayBound false positives in LLVM headers: avoid getCalledFunction, PHI
    getIncomingBlock / incoming_values, LoadInst::getPointerOperand, StoreInst::getValueOperand
    (walk use lists); IRBuilder::CreateICmp (use builder.Insert(CmpInst::Create(...))); Boost
    cpp_int `x + var` (fixed +1/-1 helpers).
  - EnumCastOutOfRange from MSVC <filesystem> file_size/last_write_time: use llvm::sys::fs::status.
  - string_view::data() to C APIs flagged: take const std::string&. `new GlobalVariable` in a local
    = leak: use getOrInsertGlobal. No int-to-ptr casts: typed pointer fields.
    -Wmissing-designated-field-initializers: static factory helpers. Lizard/tidy complexity:
    dispatch tables for op switches.
- ADL traps: local `advance`/`quoted`/`accumulate` resolve to std::/llvm:: -> rename.
- clang-analyzer (OPT3.5B): a move-only owning handle passed BY VALUE trips NewDelete "use after
  release" (MSVC callee-destroyed args): pass `T&&`. A pointer pushed into a std::atomic list by CAS
  reads as a leak: do the CAS in an out-of-line function (opaque to callers' TUs).
  bugprone-easily-swappable fires when two same-type params stop appearing in one expression.
- check-quality's changed scope is `git diff HEAD`: `git add` new files first or they are skipped.
  abi/ header edits do not pull their runtime dependents into the tidy scope.

## OTP oracle, goldens and fixtures
- OTP pin and refresh: docs/otp-reference.md. Pin 9710673e (29.1.2), oracle install 29.1.1 / ERTS
  17.1. Pin change needs regenerate.py of executables/lint/programs/printing (diff = revision lines
  only).
- Escript: "C:/Program Files/Erlang OTP/erts-17.1/bin/escript.exe" is reliable (bin/escript.exe once
  segfaulted; later worked). Probes: erl.exe -noshell -eval 'c:c(m), m:main()' in scratchpad; delete
  erl_crash.dump. Windows re-sets ProgramFiles for children: tests use --erl none. erlc needs
  absolute -pa.
- Normal build/test is OTP-free; OTP source never committed (observations only). Fixtures written as
  UTF-8 LF bytes.
- Host-transformed fixtures (qlc.hrl): commit X.abstr beside X.erl (regenerate.py --clau writes it,
  drops the header's host-path file attribute); run.py deletes X.erl in work/src. qlc_pt numbers
  annotations around every integer in the forms (header path chars too): normalize annos before
  printing info(…, {format, abstract_code}). OTP's qlc_pt calls compile:noenv_forms with the same
  options, so a --parse-transform dump runs twice (write once). qlc compile errors in fixtures:
  call qlc:q through a runtime module name.
- Fixture layout: project.toml, src, fixture.json (entry+argv, env, stdin, data files), expected/{stdout.txt,
  golden.json}, compile.txt. Golden runs may be `"authored": true`, may mark word_bits 64. Runner runs each
  combination in work/<label>. programs.py --update-diagnostics rewrites compile.txt.
- Corpus edits: change fragments + generated calls, then fixtures.tsv/manifest hashes (corpus.json
  row sha256 = LF digest; records input_manifest_sha256 = digest(corpus.json)), then regenerate.py
  --corpus X --check.
- Oracle uses warnings_as_errors: needs -compile(nowarn_deprecated_catch / nowarn_shadow_vars);
  constant folding of literal calls (apply(erlang, abs, [a]), pid_to_list(self)) and statically
  wrong calls fail -> route through id()/helper; unexported own `fun m:f/1` rejected; unused
  named-fun names warn.
- OTP printing: display = erl_printf_term.c (not ~w), latin-1 bytes for chars 128-255 (avoid such
  strings; keep fixtures ASCII, erlfmt rewrites UTF-8 literals); maps print/iterate in atom-index
  order (unstable: use integer keys, ~kw, display fields or single-field native records);
  uncaught-exception report prints ~w. OTP crash reports go through async logger: oracle usually
  halts first, author stderr. phash2 differs: digest with md5(term_to_binary).
- OTP race quirks: monitor/link after spawn of a short process races -> spawn_monitor; check goldens
  with --check 3x.
- A Clause program killed by a timeout loses its buffered stdout: the last printed line is NOT where
  it hung; find the hang with a watchdog process that prints to standard_error and halts. Full mode
  runs policies concurrently (4 at once): fixtures touching shared state (registry keys, fixed paths
  outside the work dir) need per-process names (os:getpid()). Golden runs can be per OS family
  ('os': windows|unix, other family authored).
- Ports opened in a process that traps exits send {'EXIT', Port, normal} on port_close (OTP too); library code
  closing helper ports in the caller must unlink first (file:path_call). OTP's raw files/NIFs send nothing.
- Shell edits: when the Bash classifier keeps failing, the PowerShell tool still works.
  Busy-port senders resume in any order. -eval process traps exits: probe inside spawned processes. Oracle runs
  entries from an escript while init still boots (shape-only checks for init status/arguments). init:request/1
  takes any pending {init,_} message.
- OTP compiler quirks: drops code after spawn(fun/1) (route fun via remote id/1); turns calls to
  never-returning functions into tail calls (trace fixtures need returning functions, non-tail
  sites); auto-imports setelement/3, tuple_to_list/1, list_to_tuple/1 but not make_tuple. OTP lint
  rejects `X =:= 0.0`. Never probe huge atomics:new arities; ~.*c with -1 hangs OTP.
- Reserved words unusable as fixture atoms/modules/functions: after, receive, case.
- Windows host: Python `Path.write_text` translates to CRLF; editing repo files from Python must use
  read_bytes/write_bytes (2026-10-31, every DIST3c file came out CRLF). OTP 29's Windows `epmd -daemon`
  exits 0 but no epmd stays up (from Git Bash and PowerShell): goldens of -daemon are authored runs.
- A runtime builtin replaces only remote calls of its placeholder (`clause_dist:start_daemon(...)`); a local call
  inside the same module runs the placeholder body (DIST3d).
- NEVER put a backslash in a Bash-tool heredoc or `python -c` (even `<<'EOF'`): `\\` arrives as `\`, `\\n` as a
  newline (broke regexes in golden.json, C++ literals, many retries). Write the script with the Write tool into the
  scratchpad and run `python file.py` (user request 2026-10-31).
- erlfmt: thirdparty/tools/erlfmt/_build/local; helper escript (scratchpad fmt.escript) does
  code:add_path + erlfmt:format_file and writes unicode:characters_to_binary back. Cannot parse
  `-record #r{a, b}.`, `#div`, `#m:r`, `f(...)(2)` (parenthesize); crashes on non-ASCII atoms
  (format by hand).
- "Unsupported feature" placeholders in tests: currently `fun erlang:apply/2` ([dynamic calls]);
  swap when admitted.

## Compiler internals
- Fast matrix: O0 positional + O2 (spec on) project; full = 8 combinations. Project rule: link iff
  not frontend action AND (manifest output|entry or CLI -o|--entry). -Os = O2 pipeline + optsize. -g
  implies annotate_source.
- Bindings: exported names share one BindingId across clauses; BindingIds are per-function, so Facts
  leaving a function must be `erased`. lower_match_plan's bind uses emplace: restore state.bindings
  before rematching. Named fun name lives only in FunScope::inside. A clause with neither pattern
  nor guard needs an explicit br. Module atoms must include atoms codegen lowers implicitly
  (infinity/true for receive).
- maybe keeps ast::MaybeMatch (use semantic::maybe_operands). record_info/2 is a compile-time
  pseudo-function; a local record_info/2 definition is still indexed. behaviour_info/1 hidden from
  own source (semantic::source_function). module_info/0 must not call module_info/1 locally;
  predefined functions get no debug scope.
- Frames: lower_frames post-pass (idempotent); a native function without `erlang-arity` stays a
  native call. Inline heap reads must be stored into root_slot (raw spills go stale after GC).
  Proofs read Inference::inputs, never Summary entry domains. ExpressionLowering positional init:
  7th field is `values`.
- Liveness only for executables; dead functions emit name-only exports. Builtin tail-continuations
  must be listed in semantic/liveness.cpp BUILTIN_CONTINUATIONS. referenced_modules skips catalog
  builtin calls.
- Liveness keep-all (dynamic call + atom producer) fires on every OTP-heavy batch; even without it
  ?MODULE in error tuples keeps most OTP modules whole (LIV.1). Experiments: env-gated temp build.
- Inference: solve 16 rounds then widened; expression facts erased per round; per-call re-analysis
  has its own work pool. Expectations: tests/compiler/inference/expectations.py <abs clau> <fixture>
  [--record].
- Adding a FeatureId needs tests/abi/features.cpp names snapshot + codegen_placeholders CASE for
  deferred features. ABI version bump needs cross_targets.py and linking/startup.cmake descriptor
  strings.
- Build cache: SQLite build/cache/v2 (lazy open; only put creates the db). Tests running manifests
  inside tests/fixtures pass --build-dir into their work dir; --verbose compile checks use
  --no-cache; a rebuilt clau misses every entry (warm up Release timing runs).
- Inline codegen (OPT0.2/0.3): join values with `join_values` (SSAUpdater; PHI addIncoming trips
  clang-analyzer). Block/value names must avoid float-flag words (`fast`, `nnan`, ...:
  patternmatch_floats greps IR). Every inttoptr must be an instruction named `proven.cell*`
  (facts.py): never form pointers from constant words (they fold into constant expressions);
  constant words are immediates anyway. Inline pointer results go through a root-slot store.
- OPT1.7/1.8 (2026-10-20): loops building a reversed accumulator lose to body recursion when a
  heap-check safepoint collects mid-loop (the live accumulator is copied); body recursion allocates
  its result with no safepoint. List loops use loop_reduction only (collect at yields);
  comprehensions still check. A/B codegen with one clau via a temporary env switch; time each kernel
  in its own process (heap state leaks between kernels), min and median of 300 runs.
- An atom codegen makes up (not in the source, not one of v1 fixed_atom_spellings: false, true,
  undefined, ok, error) must be added to the module's table in `codegen/module_atoms.cpp`
  expression_atoms, else lower_atom's atom_slot is null and clau crashes (0xC0000005 at -O2 only):
  OPT1.7 part 2 `{value, E}` of lists:search loops.
- `codegen_service_*` fault tests reach services only where codegen still calls them: their fixtures
  route type tests through `abs/1` and the consumer runs under a heap budget (inline allocation
  off).

## Runtime internals
- Term::bit_slice, is_function, list_elements, with_tuple_element, without_map_entry,
  with_existing_map_entry are declared but NOT defined (link errors): walk lists by hand, rebuild
  via tuple()/map_entries + TermFactory::map, slice via CLAUSE_bits_v1. bits service `part` writes
  TWO output words (1-word buffer = /GS fail 0xC0000409).
- GC: host Terms held across generated calls go stale once any call collects: native consumers keeping Terms use
  HeapOptions{1 << 16 / 1 << 20} min heap. Two blocks can share an address after shrink: test staleness, not word
  inequality. Never compare shared-subterm towers across heaps (2^depth walk). Copier ctor ++collections_.
- RuntimeMemory unlimited account returns UNLIMITED_WORDS from available(). RuntimeOptions: use
  designated initializers.
- Pids: immediate 0x3, never reused. Refs: BoxedKind::reference cell; handle references use
  RefcBinaryCell layout (layout::off_heap_cell covers both everywhere). Weak magic-ref registry in
  terms/identities.cpp.
- Scheduler: one shared queue + executor mutex; after() places the process BEFORE resuming blockers
  (else double- queued). Program default schedulers = hardware_concurrency; host RuntimeOptions
  default 1. Exit signals act at once; CallError::exited uncatchable. Asio: one thread; objects die
  before io_context (reactor_ declared first, services die after Executor::clear, outside the lock).
  Never redefine NOMINMAX; BOOST_ASIO_NO_DEPRECATED is a CMake definition. PortDriver::attach for
  every port. Close sockets only after queued writes drain (Windows resets peer). Windows ConnectEx
  under load: ERROR_DUP_NAME -> eaddrinuse; socket fixtures let the server close first and retry.
- Locks: AtomStorage/CodeServer shared_mutex; locked public lookups call private unlocked helpers
  (never call a locking public method under the lock). Thread stress lives in
  tests/runtime/concurrency.cpp.
- Runtime C++ literal atoms: write `"name"_atom` (OPTM.4), helpers that build atoms take `AtomName`.
  Raw word 0 reads as fixed `false` in `atom_table::find`; never treat 0 as "no atom" through find.
- Debug speeds: service 0.1-0.5 us, cons ~2 us, map construction ~2.5 us, library call with guards
  ~2 us. Build long strings by binary doubling. Pretty-print recursion capped at 256 levels (1 MiB
  main stack).
- Call tracing: CLAUSE_CALL_TRACE=FILE (+ _FIRST=1, _ALL=1, _QUIET=prefixes); OTP twin
  tools/luerl-trace/ctrace.erl; sink allocated only when set (lifecycle_failure counts allocations).
  See .agents/21-lib.md.
- Profiling: runtime-only `--profile FILE`.
- false/true are fixed atom words 0x0b/0x4b in every runtime (ABI 11); AtomStorage::size()/max_atoms
  exclude them, so cross-runtime atom tests must use another atom. HeapStorage::absorb/expose keep
  FrameState heap_top/heap_limit in sync (ABI 12): any new runtime path reading
  heap_.top_/used_words_ of a running process must absorb first.

## Library and builtins
- qlc (LIB73): ets/dets/erl_eval reach qlc via a runtime name (qlc_module() -> qlc) so ets users do
  not link it; internal errors thrown as {'$qlc_thrown_error', E} (OTP checks the throw_error stack
  frame). Differential scratch tests (OTP erlc run vs clau -O0) matched first try except refs and
  stack traces.
- Bridge catalog abi::v1::bridge_builtins (index = CLAUSE_builtin_v1 arg) may be reordered freely:
  no release, no users, only own tests (user direction 2026-10-14); keep groups by module;
  BuiltinRegistry names are static string_views. Helpers in builtins/support.hpp; typed_entry<F>
  derives arity. Builtin can tail-continue in Erlang via code_server().function_frame +
  stack().trap.
- A library module named only as an atom (handler/formatter/callback config) never joins the batch:
  dynamic calls to it are undef. Give it a literal call path (`formatted(logger_formatter, ...) ->
  logger_formatter:format(...)`) or `_ = fun M:F/A`. Library .erl/.terms use erlfmt width 120
  (LIB23). OTP prints small atom-key maps in atom-index order: goldens print sorted lists.
- Builtins cannot call funs and get results: fun-taking functions stay Erlang in library/*.erl,
  calling sibling builtins qualified (a local call hits the stub).
- New library module = a PROVIDED line in compiler/src/driver/library_modules.inc (rebuild clau) +
  observe_module.escript + observe_types.escript + library_modules.py generate. erlang.terms statuses hand-maintained: regenerate erlang.erl via
  tests/compiler/builtins/generate_erlang.py. Any library edit = ~4.5 min luerl rebuild; luerl
  timing: clau --project test-luerl-euler --entry luerl:init.
- luerl manifests (old test-luerl.toml split 2026-10-15, both share build_dir build-clau-luerl, run
  from the example dir): test-luerl-euler.toml (--entry euler:run, cwd
  references/luerl/examples/euler) and test-luerl-benchmark.toml (--entry benchmarks:run/0, cwd
  references/luerl/examples/benchmark; plain benchmarks:run picks run/1). Since LIB76 both use only
  library/ (no OTP paths): OTP-sourced modules are no longer part of luerl timings.
- unicode_util tables: observe host OTP (observe.escript dump, ~15 MB, scratch) + UCD files ->
  generate.py -> clang-format tables.cpp. Full 1.1M-codepoint checks ~40 s Release: tests sample.
- Benchmarks: make-benchmark.bat -> build/bench Release + benchmarks/run.py; first results ~20x
  slower than OTP JIT geomean, maps:put / binary append look quadratic, binary matching ~1000x.
- Known pre-existing: codegen_resource_cli uses 1025 inputs vs module limit 16384 (user commit
  33481a7). OTP loader literal-record-guard and core_to_ssa legacy-import crashes documented; not
  executed OTP goldens.
- 41P (2026-10-14): MSVC std::filesystem::read_symlink trips clang-analyzer EnumCastOutOfRange (like
  file_size): file_paths.cpp reads links with readlink / FSCTL_GET_REPARSE_POINT. Symlink creation
  needs a privilege on this Windows host (eperm), so reading a real link on Windows is untested
  here. OTP filelib checked by hand with a project over host OTP sources (needs COMPILER_VSN define
  + luerl's ignores, ~3.5 min build).
- 41ZA (2026-10-14): PCRE2 10.47 objects (clause_pcre2 OBJECT lib) are merged into
  clause_runtime.lib via $<TARGET_OBJECTS>, so clau/link lines and the export set need nothing; an
  installed PCRE2 instead lands in <runtime>.link (one linker input per line) that clau appends
  (runtime_link_inputs). Untested here: the installed- library path (no system pcre2 on Windows).
  library_modules.inc entries must stay one line each (library_modules.py reads them by line); a new
  provided module's .erl must -include its .types.hrl.
- 41Q (2026-10-14): zlib streams = handle refs with finalizer; leftover input after need_dictionary and
  deflateParams output are kept by zlib.erl in the process dictionary ({zlib, input|pending, Z}). OTP's
  safeInflate "finished" = queued input consumed (not end of stream). A full-scope check-quality (428 units, after a
  CMake change) once failed a tidy batch with no diagnostic; a rerun passed. Heredoc python with '\0' writes a real
  NUL into C++: use static_cast<char>(0) or the Write tool. include_guard(GLOBAL) cmake files: file-level variables
  are invisible to later includers in other directories - keep data inside functions.
- 41ZB (2026-10-14): re.erl mirrors OTP's re.erl split (urun / loopexec / precomp_repl / do_split)
  over clause_re:compile/2 and clause_re:run/4; re:inspect/2 is a builtin, so re.erl calls it
  qualified. OTP's re source (references/otp/lib/stdlib/src/re.erl) is the spec for edge cases:
  retry-after-empty uses the original offset, duplicate matches are dropped.
  lists:all/any/dropwhile/takewhile became implemented for it.
- LIB5E (2026-10-17): builtins reading big binaries repeatedly use
  detail::trusted_binary_bytes in place only above layout::heap_binary_bytes
  (off-heap, stable); copy heap binaries, since TermFactory allocation may move
  the heap (json.cpp Input). Term::binary_bytes() copies every call. OTP's
  json shapes (iodata nesting, continuation tuples with #decode{}) are matched
  exactly; the json_codec golden prints ~w shapes.
- LIB5G (2026-10-17): user directions: prefer C++ builtins whenever viable (no switch cost); C++
  must reproduce OTP's exact result shapes for every input form (deep chardata, embedded binaries).
  Builtins may raise OTP's escaping throws via ErrorReason::raised_throw. Lizard CCN<=10 and tidy
  cognitive complexity<=10 bite on clause-table ports: write small helpers from the start;
  Named<Role> args for same-typed builtin parameters; thrown structs need nothrow copy (string_view,
  not std::string). heredoc python mangles backslashes in C++ string literals: use Write.
- LIB4A (2026-10-17): porting an OTP module clause-for-clause: write the golden fixture first (OTP
  decides shapes), strip -doc/-spec from the OTP source into scratch for reading (regexes can eat
  functions whose spec precedes them: grep the original when one is missing), then C++ over
  term-level cp/gc (unicode/code_points.cpp). Body-recursive OTP walks become a loop + frame vector
  folded at the end. OTP raises badarg where it calls binary:compile_pattern([]) (empty separator
  sets): mirror the call sites. Erlang-implemented lists:reverse/1 gives function_clause for a
  non-list; the BIFs (length, ++, member) give badarg. Only two OTP quirks needed probing;
  everything else came from the golden diff.
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
- OPTM.2 (one-word Term): a Term held past its heap's next collection or its process's exit is
  undefined (no stale_term); tests must not dereference such handles. A runtime test counting
  allocations must warm the process-wide atom index first (its chunks live for the process).
  clang-tidy flags std::move of Term (trivially copyable) as performance-move-const-arg.
- Persistent term reclamation (OPTM.2E): anything that copies a term must keep "share or copy"
  decisions stable (RetiringRanges snapshot per copy); copies outside processes (message buffers,
  persistent areas) never share persistent words; direct copies into a process heap must run under
  the executor lock with the receiver not running. A process not scheduled (parked, port-suspended)
  must be evacuated by the executor. Budget arithmetic uses words_left (live words may exceed a
  limit after an evacuation).
- OPT3.5 (2026-10-20): fair A/B = runtime-only builds of base and new with the same /O2 /Ob2 /Z7
  flags (git worktree in scratch + `mklink /J thirdparty`; rmdir the junction BEFORE
  `git worktree remove --force`), link one clau against both libs, run kernels interleaved;
  build/bench's Release lib is not comparable. Symbolized ntdll/kernel: xperf -i with
  _NT_SYMBOL_PATH srv*<cache>*https://msdl.microsoft.com/download/symbols (slow first time).
  short_processes S1 vs S32 separates CPU cost from lock contention. Lock-free sends that buffer
  every message regress fan_in (heap allocator contention, receiver drains huge backlogs): only
  running receivers get arrival nodes, taken in batches of 64.
- OPT3.10B (2026-10-20): 69A timings on this host swing 20-100% per kernel; judge GC policy by words
  copied + collection counts (temporary stderr trace in copy_generation/copy_live, env-switched
  variants) and by kernels run alone (scratch bench_util with ONLY=<kernel>), then time min-of-N
  interleaved. ERTS minor `next(young + stack)` never grows (stack is inside its block); ERTS old
  heap = next_heap_size(young, 1); both measured worse in Clause than the base rules. OTP gc trace:
  erlang:trace(Pid, true, [garbage_collection]) gives heap_block_size/old_heap_block_size per GC.
- OPT3.5E (2026-10-20): ntoskrnl share in an xperf profile (page faults, MiDecommit*) points at
  large per-call temporaries (>~64 KB vectors/strings): the NT heap decommits freed big blocks, so
  reuse them (builtins/scratch.hpp). MSVC: one std::string::append of tag+value (5 bytes) measured
  2x slower than push_back + append(4). VSDiagnostics rejects an empty /launchArgs. Python edits of
  UTF-8 files on Windows need encoding="utf-8" (em dashes).
- OPT3.6 (2026-10-20): the VS collector's sc.user_aux.etl carries stacks:
  `xperf -i <etl> -o s.html -symbols -a stack -butterfly` gives inclusive callers/callees per
  function (HTML; one <tbody> per function, rows marked --> callee / <-- caller); this found the
  growth path's page faults under memset/memcpy. Appendable capacities are powers of two so freed
  buffers are reused exactly (off_heap.cpp BufferCache, thread_local like heap_blocks).
- OPT3.15 (2026-10-20): this host is an i9-14900KF (8 P-cores = logical 0-15, 16 E-cores): an
  unpinned kernel swings 2x when Windows puts it on an E-core. Time kernels pinned:
  `cmd /c start "" /b /wait /affinity 5554 X.exe` (OTP too: same with erl.exe -noshell -eval). A
  runtime-only profiling build dir must match the main build's CRT
  (`-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DCLAUSE_MSVC_ITERATOR_DEBUG_LEVEL=0`); /MD made map
  kernels 2x slower. Ownership of fold accumulators is decided at run time (a frame slot, false on
  step 1): the initial accumulator may be shared, e.g. maps:fold(F, M, M).
- OPT3.18 (2026-10-21): count a kernel's page faults with psapi GetProcessMemoryInfo on the child
  handle (scratch pf.py, Popen + ctypes); demand-zero faults (MiResolvePrivateZeroFault) mean blocks
  miss the cache. A temporary stderr line in allocate_block's miss path showed which sizes.
- OPT1.6 caches (2026-10-23): a mutable global written from generated code is shared by every
  scheduler thread; a polymorphic site rewriting it on each miss made 16 processes 6-7x slower
  (cache-line bouncing). Bound writes per site (rewrite budget) and test such changes with a
  multi-process polymorphic negative control. A copied clau.exe outside build/*/bin cannot find
  library sources: put the baseline copy next to clau.exe. Cache only literal keys: luerl keeps
  tables/envs in small maps keyed by a runtime index, so a cache at such sites mostly misses (luerl
  benchmark ~2% slower across suites; literal keys only: 1.005). luerl concat and next_vs_pairs are
  layout-bound (hot runtime helpers like Term::kind are calls): unrelated code padding with the same
  clau moves raw_insert 754-865 ms, so a +-10% move there alone means nothing.
- OPT3.9 remainder (2026-10-23): `VSDiagnostics start /launch` returns at once; wait for the exe to
  exit (tasklist loop) before `stop`, else only startup is sampled. Padding `Arrivals` flags onto
  their own cache line sped fan_in but slowed ring ~2.5% (1000 processes' arrivals on two lines
  each): dropped. A small message's direct copy was ~18% of ping_pong, mostly fixed cost (size walk,
  GraphCopy setup, HeapReservation), not words.
- OPT3.23 part 1 (2026-10-23): ring's gap was process lifecycle, not hops (ring(1000,500) hops 315
  ns vs OTP 407; ring(1000,1) setup 2.3x OTP). MSVC std::map/std::list/std::unordered_map allocate
  on default construction AND on move/exchange: lazy-make or use boost::unordered_flat_map. Count
  allocations by call stack: a temporary operator new replacement (CaptureStackBackTrace +
  llvm-symbolizer --obj=exe, image base 0x140000000), #included at the end of executor.cpp in
  build/prof only. VSDiagnostics /launchArgs with spaces: quote the whole "/launchArgs:..." token;
  runtime options go first (--scheduler-cpus=0-15). Batch stealing (half the victim's queue)
  measured null/worse on ring_setup, short_processes, fan_in: dropped. The embedded first stack
  block is charged on the first push (state limit 0 until then), else exact-account tests and
  --max-memory 64 break.
- OPT1.10 remainder (2026-10-30): to judge an LLVM attribute or metadata idea, count `= load` in
  `clau -O2 --print-optimized-ir --entry bench_X benchmarks/bench_X.erl benchmarks/bench_util.erl`
  with the idea behind a temporary env switch in the debug clau (no runtime build needed). Call
  memory effects (argmem/inaccessiblemem) changed nothing: term addresses are inttoptr, which BasicAA
  cannot separate from pointer arguments.
- OPT3.23 part 2 (2026-10-30): an old change's impact at HEAD = `git revert --no-commit` in a
  scratch worktree (runtime reverts cleanly; resolve .agents conflicts to HEAD), runtime-only
  Release builds (`-DCLAUSE_BUILD_COMPILER=OFF`, same /O2 /Ob2 flags), one build/bench clau with
  `--runtime-library`, interleaved rounds with OTP via benchmarks/run.py's run_once and
  benchmark_cpus (import it). For a new change copy the base lib/Release dir aside first, then
  rebuild in place. Lock-free posts to queued receivers did not reproduce OPT3.5B's allocator
  regression: the direct copy into parked receivers stays.
- OPT3.23 part 3 (2026-10-30): runtime-only builds default to the /MD CRT (WindowsHost.cmake);
  the compiler build is /MT (LLVM). Configure A/B and profiling runtimes with
  `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`, else std::mutex goes through msvcp140.dll. The A/B
  scaffolding (run_ab.py, bench_ab.erl with ring_setup/pairs) is in a session scratchpad under
  `%TEMP%/claude/f--CLAUSE-clau/*/scratchpad/ab`: copy it forward. xperf `-a stack -butterfly`
  callers of mtx_do_lock split the lock cost per site. One ping-pong pair is fast; 16 concurrent
  pairs are 3.1x behind OTP (executor mutex contention).
- LIB74 shell (2026-10-25): a call that resolves to a C++ builtin does not pull the module's Erlang
  source into the batch, so its other exports (erl_lint:format_error/1) are absent for dynamic calls
  even with --keep-exports: name them statically (clause_shell_eval:format_error/2). A static M:F
  call to a module neither in the library nor on the search path is an "unknown module" compile
  error (c needs shell_docs to exist). A library module exporting main/1 makes entry detection
  ambiguous (clause_shell:run/1). Python text-mode stdin on Windows sends CRLF; OTP's stdin reads a
  CRLF line end as LF. The escript oracle can run shell:start_interactive(); goldens mask the system
  version line with "stdout_masks" and add compile flags with "options".
- LIB79A crypto (2026-10-26): OpenSSL 4.1.0 out-of-tree Configure with `no-apps` dies (apps/include
  missing): drop no-apps, build target `build_libs` + `install_dev`. Windows Configure needs native
  Strawberry Perl (Git's MSYS perl breaks paths); serial nmake ~4 min. Clang driver reads
  `ws2_32.lib` as a file: clau turns bare `.lib` names into `-l`. Generated `.link`/`.openssl` files
  are named after the archive (`$<TARGET_FILE_PREFIX>`): clau reads `<archive>.link`
  (libclause_runtime.link on Linux). Ubuntu's static libcrypto needs `-lz -lzstd` (read from
  libcrypto.pc Libs.private; no pkg-config in WSL); libcrypto in lib/x86_64-linux-gnu. Clause has no
  frame for a failing builtin, so `?nif_call` passes the builtin's args to error/3 explicitly.
  `maybe` is a reserved word in OTP 29 fixtures. Library apps with a `<dir>/<dir>.app` (crypto)
  count as resources beside their modules.
- LIB79B ciphers (2026-10-26): host OTP 29.1.1's OpenSSL 3.5.5 has no legacy provider (no des_cbc,
  blowfish, rc4, rc2): goldens use default-provider ciphers only. OTP's crypto on_load loads the
  crypto app (rand_cache_size 896 from crypto.app); Clause loads it in rand_cache_size().
  enif_snprintf %T prints like ~p on one line (strings): io_lib::pretty_print with line_length 0.
  One ?nif_call per function clause (its catch variables are unsafe after the try). Compile OTP-side
  fixtures into a fresh scratch subdir: stray Clause rand.beam on -pa kills boot.
- LIB79F FIPS (2026-10-26): OpenSSL Configure disables fips with no-module, so a FIPS provider is
  always a separate module (static libcrypto still loads it: provider_core has no OPENSSL_NO_MODULE
  check). OSSL_PROVIDER_load_ex params become the provider's config (module-mac etc.). Windows
  LoadLibrary of a bad DLL pops a modal "Bad Image" box that blocks tests: wrap loads in
  SetThreadErrorMode(SEM_FAILCRITICALERRORS). Python subprocess on Windows resolves argv[0] with the
  parent's PATH, not env=: shutil.which(name, path=env PATH). `after` is reserved.
- LIB79C public keys (2026-10-26): `TermResult<T>{}` default-constructs a VALUE, not an error (use
  `integer_of`/optional). An OSSL_PARAM big number of zero needs a non-null data pointer (fromdata of
  explicit curves with A=0 fails otherwise). OTP quirks kept: `sign(dss, none, ...)` = "Bad digest type"
  (compat clause swaps algorithm/type), DH keygen ignores a given private key, RSA exponent bytes go to
  OpenSSL native-endian (65539 -> 196609 on x86). OTP's sign/dh/eddh ?nif_call pass Args/ArgMap swapped:
  error_info arg = C arg + 1. Configuring build/debug once without the toolchain script (VS generator,
  cl) wiped thirdparty's OpenSSL install (identity has the compiler) -> 4 min serial rebuild; clang-cl
  23.1.2 crashed twice at random files in that nmake build, a retry passed.
- LIB80A ASN.1 (2026-10-26): oracle = host asn1ct (escript compiles specs with outdir+i+noobj, then
  loads); asn1ct's runtime record names (list2rname) differ from its .hrl names (SEQOF/SETOF always,
  SET with pname skipped, ptype records first, .set.asn lists reversed, object-instantiated ptypes
  `<Def>_<PType>` after types and INSTANCE OF). DER decodes a SET in tag-sorted order (OTP quirk).
  OTP hrl/value functions keep `{'Externalvaluereference',...}` inside open-type values (PKCS-1);
  ours resolves (differences.md). Scratch cycles compared Kitchen (8 option sets) and all
  public_key_SUITE PEMs + PKITS certs/CRLs byte-identical; redo for any codec change. Analyzer flags
  a leak when a [[noreturn]] throw runs with a boost cpp_int alive: keep the value as text first.
- LIB84 PER (2026-10-26): asn1ct PER is the oracle, not X.691: CHOICE alternatives in textual order, SET root sorted
  by tag, size bounds >= 64K ignored, k_m strings align when Ub*unit >= 16, empty open type/encoding is <<0>>.
  Records: additions after a multi-component [[group]] are read shifted (OTP bug, mirrored). Decode error payloads:
  dec_string cases -> case_clause on bits before the determinant; a separate read -> badmatch on bits before its
  alignment padding; uper skipextensions/decode_fragmented_1 are function clauses. Scratch cycles: kitchen/cycle.sh
  `<opts> per_test PerKitchen.asn` and emqx/cycle.sh (all 8 emqx specs). The directory index caches listings from the
  source walk: forget() the ASN.1 outdir after generating into it.
- LIB80E (2026-10-26): public_key C++ ports call other builtins via `calls.hpp` (registry find + call_builtin,
  `Recorded` -> return Word 0); lists:reverse/1 is not registered (Erlang), write it locally. PKITS oracle: a scratch
  python turns a copy of OTP's pkits_SUITE into pkits_run (main/1 runs all 99 cases, prints each
  pkix_path_validation result with maps as sorted lists; OTP prints small maps in atom-index order); Clause side
  needs shims pubkey_cert (extensions_list/select_extension/is_issuer) and pubkey_cert_records (transform/2) and a
  minimal erl_make_certs (pem_to_der); clau rejects -compile(export_all). 211 validations, compare after any
  public_key path/CRL/name change.
- Perf (2026-10-27, Release): a C++ builtin walking Erlang terms through the Term API (is_tuple, tuple_element,
  head/tail, same_oid) ran 1.7-4x slower than LLVM-compiled Erlang pattern matching over the same 100k CRL entries.
  C++ wins only on algorithmic gains (avoiding O(n^2) binary appends, native loops over bytes) or skipped Erlang
  wrapper calls; measure in build/bench (Release) before keeping a port, never on the Debug runtime.
- LIB81A ssl (2026-10-27): user chose hybrid libssl design (Erlang connection process, memory BIOs, verify pause
  for public_key path validation), TLS 1.2/1.3 first. clau only adds library modules named by calls/funs/behaviours:
  start modules of library `.app` `{mod, ...}` are now added (frontend `library_start_module`), and
  clause_applications includes library resources of `applications` deps (else ensure_all_started(ssl) fails on
  public_key.app). Inventory stubs need bare `_` heads. OTP quirks kept: clear_pem_cache sets the caller's
  `ssl_pem_cache` dict key; protocol_version=[sslv2] stores defaults then function_clause; str_to_suite parses
  without the table (list_to_existing_atom). Host OTP lacks legacy provider: fixtures drop RC4/DES suites.
- LIB81B ssl engine (2026-10-28): calls of catalog builtins (clause_ssl_engine:*) name no module, so its .erl never
  joins the batch: link decisions read semantic services (backend calls_ssl_engine -> Compiled::ssl, cached part
  "ssl"). OpenSSL headers pull windows.h: `small` and `max` are macros (use is_integer/integer_value, no std::max).
  SSL_get_error depends on the last libssl call: compute a status before any follow-up call. A received KeyUpdate
  request is only queued (key_update=NOT_REQUESTED, not in init): SSL_key_update(NOT_REQUESTED)+do_handshake sends
  the answer. SSL_set_retry_verify pauses servers only from libssl 4.1 (3.1.2 fails with -1); the callback must
  return 1 after it. linking_crypto_minimum runs the ssl_engine golden on 3.1.2 (deferred server verification).
- LIB77A transform host (2026-10-27): host = loader linked by `clau --shell --entry
  clause_transform_loader:main/1`, ~40-60 s Debug build, cached in %LOCALAPPDATA%/clause/transform-host
  (delete it to force a rebuild). The clau target must not link clause_llvm_sdk (its interface flags
  turn RTTI off and change the iterator debug level): LLVM-using code goes to clause_transforms etc.
  Differential check of host vs OTP: scratch mkreq.escript (epp:parse_file forms -> request.etf), run
  host `-- DIR` and erl loader, compare reply.etf terms. qlc_pt's per-answer replay was quadratic
  (qlc_queries 220 s): answers keyed by query, unanswered attempts batched (3 runs, ~4 s each on the
  Debug runtime). User direction: provide only the non-cascading `compile` functions (format_error,
  env options, output_generated); compiling stays stubs.
- LIB77B clause_interp (2026-10-27): runtime routes undefined calls into registered modules to the frame of the fun
  passed to clause_interp:register/4 (error_handler:undefined_function/3) with registers [M, F, Args]; error_handler
  hands interpreted modules to clause_interp:call/3. Function closures take ONE list argument (no 20-arity limit);
  local calls -> '$clause_interp_local':F(...), tail calls wrap the module expr in {'$clause_interp_tail', M}
  (erl_eval's remote call accepts any module term and passes {M, F} to the 3-arity non-local handler with Anno).
  Compiled calls get no try wrapper (next interpreter catch rewrites from the unchanged shadow stack). Clause never
  shows failing-BIF frames ({erlang, element, ...}), so goldens print only interpreted frames. OTP's
  code:module_status of a load_binary module whose file is a source = removed. Interpreted sources in executables
  fixtures need a non-.erl suffix (`.erl.in`), else run.py's rglob compiles them into the program.
- LIB77C transform sources (2026-10-27): routing in pipeline.cpp request_of: all library-provided -> host; else erl
  when found; else host if every transform is library/.beam/project source (library module shadowed by a .beam is
  not interpretable: host links its own). erlc omits debug_info by default -> test fixtures build with
  +debug_info. erl_error prints "(File:Line)" in OTP 29, not "(File, line N)". Python test edits via bash heredoc
  turn "\n" in strings into real newlines (use Write/Edit).
- LIB77D switch-over (2026-10-27): request_of routes to the host unless --erl names a path (explicit erl keeps the
  old erl routing, so tests with `--erl erl` still exercise erl). Parse/preprocess diagnostics go to transforms as
  {error|warning, {{L,C}, clause_source, Text}} forms before eof (importer skips them; frontend prints them after the
  transform's diagnostics unless it failed). Loader rewrites returned {error|warning, {Loc, M, D}} to clause_message
  text. Removing the executables .abstr goldens made qlc_queries compile qlc_pt in the host: +56 s alone, >300 s
  (timeout) in the loaded full suite, so the .abstr goldens stay.
- LIB77E1 compile (2026-10-27): user asked "continue LIB77" = LIB77E (split E1 compile, E2 erl_syntax, E3 prettypr,
  E4 syntax_tools lib/epp_dodger, E5 merl). compile:forms/file return interpreter images (beam_lib container: AtU8,
  Attr, CInf, Dbgi, extras, ClFm forms chunk) built by clause_interp:beam/2; code:load_* -> clause_interp:load_binary
  via a non-literal module. erlang:module_loaded(M) is TRUE for any module with runtime builtins even when not
  linked (clause_interp has 4) -> test linkage with function_exported of an Erlang function. OTP's listings return
  {ok, [], ...} (module set only by v3_core). compile_forms golden identical on first run; host OTP compiler 10.0.6,
  pin 10.0.7 (golden prints no version). Windows Debug build was broken since c163afd5 (boost.parser pulls
  windows.h min/max into numbers.cpp): NOMINMAX in parsing/boost_parser.hpp. Git Bash `cmd //c` mangles
  backslash paths: launch .state scripts via PowerShell Start-Process.
- LIB77E3a prettypr (2026-10-27): done in parallel with session clau-70 (LIB77E2 erl_syntax) in the SAME working tree:
  agree file ownership by message, announce every build/ctest (shared CPU, ninja log lock gives "failed recompaction:
  Permission denied"), commit with explicit paths. Library local calls to builtin placeholder functions run the
  placeholder (function_clause later) -> qualify them (prettypr:best/3 from format/3). prettypr best/3 is a C++ loop
  of OTP's rewrite/2 rules; golden prints best/3 docs (OTP's joined strings [W | [C1 | C2]]) identical.
  clang-tidy bugprone-easily-swappable-parameters: pass structs (Ctx/Doc) instead of adjacent Index/Number params.
- LIB77E2 erl_syntax (2026-10-27): all 303 C++ (syntax/), golden identical on 2nd try (pid in error, map print
  order: atom-index in OTP -> print sorted). Tables + consteval names: FieldName/SimpleName need a char-array
  template ctor (literal -> string_view -> class is two user conversions). Tidy swappable-params: Named roles
  Pos/Value/Target/Groups/Tail in syntax.hpp. observe_types.escript missed records nested in records (#com in #attr):
  fixed. Shared tree with peer session clau-5b (prettypr): commit own versions of shared files through a temporary
  GIT_INDEX_FILE (read-tree HEAD, hash-object -w, update-index --cacheinfo), leaving the peer's hunks and index alone.
- LIB77E3b erl_prettypr (2026-10-27): Erlang (hooks). Quick oracle trick: compile the library .erl with host erlc
  into a dir, run the golden program with `erl -pa thatdir` (syntax_tools is not sticky) and diff with plain OTP run
  -- identical before any Clause build. Marker text needs Module:format_error, absent unless the program links the
  module (golden links erl_lint/erl_parse explicitly). erl_prettypr needs erl_syntax_lib map/analyze_type_application:
  private copies (whole-module rule) until LIB77E4.
- LIB77E4a erl_syntax_lib (2026-10-27): split E4 into a (erl_syntax_lib), b (comment scan/recomment), c (epp_dodger).
  Golden identical on first run, rand-based new_variable_name included (Clause rand exsplus = OTP's). New executables
  case: write a golden.json skeleton ({schema, entry, runs:[{args:[],stderr,exit_status,stdout:""}]}) before
  regenerate.py --case accepts it; erlfmt the fixture BEFORE the final regenerate (inputs hash). erl_parse rejects
  `-import(M).` (build such forms with erl_syntax). C++ catching a nested erl_syntax error: catch BuiltinFailure,
  rethrow when `.term` is set; throw(syntax_error) = raised_throw with the atom payload. typed_entry works for any
  module (made_entry is erl_syntax-only); Named<Role> params silence swappable-parameter findings.
- LIB77E4b comments (2026-10-28): erl_comment_scan + erl_recomment C++ (builtins/comments/); golden identical once
  file/1 called string/1 qualified (local call to a placeholder runs the placeholder). string:trim of a list with
  non-chars raises function_clause in OTP's unicode_util: keep goldens to char texts. Long python edits: write the
  script with Write to the scratchpad and run it (a heredoc with many quotes broke bash parsing). Clang warns on
  missing designated-field initializers (-Wmissing-designated-field-initializers): use small factories instead.
- LIB77E4c epp_dodger (2026-10-28): golden diffs came only from Clause's file I/O server feeding undecodable UTF-8
  as Latin-1 (and over-unreading by re-encoding them): now chunk_chars seeks back over them and until/5 stops with
  OTP's {error, {Loc, file_io_server, invalid_unicode}, Loc} (erl_scan cont) or {error, F}. Quick scratch repro:
  clau X.erl --entry X -o X.exe from a .cmd that calls tools\windows-toolchain.cmd, then run the exe from bash.
- LIB77E5 merl (2026-10-28): golden identical once (1) frontend.cpp maps include_lib "syntax_tools" to library/
  (library apps are listed by hand there: erts, kernel, stdlib, public_key, syntax_tools) and (2) the program calls the
  compile_and_load'ed module through a runtime name (clau rejects static calls to unknown modules). `std::vector{x}`
  with one vector argument copies it (CTAD): write std::vector<Items>{...}. Lizard CCN 10 also flags if-chains of
  shapes: use a table of {type, sizes, build} rows. syntax_tools now 422/422 documented functions.
- LIB81C1 ssl connections (2026-10-28): OTP probes in scratch (erlc + `erl -noshell -s M main x`) before the golden.
  OTP quirks: `ssl:listen(P, [])` = {error, nooptions} (any non-empty list listens, even without certs); handshake/3
  ignores socket options (use setopts after); active-mode data coalesces unless {packet, N}; pkix_test_data roots
  share subject names, so issuer lookup needs the signature check (pkix_verify) or unknown_ca turns bad_certificate;
  server sends cert + issuers from its cacerts (partial_chain needs that); upgraded gen_tcp sockets inherit
  mode/packet/header/active; Windows connect to a closed port takes ~2 s (no "refused" cases with short timeouts);
  pkix_test_data SAN defaults to the host name (set {extensions, [SAN]}). Clause decode_packet badargs on
  line_delimiter for non-line types. Golden prints alerts as {tls_alert, Desc} (texts name OTP source lines).
- LIB81C2 ssl hello pause/closing (2026-10-28): OTP quirks: handshake/3 with non-empty options on an accepted
  socket returns {ok, Ext} (raw records, no socket; ssl_gen_statem:handshake/3 passes the reply through) - use a
  {handshake, hello} listener + handshake/2 in probes; calls during the pause are postponed (hang); close/1,2 use
  DEFAULT_TIMEOUT (5 s shutdown(write)+recv), close/2's timeout unused; shutdown(write) sends no close_notify.
  Pause design: Erlang holds the peer's first flight until the hello message is complete (records parsed in
  clause_ssl_hello), libssl sees it on continue; cancel/internal_error go out as plaintext alert records. libssl
  ignores a TLS 1.3 user_canceled, so transport_ended reads info(Engine).alert. Peer hellos differ (libssl client:
  renegotiation_info, no signature_algs_cert): golden prints keys minus those. Bash heredoc with nested python
  '''strings failed ("unexpected EOF"): use Edit tool or python -c.
- Fast gate slowdowns (2026-10-28): wall time 460-1519 s for the same ~430 tests is contention, not growth. Causes:
  the transform host rebuilds after every library change (first host user ~90-110 s at all threads, other host
  users wait on its lock), a fresh build dir has no CTestCostData, VS Code cpptools re-indexes after reconfigure, and
  each executables case runs 2 clau at once with all hardware threads (no --jobs). Rank candidates by time alone
  (ctest1.cmd), not by loaded runs. Load-sensitive list + PROCESSORS in the top CMakeLists.txt.
- LIB81D1 ssl features (2026-10-28): split LIB81D into D1 (renegotiate/update_keys/exporters/prf/keep_secrets) and
  later steps. OTP quirks: TLS 1.2 export_key_materials takes only one-element lists and returns {ok, Bin} (others
  crash the connection -> {error, closed}); export with length 0 or -1 hangs OTP; update_keys on TLS 1.2 crashes OTP's
  sender; connection_information/2 gives client_random/server_random/master_secret without keep_secrets, and raises
  case_clause in the caller with a keylog fun; /1 never gives secrets; items come in OTP's order, not the asked one.
  Idle passive OTP connections process handshake records (renegotiation works) but act on close_notify only once read
  from or active (passive close/2 downgrade after the peer's close_notify hands over). libssl: a refused renegotiation
  (no_renegotiation warning) is fatal for the requester; servers refuse client renegotiation without
  SSL_OP_ALLOW_CLIENT_RENEGOTIATION; a server's HelloRequest fires HANDSHAKE_DONE while SSL_renegotiate_pending is
  still 1; TLS 1.3 post-handshake messages fire no START/DONE; a libssl client logs CLIENT_HANDSHAKE_TRAFFIC_SECRET
  only after the server's Finished. OTP probes with possibly hanging calls: wrap in a spawned limited/1 with timeout.
- LIB81D2a ssl sessions (2026-10-28): split D2 into a (TLS 1.2 reuse), b (TLS 1.3 tickets), c (early data). OpenSSL
  has no async session lookup (SSL_magic_pending_session_ptr is BoringSSL): pause in the ClientHello callback
  (SSL_CLIENT_HELLO_RETRY -> SSL_ERROR_WANT_CLIENT_HELLO_CB), answer, then the external get_cb returns it. A libssl
  server without SSL_SESS_CACHE_SERVER sends an EMPTY session id (OTP always sends one): servers keep SERVER|NO_INTERNAL.
  Libssl's default SSL_CTX cache mode is SERVER with internal store. OTP quirks: reuse_session fun gets the suite as a
  2-byte binary (docs say map); client registers only verify_peer sessions; server reuse_sessions default false when
  verify_peer; ssl:stop prints an =INFO REPORT= unless logger primary level >= warning. New OTP-named library modules
  need PROVIDED(...) in compiler/src/driver/library_modules.inc (`library_modules.py check .`). clau crashed 3 times
  (0xC0000005 / 0x80000003) compiling ssl_sessions -O2 cold with a clause_ssl_options variant, jobs >= 2; jobs 1
  passed and cached it; not reproducible after a reboot (function-level cache: comment-only probes stay warm, a
  failed compile caches nothing). 2026-10-28 retry with CLAUSE_OPTIMIZED_COMPILER=OFF: 54 cold -O2 compiles
  (positional --no-cache and project with fresh build/, jobs 2/4/8, current and crash-time D2a library) plus
  5 run_case loops all compiled; no crash. Suspect a transient host fault (pre-reboot) or optimized-only UB.
- LIB81D2b ssl tickets (2026-10-28): OTP's auto ticket store is keyed by SNI only and takes oldest first, so stale
  tickets of other listeners (same SNI) make later connections full handshakes: fixtures give each scenario its own
  server_name_indication (client verify_none). Resumed TLS 1.3: client peercert {error, no_peercert}; server only with
  _with_cert. libssl: generate_ticket_cb (after timeout calc) sets per-ticket lifetime; stateful TLS 1.3 tickets go
  through the external get/new session callbacks (needs SSL_SESS_CACHE_SERVER); clear SSL_OP_NO_TICKET for stateless
  and set it per connection in the ClientHello callback when TLS 1.3 is not chosen (no RFC 5077 tickets). Client
  new_session_cb needs SSL_SESS_CACHE_CLIENT. Bash heredocs with apostrophes in python strings fail: use scratch .py.
- LIB81D2c ssl early data (2026-10-28): OTP 29.1.1 server crashes (unexpected_message) on early data above the
  ticket limit and on a used stateful ticket with early data: fixtures leave those out (rows in differences.md). A
  legit ticket reuse under anti_replay is accepted (fresh binders). libssl's anti-replay calls SSL_CTX_remove_session,
  which only succeeds for the INTERNAL cache: set SSL_OP_NO_ANTI_REPLAY on every ticket server (own single-use store).
  Server early data: SSL_read_early_data loop before SSL_do_handshake (it drives the ClientHello callback pauses too).
- linking_debugger flake (2026-10-28): lldb sometimes prints `expr -- clause::runtime::debug_erlang_stack()` output
  late (hex inside "unexpected response to k packet"), failing debugger.py's assert; fails ~1 in 3 runs, passes alone
  on rerun. Not related to library changes.
- LIB81D3a ssl SNI (2026-10-28): OTP calls sni_fun twice for a name answered undefined (fixture prints usort).
  clause_ssl_options update with cert/key kept the old certs_keys (fixed: a new cert/key replaces them). Switching
  SSL_CTX in the ClientHello pause: SSL_set_SSL_CTX copies certs only; copy verify mode, version bounds, options and
  ciphers (SSL_CIPHER_get_protocol_id >> 8 == 0x13 for TLS 1.3 names; no SSL_CIPHER_get_min_tls in 4.1).
- LIB81D3b ssl rekeys/transports (2026-10-28): OTP TLS 1.3 key_update_at: before a send, if bytes+size > limit send
  KeyUpdate(requested), reset, split data larger than the limit (sizes 5,5,1,25 with limit 10 -> traffic secret 0,0,1,4);
  TLS 1.2 renegotiate_at counts the write sequence (Finished = 1) and renegotiates when seq >= limit; a second client
  renegotiation within 12 s is refused by the server and OTP's client connection closes. cb_info modules already
  worked; OTP crashes (badarg in apply) for a non-atom module, so no golden for it.
- LIB81D4a ssl CRLs (2026-10-28): OTP's insert/1 (no DP) stores CRLs by their encoding's issuer while the same-issuer
  lookup uses the OTP-form issuer from pkix_dist_point, so "revoked by issuer" is undetermined (bad_certificate) in
  OTP 29.1.1 - mirrored, documented. Build CRLs in fixtures: plain TBSCertList + public_key:sign + der_encode
  CertificateList; root key via pkix_test_root_cert. Catalog "declined" stubs need bare `_` heads. Clause has no inets.
- LIB81D5 ssl PSK (2026-10-28): user chose PSK only, SRP declined; OCSP stapling skipped (optional LIB81D4b). libssl PSK
  callbacks are synchronous: engine scans the peer's plaintext records (server: ClientKeyExchange identity, client:
  ServerHello suite kx + ServerKeyExchange hint + ServerHelloDone) and holds that chunk while Erlang runs
  user_lookup_fun. OTP: client fun gets the hint (undefined without SKE, <<>> for empty), server fun the client's
  identity (default <<"psk_identity">>), error -> illegal_parameter. libssl lacks ECDHE-PSK AES-GCM/CCM suites.
  ssl no longer PARTIAL in library_modules.py.
- Windows `start` (2026-10-29): never pass `start "" ...` or `start "title" ...` through Git Bash
  (`cmd //c "start ..."`): the quotes are mangled and Windows pops "cannot find" dialogs on the
  user's desktop. Put every start/affinity/belownormal line in a scratch .cmd file (use "%~dp0x.cmd"
  for paths; printf turns `\f` in paths into form feeds, write .cmd files with a quoted heredoc).
- OPT1.8 remainder (2026-10-29): measure a sinking idea first by writing the bound and the
  written-in-call forms side by side (the written-in-call form is the ceiling); sunk bindings bind
  a placeholder word and any read that does not inline builds the fun (sunk_read).
- OPT1.9 leaf inlining (2026-10-29): a Clause call costs ~2.8 ns (frame push + 48-byte header memset +
  return through the frame; OTP ~0.7), so T2's "no inlining for call overhead" is false here; LLVM
  cannot inline across musttail frame transfers, so lowering inlines (nested ExpressionLowering of
  the callee: own function/reads/bindings, shared builder/roots/handler/exits). Tests that patch
  function bodies after lowering (codegen_failure's fault seams) must call them remotely
  (?MODULE:f()). lldb + PDB cannot break by .hrl line inside inlined code (no own function left).
- OPT1.9 remainder/OPT1.13 (2026-10-29): LLVM 23 BasicBlock::getTerminator() asserts on an open block
  (use hasTerminator()/getTerminatorOrNull()); the Release clau read garbage and crashed only on luerl
  (fixtures passed): build luerl euler with both clau after lowering changes. Lowering records its joins
  (ExpressionLowering::joins) because PHI operands trip the ArrayBound false positive; join answers with
  SSAUpdater. Leaf budget 64 grew luerl 1.3% for nothing measurable, 32 lost a 40-expression leaf: 48.
  User saw a "Windows cannot find '\'" dialog from an inline `cmd //c "start \"\" ..."`: never inline start.
- CPU budget in practice (user 2026-10-29: a plain belownormal -j 12 run loaded the PC to 100%):
  launch every build/tidy/CTest run with `start "" /belownormal /affinity FFFFFF /b /wait cmd /c
  X.cmd` (logical 0-23: P-cores + 8 E-cores; children inherit it), leaving E-cores 24-31 free.
- OPT1.10 (2026-10-29): datagen.list_of_tuples swings ~15% on generated-code layout alone (the
  tuples/2 loop byte-identical at the same address; padding the program with an unused module
  flipped fast/slow either way; runtime library swap no effect). Before blaming a change on it,
  cross-link (each clau x each runtime) and pad with a dummy module (--keep-exports). Bisect codegen
  changes with temporary env switches in the debug clau + `--runtime-library` of build/bench.
- DIST4a1 (2026-10-31): application_controller may not log before it registers: clause_logger's init reads kernel
  env through application, whose ensure_started spawns a second controller (hang). OTP's merge_app_env reverses its
  result, so even a merge with an empty sys.config env changes which --env duplicate wins: copy OTP's two merges.
  Python write_text on Windows writes CRLF; use write_bytes. Library .erl files are erlfmt width 120
  (.state/scripts/fmt.escript); width 80 reflows cannot be undone by reformatting.
- DIST4b2a (2026-10-31): on this PC `wslrelay.exe` holds 127.0.0.1:4369 and ::1:4369 (WSL's forwarding), so plain
  OTP `erl -sname` nodes on the default epmd port never see each other; experiment with `ERL_EPMD_PORT` set (the
  goldens use their own ports). The executables oracle sends logger output to stderr, so OTP's log lines belong in
  a node's `stderr` pattern. `process_flag(priority, _)` is badarg in Clause (spawn option `{priority, max}` is
  accepted). Python `Path.read_text()` without `encoding="utf-8"` reads cp1252 on Windows and double-encodes
  non-ASCII on write (`×`, en dash): always pass the encoding. Library inventories must list exactly OTP's
  exports, so helper processes spawn funs instead of new exports.
