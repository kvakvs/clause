# Execution model: frames and continuations

Decision of plan 11 step 17 (2026-10-05). It fixes how generated Erlang
functions call, return, suspend and fail once recursion, tail calls and
processes arrive. Step 19 implemented calls, returns, tail calls and frames
([Implementation](#implementation) lists what is still open); steps 23, 24,
26 and 43 build on it.

## Decision

Every Erlang process runs on its own **flat stack of explicit frames**, and
generated code moves between functions only by **guaranteed tail transfers**
(LLVM `musttail`). A non-tail call stores the caller's continuation in the
caller's frame and jumps to the callee; a return jumps back to that
continuation. The native stack therefore stays one call deep above the
scheduler, whatever the Erlang recursion depth, and a process can stop at any
transfer and resume later on any thread.

- One flat per-process stack replaces the 8F segmented root stack. It holds
  frame headers and slots, grows by moving, and is addressed as base plus
  offset.
- Each function that needs a frame is lowered to an **entry** and a **body**.
  The body starts with a `switch` on the frame's continuation index, so one
  LLVM function keeps all of the function's blocks, joins and handlers.
- Arguments and results travel in process registers `x[0..n)` (BEAM X
  registers); every code pointer has the single signature
  `void code(Process *)`.
- Exceptions unwind frames to the innermost frame whose header names a
  handler continuation.

## Process state

| Field | Meaning |
| --- | --- |
| `stack`, `capacity` | One growable word array; moves when it grows |
| `frame`, `top` | Word offsets of the current frame header and of the first free word |
| `x[]`, `live` | Argument/result registers; the first `live` words are roots at a transfer |
| `reductions` | Calls left in the time slice |
| `resume_at` | Entry a suspended process continues at |
| failure channel | Today's revision-2 channel: reason, payload, stack trace, `halted` |

## Frames

A frame is a fixed header followed by the function's slots (BEAM Y
registers). Slots are zeroed at push, as root frames are today.

| Header word | Meaning |
| --- | --- |
| `previous` | Offset of the caller's header (frames link by offsets, never pointers) |
| `function` | The function's descriptor |
| `resume` | Continuation index the body switches on when control returns here |
| `handler` | Continuation index of the innermost active handler, 0 when none |

The descriptor extends today's `abi::v1::FrameDescriptor` (module descriptor,
module and function atom slots, arity) with the entry and body code pointers
and the slot count. Header words are not terms; a walker follows `previous`
and reads slot counts from descriptors. A runtime-owned **bottom frame** sits
under the first call of every process: continuation 1 is a normal exit
(result in `x[0]`), continuation 2 the handler for an uncaught exception.

## Operations

- **Call (non-tail).** Live values are already in slots (today's rooting
  rule). The caller sets its own `resume` to the next continuation index,
  writes the arguments to `x[0..n)` and transfers to the callee entry. A remote
  call transfers to the exported entry symbol.
- **Entry.** Counts one reduction (see yield), pushes a zeroed frame (moving
  the stack when full), copies `x[0..n)` into slots, sets `resume = 0` and
  transfers to the body.
- **Return.** The callee writes the result to `x[0]`, pops its frame
  (`top = frame`, `frame = previous`) and transfers to the caller's body, which
  switches on the caller's `resume`.
- **Tail call.** Arguments go to `x[0..n)`, the caller pops its own frame
  without transferring, then transfers to the callee entry. The caller's
  caller remains the return target, so tail recursion runs in constant Erlang
  and native stack. Local, mutual and remote tail calls are the same jump.
- **Yield.** Every entry spends one reduction. At zero it records itself in
  `resume_at` and returns to the scheduler instead of pushing a frame; the
  arguments stay in `x[0..arity)`, which are then the process's only
  registers to root. Resuming refills the budget and transfers to
  `resume_at`, which repeats the entry. Later waits (`receive`, step 46) store
  a body continuation index instead of an entry.
