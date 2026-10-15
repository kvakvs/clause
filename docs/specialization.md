# Type specialization

Speed mode (`-O2`) uses [inferred facts](semantic.md#inference) to remove
runtime checks (proofs) and may clone a function into representation variants
guarded by runtime tag checks. `-O0` and
`--no-type-specialization` produce generic code only: every check stays, every
compound access goes through its runtime service.

## Proofs

A proof comes only from facts that hold whenever the code runs:

- the facts of a local function's arguments joined from all its callers
  (`Inference::inputs`; exported functions and functions named by `fun F/A` have
  none);
- the recorded fact of an expression (an operand's fact is recorded before the
  operation that uses it narrows it);
- a shape test earlier in the same match plan: plan nodes run in a line, so a
  passed tuple or cons test dominates every later node.

Specifications never prove anything. `term()` and `none()` (code inference found
unreachable) prove nothing; anonymous fun bodies get no proofs.

| Operation | Proven by | Generated code |
| --- | --- | --- |
| `+ - *` | both operands small-integer ranges, result range within the immediate payload | decode, compute and encode inline; no tag, overflow or fallback |
| `+ - *` | one or both operands small-integer ranges | the proven tag tests go; overflow check and service fallback stay |
| tuple pattern | argument or scrutinee fact: exact tuples of the pattern's arity | shape test removed (unproven: tested inline) |
| tuple element | a shape test or fact proving the tuple | inline load of the element |
| list pattern | fact: surely a cons cell | shape test removed (otherwise a primary-tag test at every level) |
| head, tail | a cons test or fact proving the cell | inline loads |
| exact literal `[]`, `{}`, small integer, atom | always (at every level) | word comparison: equal immediates are one word |
| list generator | generator input fact: a proper list | cons test and cell reads inline on every iteration |

Facts reach patterns of function heads (caller-joined arguments), body matches
(the right side) and case/try clauses (the scrutinee or try body value);
element, head and tail facts follow extractions. An inline read is stored into a
root slot like a service output, so frame lowering keeps it as a term across
safepoints ([reload rule](runtime-heap.md#reload-rule)). The only pointer
conversions in generated code are these proven reads and the header loads of
inline tuple shape tests (`proven.cell`). Layout constants come from
[`abi/term.hpp`](abi.md#terms); the runtime asserts them against its private
cell layouts.

## Variants

- Profiles come from call-site facts: an argument whose fact holds only small
  integers (singletons or ranges) proves a small-integer argument. Specs, broad
  `integer()`, unions with other kinds and unknowns stay generic.
- A variant is justified only if it removes an implemented check: an exact
  low-tag comparison of an argument word anywhere in the body (the argument
  array is never written, and a collection never changes a small integer).
- Limits: 3 variants per function, 32 per module, 128 per target. Estimated and
  then actual growth (clone + guards + dispatch + fallback) must stay within 2x
  the generic instruction count per function and module.
- Generic bodies are always kept. Over-budget drafts are discarded silently; the
  program never fails because of specialization.
- Selection is deterministic (module, symbol, profile order).

LLVM cloning/simplification replaces proven checks inside the clone. A bounded
dispatcher tests every constrained tag and forwards context and arguments to a
variant or the generic body; the public symbol and ABI do not change. No spec
assumptions or fast-math flags are introduced.

## Current effect

Proofs act on real source: local functions whose callers pass small integers,
tuples and lists lose their tag, shape and service checks
([`proofs`](../tests/fixtures/executables/proofs/proofs.erl), checked by CTest
`codegen_proofs` and run as the executable golden `executables_proofs`). A
variant must save more instructions than its dispatch costs; one removed tag
test of an argument rarely does, so source variants are usually planned and then
rejected by the growth limit. Focused LLVM fixtures inject repeated tag checks
to exercise hits, fallbacks and growth rollback with native execution.

Executable goldens run under every policy in full mode; fast mode runs `-O0`
(generic) and `-O2` with specialization (proofs).

`codegen_measurements` writes `measurements/<config>/measurements.json` in the
codegen test build directory (compile time, IR/object bytes, variants, proven
reads, native time). Times are descriptive, never thresholds.
