# Project memory — 2026-10-03

Completed pattern/guard steps 1–20 and added15a are archived in .agents/00-finished.md.
The retired checklist was removed by user request; durable context is .agents/11-plan.md#completed-patternmatch.
No subagents authorized or used. Separate current commits: step17 7d83b99,
step18 684af35, step19 428c388; final step20 commit 2be9626; step21 d5a9d36.
Each implementation commit followed fresh combined Debug build/fullCTest/Lizard/tidy.

Current admitted scope: recursive local/exported remote functions, ordered heads,
body matches/sequences, grouped/strict/lazy guards, rooted checked construction/
access/comparison/numeric services for owned atoms, arbitrary integers, finite
floats, tuple/list/string/map/bitstring/ordinary tuple records. Descriptor ABI4,
checked failure channel2; ownership checked before extraction, handoff before pop.
Guards reject reached semantic errors; infrastructure failures halt. Specs grant
no representation authority. Clause candidates isolate SSA, facts index successful
whole-value assignments; extracted/unproved values stay top. Specialization/work/
IR caps unchanged; generic fallback verifies.

Evidence summary docs/validation.md (history), patterns.md and
guards.md; old step records/evidence JSON removed in plan11 step 1C (Git history). Nineteen owned corpora: 67,634 native expected outcomes +106
semantic rows, eight driver/policy combinations, two executions =>1,082,144
comparisons. Seeded closure1969, seed0x29A07, depth64,width255,128alternatives;
allmanifest hashes and81catalog mappings reconciled. 77signatures admitted-domain,
fourdependency gates:self0,node0/1,nativeis_record1. Positive pid/port/ref/fun
representations not admitted. All19 explicit OTP regeneration --check pass.
Updated example42,-7,record,map,binary,list,integer,other passes four policies.

Step20 final fresh gate124/124 no skips,167.43s,258production quality units,
LizardCCN10/tidy cognition10 unchanged. Logs build/patternmatch-step20/{gate,tests,
quality,all-regeneration}.log. Initial123/124 failed testserialization256 around
wrapped255-celllist; boundedtesttransport512 fixes; no production ceiling changes.
Historical records preserved. Native Windowsx64 only; Linux/AppleSilicon/native32
and new frontend sanitizer unavailable. Foreignobjects/32-64IR/suiteparsing separate.

Official maint-29 fetched at each OTP-dependent step; last2026-10-03 unchanged
21776803ecd11f5fa948732c0ec66b8f325dedfc upstream/pin/cleancheckout. Follow
docs/otp-reference.md to refresh future work, synchronize hashes/grammar/docs and
preserve historical revisions. Oracle29.1.1/ERTS17.1. Installed escript broken
erl.ini; workspace shim build/patternmatch-step16/otp-launch/escript.exe points
installedruntime via privateerl.ini, no installedfilesmodified. Regenerate
python tests/compiler/patternmatch/regenerate.py --otp references/otp --escript
build/patternmatch-step16/otp-launch/escript.exe --corpus all --check.
Normalbuild/test OTP-free; liveaudits opt-in. Fixtures writebytes UTF8LF (-text).

Toolchain LLVM23.1.2/SDK, VS18x64SDK10.0.26100,/MT iterator0, Ninja, Boost1.90,
toml3.4,Lizard1.24,tidy22.1.8. VSdevshell required for native consumers; SDKdependent
gate and protected .agents/.git writes require escalation. No auto-review rejection.
Gatecmd build/patternmatch-step20/gate.cmd uses --fresh combined Debug. Keep intent
comments/clangformat and low complexity; no quality suppressions/threshold increases.

Remaining owners: F01launcher/linking,F04GC,F05graphcopy,F07identities,F16other
control,F17updates/record_info/native-qualified-inferred,F18closures,F19dynamiccalls,
F20handlers/raise/traces,F21recursion/tailcalls,F22-25processworkers/messages/receive,
F26genericproductionbuiltinregistration,V01nativematrix,V02sanitizers. Backlog closes
only delivered function/body/guard and prerequisite representation/root/service slices.
OTP loader huge literalrecordguard and core_to_ssa legacy-modernimport crashes
remain documented17/18; do not claim those contexts as executed OTPgoldens.

Planning update2026-10-03 (rewrite): user deleted 51-step draft; .agents/11-plan.md now
78 small single-commit steps, phases A-N, each with success criteria+tests. Exes early
(3-8) so later steps test via executable golden runner (step8). Decisions: 3 entry,17 frame
model,24 GC policy,53 ports,71-77 D-items. Mandatory1-70+78. 01-todo links remapped.

OTP source cleanup2026-10-03: replaced130 notice-bearing fixture source files and
removed copied assertion/file headers+license. Local source fixtures now under
tests/fixtures/patternmatch/fragments (222 Erlang/include files+corpus.json).
authored.py stages fixed local inputs; stored.py resolves source/observation roots.
Regeneration observes local code, never extracts OTP source. Keep observations
committed per explicit user answer; OTP source/generated headers/audits transient.
erlfmt installed+moved to ignored thirdparty/tools/erlfmt per user instruction;
valid source/header and changed term files formatted. Preserve user's AGENTS edits.
Fresh combined gate125/125+258unit Lizard/tidy, all19 --check, upstream audit,
grammar/corpus pass; maint29+pin unchanged21776803. Audit summary docs/validation.md;
logs build/otp-cleanup*. Existing dated validation evidence not rewritten.

MSVC cl (non-clang-cl) 2026-10-03: /external:W0 misses codegen C4702 (Boost.Parser) and STL pair
narrowing C4244/C4267 from LLVM headers; disabled only on clause_parser_dependency and
clause_llvm_sdk interfaces for cl. clau builds under cl; runtime still fails cl C4554
(float_factory.cpp/bit_factory.cpp:23, project code, already parenthesized).

Plan11 steps 1-8I done 2026-10-03..04 (compact record in .agents/11-plan.md; logs build/plan11-step*).
Step facts beyond the plan record:
- 1/1A/1B: maint29 21776803 unchanged. Fast mode = matrix.py O0 positional + O2-off project, mutations
  once, LABELS full_only excluded. CLAUSE_QUALITY_BASE overrides HEAD for changed-scope quality.
- 2: fixture layout project.toml, src, fixture.json entry+argv, expected/{stdout.txt,golden.json},
  compile.txt exact stderr; oracle tests/compiler/programs/oracle.escript (main/1 in spawn_monitor).
- 3/3A: project/entry parse_entry, manifest decode entry(); driver/entry resolve_entry after
  index_inputs; escript rewrites line 1 to `-module('<base>__escript').`, %%! warns.
- 4: OTP display = C printer erl_printf_term.c, not ~w. OTP 26+ map order follows atom index (varies per
  VM run) / hash order > 32 keys -> print key order, goldens skip unstable (values.stable_order).
  AtomStorage::boolean needs "true" pre-interned (registration does it).
- 5: startup llvm module appended after inputs (index >= inputs = startup). Windows argv via
  _configure_wide_argv + __wargv (no shell32). halt -> CallFailure{halted, halt_status, slogan}.
- 6/6A/7: linking lib clause_linking; default runtime = CLAUSE_DEFAULT_RUNTIME relative to
  clau; clang found via --linker, PATH, $ProgramFiles/LLVM/bin; works outside vcvars (lld-link).
  Project rule: link iff not frontend action AND (manifest output|entry or CLI -o|--entry); else
  in-memory library compile. -Os = O2 pipeline + optsize attr (LLVM 23 has no Os level).
- 8: run.py uses matrix.combinations() (8 full / 2 fast), ~3 s per case. Pass --suffix=... as one token.
- Phase C (user commit bb09359): single heap only, old heap/minor GC deferred (F04).
- 8B: refc cell linked only after publish (built with empty shared_ptr, rollback needs no destructor).
- 8C: abi::v1::primary_mask is 32-bit `unsigned`: cast to Word before `~`.
- 8E: Term {value_, atom_, heap_, weak lifetime_, collections_}; GeneratedCallState::visit rebinds payload.
- 8F: Segment = ONE operator-new of 4096 - 2*sizeof(void*) (Win64 HEAP_ENTRY 16 B). Segments exist only
  because generated code holds absolute Word* from roots_enter across calls.