- **Exit.** Returning into the bottom frame records a normal exit; unwinding
  into it records an uncaught exception. Either way the code returns to the
  scheduler.
- **Exception propagation.** A raise or a failed service check records the
  error in the channel as today and names the innermost 8 frames for the trace
  from the frame chain. The unwinder then pops frames whose `handler` is 0,
  sets `resume = handler` in the first frame that has one and transfers to its
  body. `catch`, `try` and `after` handlers are continuation indices;
  entering a protected region sets `handler` and leaving it restores the
  enclosing index, both known statically within one function. Halts and
  infrastructure failures skip every handler and unwind straight to the bottom
  frame, as they skip handlers today. The handler takes the exception with
  today's services (`CLAUSE_catch_v1`, `CLAUSE_exception_v2`) and
  re-raises through the unwinder.
- **Host invocation.** The runtime pushes a bottom frame, loads `x[]` and runs
  the scheduler loop until that frame is reached. Runtime services remain
  ordinary native calls and never re-enter generated code; only this host
  loop starts it.

A function that makes no non-tail call and keeps no slot across a safepoint
may skip its frame and return straight to its caller's body. This is an
optimization the compiler may add later, not part of the contract.

## Root visibility

At every transfer and every safepoint the roots of a process are: all slots of
all frames on its stack, `x[0..live)`, and the failure channel's payload,
argument list and stack term (already process roots). The result handoff
words of today's root stack disappear; `x[0]` takes their role.

