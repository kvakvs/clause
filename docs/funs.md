# Function values

`fun F/A`, `fun M:F/A`, anonymous funs with captured variables (closures),
named funs (`fun Name(...) -> ... end`), calls of function values (`F(Args)`)
and dynamic calls (`M:F(Args)`, `apply/2,3`). Facts come from the pinned
`maint-29` sources (`erts/emulator/beam/utils.c` `erts_cmp`,
`erl_printf_term.c`, `erl_lint`) and probes on OTP 29.1.1.

## Values

| Source | Value | Calls enter |
| --- | --- | --- |
| `fun f/1` | Local fun of this module; every `fun f/1` of a module is the same value | `f/1` of this module, exported or not |
| `fun m:f/1` | External fun naming `m:f/1`, also for the current module | `m:f/1` when a module of the program exports it |
| `fun(X) -> ... end` | Local fun capturing the variables it reads from its creator; each evaluation builds a new value | The fun's own generated function |
| `fun Name(X) -> ... end` | The same, with `Name` bound to the fun inside its clauses | The fun's own generated function |
| `fun M:F/A` with variables | External fun built when evaluated; equal to the literal `fun m:f/1` it names | `m:f/1` when a module of the program exports it |

- `fun F/A` must name a function of the module (`function F/A undefined`) or an
  auto-imported builtin: `fun is_atom/1` is the external fun `erlang:is_atom/1`.
  Funs of builtins call them through the [builtin bridge](builtins.md); a
  literal `fun erlang:F/A` of a function outside the catalog
  (`fun erlang:apply/2`) reports the unavailable `dynamic calls` capability.
- `F(Args)` evaluates `F`, then the arguments left to right, then checks the
  value: a non-function raises `{badfun, F}`, another arity
  `{badarity, {F, Args}}` (checked before the module), an external fun whose
  function nothing in the program exports `undef`. A call in tail position is a
  tail call, as for named functions.
- `is_function/1,2` are true for funs in bodies and guards.

## Dynamic calls

- `M:F(Args)` with a variable (or any expression) as module or function
  evaluates the module, then the function, then the arguments left to right. A
  non-atom module or function raises `badarg`; a function no module of the
  program exports with that arity raises `undef`. A call in tail position is a
  tail call.
- `apply(Fun, Args)` and `apply(M, F, Args)` (auto-imported unless the module
  defines `apply/2,3` or suppresses the import; also `erlang:apply/2,3`)
  evaluate their arguments, then require `Args` to be a proper list (`badarg`).
  `apply/2` then calls `Fun` as `F(Args)` does (`{badfun, Fun}`,
  `{badarity, {Fun, Args}}`); `apply/3` looks up `M:F/length(Args)` as above, so
  more than 255 arguments raise `undef`. Both are tail calls in tail position
  and never legal in guards.
- `fun M:F/A` with variables raises `badarg` unless `M` and `F` are atoms and
  `A` is an integer in 0..255; the function need not exist until the fun is
  called.