- 8G: HeapArea{words_,capacity_,top_}; HeapOptions positional {words, bytes}; rollback drops a new
  fragment/heap block, so tests expecting capacity 0 after failure hold. Fragment vector grows
  geometrically (reserve(size+1) per push was quadratic). 100k kernel ~3,000 fragments.
- 8H: Copier ctor allocates to-space then ++collections_ (GeneratedCallState::visit rebinds payload with the
  new count). First block = policy(used words); shrink = second full copy when <25% live (no offsetting).
  collect() never touches the generated failure channel; native/failure consumers now use deferred send
  and heap reservation faults. Term::bit_slice is unimplemented (link error): slice via CLAUSE_bits_v1.
  Python Path.read_text defaults to cp1252 here: always read_text(encoding="utf-8") or use Edit.
- 9: case = one-input body plan per clause (`body_pattern_plan`, `semantic::pattern_root`); exported names share one
  BindingId across clauses (first clause allocates, later reuse via branch_names stack), so lowering PHIs only
  `Function::exports`; inference keeps multi-definition identities top. expression_children(case) = scrutinee +
  guard tests + bodies (patterns via function.patterns); guard_analysis schedules case guards as guards.
  Pre-existing fix: capability pattern planning skipped after failed binding pass (was "invalid map<K, T> key").
  Bindings corpus: 8 case_* rows, OTP classes via regenerate --corpus bindings; sibling_local now compiles.
  Old step-9 attempt (pre phase C) sources reused for executables case_select/case_scope.
- 10: if = case without scrutinee/pattern via semantic::branch_clauses (Branch{pattern*, guard*, body*}); CaseJoin.value
  null for if. Runtime CLAUSE_raise_v2 whitelists atom-only reasons (plain_reason) -> new plain reasons need it.
  OTP lint rejects `X =:= 0.0` in fixtures (match_float_zero). bindings.term rows are authored expectations; OTP
  regenerate only verifies them (fill corpus.json row sha256 = LF digest). Edit tool may write CRLF into .py: normalize.
- 11: raise = ErrorReason raised_error/exit/throw (11-13) via existing CLAUSE_raise_v2 (no new symbol/ABI rev);
  payload_reason range covers them; startup exception_class(). body_builtin moved to pattern_calls.cpp (needs
  auto_import); unqualified only for the raise family, display/halt stay erlang:-qualified until step 36. Oracle stderr
  prints `uncaught <class>: ~p` (compare class/reason manually; not stored).
- 12: catch = ExpressionLowering::handler (innermost catch); failure_exit and raise_reason branch there; cached
  bad_argument/bad_arithmetic exits reset inside the catch. Runtime CLAUSE_catch_v1 in process/exceptions
  (error_name shared with startup); stack placeholder []. binding_children lacked CatchExpression (semantic walks
  skipped catch bodies: segfault / "invalid map<K, T> key") - fixed. OTP 29 warns deprecated_catch by default and
  the oracle uses warnings_as_errors: fixtures need -compile(nowarn_deprecated_catch); nowarn_* options admitted.
  Program fixtures hide stacks with show({'EXIT',{R,S}}) when is_list(S).
- 13: CatchClause class/stacktrace are ExprIds (parser make()). Branch::handler + first_handler: try = of clauses then
  catch clauses. Walker ProtectedScope (catches map) protects only the body; handlers() takes {class, reason} via
  CLAUSE_exception_v1, catch clauses start from pre-try bindings; unmatched -> CLAUSE_reraise_v1 (raised_*
  reason, observably identical). Walk::visit split (visit/branch) for Lizard CCN. Bindings OTP regenerate: use
  "C:/Program Files/Erlang OTP/erts-17.1/bin/escript.exe" directly (otp-launch shim gone). Atoms > numbers in term order:
  `zero > 0` is true in guards.
- 14: after = second ProtectedScope map `afters` + `after_paths` (result/resume/exception); after body lowered twice
  (normal path, then from the after handler). Never return/move ProtectedScope by value: MSVC std::map move may throw ->
  tidy bugprone-exception-escape; emplace in place, extract node handles. `opt->` and `.value()` on std::optional are
  both flagged by bugprone-unchecked-optional-access: test first. An opt-in process budget overflow reports resource_limit (7).
  `after` is a reserved word: fixture atoms must not be `after`.
- 15: trace captured at GeneratedCallState::fail from GeneratedRoots frames (FrameDescriptor per function, private
  `frame.<symbol>` global); OTP compiler turns calls to never-returning functions into tail calls, so trace fixtures
  need functions that can also return and non-tail call sites ({tag, f(X)}). Term::is_function is declared but not
  defined (link error). Recursion admitted from step 18.
- 16: maybe keeps ast::MaybeMatch (not an ExprId): walkers use semantic::maybe_operands and Visit.field = item index.
  The "pattern matching" capability fallback in match_plan is unreachable from source (all PatternKinds planned).
- 17: decision only (docs/execution-model.md). Prototype tests/prototypes/execution_model/run.py uses clang++ from
  PATH or %ProgramFiles%/LLVM (works outside vcvars); generated.cpp is freestanding (-ffreestanding, stddef/stdint only)
  so it cross-compiles for all 7 targets. ARM asm has no TAILCALL comment: run.py counts b/br/bx to symbol/register.
  clang-format collapses one-line function bodies there (root .clang-format). Step 19 must: split walker at calls via
  body resume switch, reload frame base after every transfer/service that can push, replace GeneratedRoots + handoffs
  with flat stack + x registers, entry ABI becomes void(Process*).
- 18: CallGraph::components (Tarjan, callees first; members sorted by declaration index) + flattened order. Inference
  solve(): members start Fact{bottom}; merged() treats {bottom, no argument} as identity (also in clause joins);
  expression facts erased per round via `recorded` (stale earlier-round facts would be unsound); 16 rounds then
  graph.exhausted() (widened flag). A 16-function ring hits the limit, 15 converges. Codegen needed no change (all
  functions declared before definition; native recursion). Catalog recursive_calls implemented (placeholders.py set must
  equal deferred compiler entries). type_inspection.py needs an absolute tool path when run by hand.