Values never survive a transfer in native registers or SSA values. Each body
reloads its frame address from `stack + frame` after it is entered, and reads
live values from slots. Within one continuation, a service that can push a
frame or move the heap invalidates every slot pointer and heap pointer held in
SSA values; the reload rule for collections is fixed in
[collection in generated code](runtime-heap.md#collection-in-generated-code).

## Successor of the 8F root stack

The segmented stack exists only because generated code holds absolute frame
pointers across calls. Under this model no frame pointer survives a transfer,
so the stack becomes one flat block per process:

- frame headers live in the stack, slots are addressed as
  `stack + frame + header + index`;
- the block grows by doubling and moving (`realloc`), never shrinks while
  running, and is separate from the heap block;
- the 4,096-frame bound is dropped; stack words count toward the process
  memory budget, and exceeding it is the documented failure of step 20.

## Targets

`musttail` with the uniform `void (Process *)` signature is accepted by every
required target at O0 and O2 (prototype below, clang 23.1.2):

| Target | Word | Result |
| --- | --- | --- |
| `x86_64-pc-windows-msvc`, `x86_64-unknown-linux-gnu` | 64 | tail jumps |
| `i686-pc-windows-msvc`, `i686-unknown-linux-gnu` | 32 | tail jumps |
| `aarch64-unknown-linux-gnu`, `arm64-apple-macosx14.0` | 64 | tail jumps |
| `armv7-unknown-linux-gnueabihf` | 32 | tail jumps |

The backend reports an error when it cannot honour a `musttail` call, so a
successful compile is the guarantee. Fallback for a future target that rejects
it: a **trampoline**. Each code returns the next code pointer to the scheduler
loop instead of jumping (null suspends); frames, roots and continuation
indices are unchanged. No required target needs it.

## Implementation

Step 19 (2026-10-05) implements the model with these choices and gaps:

- **Two stages.** Lowering still emits *native form*: one
  `TermWord(context, arguments)` function per Erlang function, ordinary calls
  between them, a placeholder `clause.frame` call naming the term slots,
  and `ret` of a call's result in tail position. `lower_frames`
  (`compiler/src/codegen/frames.cpp`) then moves each body into
  `<symbol>.body`, adds the prologue (frame header, registers, resume
  switch), splits blocks after non-tail calls and loop-head safepoints,
  spills values used after them (terms to term slots, other words to raw
  slots), hoists constant slot addresses into the prologue, and
  turns calls, tail calls and returns into `musttail` transfers. Type
  specialization and test seams work on native form; the backend runs
  `lower_frames` before IR inspection and `optimize` runs it if still needed.
- **Entry and body.** There is no separate entry function. The process state
  table above is `abi::v1::FrameState`, at the start of every context, so
  generated code transfers inline (OPT0.1, ABI revision 9): an entry spends a
  reduction, pushes the zeroed frame, copies the arguments from the
  registers and transfers to the callee's body; a tail entry first reuses the
  caller's place; a return pops and transfers to the caller's body. Each path
  falls back to `CLAUSE_enter_v1`, `CLAUSE_tail_v1` or `CLAUSE_return_v1` when
  the heap wants a collection, the process profiles, no reduction is left,
  the stack must grow or reach its limit, or the callee is a builtin. The
  failure check after a call reads the state's `failed` word.
- **Register parameters** (OPT0.1A, ABI revision 10). Code is
  `void(void *, TermWord x0, TermWord x1, TermWord x2)`: every transfer
  passes the first three X registers as C parameters, so a call's leading
  arguments and a return's result stay in machine registers. A body reads its
  leading arguments from the parameters when it starts and the call result
  from `x0` when it resumes; `lower_frames` spills either to a slot only when
  it is live across a call or safepoint. The service paths store the
  parameters into the register array before the service and reload them
  after it. With every body sharing one prototype, LLVM turns a self tail call
  whose inline push leads back into the same body into a loop.
- **Tail positions** are the last expression of a clause body, followed
  through blocks, parentheses, `case` and `if` clause bodies. Calls in
  `catch`, `try`, `maybe` and `andalso`/`orelse` operands are not tail calls.
- **Exceptions** return through every caller, which checks the channel after
  the call as before; handler indices and direct unwinding remain an
  optimization. Stack traces read the frame chain at raise time.
- **Loops.** Comprehension generators are loops inside one body. Their
  cursors and accumulator live in term slots, so no SSA value is carried
  around a loop and a resume point inside it needs nothing beyond the usual
  spills.
- **Yields** (step 43, [processes](processes.md)). Every entry, inline or
  through `CLAUSE_enter_v1` and `CLAUSE_tail_v1`, spends one of the
  process's reductions; with none left the services take over:
  they record the entered function (`ProcessStack::resume_`, the model's
  `resume_at`), keep its arguments as register roots and return code that
  ends the time slice, so the native stack unwinds to the executor, which
  later repeats the entry. Loop heads do not yield. The collector enumerates frame term slots, the registers a
  suspension keeps live (`ProcessStack::keep_registers`) and the failure
  channel (step 23, [roots](runtime-heap.md#roots-and-safe-points)). Function
  entries and comprehension loop heads are safepoints that collect when the
  heap asks for it (step 26,
  [collection in generated code](runtime-heap.md#collection-in-generated-code));
  raw spill slots never hold terms.
- **No cap.** The stack grows until the host refuses memory, which fails
  with `out_of_memory` (exit 70, [executables](executables.md)), as an OTP
  process grows. An optional per-process `StackOptions::limit_words`
  (separate from the optional heap budget) fails a push beyond it with
  `resource_limit`. Frames take 4 header words plus 1-40 slots today.
- **Host entry.** An exported symbol keeps the native signature and runs its
  function above a runtime bottom frame with `CLAUSE_invoke_v1`; native
  exceptions thrown by services are contained there.

## Alternatives compared

Prototype in [tests/prototypes/execution_model](../tests/prototypes/execution_model/):
the same Erlang functions (`sum/1` body recursion, `loop/2` tail recursion,
`fail/1` raising `boom` at depth N, `catcher/1` catching it) hand-lowered three
ways. Host: Windows x64, clang 23.1.2; times are single runs at O2. Run
`python tests/prototypes/execution_model/run.py`.

| | Explicit frames + `musttail` (chosen) | Native calls + root frames (today) | LLVM coroutines (C++20) |
| --- | --- | --- | --- |
| 1M-deep body recursion | ok, 17 ms; 5 words per frame; 17 stack moves | 80 B native stack per level: about 13,000 levels in a 1 MiB thread | ok, 60 ms; one heap allocation of 64–80 B per call |
| 10M tail calls | ok, 5 ms; constant stack | O0 grows 80 B per call; O2 only by sibling-call luck | no tail calls: 1M iterations keep 1M frames |
| Yield / resume | every entry; two processes interleave | impossible without a native stack per process | symmetric transfer (itself `musttail`) |
| Native stack at depth 1M | 136–144 B | grows per level | 144–520 B |
| Roots visible to GC | slots in known frames | slots in known frames | coroutine frame layout chosen by LLVM; terms would need a second rooted copy |
| Exceptions | unwind to handler frame | channel check per return | channel check per return |

The trampoline variant of the chosen model passes the same runs (20 ms
recursion, 16 ms for 10M tail calls at O2). Rejected:

- **Native calls with explicit root frames.** Deep recursion needs a native
  stack per process sized for the deepest call; suspension needs per-target
  stack switching; 32-bit targets cannot reserve large stacks for many
  processes.
- **LLVM coroutines.** One allocation per call, no tail calls, opaque frames
  for the collector, coroutine passes even at O0, and symmetric transfer
  depends on `musttail` anyway.
- **One LLVM function per continuation** (classic CPS). Same transfers, but
  joins and handlers reached from several continuations would have to be split
  into further functions; the resume switch keeps today's single-function
  walker.

Accepted costs: one indirect jump per return plus a switch dispatch; values
live across calls are reloaded from slots (they are stored there already);
native debugger backtraces show only the current function, while Erlang
stack traces come from the frame chain.

## Prototype evidence

`run.py` builds the chosen model (`musttail` and trampoline), the native
baseline and the coroutine model for the host at O0 and O2, runs them, and
compiles the hand-lowered functions (`generated.cpp`, freestanding) for each
target above at O0 and O2, counting `musttail` calls in the IR and tail jumps
in the assembly. 2026-10-05 result: PASS. For the chosen model at both levels:
return (42), 1,000,000-deep body recursion, 10,000,000 tail calls, an error
raised at depth 100,000 caught by a handler frame, the same error uncaught
(bottom frame, trace of 8 `fail` frames), and two processes interleaving in
4,000-reduction slices (1,002 slices). All 15 transfer sites are `musttail` at
O0 on every target (14 at O2 after inlining).

The `sum/1` body for `i686-pc-windows-msvc` (O2 IR, names shortened). The
`x86_64-pc-windows-msvc` IR is the same with `i64` words and doubled offsets.

```llvm
%2 = load ptr, ptr %0, align 4                      ; stack base
%3 = getelementptr inbounds nuw i8, ptr %0, i32 8
%4 = load i32, ptr %3, align 4                      ; current frame offset
%5 = getelementptr inbounds nuw [4 x i8], ptr %2, i32 %4
%6 = getelementptr inbounds nuw i8, ptr %5, i32 16  ; slot 0 (N)
%7 = getelementptr inbounds nuw i8, ptr %5, i32 8   ; header: resume index
%8 = load i32, ptr %7, align 4
%9 = icmp eq i32 %8, 0                              ; switch on resume
...
16:                                                 ; N > 0: call sum(N - 1)
  store i32 1, ptr %7, align 4                      ; resume = 1
  ...                                               ; x0 = N - 1
  %19 = tail call ptr @call(ptr %0, ptr @SUM)       ; push frame or park
  musttail call void %19(ptr nonnull %0)
  ret void
20:                                                 ; resume 1: x0 += N
  ...
  %24 = tail call ptr @leave(ptr %0)                ; pop, caller body
  musttail call void %24(ptr nonnull %0)
  ret void
```