- Lookup uses the code server's hash indexes of the program's exports
  ([runtime](runtime.md#code-server-and-builtins)), which stay registered for
  the program's lifetime, so a found function cannot go away during the call.
  A name no module exports is then looked up among the
  [builtins](builtins.md), so `M:F(...)`, `apply/3` and runtime `fun M:F/A`
  reach the builtins of the bridge catalog.
- **Services.** `CLAUSE_call_v1(context, module, function, arity)` checks the
  names and returns the `FrameDescriptor` of the export (from its
  `ExportDescriptor`); the arguments are already in the registers.
  `CLAUSE_apply_list_v1(context, fun, list, registers)` and
  `CLAUSE_call_list_v1(context, module, function, list, registers)` copy the
  list into the registers first. All three return null after recording the
  error, and generated code then transfers through the `clause.apply` marker as
  for `F(Args)`. `CLAUSE_make_external_fun_v1` builds the fun of an external
  `FunDefinition` the code server creates once per `M:F/A`
  (`CodeServer::external_fun`).

## Closures

- Each clause starts from the scope at the fun. Head variables are new names
  that shadow outer ones (OTP warns); a name repeated within one head must
  match. Guards read the head's names. Nothing a fun binds is visible after it,
  and case-clause names of the enclosing function do not reach into it.
- A fun captures every outer definition its heads, guards or bodies read (nested
  funs included), in definition order, as OTP orders a function's free
  variables. Captured values are copied into the fun cell when the fun
  expression is evaluated, so they survive the creator's return and collections,
  and copy with the fun.
- The code is a private function `-f/A-fun-N-` (N counts the funs of `f/A` in
  source order) taking the fun's arguments, then its captured values; stack
  traces show that name with the combined arity, as OTP's do. No clause matching
  raises `function_clause`.
- Arguments plus captured values are limited to 255.
- A named fun's clauses see `Name` as the fun itself: a new name shadowing an
  outer one (OTP warns), never captured and not visible after the fun. A head
  variable of the same name shadows it in turn. When a clause reads `Name`, the
  fun's code builds the value on entry from its captured values, so it is equal
  (`=:=`) to the fun being called, and `Name(...)` is an ordinary fun call: in
  tail position it is a tail call and runs in constant stack.
- An anonymous fun in a record field default is one fun for every construction
  that uses the default (OTP expands a copy per site).

## Representation

- **Descriptor.** Each distinct value a module creates compiles to one
  `abi::v1::FunDescriptor` in the module's private `<prefix>.funs` table
  ([funs.hpp](../abi/include/clause/abi/funs.hpp)): module descriptor, module
  and function atom slots, Erlang arity, index, external flag and the
  `FrameDescriptor` a call enters (null for an external fun outside the
  program). Registration binds them to `FunDefinition`s (`ModuleAtoms::funs`,
  `CodeServer::fun_definition`); a local fun's captured value count is its
  code's arity minus the fun's.
- **Cell.** Boxed kind `fun_closure`: header (count `1 + n`), an untraced
  `const FunDefinition *`, then `n` captured values. Walking, collection,
  copying and verification skip the definition word, as for native records.
- **Services.** `CLAUSE_make_fun_v1(context, descriptor, captures, count,
  output)` builds a fun. `CLAUSE_apply_v1(context, fun, arity, arguments)`
  checks a called value, records the errors above in the failure channel
  (`ErrorReason` 22-24) and otherwise appends the captured values after the
  arguments and returns the `FrameDescriptor` to enter.
- **Inline closures.** The runtime that claims a module's atom cache also
  fills `ModuleDescriptor::fun_cache` with the address of each fun's
  definition; while the claim is its own, generated code bump-allocates the fun
  cell (header, definition, captured values) itself and calls
  `CLAUSE_make_fun_v1` only without heap room or in another runtime.
  `CLAUSE_apply_v1` and `CLAUSE_apply_list_v1` read a fun of the right arity
  (and the argument list) straight from its cells before the checked path that
  reports errors.
- **Calls.** Native form passes the arguments in a word array and calls the
  `clause.apply` marker with the descriptor; `lower_frames` makes the array the
  process registers and the marker an enter or, in tail position, a tail
  transfer ([execution model](execution-model.md#implementation)).

## Comparison and printing

- Funs order after references and before ports
  ([term order](terms.md#comparison-and-order)).
- Local funs order before external funs. Local funs compare by module, then
  index, then captured values in order (`==` compares those with `==`); external
  funs by module, function and arity. Equal descriptors give equal values, so
  `fun f/1 =:= fun f/1`.
- `erlang:display/1`, `~w` and uncaught-exception reports print an external fun
  as `fun m:f/1` (atoms quoted like the emulator does) and a local fun as
  `#Fun<m.Index.0>`.

## Differences

Fun numbering, printing and ordering, capture order in comprehensions, `undef`
stack traces and calls of modules outside the program differ from OTP as
recorded in [differences](differences.md).

## Fun information

`runtime/src/builtins/erlang/fun_info.cpp`. `erlang:fun_info/1` lists
OTP's items in OTP's order: for a local fun `pid`, `module`, `new_index`,
`new_uniq`, `index`, `uniq`, `name` (`-F/A-fun-N-` for an anonymous fun),
`arity`, `env` (the captured values) and `type`; for `fun M:F/A` `module`,
`name`, `arity`, `env` (`[]`) and `type`. `fun_info/2` returns one
`{Item, Value}`; an unknown item is `badarg`. `pid` is `undefined`, `new_uniq`
the MD5 of the module name and `uniq` its first 24 bits, so they differ from
OTP's. `fun_to_list/1` is the `~w` text. `is_builtin/3` is true for the
runtime's builtins and `erlang:apply/2,3`.