- 19: native form + `lower_frames` post-pass (codegen/frames), not a walker rewrite: test seams (failure_emit,
  service_emit, specialization_emit) edit native form after analyze_and_lower, and `optimize` runs lower_frames
  (idempotent: wrappers lose `erlang-arity`). Calls found via callee use-lists, never `getCalledFunction`/PHI
  `getIncomingBlock`: clang-analyzer ArrayBound false positives in LLVM headers (Op<-1>, hung-off operands). Spills
  use llvm::DemoteRegToStack then alloca -> raw slot. A native function without `erlang-arity` (test seams, `.reference`)
  stays a native call. Removed roots_enter/leave, GeneratedRoots, RootInvocation, handoffs, service_consumer
  root_failures (entry budget now tests/runtime/stack.cpp). ABI version 5; FrameDescriptor 7 words.
  Gate MUST configure with -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl: plain --fresh picked GNU
  clang++, MSVC=false, no UTF-8 manifest -> Unicode-path tests fail, parser_hardening segfaults. Switching compiler
  needs rm -rf build/debug (nested native consumer caches). Scripts build/plan11-step19/*.cmd.
  Debug runtime makes services ~0.1-0.5 us: 2M-step tail loop ~1 s, 200k-deep list build+len+sum ~1.3 s.
- 20: user asked for minimal iteration counts (just exceed the limit), not plan's 10M/1M. Golden runs may be
  `"authored": true` (regenerate.py keeps them) for Clause-only outcomes (none left since the step-27 correction).
- 21: comprehension = loops in the body (codegen/lowering_comprehensions), loop state in term slots (root_slot), no PHIs;
  result reversed once (ContainerConstruction::reverse=2). Generator patterns bind via BindingCandidate::fresh
  (define_fresh, shadow()); find() prefers tentative names when fresh. Guard-test filters classified in guard_analysis
  (guard_test, top-level legacy names allowed) -> Function::guard_filters; lowered with a synthesized GuardSyntax.
  OTP facts: guard filter errors skip; non-guard filter non-boolean -> {bad_filter,V}; strict -> {badmatch,E}; zip ->
  {bad_generators,{Tails}} (strict rejection too); compr_assign is experimental/off (OTP error text). lower_match_plan's
  bind uses emplace: restore state.bindings before rematching the same identities. Name clash: a free function
  `accumulate` resolves to llvm::accumulate via ADL. "Unsupported" placeholders now use record update `#r{}#r{a = 1}`.
  programs.py --update-diagnostics rewrites programs compile.txt. Oracle needs -compile(nowarn_shadow_vars) for shadowing.
- 22: OTP evaluates a map comprehension's value before its key; binary template not a bitstring -> badarg at that
  element; map generator on non-map -> bad_generator before the loop (also in zips); zip payload for a map generator
  is OTP's iterator {K,V,Next..none}, emulable for flatmaps. Atom-keyed maps iterate/print in atom-index order in OTP
  (unstable): fixtures use integer keys. OTP display writes latin-1 bytes for chars 128-255; regenerate.py needs UTF-8
  stdout, so avoid displaying such strings. erlfmt rewrote a UTF-8 literal as latin-1: keep fixtures ASCII.
  lower_match_plan now returns candidate values (binary rest). Tidy: IRBuilder::CreateICmp trips
  clang-analyzer ArrayBound; use builder.Insert(CmpInst::Create(...)).
- 23: before it, no root but explicit ones could be live at a collection (frames/failure exist only while a
  GeneratedInvocation is active, and collect() refused then). Added runtime `SafePoint` scope (generated_calls.hpp;
  collect allowed while active only inside one) and `ProcessStack::keep_registers` (x[0..live) roots, cleared by
  push/truncate). Test: collection.cpp `root_owners` hand-written frames spread one shared graph over every owner.
  Raw spill slots may hold stale term copies after a call: generated code is NOT a safe point until 24/26.
  build/debug Ninja may skip test targets (stale BUILD_TESTING): always run the fresh gate first.
- 24: decision only. Safepoints = entry (in enter/tail before push, keep_registers(arity)) + comprehension loop heads
  (CLAUSE_safepoint_v1); services are critical sections (fragments). Prototype tests/prototypes/safepoint/run.py
  (clang on PATH or ProgramFiles/LLVM); O2 GEPs print as `getelementptr inbounds nuw i8`.
- 26: safepoints collect when ProcessHeap::wants_collection(); host Terms held across generated calls go stale
  once any call collects: native consumers that keep Terms use HeapOptions{1 << 16 / 1 << 20} min heap
  (match_consumer also host-collects between calls). Two different blocks can share an address (shrink copy reuses
  a freed block): test collection by stale host Terms, not by word inequality. clang-analyzer ArrayBound also fires
  on LoadInst::getPointerOperand / StoreInst::getValueOperand / PHI incoming_values: walk use lists instead.
  bugprone-unused-return-value flags discarded std::expected (static_cast<void> too).
- 27: CORRECTED by user: no default memory cap (heap UNLIMITED_HEAP_BYTES, stack unlimited); caps are
  opt-in per process (create_context(HeapOptions, StackOptions)); runtime-wide limit + program-facing caps = 27A.
  Host refusal = out_of_memory exit 70; opt-in budget = limit_exceeded -> resource_limit (no new rule). Rejected a "live > 3/4 budget
  fails at entry" rule: frames keep stale garbage in term slots (garbage_collection deep is 7.0M of 8.4M words live
  after collection). Fix = block_limit (survivors + half the free budget) + capped binary_limit_words_. Heap-word
  goldens near 64 MiB cost ~5-9 s per run in Debug (GC copying), so the golden uses 64 KiB off-heap binaries
  (bit_limit = 1,000,000 bits per value); BitWriter::append got a byte-aligned copy (was bit-by-bit, 330 us/8 KiB).
  erlfmt: escript with code:add_path + erlfmt:format_file returns {ok, IoData, []}: write it back yourself
  (scratch fmt.escript). Never `cat > file` without heredoc: hangs on stdin.
- 27 follow-up: no binary size cap, no context-count cap, atoms 2^20 default / --max-atoms <= 2^26 via
  startup/options (argv + CLAUSE_FLAGS, `--` ends, --args-file notimpl). RuntimeOptions{2} positional init
  now means max_atoms (max_contexts field removed): use designated initializers. Golden runs may set `env`.
- 27A: RuntimeMemory (memory/runtime_memory.hpp) shared_ptr in Runtime::Impl, ProcessContext::Impl (memory()),
  HeapStorage::memory_; charges mirror capacity_words_ + off_heap_words_ (replace(), rollback, dtor) + Copier
  to-space force; stack charges capacity in grow(). Unlimited account must return UNLIMITED_WORDS from available()
  (finite limit broke codegen_failure mode 1). Runtime options --max-heap/--max-stack/--max-memory (BYTE_OPTIONS
  table, options.cpp); create_context() = runtime process defaults. cmd scripts: never pass `|` patterns as %1,
  use pattern.txt. Don't edit runtime sources while a gate runs (nested consumers compile them).
- 27B: list/compare caps gone; structural_order default budget = SIZE_MAX, word-equal fast path in step(). Tuple
  1M check now only in TermFactory::tuple. A 2^24-1 tuple via vector<Term> costs ~800 MB (Term is 48 bytes): 27C
  needs a word-based tuple path. Map construction is insertion O(n^2): 27D should sort.
- 27C/27D: MAX_TUPLE_ARITY + TermFactory::tuple_words in runtime/include/terms.hpp (construct service uses words).
  structural_order has no budget overload anymore; map make = adjacent_find ascending check, else stable_sort +
  keep-last dedupe (OrderFailure exception out of comparator); publish checks MAX_MAP_SIZE (header count / 2).
- 27E: system_limit = ValueOutcome 3 + ErrorReason 19 + TermError::system_limit; codegen checked_arithmetic (switch)
  with cached state.system_limit exit in ProtectedScope. Compiler INTEGER_BIT_LIMIT/decimal_fits/decimal_number in
  preprocessor/value.hpp + integer.cpp; lexer sized_integer() rejects literals (Lexer::literal name clash). Funs are
  not implemented: fixtures dispatch on atoms instead of closures. OTP probes: erl.exe -noshell -eval 'c:c(m), ...'
  (delete erl_crash.dump). Python 3.14 needs sys.set_int_max_str_digits(0) for huge str(int).
- 28: ProcessHeap::add copies foreign graphs (copy.cpp GraphCopy); factories call retain(). Off-heap buffers: runtime
  charge in shared_ptr deleter (make_buffer), per-process hold/drop counts in HeapStorage::buffers_; verify() checks
  counts. Never compare shared-subterm towers across heaps (no identical-word shortcut => 2^depth walk).
- 29: update children = values in source order, then base (semantic::expression_children(module)); walker's
  record_enter is construction-only, updates use the generic value path + lower_record -> update(). Placeholders for
  "unsupported" now use `receive` (body) or `-record(#r{a}).` (one heap-expressions marker). Records corpus
  input_manifest_sha256 = evidence.digest(corpus.json) must change with any corpus.json edit. Bash heredocs here
  collapse `\\` to `\`: write JSON/regex/edit scripts with Write into scratchpad. A killed session can leave the
  background gate running: check Get-Process ctest/cmake/ninja/python before rerunning.
- 30: record_info/2 = compile-time pseudo-function: semantic::record_info_call (unqualified, 2 args) gates calls.cpp,
  inference evaluate (top), specialization_analysis and codegen call_value; expression_children(module) returns {}
  for it. A local record_info/2 definition is still indexed (skipping it crashed a later lookup).
- 31A: native records split into 31B runtime, 31C local, 31D qualified/imported, 31E anonymous (contract
  docs/native-records.md). OTP probe modules (scratchpad/native) are transient. Key OTP quirks kept: local `X#r.f`
  checks only the name, `X#_.f` skips export check, failed external construction -> {badrecord,{M,N}}.
- 31B: RecordDefinition (code_server.hpp) lives in ModuleAtoms::records; cell word 1 is a typed
  `const RecordDefinition *` (NativeRecordCell::definition_; tidy forbids int-to-ptr casts). Walker `untraced()`
  returns the untraced payload prefix; HeapCell slots of untraced cells are an empty span at the cell end. ABI
  version bump needs cross_targets.py and linking/startup.cmake descriptor strings updated. Full gate > 10 min
  when runtime changes: run it with run_in_background.
- 31C: native construction = explicit fields in source order (record_order), native update = record first;
  `display` of multi-field native records is unstable vs OTP (atom index order): goldens display fields or
  single-field records. erlfmt cannot parse `-record #r{a, b}.` nor `#div`: fixture left partly unformatted
  (it rewrote parseable ones to `-record(#r{...}).`). Uncaught-exception report prints `~w` (`x = 1`).
- 31D: external construction lowers the peer's default literals with a temporary ExpressionLowering bound to
  the peer module (atom_owner = importing module for atom slots/descriptor); peer tables declared via
  getOrInsertGlobal (tidy flags `new GlobalVariable` kept in a local as a leak). erlfmt also cannot parse
  `#m:r` forms. The OTP oracle compiles with warnings as errors: avoid updating literals (`(#r{})#r{...}`).
- 31E: step 31 closed. "Unsupported" test placeholders use `-feature(compr_assign, enable)` + `[Y || X <- L, Y = X]`
  (last `heap expressions` capability). features.hpp heap_expressions still says plan_step 29 (stale).
- 32: funs = Module::funs (index_funs at end of bind_parameters) + <prefix>.funs FunDescriptor table; cells
  fun_closure {hdr, FunDefinition*, captures}. Fun call = args alloca (>=1 word) + CLAUSE_apply_v1 (writes captures
  after args, returns frame or raises) + `clause.apply` marker; frames.cpp ErlangCall{call, descriptor, arity}
  treats it as an Erlang call (alloca -> registers). lower_module reuses `.frame` globals the fun table declared.
  OTP oracle: warnings_as_errors rejects `fun m:f/1` of own unexported f, calls with statically wrong arity
  (`F = fun(ok)..., F(nope)`: nomatch) -> route through a helper. tidy bugprone-easily-swappable-parameters: Word and
  size_t are the same type; parameters used together in one call expression are not flagged.
- 33: closures = binding walker actions fun_clause/fun_clause_end/fun_exit (FunScope: incoming without checks, saved
  branch_names, first_local/first_binding -> Function::captures = non-definition events on identities with local <
  first_local). fun_clauses() now includes named funs (step 34); expression_children lists fun guards+bodies
  (codegen walker skips them). Lambdas: FunEntry.expression/owner/captures, native `-f/A-fun-N-` arity+captures,
  lower_lambda via ExpressionLowering::lambda; a record default fun is one entry (dedupe by expression).
  ExpressionLowering positional init: 7th field is `values`, set bindings afterwards.
- 34: named fun name lives only in FunScope::inside (putting it into `incoming` leaked it after the fun: outer
  `F = fun F(...)` became an exact check -> codegen "invalid map<K, T> key"). OTP oracle warns unused fun name
  (`fun F(F) -> F end`) and shadowing (-compile(nowarn_shadow_vars)). cmd: pattern file build/plan11-step34/pattern.txt.
- 35: OTP: M:F non-atom -> badarg, missing -> undef ({M,F,Args,[]} top frame, ours caller), apply list checked
  first (apply(1, foo) badarg, apply(1,[1]) {badfun,1}), >255 args -> undef / badarity with the whole list, fun M:F/A
  bad operands -> badarg. erlfmt cannot parse `f(...)(2)`: parenthesize. "dynamic calls" capability now only builtin
  funs (catalog plan_step 36). Service resolutions without an operation report `guards` notimpl unless apply().
  TermFactory::fun_words checks CodeServer::owns (descriptor-less definitions = interned external funs).
  Bash heredoc with many quotes fails ("unexpected EOF"): write edit scripts with Write into scratchpad.
- 36: bridge = ABI catalog abi::v1::bridge_builtins (append-only, index = CLAUSE_builtin_v1 arg) + runtime
  BuiltinRegistry in CodeServer (names are string_views: static storage). Builtin frame = FrameDescriptor with null
  body (BuiltinFrame, reinterpret via builtin_frame); ProcessStack::enter runs it and returns the caller's body (no
  push). erlang builtins are adapters over the C ABI services (CLAUSE_immediate_v1, _map_v1, _bits_v1 ...):
  bits service `part` writes TWO output words (value + cursor) -> 1-word output = /GS fail-fast 0xC0000409 (exit 127
  in bash). BindingAnalysis::module is const: module-level effects of resolve_services go through Function fields
  (builtin_funs) applied after the loop. Capability "guards" notimpl fires for services without operation unless
  apply()/builtin. OTP oracle constant-folds apply(erlang, abs, [a]) with literals -> warnings_as_errors failure:
  route via id(). erlfmt beams live in thirdparty/tools/erlfmt/_build/local.
- 37: OTP auto-imports setelement/3, tuple_to_list/1, list_to_tuple/1 but NOT make_tuple/2,3 (oracle lint
  undefined_function). `--` uses exact order (CMP_TERM = erts_cmp exact): [1,1.0,1]--[1.0] = [1,1]. Term::list_elements
  and with_tuple_element are unimplemented (link errors): walk head/tail, rebuild via tuple(). User: long-running
  builtins must later run in interruptible portions -> TODO(step 43A) markers + plan step 43A, not now.
- 38: OTP float_to_list probes: {scientific,-1} = default 6 digits; {decimals,-1} badarg; text >= 256 bytes badarg;
  {decimals,0},compact on 1.0e20 gives "1" (OTP trims integer zeros); [short,{decimals,2}] = "1.50" (last wins).
  list_to_integer: OTP size check (system_limit) precedes digit validation only once the first ~18 digits are valid;
  chars > 255 use the low byte (difference recorded). Building a 1.3M-element list in Debug costs ~2.5 s
  (2 us per cons): build long strings by binary doubling + binary_to_list.
- 39: OTP lists:map/foldl raise {case_clause, X} for a non-list (case at top, function_clause in the helper),
  nth has is_integer(N). programs.py --update-diagnostics must get an uppercase absolute work dir (F:/...):
  a lowercase f: cwd breaks the <fixture> substitution. Library added only for literal module names.
- 40: io = bridge catalog entries of module io (no library file); OTP device = user/group, encoding unicode,
  io_lib:format list path (not build_bin). ~p printable range latin1 even with t. Probes: ~.*c with -1 hangs OTP
  (kill erl/beam.smp). Pretty layout recursion ~1.2 KiB/level Debug: 800 levels overflow 1 MiB main stack -> cap
  256. ADL: local `advance`/`quoted` resolve to std:: -> renamed. avltree/frames/textstats now compile and their
  linked executables match expected stdout (step 58 runs them). Fixture console: lists:duplicate not in library.
- 41: BuiltinFailure/need/bad_argument/fail live in builtins/support.hpp (io used FormatFailure before).
  typed_entry<F>(module, name) derives arity. Tests reach private headers via target_include_directories
  runtime/src (runtime_typed_builtins). TermFactory: integer, integer_decimal, floating. term_status maps
  stale_term -> internal_error (use wrong_owner to distinguish). Reserve sweep (a2be154) measured no change.
- 42: pid = immediate 0x3, number from process-wide atomic (never reused); Runtime::Impl::process_numbers runs;
  ProcessIdentity::serial_ IS the pid number. HeapStorage::processes_ admits pids (TermAccess::pid); validate()
  accepts pid Terms (retain re-admits). Ref cell = BoxedKind::reference + 8 bytes (program-wide counter). OTP
  constant-folds pid_to_list(self) with literal atom -> route via id(). Golden prints identity text through
  pid_to_list/ref_to_list with digits replaced by N. Body self() = guard sig + bridge builtin (guard_analysis
  call()); guard self() still gated (step 52). Term-services deferral now only via TermFactory::port(PortIdentity{})
  (PortIdentity became a complete empty class). Many files are CRLF in the worktree (autocrlf=input): python
  edits must keep endings (scratchpad edtool.py). Background `cmd //c gate.cmd &` detaches: poll logs.
- 43: yield = enter() with reductions_ 0 -> resume_ = frame, keep_registers(arity), return `pause` (empty body);
  ProcessStack::start pushes bottom + suspends, run(reductions) resumes; invoke loops run() (host never runs other
  processes). Executor (scheduler/executor) in Runtime::Impl; friend of Runtime/ProcessContext (runtime()). Child
  first call prepared IN the child via apply_list_service/call_list_service (declared in terms/funs.hpp). OTP quirk:
  compiler drops code after spawn(fun/1) (type analysis) -> fixtures route the fun via remote processes:id/1 (local
  id/1 is still inferred). Crash run stderr authored ^$ until step 44 adds reports. OTP spawn/1 non-fun -> badarg.
- 45-47: `receive` is a reserved word (fixture module names!). Unsupported-feature placeholders in tests now use
  `fun erlang:node/0` ([dynamic calls]); swap again when step 52 admits node/0. A clause with neither pattern nor
  guard needs an explicit br to its body (start_clause). Module atoms must include atoms codegen lowers implicitly
  (infinity/true for receive) or lower_atom crashes. Old test placeholders also in patternmatch/clauses.py.
- 43A: enter() must call call_builtin_portion (call_builtin loops traps for host paths: using it in enter ran
  builtins to completion). Python heredocs in bash turn "\n" into a real newline inside C++ literals: use Edit or
  Write for C++ text. Single-file tidy: clang-tidy -p build/debug/quality works for runtime units only (compiler units
  miss generated includes); official check-quality changed scope = 39 batches (~20 min). OTP: `{Name, Node}` sends
  are silent, unregistered atom badarg, Dest evaluated before Msg; bad receive timeouts (foo, -1, 1.0, 2^32) raise
  timeout_value only when no message matches. OTP crash reports go through async logger: the oracle usually halts
  first, so goldens see no OTP stderr (author stderr). runtime_containers SegFaulted once in a full -j 12 run, not
  reproduced.
- 48: exit signals act at once (only the running process sends); `CallError::exited` (raised_exit + value) is
  uncatchable; executor `running_`/`Running` scope, `ending_` drain, `finished_`/`stopped_`; run() resets finished_.
  OTP -eval process traps exits (link to dead returned true there): probe inside spawned processes. Monitors (49):
  demonitor flush only when the monitor was not found; monitor(process, self()) creates nothing (info false).
- 52: all 81 guard signatures lower; "dynamic calls" placeholders now `fun erlang:apply/2` (no local unbridged
  builtin fun left). Corpus edits: change fragments + generated calls, then fixtures.tsv hashes (guards.tsv,
  guard-resolution.json), then regenerate.py --corpus X (needs references/otp). linking_executable -Os size check
  tolerates one alignment unit (.reloc crossing 512 B made Os bigger than O2).
- 54: AtomStorage shared_mutex; private static atom_term (Term fields are friend-only). tests/runtime/concurrency.cpp
  (runtime_concurrency, links Threads::Threads) is the home of thread stress for 55/57.
- 55: CodeServer shared_mutex; locked public lookups call private unlocked find_fun/find_export/find_function
  (never call a locking public method under the lock). concurrency.cpp needs <clause/runtime/code_server.hpp>.
- 56: one shared queue + executor mutex (documented alternative to stealing); typed adapter catches builtins::Blocked
  and traps to Adapter::RETRY. after() must place the process BEFORE resuming blockers (an ending blocker's 'DOWN'
  woke+queued it, then place queued it again: double-queued -> resume_ null -> run() true -> main "ended", exit 0
  with truncated stdout). Fixture races under N workers: spawn then monitor/link of a short-lived process (names
  fixed with spawn_monitor); concurrent crash reports reorder (regex with lookaheads). Program default schedulers =
  hardware_concurrency; host RuntimeOptions default 1 (runtime tests rely on round robin). build/plan11/repeat.cmd
  NAME N repeats TESTRE tests until fail.
- 57: no code; executables_wakeups stress (workers 1,2,4; runs [], teardown, halt). OTP races too when monitoring after
  spawn of a 0 ms process: use spawn_monitor in fixtures; check goldens with regenerate --check 3x. Phase J closed: full
  CTest 209/209 (102 s, -j 32), check-quality-all clean.
- 57A-57B: WSL Ubuntu exists (g++14, clang20, no cmake): `wsl -e bash -c ...`; tests/prototypes/poller/run.py --wsl.
  OTP port probes: erl.exe -noshell -eval 'c:c(m), m:main()' in scratchpad/probe (python helper.py there). ERTS
  io.c holds port exit/badsig rules. fd ports in fixtures: [out], one open at a time (OTP logs driver_select
  stealing reports otherwise). -Wmissing-designated-field-initializers: give structs static factory helpers.
  Executor ctor not noexcept (MSVC unordered_map allocates; lifecycle_failure terminates). tidy: Term/Word params
  used separately are "swappable" -> pass Word or use them in one call. erlfmt: scratchpad/fmt.escript FILE.
- 57C: WSL syntax check of POSIX runtime files: wsl -e bash -c 'cd /mnt/f/CLAUSE/clau && clang++ -std=c++23
  -fsyntax-only -Wall -Wextra -Werror -Iruntime/include -Iruntime/src -Iabi/include -isystem thirdparty/boost_1_90_0 F'.
  Golden runs take authored "stdin". OTP port input counts raw bytes. build/plan11/{qonly,fastonly,repeat}.cmd.
- 57D: golden `data` files + runs in staged dir + CLAUSE_TEST_PYTHON env; OTP Windows drops empty spawn args
  and gives eacces for an empty env name (left out of fixtures). User asked how ports get CPU (2026-10-08): answered
  ports are not scheduled entities (I/O threads + synchronous port ops); offered an ERTS-like port-task redesign as a
  later plan step if wanted.
- 57E: library modules io.erl/file.erl exist now; referenced_modules skips catalog builtin calls (io:format) so
  they join only programs that call their Erlang functions. Runner runs each combination in work/<label> (data files
  copied) so file-writing tests do not collide. Lizard/tidy: use dispatch tables for op switches.
- 57F: sockets: one Asio thread; PortDriver::attach must be called for every port (open_port too; missing it made
  replies carry port 0 -> invalid_argument). Closing a socket with an in-flight async write resets the peer on
  Windows: close after queued writes drain. Library has no maps:get/3, lists:all/2, binary_to_atom/1. Timeout
  races: cancel op posts `cancelled` after earlier messages (barrier). build/plan11/sock.cmd, rep.cmd.
- 57G1 (2026-10-09): repo now F:/CLAUSE/clau, build/ was wiped: scripts recreated in build/plan11/{env,configure,
  build,fast,test,quality,gate,run}.cmd (write .cmd files with Write: printf/heredoc eat backslashes; a cmake run
  without vcvars wiped build/debug's cache -> rm -rf build/debug + configure.cmd). WSL: MSYS_NO_PATHCONV=1 wsl -e bash
  /mnt/f/CLAUSE/clau/build/plan11/wsl_port_io.sh builds the whole runtime + tests/runtime/port_io.cpp with clang 20 and
  runs it (POSIX port I/O really runs there). Asio's config includes <windows.h> with NOMINMAX: never redefine.
  BOOST_ASIO_NO_DEPRECATED is a runtime CMake definition (ODR). Asio objects must die before their io_context: executor
  `reactor_` declared before ports/services, clear() stops it first. OTP escript: "C:/Program Files/Erlang OTP/bin/
  escript.exe" works for regenerate.py now. make-test.bat now selects clang-cl + Ninja Multi-Config itself.
- 57G2: TESTRE="a|b" cmd //c build/plan11/test.cmd (env var; `|` in cmd args breaks). Fairness goldens need more
  CPU-bound processes than workers (idle workers take port tasks freely) and must check state when the CPU work ends
  (ping-pong behind 8 hogs is slow by design). Validate such tests by breaking the scheduler on purpose.
- 57G3: OTP resumes several senders of a busy port in any order (a fixture must wait for one before the next
  command). Helpers whose writes may now block must exit via os._exit on OSError (Python's exit flush prints to
  stderr). build/plan11/trace_57g3.py apply|restore shows how to trace the runtime temporarily.
  Socket fixtures with ~1000 connections: let the server close first (TIME_WAIT on its side) and retry connect;
  under parallel CTest load Windows ConnectEx fails with ERROR_DUP_NAME (now eaddrinuse). Reproduce load flakes by
  running 6 instances at once (build/plan11/trace_eio.py names unmapped socket errors).
  Phase J2 closed 2026-10-09: full CTest 218/218, check-quality-all clean (build/plan11/{phase,full_quality}.cmd).
- 53: decided no ports; feature `ports` (26) deferred; adding a FeatureId needs tests/abi/features.cpp names snapshot
  and a codegen_placeholders CASE for deferred compiler features. Phase I closed: full CTest 206/206 (131 s, -j 32);
  check-quality-all found one tidy complexity issue (fixed, changed-scope rerun clean). Phase script build/plan11/phase.cmd.
- 58B-58I (2026-10-09): inference = postorder Frame walk (inference_scopes) + evaluate dispatch (operators, values,
  containers, funs) + BindingFacts (publish pattern parts, saved scope stack, aliases, merged clause-end facts,
  entry/success domains). Expectations: python tests/compiler/inference/expectations.py <clau> <fixture> [--record];
  scratchpad update_inputs.py pattern = set expect to inferred when only inputs differ. Specs (58I) are errors only
  when DISJOINT (meet none()) from inferred facts: subtype would give false errors since facts over-approximate.
  Deliberately false specs in patternmatch fragments must overlap now; editing a fragment needs
  regenerate.py --corpus X (hash-only change). build/plan11/fmt.escript crashes (badarg) on fragments with
  non-ASCII atoms (atoms, bits): format those by hand. clang-tidy flags easily-swappable (Id, Id) params unless
  used together: pass structs (Operands, Cell, Slot, Lookup) or compare them first; it also flags implicit move of
  structs holding std::map (exception-escape): copy instead. Boost cpp_int `x + var` in narrowing tripped the
  analyzer (ArrayBound): use fixed +1/-1 helpers.
- 58J1/58K (2026-10-09): try/maybe frames join completing paths (of clauses from body end; catch/else/after from
  before). Function types: Summary::types (entry facts per clause / trailing case-if branch), overloaded funs =
  Kind::function "clauses" node; joined_fun() for old consumers. Scratch helpers: rep.py (exact <<<OLD/===/>>> spec
  replacements; avoids heredoc escaping), accept.py (set expect lines to current --print-types output; review diff).
  Compiler CMakeLists source edit makes check-quality run all 334 tidy units (~6 batches).
- 58L/58M (2026-10-09): call selection needs FunctionType::exact for first-match (`g(1) -> one`); per-call
  re-analysis uses its own work pool so base inference never widens; plan 11 inference series 58A-58M done
  (next: 59 specialization). Optional 58L spec report of calls no type admits was not added.
- 58N1 (2026-10-10): dependent facts = Fact::dependent index into interned Inference::dependents; BindingIds are
  per-function, so every Fact leaving a function must be `erased` (run_body result, clause_types, fun facts use
  .type). Selection without relation instantiation: function_types::entered (indices). Expectation runner needs an
  absolute clau path on Windows (F:/CLAUSE/clau/build/debug/bin/Debug/clau.exe). erlfmt: erts-17.1 escript +
  build/plan11/fmt.escript thirdparty/tools/erlfmt/_build/local <files>.
User directions (keep):
- Ports (2026-10-08): ports must exist later; sockets, file I/O, subprocess stdin/stdout are ports (plan phase J2,
  57A-57F, backlog F35); step 53 decision superseded.
- Inference plan (2026-10-08): steps 58A-58I before specialization 59 (F34); each closes its today: lines in tests/fixtures/inference/values.erl.
  2026-10-09 user direction: guards/patterns define a function's entry domain (never entered otherwise) and uses
  (arithmetic, builtins, calls) narrow operands after them, else crash: 58G entry domains, new 58H uses/success
  domains, spec rejection renumbered 58I. Inputs in --print-types become these domains.
  2026-10-09 user direction: new step 58H1 (before 58I) narrows proven-integer ranges by guard comparisons.
  2026-10-09 user direction: consecutive integers print as N..M (step 58F1, printing only).
  2026-10-09 user direction: printed types write term() as `_` (TERM_SOURCE, step 58I1).
  2026-10-09 user direction: argument relations print as variable names / _argumentN (step 58I2).
  2026-10-09 user direction: step 58J (wide known tuples, positional lists, #{1..17 => a} maps, records printed as
  records) added after 58I; implemented next. 78B (user direction): rename runtime/src/builtins files to builtin_<name>.
  2026-10-09 user direction: clause-dependent function types before 59: 58K per-clause function types + printer,
  58L call matching (meet with clause inputs, union fallback, none() when nothing matches), 58M per-call re-analysis.
  Then (user): 58J1 try/maybe values; 58K also splits case/if on arguments and gives funs function types; 58L
  matches fun calls and makes per-call fun evaluation clause-aware.
  2026-10-10 user direction: case/if/try-of values are dependent facts (fun of the narrowed variables), printed as
  function types named $case_of_operator/$if_operator/$try_of_operator; steps 58N1-58N3.
- Timer wheel (plan step 62B, 2026-10-08): replace step 47's deadline map + per-slice clock reads with a timer wheel.
- Test/gate time (2026-10-08): per step fast CTest + check-quality; full CTest only at phase/major completion and
  then INSTEAD of fast (no duplicate runs). Tidy default jobs = half the logical cores. Keep slow tests parallel
  (executables run.py runs 4 combinations at once; parser_mutations sharded 0..3).
- No hard memory cap by default, per process or runtime; caps only as options (step 27 correction).
- Test iteration counts: just large enough to prove the property (exceed native stack / stack budget), no more.
- All ABI symbol/namespace versions collapse to v1 in plan step 78A (never released; no compatibility).
- Minimal first, iterate later; no defenses for impossible cases (8D: no start bitmap / interior-pointer
  checks, classic ERTS trust model). 8F first version (doubling/spare/trim) rejected as over-engineered.
- ERTS host model: no handle table; Term valid only in own process, read-only elsewhere.
- Binaries > 64 B stay std::shared_ptr buffers outside heaps; prefer C++ style designs.
- Heap is one block per process; each process owns its heap.
- Don't stage user's test1.toml. Stale IDE buffers may revert plan edits: re-check before commit.
Host and tool gotchas:
- VS discovery (see AGENTS.md Windows Toolchain): vswhere at C:/Program Files (x86)/Microsoft Visual Studio/Installer/
  vswhere.exe -latest -products * -property installationPath -> C:/Program Files/Microsoft Visual Studio/18/Community;
  vcvars64 = <that>/VC/Auxiliary/Build/vcvars64.bat. Copy build/plan11-step11/*.cmd (sed step dir) for new steps.
- Gate: vcvars64 + PATH "C:\Program Files\LLVM\bin"; never pass LLVM_DIR (skips /MT+IDL0). Scripts
  Configure with -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl: `--fresh` without it picks GNU-like
  clang++ and nested test caches (clang-cl) fail ("cache to be deleted", missing match_consumer.cpp). Strawberry
  cmake/ninja first on PATH is fine. Scripts build/plan11/{env,gate,build,test,fast}.cmd (test.cmd regex: TESTRE env).
  build/plan11-step8g/{gate,rt,quality,all}.cmd; cmd /c needs full .bat path; run .exe via PowerShell.
- build/debug may have BUILD_TESTING=OFF and Ninja may not rerun CMake: use the fresh gate. 2026-10-09: it was OFF
  from 58A to 58F1 and ctest ran stale C++ unit-test exes (semantic_inference); check
  `grep BUILD_TESTING build/debug/CMakeCache.txt` before trusting a fast run (likely the IDE CMake Tools reconfiguring
  from the debug preset); build/plan11/fast.cmd and quality.cmd now reconfigure with tests on first.
- First run after runtime source edits can time out tests while native sub-builds recompile; rerun.
- clang-tidy may crash (0xC0000005/0xC0000409) or exit 1 silently with 2 jobs; rerun with one job.
  Tidy runs in batches (QUALITY_BATCH, default 4 x jobs) with per-batch pass/FAIL lines; split long runs with
  -DQUALITY_SHARD=K/N (script build/plan11-step40/shard.cmd K/N BATCH). Root CMakeLists edits select all units.
  The check targets do not forward QUALITY_JOBS: run `cmake -DQUALITY_SCOPE=all
  -DQUALITY_BUILD_DIR=<build> -DQUALITY_NINJA=<ninja> -DQUALITY_JOBS=1 -P cmake/CheckClangTidy.cmake`
  (default jobs = half the logical cores since 2026-10-08, user request; was 2 on Windows, cap 6).
  (script build/plan11-step8i/rerun.cmd). codegen_dependency can time out (120 s) under full -j 16. Runtime CMake edits select all tidy units.
- MSVC C4554 false positive on static_cast<Word>(n - 1) << shift: hoist to a local.
- Bash heredocs mangle non-ASCII, `\n` and `\`, and many quotes break them: write files with Write
  or scratchpad scripts. `cat > file` without heredoc hangs. Python Path.write_text writes CRLF on
  Windows: use write_bytes. CMake drops empty ARGN args; execute_process needs ENCODING UTF-8.
- ADL can pick std::quoted for a local `quoted`: rename. No make here; gmake at C:/Strawberry/c/bin.
- OTP oracle: erts-17.1/bin/escript.exe directly (bin/escript.exe segfaults). erlfmt: escript with
  code:add_path("thirdparty/tools/erlfmt/_build/local") + erlfmt:format_file(F, []).
- 59 (2026-10-09): proofs = ExpressionLowering::proofs (O2 + specialization, functions only, not lambdas). Inline
  heap reads MUST be stored into root_slot (frames classify only term-slot/register loads as terms; raw spill went
  stale after GC: wakeups golden). Proofs read Inference::inputs (caller-joined) never Summary entry domains (those
  come from the patterns themselves). Fast mode matrix now O0 positional + O2 (spec on) project. Inference loses
  range precision for products of unions with negatives ((-5|1|3)^2 -> integer()).
- 60 (2026-10-09): -g = request.debug_info (implies annotate_source) + CodeView/Dwarf module flag; link -g plus
  /PDBALTPATH:%_PDB%, StagedExecutable::symbols published beside exe. LLDB at C:/Program Files/LLVM/bin/lldb.exe
  works with our PDB (break file.erl:N, `expr -- clause::runtime::debug_erlang_stack()`). Python text capture of a
  Windows console tool doubles CR: drop blank lines. Bash heredocs turn \n in C++/Python string literals into real
  newlines: use Edit/Write for code with escapes.
- 61 (2026-10-09): profiling is runtime-only (`--profile FILE`, RuntimeOptions::profile non-empty enables):
  ProcessStack::profile_ hooks in enter (Erlang bodies only)/leave/run; merged in Runtime::destroy_context.
- 62A/62B (2026-10-09): CodeServer hash indexes (publish() indexes then emplaces, unindex on throw). Timer wheel:
  executor timer thread (time()) started/joined in run(); destroy() cancels the timer before erasing Schedule.
  Phase L closed after 62B (full CTest + check-quality-all).
- 63-65 (2026-10-09, user: do everything doable on WSL/Windows; M1 Mac later for 66): WSL Ubuntu 25.04 has
  passwordless sudo, cmake/ninja/qemu-user-static/debootstrap installed. Scripts build/plan11-wsl/: sync.sh (Windows
  git.exe diff -> ~/clau-stage -> rsync -c into ~/clau; Linux git over /mnt/f stalls), gate.sh (Linux x64 gate, SDK
  clang 23, LD_LIBRARY_PATH ~/clau-hostlibs = Ubuntu 22.04 ICU70 for SDK ld.lld; SDK lldb needs libpython3.14 ->
  GDB), target.sh (runtime-only build/tests in Debian trixie chroots /srv/clause-{i386,arm64,armhf}, i386 native),
  cross-setup.sh + cross.sh (host clang --target + sysroot, absolute loader path -> ARM runs via binfmt qemu; ARM
  compiling under qemu was 20x slow and overloaded the CPUs), win-x86.cmd (clang-cl i686 runtime-only, WoW64),
  win-phase.cmd/win-test.cmd/win-repeat.cmd (JOBS cap). User: keep 20-25% CPUs free (memory cpu-headroom).
  Findings: Windows tidy HeaderFilterRegex never matched backslash paths (headers unchecked on Windows); libstdc++
  exposes unchecked-optional findings MSVC STL hides; LineTablesOnly CU drops DWARF subprograms (FullDebug, no
  linkage name); 32-bit `size >> 32` UB in packet framing; Executor::clear freed schedules before wheel_.clear();
  32-bit installer detection needs embedded manifest (/MANIFEST:EMBED); qemu-user posix_spawn never reports
  ENOENT; nested test consumers need CMAKE_CXX_COMPILER_TARGET forwarded. Goldens may mark runs word_bits 64.
- 67-68 (2026-10-10): build/plan11-wsl/sanitize.sh asan|tsan (flags in CMAKE_*_FLAGS, clang/clang++ wrappers on PATH
  for clau-linked programs, CLAUSE_TEST_TIMEOUT_SCALE 10/20). Sanitizers abort on impossible allocations (host-refusal
  tests out of scope); TSan defines operator new (lifecycle_failure, heap_measurements don't link). TSan found
  Executor::clear destroying IoService under the executor lock (gate lock-order inversion): services die after it.
  One unexplained port_spawn os:cmd [] under ASan full load, never reproduced. Tidy runs on Windows only (user).
- 65A (2026-10-10, -behaviour): OTP lint runs before erl_internal adds behaviour_info/1, so the module's own source
  cannot export/call/fun/spec it (semantic::source_function hides it). erl_lint locates attribute diagnostics at the
  attribute NAME token (column 2), report(TokenOrigin) + extent[1]; OTP sorts messages by location then tag and prints
  errors before warnings (lint.py compares unordered). OTP behaviour check needs the behaviour module loaded: lint
  oracle compiles files in golden order and loads each; programs oracle defers modules failing only on
  undefined_behaviour. Python Path.write_text on Windows writes CRLF: use write_bytes(t.encode()). erlfmt helper with
  unicode sources: build/plan11/fmt.escript now writes unicode:characters_to_binary. AGENTS.md (user, 2026-10-10): struct
  fields also take trailing underscore; rename fields being worked on. Scripts build/plan11-step65a/*.cmd (-j 24,
  ctest 16 slots).
- 65B (2026-10-10, module_info): OTP facts: exports = exported functions in DEFINITION order then predefined;
  attributes drop module/export/import/types/specs/records/compile/export_type/optional_callbacks/export_record/
  docs/feature, wrap non-lists, prepend {vsn,[md5 as 128-bit int]} when no -vsn; -nominal kept in abstract format
  (Clause drops it); module_info local calls/funs allowed, export of it warns, definition errors; duplicate export
  is a warning. module_info/0 must not call module_info/1 locally: literal-key calls made specialization variants
  that broke codegen_inspection (normal==disabled) and patternmatch_facts dominance. Predefined functions get no
  debug scope (else -g maps them onto real-file lines). Synthetic lexer buffer takes the module's physical name so
  IR "Erlang source files" lists real files only.
- 65C (2026-10-10): erl_lint import rules: redefine_import rejects the whole attribute; define_import at the function;
  fun_import for any imported name (even erlang); redefine_bif_import warning is two lines (lint.py joins
  continuation lines). Capability diagnostics carry FeatureContext.operation. Python heredoc edits that contain
  "\n" or "\d" inside replacement strings get mangled: use the Edit tool for such lines. Fixture import of a BIF
  name: guard BIFs (length/1) cannot be redefined in OTP, use integer_to_list/1. luerl src now stops only on missing
  library modules/BIFs (array, ordsets, math, io_lib, atom_to_binary/2, fun_info/2, get_module_info/2).
- 2026-10-10 user: --entry may name FUNCTION/0 (startup_no_arguments flag), commit fe55f8b.
- 2026-10-10 user: source_search_paths take */** patterns (project::pattern_directories, shared with include_dirs) and are searched for Module.erl of modules the target names but does not list, before the library (frontend add_referenced). codegen_placeholders: warning-only features publish into a separate artifact dir; output dir is wiped first so reruns pass.
- 73A-73G parse transforms (2026-10-11, docs/transforms.md): user direction: External Term Format code is shared
  (abi/include/clause/abi/external_term.hpp Writer/Reader), never reimplemented by runtime term_to_binary/
  binary_to_term/distribution. Pre-existing failure: codegen_resource_cli (user commit 33481a7 raised the module
  limit to 16384, test still uses 1025 inputs). Terms arena: node() references die on any later add (copy children).
  Gate scripts build/plan11-73/*.cmd.
  73F-73G: Windows re-sets ProgramFiles for every child process (env cannot hide an installed OTP): tests use
  --erl none. erlc -pa . does not find modules; pass absolute -pa (loader makes code paths absolute). Bash
  heredocs still collapse backslashes in python edits: write edit scripts with Write (scratchpad apply*.py).
  User AGENTS.md edit (2026-10-11, uncommitted): no feature tags on trivial one-line changes.
- 41A-41H (2026-10-11): testing-off wrappers (clau.bat, make-build.bat, make build, run-macos.sh) now build in
  TOOL_BUILD_DIR=build/tool, so build/debug keeps BUILD_TESTING=ON (user request). cmd scripts: never pass "|" in
  arguments (cmd pipes it). 41B: erlang.terms statuses are hand-maintained; regenerate erlang.erl with
  tests/compiler/builtins/generate_erlang.py after a status change.
- 62C (2026-10-11): liveness only for executables (backend compile: executable_output && !keep_exports). Dead =
  Function::live_ false -> codegen skips declare/define/lambdas/source scopes/specialization; registration emits
  name-only exports (entry+frame null) and null fun frames when the symbol is not declared. Runtime accepts both.
  Scratch edit tool: scratchpad rep.py (<<< old === new >>> blocks per @@@ file; handles CRLF files).
  Build/test scripts build/plan11-62/{build,fast,test,quality}.cmd (-j 24, ctest 16).
- 62E (2026-10-11): Release clau for measurements = build/plan11-62/release.cmd (build/tool, Release). Probe counts
  measured with a temporary counter header (scratchpad probe_stats.hpp + e7 spec), reverted before implementing.
- 62D1-62D4 (2026-10-11): project builds now write <manifest-dir>/build/cache by default: tests that run
  manifests inside tests/fixtures must pass --build-dir into their work dir (startup, match/atoms/native cmake);
  tests asserting "no build dir" accept build/cache only. --verbose compile checks use --no-cache (a hit prints no
  [comp] lines). A rebuilt clau (new exe mtime) misses every cache entry: Release timing runs need a warm-up run.
  Never `git checkout <file>` to drop temporary instrumentation from a file with uncommitted work (lost 62D2
  pipeline.cpp edits once; reapplied from the scratch spec).
- 62D5 (2026-10-12): cache = SQLite (build/cache/v2/cache.db + blobs/). Store opens lazily; only put creates the db
  (codegen_inspection/codegen_types assert no build/ after inspection runs). MSVC <filesystem> file_size /
  last_write_time trip clang-analyzer EnumCastOutOfRange: use llvm::sys::fs::status instead. tidy flags
  string_view::data() passed to C APIs: take const std::string&. Test scripts: build/plan11-62/ctest.cmd -R <one
  word> (cmd treats "|" in -R as a pipe). Bench: cache_store bench ROOT (Release needs a temp target in build/tool,
  BUILD_TESTING is OFF there); luerl timing = clau --project test-luerl.toml --entry luerl:init (exit 1 expected).
- 69A benchmarks (2026-10-12): `make-benchmark.bat` -> build/bench Release + benchmarks/run.py; baseline worktree
  build/bench-base/<hash> (thirdparty junction, ~5 min first build, cached). Windows OTP has no emu flavor.
  First results vs OTP JIT: geomean ~20x slower; maps:put and <<Bin/binary, X>> look quadratic (500-3000x by size),
  binary matching ~1000x, map update ~800x, lists:sort ~100x; sockets at par. Kernel spreads up to +/-60% on Clause.
  History (gitignored, local) keyed by run day (rerun same day replaces). Python edits via heredoc: "\n" inside a '''...''' string
  became a real newline; edit escapes with the Edit tool. Lizard on run.py: .venv-quality python -m lizard -C 10.
- 58U-58W inference cache (2026-10-13): helper scripts live in the session scratchpad (user runs make-clean.bat,
  which wipes build/; never keep helpers there). Fresh
  configure MUST pass %CLAUSE_CONFIGURE_ARGS% from tools/windows-toolchain.cmd (with BUILD_DIR set), else CMake
  picks the VS generator + cl (C2220 build failure). Edit tool strips a trailing space at the end of new_string:
  never end old/new_string mid-line before a quoted argument (cmake `check(x 0 "..."` became `0"...`).
- luerl run-time tracing (2026-10-14, .agents/21-lib.md): runtime CLAUSE_CALL_TRACE=FILE (+ _FIRST=1, _ALL=1,
  _QUIET=prefixes) logs boundary calls program->library with bounded args/results; OTP twin
  tools/luerl-trace/ctrace.erl prints identically. Scratch compiler tree (clau.exe + clause_runtime.lib + library
  copy at bin/Release/../../../../library) lets stubs be filled without touching the repo; any library edit =
  ~4.5 min luerl rebuild. difflib on 186k-line traces hangs: use GNU diff (mismatch.py). Bash heredocs eat
  backslashes in C++/Erlang escapes: use Edit for those lines.
- Phase H4 (2026-10-14, steps 41T-41Z): library functions as C++ builtins. Term::list_elements, without_map_entry,
  with_existing_map_entry (and bit_slice) are declared but NOT defined: walk lists by hand, rebuild maps from
  map_entries + TermFactory::map. tidy bugprone-easily-swappable-parameters: two adjacent const Term& that are not
  passed to the same call get flagged; fix with a tiny wrapper struct + Argument<> specialization accepting any term
  (lists::Count, MapOperand, KeyList, IoDevice, EncodingArgument, Default). A builtin can tail-continue in Erlang via
  code_server().function_frame + stack().trap (io device -> file:write/2); export liveness must then keep the target
  (semantic/liveness.cpp BUILTIN_CONTINUATIONS). Builtins cannot call funs and get results: fun-taking functions stay
  Erlang in library/*.erl, which must call sibling builtins qualified (a local call hits the stub). New library module
  = add to library/modules.terms, observe_module.escript, observe_types.escript, library_modules.py generate. Golden
  oracle compiles with warnings_as_errors (hide bad formats behind id/1) and prints maps in atom-table order (use ~kw).
  Bash tool heredocs containing apostrophes in python fail ("unexpected EOF"): write scripts to scratchpad files.
  Call tracer sink is now allocated only when CLAUSE_CALL_TRACE is set (lifecycle_failure counts allocations).
- OTP pin refresh 2026-10-14 to d199a49a (29.1.2; oracle still 29.1.1): executable/lint/programs/printing goldens
  store the pin revision and their --check compares it, so a pin change needs those four regenerate.py runs (diff
  must be revision lines only). executables external_terms prints an ETF map whose key order follows OTP's atom
  index: its oracle check flips between runs; keep the committed stdout. ctest via cmd: select tests with -I
  numbers, never -R "a|b".
- 41L init (2026-10-14): OTP's init:request/1 does a bare `receive {init, Rep}`, so a pending {init, _} message in
  the caller's mailbox is taken as the reply (fixture sends {init, started} only after its last request). The
  executables oracle runs entries from an escript while init still boots: plain arguments start with the escript's
  own, get_status {starting, started}, init:ensure_loaded loads; fully started OTP gives {started, started},
  not_allowed, fetch_loaded []. Golden checks shape only for those. init:stop("bye") under OTP writes erl_crash.dump
  in the staged dir, exit 1.
- 41N (2026-10-14): handle reference = RefcBinaryCell layout with kind reference (count 5); every refc-binary
  site that keyed on BoxedKind::refc_binary now uses layout::off_heap_cell (copy.cpp, heap_collect, heap_verify,
  heap_walk fixed_payload). OTP revives magic refs through binary_to_term on the same node (atomics still usable):
  Clause's weak registry in terms/identities.cpp does the same. OTP small-map iteration order follows atom index
  (user 2026-10-14: leave Clause's key order, recorded in differences). OTP atomics:new(2^60) kills the VM
  (allocator crash dump): never probe huge arities. Python edit scripts: write with the Write tool, run with python.
