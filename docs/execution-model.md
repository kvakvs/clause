# Execution model: frames and continuations

How generated Erlang functions call, return, suspend and fail. The model was
chosen in plan 11 step 17 (2026-10-05) after the prototype comparison in
[alternatives compared](#alternatives-compared); the generated-code side of its
ABI is in [frames and transfers](abi.md#frames-and-transfers).

## Decision

Every Erlang process runs on its own **flat stack of explicit frames**, and
generated code moves between functions only by **guaranteed tail transfers**
(LLVM `musttail`). A non-tail call stores the caller's continuation in the
caller's frame and jumps to the callee; a return jumps back to that
continuation. The native stack therefore stays one call deep above the
scheduler, whatever the Erlang recursion depth, and a process can stop at any
transfer and resume later on any thread.

- One flat per-process stack (`ProcessStack`, `process/stack`) holds frame
  headers and slots, grows by moving, and is addressed as base plus offset.
- Each function is one LLVM function, its **body**, that starts with a `switch`
  on the frame's resume index, so it keeps all of the function's blocks, joins
  and handlers.
- Arguments and results travel in process registers `x[]` (BEAM X registers).
  Every code pointer has the single signature `abi::v1::Code`,
  `void(void *context, TermWord x0, ..., TermWord x5)`: the first
  `abi::v1::register_arguments` (6) registers also pass as parameters, so a
  call's leading arguments and a return's result stay in machine registers.
- A failure returns through every caller, which checks the failure channel after
  the call ([failure channel](abi.md#failure-channel-revision-2)).

## Process state

`abi::v1::FrameState` starts every context, so generated code reads and updates
it inline:

| Field | Meaning |
| --- | --- |
| `stack`, `top`, `limit` | The stack block (moves when it grows), its first free word, and the words a push may reach without growing or passing the stack limit |
| `frame` | Word offset of the current frame header |
| `registers`, `live` | The 256 argument/result registers; the first `live` words are roots at a transfer |
| `reductions` | Function entries left in the time slice |
| `slow`, `failed` | Bits that send transfers to the services (`slow_collect`, `slow_profile`); nonzero while the failure channel holds a failure |

`heap_top`/`heap_limit`, `runtime` and `self` serve inline allocation, atom
loads and `self/0` ([frames and transfers](abi.md#frames-and-transfers)). A
suspended process records the entry it continues at in `ProcessStack::resume_`.

## Frames

A frame is a 4-word header followed by the function's slots (BEAM Y registers):
term slots, which are roots, then raw slots for spilled native values.

| Header word | Meaning |
| --- | --- |
| `previous` | Offset of the caller's header (frames link by offsets, never pointers) |
| `function` | The function's `FrameDescriptor` |
| `resume` | Continuation index the body switches on when control returns here |
| `handler` | Handler index; always 0, as failures return through callers |

The descriptor `<symbol>.frame` names the module, function and arity and holds
the body code pointer and the slot counts ([abi](abi.md#frames-and-transfers)).
Header words are not terms; a walker follows `previous` and reads slot counts
from descriptors. A runtime-owned **bottom frame** sits under the first call of
every process and of every host invocation: returning into it stores the result
in the first register and ends the native call.

## Operations

- **Call (non-tail).** Values live after the call are spilled to slots. The
  caller sets its own `resume` to the next continuation index, puts the
  arguments in the registers and transfers to the callee. Local, remote, fun and
  dynamic calls are the same transfer; a fun or dynamic call first asks a
  preparation service for the callee's `FrameDescriptor`
  ([funs](funs.md#dynamic-calls)).
- **Entry.** Spends one reduction, pushes a zeroed frame (growing the stack when
  full), copies the arguments past the parameters into slots, sets `resume = 0`
  and transfers to the callee's body.
- **Return.** The callee passes the result as `x0`, pops its frame
  (`top = frame`, `frame = previous`) and transfers to the caller's body, which
  switches on the caller's `resume`.
- **Tail call.** The caller pops its own frame without transferring, then enters
  the callee in its place. The caller's caller remains the return target, so
  tail recursion runs in constant Erlang and native stack. A self tail call
  keeps its frame where it is: it spends a reduction, resets the frame's resume
  and handler words and the live count and copies the arguments past the
  parameters, without rewriting the header or zeroing the slots; LLVM turns it
  into a loop. A tail call to another member of the caller's tail-call cycle
  (functions of one module that reach each other through tail calls) keeps the
  caller link and the term slots both frames have: it writes the callee's
  descriptor, zeroes only the callee's term slots past the caller's and skips
  the stack limit test when the callee's frame is no larger. Like a self tail
  call, a kept slot may hold a term of an earlier member until the callee writes
  it, so collections can keep such a term alive a little longer. The members of
  a cycle share one native function: every member's descriptor names it as the
  body, its resume indices count across the members, and a start (resume 0)
  picks the member from the frame's descriptor, so a tail call between members
  is a branch rather than a transfer. Debuggers see the members as inlined into
  that function.
- **Yield.** With no reduction left, an entry does not push: it records itself
  in `ProcessStack::resume_`, keeps its arguments as register roots
  (`ProcessStack::keep_registers`) and returns code that ends the time slice, so
  the native stack unwinds to the executor, which later refills the budget and
  repeats the entry. Comprehension loop heads also spend a reduction and yield
  through an entry when none is left. A `receive` waits through the
  `CLAUSE_wait_frame_v1` builtin, entered like a call
  ([receive](processes.md#receive)).
- **Exit.** Returning into the bottom frame ends the process with its result; a
  failure that returns into it ends the process with the failure.
- **Exceptions.** A raise or a failed service check records the error in the
  channel and copies the 8 innermost frames of the chain for the stack trace
  ([stack traces](abi.md#stack-traces)). Each caller checks the channel after
  the call and returns at once, or jumps to the handler block of an enclosing
  `catch`, `try` or `after` in the same body
  ([failure channel](abi.md#failure-channel-revision-2)). Halts and
  infrastructure failures skip every handler and return to the bottom frame.
- **Host invocation.** An exported symbol keeps a native signature and runs its
  function above a bottom frame with `CLAUSE_invoke_v1`, resuming it after each
  yield; native exceptions thrown by services are contained there. Runtime
  services remain ordinary native calls and never re-enter generated code.

Every call pushes a frame; leaf functions do not skip it, so every function
keeps its stack-trace entry.

## Root visibility

At every transfer and every safepoint the roots of a process are: the term slots
of all frames on its stack, `x[0..live)`, and the failure channel's payload,
argument list and stack term ([roots](runtime-heap.md#roots-and-safe-points)).

Values never survive a transfer in native registers or SSA values, except the
parameters of the transfer itself. Each body reloads its frame address from
`stack + frame` after it is entered, and reads live values from slots. Within
one continuation, a service that can push a frame or move the heap invalidates
every slot pointer and heap pointer held in SSA values; the reload rule for
collections is fixed in [reload rule](runtime-heap.md#reload-rule).

The stack block grows by doubling and moving while the runtime-wide limit allows
it, never shrinks while running, and is separate from the heap block. It has no
frame-count bound; stack words count toward the
[runtime memory limit](runtime-heap.md#runtime-memory-limit).

## Targets

`musttail` with one uniform code signature is accepted by every required target
at O0 and O2 (prototype below, clang 23.1.2):

| Target | Word | Result |
| --- | --- | --- |
| `x86_64-pc-windows-msvc`, `x86_64-unknown-linux-gnu` | 64 | tail jumps |
| `i686-pc-windows-msvc`, `i686-unknown-linux-gnu` | 32 | tail jumps |
| `aarch64-unknown-linux-gnu`, `arm64-apple-macosx14.0` | 64 | tail jumps |
| `armv7-unknown-linux-gnueabihf` | 32 | tail jumps |

The backend reports an error when it cannot honour a `musttail` call, so a
successful compile is the guarantee. Fallback for a future target that rejects
it: a **trampoline**, where each code returns the next code pointer to the
scheduler loop instead of jumping; frames, roots and continuation indices are
unchanged. No required target needs it.

## Implementation

- **Two stages.** Lowering emits *native form*: one `TermWord(context,
  arguments)` function per Erlang function, ordinary calls between them, a
  placeholder `clause.frame` call naming the term slots, and `ret` of a call's
  result in tail position. `lower_frames` (`compiler/src/codegen/frames.cpp`)
  then moves each body into `<symbol>.body`, adds the prologue (frame header,
  registers, resume switch), splits blocks after non-tail calls and loop-head
  safepoints, spills values used after them (terms to term slots, other words to
  raw slots), hoists constant slot addresses into the prologue, and turns calls,
  tail calls and returns into `musttail` transfers. Type specialization and test
  seams work on native form; the backend runs `lower_frames` before IR
  inspection and `optimize` runs it if still needed.
- **Inline transfers.** Entries, tail entries and returns run inline on
  `FrameState`. Each falls back to `CLAUSE_enter_v1`, `CLAUSE_tail_v1` or
  `CLAUSE_return_v1` when the heap wants a collection, the process profiles, no
  reduction is left, the stack must grow or reach its limit, or the callee is a
  builtin. The failure check after a call reads the state's `failed` word. The
  service paths store the parameters into the register array before the service
  and reload them after it.
- **Parameters.** A body reads its leading arguments from the parameters when it
  starts and the call result from `x0` when it resumes; `lower_frames` spills
  either to a slot only when it is live across a call or safepoint. A float loop
  variant ([abi](abi.md)) takes some parameters as the bits of doubles: only its
  inline transfers pass them, and its service paths enter the function with
  cells instead, so registers a service reads hold terms.
- **Tail positions** are the last expression of a clause body, followed through
  blocks, parentheses and the clause bodies of `case`, `if` and `receive`. Calls
  in `catch`, `try`, `maybe` and `andalso`/`orelse` operands are not tail calls.
- **Loops.** Comprehension generators are loops inside one body. Their cursors
  and accumulator live in term slots, so no SSA value is carried around a loop
  and a resume point inside it needs nothing beyond the usual spills.
- **Safepoints.** Function entries and comprehension loop heads collect when the
  heap asks for it
  ([collection in generated code](runtime-heap.md#collection-in-generated-code));
  raw spill slots never hold terms.
- **No cap.** The stack grows until the host refuses memory, which fails with
  `out_of_memory` (exit 70, [executables](executables.md)), as an OTP process
  grows. An optional per-process `StackOptions::limit_words` (separate from the
  optional heap budget) fails a push beyond it with `resource_limit`.

## Alternatives compared

[tests/prototypes/execution_model](../tests/prototypes/execution_model/)
hand-lowers the same Erlang functions (`sum/1` body recursion, `loop/2` tail
recursion, `fail/1` raising at depth N, `catcher/1` catching it) three ways and
compiles them for every target above at O0 and O2
(`python tests/prototypes/execution_model/run.py`). On Windows x64 at O2 the
chosen model ran 1M-deep body recursion in 17 ms with 5 words per frame and 10M
tail calls in constant stack; its trampoline variant passed the same runs.
Rejected:

- **Native calls with explicit root frames.** Deep recursion needs a native
  stack per process sized for the deepest call (about 13,000 levels in a 1 MiB
  thread); suspension needs per-target stack switching; 32-bit targets cannot
  reserve large stacks for many processes.
- **LLVM coroutines.** One heap allocation per call, no tail calls, frames whose
  layout the collector cannot see, coroutine passes even at O0, and symmetric
  transfer depends on `musttail` anyway.
- **One LLVM function per continuation** (classic CPS). Same transfers, but
  joins and handlers reached from several continuations would have to be split
  into further functions; the resume switch keeps a single-function walker.

Accepted costs: one indirect jump per return plus a switch dispatch; values live
across calls are reloaded from slots; native debugger backtraces show only the
current function, while Erlang stack traces come from the frame chain
([debugging](debugging.md)).
