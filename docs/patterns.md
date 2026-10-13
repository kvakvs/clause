# Patterns, clauses and body matches

Function clause heads, body matches, `case` clauses and `if` guards are executable for every
admitted term kind ([terms](terms.md)). Legality and availability are checked
separately: invalid Erlang is a **semantic** error even in unreachable code;
legal code that needs a missing feature gets a **capability** diagnostic; a
runtime **mismatch** is never a compiler error.

| Context | Status |
| --- | --- |
| Function heads + guards | Implemented; exhaustion raises `error:function_clause` |
| Body matches and sequences, `begin`/`end` | Implemented; failure raises `error:{badmatch, RHS}` |
| `case` clauses + guards | Implemented; exhaustion raises `error:{case_clause, Value}` |
| `if` guard clauses | Implemented; exhaustion raises `error:if_clause` |
| `maybe` with `?=` and `else` clauses | Implemented; a failed `?=` yields its value or selects an `else` clause, whose exhaustion raises `error:{else_clause, Value}`; needs the `maybe_expr` feature ([preprocessor](preprocessor.md)) |
| List, binary and map comprehensions | Implemented ([below](#comprehensions)) |
| `catch Expr` | Implemented; no patterns ([ABI](abi.md#failure-channel-revision-2)) |
| `try` `of` and catch clauses | Implemented; `of` exhaustion raises `error:{try_clause, Value}`, unmatched exceptions re-raise and `after` runs on every path ([ABI](abi.md#failure-channel-revision-2)); `Class:Reason:Stack` binds the [stack trace](abi.md#stack-traces) |
| Fun clauses | Capability (F18) |
| `receive` | Capability (F22/F25) |

## Comprehensions

`[T1, ..., Tn || Q1, ..., Qm]`, `<< T || Q1, ... >>` and `#{K => V, ... || Q1, ... }`
follow OTP 29:

- Qualifiers run left to right; each generator loops over the rest. Templates
  are evaluated in that order for every surviving combination; several
  templates add several elements per combination. A binary comprehension's
  template must be a bitstring (else `error:badarg` at that element); the
  pieces are joined in order, partial bytes included. A map comprehension
  evaluates its (first) value before its key, and a later duplicate key wins.
- Generators: `P <- List`, `<<Segs>> <= Bits` and `K := V <- Map`, with the
  strict forms `<:-` (lists, maps) and `<:=`. A bitstring generator matches its
  pattern against a prefix and continues with the rest. When a relaxed one
  rejects an element it skips as many bits as the pattern's sizes describe
  (values ignored, floats read as integers, as OTP does); when even that
  fails, or fewer bits remain, the generator ends. A map generator walks the
  map in key order (OTP also iterates up to 32 keys in key order, except atom
  keys, which it orders by atom index; larger maps follow OTP's hash order,
  which Clause does not reproduce).
- A generator pattern binds new names: it shadows outer names, and nothing a
  comprehension binds is visible after it. A relaxed generator skips
  elements its pattern rejects; a strict one raises `error:{badmatch, E}` with
  the list element, the remaining bitstring or `{Key, Value}`.
- A zip group (`P1 <- L1 && P2 <- L2`) takes one element of every input per
  step; its patterns bind together, so a repeated name must match. A rejected
  step is skipped unless a strict pattern rejects it. Inputs running out
  unevenly, or a strict rejection, raise `error:{bad_generators, {L1', L2'}}`
  with the inputs remaining at that step (a map generator shows OTP's iterator
  `{K, V, Next}`, ending in `none`). Filters inside a zip group are semantic
  errors. When relaxed and strict generators of one group share a variable,
  the skip rule can differ from OTP ([differences](differences.md)).
- An input that is not a map raises `error:{bad_generator, Input}` before a
  map generator starts, even inside a zip group.
- A list input that is not a list, or an improper tail, and a bitstring input
  that is no bitstring raise `error:{bad_generator, Tail}` once the elements
  before it are done.
- A filter that is a guard test (OTP `erl_lint:is_guard_test/3`: guard syntax
  calling only unshadowed guard BIFs, legacy type tests at top level) rejects
  the element on any failure, as a guard. Any other filter must return `true`
  or `false`; other values raise `error:{bad_filter, Value}` and its exceptions
  propagate.
- A top-level match qualifier (`P = E`) is a semantic error unless the
  experimental `compr_assign` feature is enabled; executing it then is not
  implemented (capability).

Each generator is a loop in the function body whose input cursor (a list or
bitstring rest, or a map and a position) lives in frame term slots; the
produced elements accumulate reversed in another term slot and become the
list, the joined bitstring or the map once at the end, so long inputs need
constant native and process stack.

## Pattern forms

| Form | Rule | Rejection |
| --- | --- | --- |
| `_` | Never binds; occurrences independent | Reading `_` is semantic |
| `Name`, `_Name`, repeats | Single assignment; repeats need exact equality (`1` ≠ `1.0`) | Unbound/unsafe read semantic; inequality mismatches |
| `P1 = P2`, parentheses | Both constrain the same value; no key/size bindings between siblings | Illegal sibling dependency semantic |
| Atoms, integers, chars, floats | Exact equality; signed zero distinct | Mismatch |
| Constant arithmetic | OTP pattern operators folded exactly; `1 div 0` and non-constants rejected | Semantic |
| Tuples, lists, improper tails, strings | Exact arity and cons/nil shape; strings are lists | Mismatch |
| `"prefix" ++ Tail` | Literal string or literal integer list prefix only | Variable prefix semantic |
| `#{K := P}` | `:=` only; extra keys allowed; `#{}` tests type; keys are guard expressions over incoming bindings | `=>`, unbound key semantic; missing key mismatches |
| Bitstrings | Validated type/size/unit; earlier segments may size later ones; unsized tail last | Invalid specifier semantic; short data mismatches |
| Tuple records `#r{f = P}`, `#r.f` | Expanded to tuple constraints; omitted fields unconstrained | Unknown record/field semantic |
| Local native records `#r{f = P}` | Record of this module named `r`, then each listed field (a field it lacks fails) | Unknown record semantic |
| Qualified/imported native records `#m:r{f = P}` | Record of module `m` named `r`; exported when a field is listed; then each field | None at compile time |
| Anonymous records `#_{f = P}` | Any native record; exported or defined in this module when a field is listed | `#_{...}` as an expression is an error |
| Calls, variable arithmetic, other expressions | Not patterns | Semantic |

## Scopes

Map keys and binary sizes read the incoming environment, never siblings:
`#{K := V} = #{key := K}` and `<<X:N>> = <<N:8>>` cannot bind their own key or
size. Inside one binary, a size may read earlier segments: `<<N:8, X:N>>` is
legal; `{N, <<X:N>>}` needs `N` bound beforehand. Constant bad arithmetic in a
key or size is legal and fails at match time.

Binary validation rejects nested-container/alias segments, conflicting modifiers,
bad unit ranges, unit without size for integer/float defaults, invalid UTF
size/unit, typed/sized literal strings and non-final unsized binary segments.

## Execution

- A flat match plan per head/LHS over original argument slots; aliases share an
  input, first definitions bind SSA values, repeats emit exact-equality tests.
  Shape checks dominate extraction; extraction uses checked runtime services.
- Clauses run in source order, each with a fresh environment that reloads the
  original arguments. Head mismatch or guard rejection goes to the next clause;
  body errors and infrastructure failures never retry later clauses.
- A `case` evaluates its scrutinee once, then tries each clause in order with
  the scrutinee as the single plan input: pattern mismatch or guard rejection
  (including guard errors) goes to the next clause, exhaustion raises
  `{case_clause, Value}`. Every clause starts from the bindings before the
  case; the case value and each exported binding join in one PHI per value.
- An `if` is a `case` without scrutinee or patterns: each clause guard is tried
  in order (a guard error rejects the clause), exhaustion raises `if_clause`,
  and values and exports join the same way.
- `begin`/`end` runs its sequence in the enclosing scope and yields its last
  value.
- Body sequences run in order and return the last value. A match evaluates its
  RHS once, binds new names, checks existing ones and returns the RHS (also for
  `_ = RHS`). Chains evaluate the innermost RHS first.
- Atom literals load module bindings; matching never interns atoms.
- Unconditional variable heads compile to compact projection IR.

## Limits

Pattern normalization shares the module's semantic budget (1,000,000 units, or
32 per expression for larger modules)
(an integer costs about its decimal digits); a constant past the integer limit
of 4,194,240 bits is an `illegal pattern`, as in OTP
([terms](terms.md#integers)).
Each match plan has a 100,000-work ceiling. The parser separately limits nesting
to 256 (hard ceiling 512).
