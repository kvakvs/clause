# Semantic analysis

Runs after parsing in default compilation and `--print-types`; syntax-only
actions skip it. Errors stop the affected batch before LLVM; diagnostics keep
macro/include origins, and later inputs are still diagnosed.

## Module and call checks

- Module declaration required and unique; function arities 0..255; no duplicate
  definitions; every export exists (a repeated export warns, as in OTP).
  Quoted/Unicode names keep their exact identity.
- Every function is checked, including unused and unreachable code.
- Calls resolve within the batch by module/name/arity. Remote calls (including
  self-qualified) require exports. Missing/private callees and duplicate modules
  are errors.
- `fun F/A` must name a function of the module (`function F/A undefined`);
  `fun M:F/A` and calls of values (`F(Args)`) are not resolved at compile time
  ([funs](funs.md)). Each distinct fun value gets a `Module::funs` entry after
  binding analysis.
- Self, mutual and cross-module recursion are accepted. The call graph is split
  into strongly connected components (iterative Tarjan) in callee-before-caller
  order; a component is recursive when it has several members or a member calls
  itself.

## Behaviours

Follows OTP's
[behaviours](https://www.erlang.org/doc/system/design_principles.html#behaviours),
`erl_internal:add_predefined_functions` and `erl_lint`'s `check_behaviour`.

- A module declaring `-callback` gets OTP's predefined, exported
  `behaviour_info/1`: `callbacks` gives the declared callbacks in source order,
  `optional_callbacks` those of the well-formed `-optional_callbacks` lists
  (callbacks qualified with a module are left out). The frontend appends it as a
  function at the `-module` line (`driver/predefined`). As in OTP the module's
  own source cannot name it: an `-export`, local call, `fun
  behaviour_info/1` or `-spec` of it is an undefined function; remote calls
  (also self-qualified) reach it. `-callback` beside a hand-written
  `behaviour_info/1` is an error
  (`cannot define callback attibute for F/A when behaviour_info is defined`,
  OTP's spelling).
- `-behaviour(M)` and `-behavior(M)` may repeat. `M` resolves in the batch; a
  name with a library module (`library.md`) adds that module like a literal call
  does. Diagnostics use OTP's text at the attribute name:
  - `undefined callback function F/A (behaviour 'M')` for each required,
    non-optional callback the module does not export (sorted);
  - `conflicting behaviours -- callback F/A required by both 'M2' and 'M1' (line
    L, column C)` when an earlier behaviour also requires it (an optional
    callback counts when exported);
  - `behaviour M undefined` when `M` is not in the batch or has no
    `behaviour_info/1` (OTP behaviours such as `gen_server` are not in the
    Clause [library](library.md)), plus OTP's module-name errors for names that
    cannot be modules (`the module name must not be empty`,
    `... must be an atom`, ...).
- Warnings do not fail compilation. `-compile` options `nowarn_behaviours` (the
  whole check, module-name errors included), `nowarn_undefined_behaviour_func`,
  `nowarn_undefined_behaviour` and `nowarn_conflicting_behaviours` turn them
  off.
- Not evaluated: a hand-written exported `behaviour_info/1` of a behaviour
  module, so its users' callbacks are not checked, and OTP's ill-defined and
  deprecated-callback warnings never occur
  ([differences](differences.md#errors-stack-traces-and-reports)).
- Evidence: `tests/fixtures/lint` (OTP-generated diagnostics, CTest
  `lint_diagnostics`), `executables_behaviours`, `semantic` CLI cases.

## Imports

Follows `erl_lint`'s import rules.

- `-import(Module, [F/A, ...])` makes a local call `F(...)` the remote call
  `Module:F(...)`: the module must be in the batch (a library module joins it
  like a literal call) and export the function, or be a builtin module
  (`-import(io, [format/2])`, `-import(erlang, [display/1])`). An import takes
  precedence over an auto-imported BIF of the same name.
- Errors with OTP's text: `function F/A already imported from M` (the whole
  attribute is then ignored), `defining imported function F/A`,
  `creating a fun from imported name F/A is not allowed`. An imported function
  in a guard is an illegal guard call (except erlang guard BIFs).
- Warning `import directive overrides auto-imported BIF F/A -- ...` unless
  `no_auto_import` names it or `nowarn_bif_clash` is given; Clause knows the
  auto-imported BIFs it implements.
- Evidence: `executables_imports`, lint cases `imports_conflicts`,
  `imports_fun`, `imports_bif`, semantic CLI cases.

## Predefined functions

Follows OTP's [modules](https://www.erlang.org/doc/system/modules.html) and
`erl_internal:add_predefined_functions`. The frontend appends `module_info/0,1`
to every compiled module (after a generated `behaviour_info/1`) as Erlang
functions returning literal data (`driver/predefined`, `semantic/module_info`);
`Module::predefined_` counts these trailing forms, which are always exported and
left out of `--print-types`.

- `module_info()` gives
  `[{module,_},{exports,_},{attributes,_},{compile,_},{md5,_}]`;
  `module_info(Key)` also answers `functions`, `nifs` (`[]`) and `native`
  (`false`), and raises `badarg` for any other key.
- `exports`: the exported functions in definition order (escripts add `main/1`),
  then `behaviour_info/1` when generated, `module_info/0`, `module_info/1`.
  `functions`: every function in definition order, then the same predefined
  ones.
- `attributes`: every attribute except `module`, exports and imports,
  type/spec/callback forms, records, `export_type`, `optional_callbacks`,
  `export_record`, `compile`, `file`, documentation and `feature`, in source
  order; a value that is not a list is wrapped in one (`-tags(a)` is
  `{tags,[a]}`). Without `-vsn`, `{vsn,[N]}` comes first, `N` being `md5` read
  as an unsigned big-endian integer, as OTP derives it.
- `md5`: MD5 of the module's printed source (`print_source`: macros expanded,
  includes inlined, comments and layout dropped). `compile`:
  `[{version,ClauseVersion},{options,[]},{source,AbsolutePath}]`.
- Unlike `behaviour_info/1`, the module's own source sees them: local calls and
  `fun module_info/1` work. Defining either is an error
  (`function module_info/0 already defined`); exporting one, like any repeated
  export, only warns (`function module_info/0 already exported`).
- Attributes are data; only `on_load`, `nifs` and non-inert `-compile` options
  are rejected as behavior-changing.
- The generated functions have no source line (OTP gives them none): no debug
  locations, IR source comments or `--print-types` entries. `module_info/0`
  holds the same literals as `module_info/1`, so it makes no calls.
- Evidence: `executables_module_info` (OTP stdout; derived values by shape),
  `tests/fixtures/lint` cases `module_info_defined`, `exports_repeated`.

## Bindings

Each binding has a function-relative identity `clause[N].local[M]`. Occurrences
are definitions, reads or exact-equality checks, tagged with head/guard/body
context. Analysis is deterministic.

- Each clause starts from an empty environment. Head candidates collect
  tentative definitions; guards read them; the body sees them only on success.
- `_` binds nothing; `_Name` is an ordinary variable. A repeated name is an
  exact-equality constraint.
- Body matches visit RHS before LHS; chains are right-to-left. A parenthesized
  pattern `P1 = P2` is an alias, not a sequence.
- Sibling expressions read the same incoming names; their definitions export
  after the whole expression. `{X = 1, X}` is an unbound read; `{X = 4, X = 3}`
  is legal and fails at run time.
- Definitions on the RHS of `andalso`/`orelse` or inside `catch Expr` are unsafe
  afterwards (OTP `vtunsafe`); names bound before a `catch` stay usable. Every
  name bound inside a `try` is unsafe afterwards; `of` clauses see the body's
  names, catch clauses see them as unsafe, and the after body sees every name
  bound earlier in the try as unsafe. A catch clause's stack variable must be
  new (OTP `stacktrace_bound`) and its guard must not read it
  (`stacktrace_guard`). A `maybe` exports nothing: each `?=` binds for the
  following body expressions, `else` clauses see the body's names as unsafe and
  every name bound inside is unsafe afterwards. Guards never publish bindings;
  matches in guards are errors even when unreachable.
- Map keys read only incoming bindings; binary sizes also read earlier segments
  of the same binary (see [patterns](patterns.md#scopes)).
- A `case` scrutinee binds in the enclosing scope. Each clause starts from that
  scope; its pattern definitions are tentative until the guard, which only
  reads. Names bound by every clause are exported with one identity (later
  clauses reuse the identity an earlier clause gave the name); names bound by
  only some clauses, or unsafe in any, are unsafe afterwards. Exports join
  conservatively like OTP's `erl_lint` (`icrt_export`); OTP's warning when a
  later pattern matches an exported name is not emitted. `if` clauses follow the
  same rules with a guard and no pattern.
- `begin`/`end` is a sequence in the enclosing scope.
- Unbound, unsafe and wildcard reads are located errors. Messages keep the
  compiler's wording (`unbound variable X`, `unsafe variable X`); the bindings
  corpus checks each against OTP's `unbound_var`/`unsafe_var` class.
- A comprehension evaluates each qualifier in order in the scope left by the
  previous ones. Generator patterns bind new names that shadow outer ones (reads
  inside the pattern prefer names it already bound); a zip group binds all its
  patterns together. Templates read like siblings. The scope after the
  comprehension is the one before it: its names are unbound there.
- An anonymous fun's clauses each start from the scope at the fun: head names
  are new and shadow outer ones, guards read them, and nothing bound inside is
  visible after the fun. Every outer definition read inside is recorded as a
  capture (`Function::captures`, definition order). A named fun's name is one
  more definition every clause starts with (`Function::fun_names`); it is never
  captured. The variables of `fun M:F/A` are reads of the fun expression
  (`Function::fun_operands`).

Walks are iterative with a module budget of 1,000,000 work units, or 32 per
expression when that is more (generated parsers such as OTP's `erl_parse` need
about 10 per expression). Service resolution
has a budget of the same size; it scans the module's `-compile` and `-import`
forms for a name's auto-import state once per name, not once per call (each of
`erl_parse`'s thousands of clauses calls `hd/1`). Exhaustion or any semantic
error clears the module's binding and normalization tables.

## Types and specifications

A private type graph represents all parsed type forms independently of runtime
layout: singletons, ranges, containers, map field roles, function products and
unresolved applications. Unions flatten and deduplicate; `term()` is top and
`none()` bottom. Defaults: 1,048,576 nodes, 256 union members, 200,000 work
items per declared type walk (resolution, translation, expansion) and 8,000,000
per whole-program inference pass; a debug build of `clau` (no `NDEBUG`) allows
16,000 and 1,000,000 so large programs still compile quickly, and may widen
more. Exhaustion widens to `term()` with a visible flag and never narrows a
representation.

Declared metadata (`-type`, `-opaque`, `-nominal`, `-export_type`, `-spec`,
`-callback`, `-optional_callbacks`) follows the OTP typespec reference and
pinned `erl_lint`/`erl_internal`/`erl_types` behavior:

- Local aliases may shadow builtin type names. Remote batch types must be
  exported. Missing types, duplicates, malformed metadata, singleton type
  variables, invalid bounds and specs for missing functions are errors.
  Unavailable external types warn and become `term()`.
- Each alias or overload has its own variable scope; repeated formals follow
  OTP's last-argument substitution.
- Recursive aliases stay finite named references; opaque bodies expand only in
  their module; nominal names are kept everywhere, and only the specification
  check reads a nominal type's definition, inside its module.
- Constant evaluation is exact, limited to 10,000 decimal digits per value.

## Inference

Inference is separate from declared types and never trusts specs.

- Exported inputs, and inputs of functions that `fun F/A` names, are arbitrary
  terms. A function entered only by direct calls of the batch (neither exported
  nor named by a fun) takes the join of its call sites' argument facts as inputs
  (`semantic/types/inference_inputs`), matched by each clause's head patterns;
  recursive calls count too. Inputs start at `none()` (a function nothing calls
  never runs and infers `none()`) and grow over passes of the batch: 8 passes
  join, later ones widen like recursive results; inputs that have not settled
  after 16 passes become `term()` (reported as widened).
- Literals have exact facts: integers of any size and characters are singletons,
  atoms singleton atoms, floats `float()`, strings nonempty lists of their
  characters (`[]` for `""`), and `-`/`+` of a literal number keep its fact.
  Tuples keep their elements' facts; maps built with constant keys (a fact of
  one value: an integer, an atom, `[]`, or a tuple or map of such) keep each
  key's value fact, a key given twice keeping its last value, and any other key
  makes `map()`. A bitstring construction counts its size: literal, sized and
  UTF-encoded literal characters exactly, a UTF segment of another value by its
  encoding (UTF-8: 8 to 32 bits by bytes), and a `binary`/`bitstring` segment by
  its value's fact or any multiple of its unit (`<<1, Rest/binary>>` is
  `nonempty_binary()`). An operand that never produces a value (`none()`) makes
  the construction `none()`.
- Operators and builtins (`semantic/types/inference_operators`) compute their
  result from their operands' facts. Integer singletons fold exactly up to 4,096
  bits per operand and result (bignums included; a larger one is `integer()`);
  other integers use interval arithmetic for `+`, `-`, `*` of nonnegative
  ranges, `band` with a nonnegative operand (`X band 15` is `0..15`), `rem` by a
  bounded divisor (`X rem 10` is `-9..9`), `bnot`, unary `-` and `abs/1`; a
  float operand makes `float()`, an unknown one `number()`, and `/` is always
  `float()`. Comparisons fold when the operands' values can only compare one way
  (singleton atoms or integers, disjoint integer ranges, numbers against
  non-numbers) and are `boolean()` otherwise; `and`/`or`/`xor`/`not` and
  `andalso`/`orelse` combine the truth values their operands can have (the right
  operand of `andalso` is the result when the left one is `true`). A table gives
  each bridge builtin its result: `pid()` for `self/0` and `spawn/1,3`,
  `reference()`, `port()`, `non_neg_integer()` for sizes, `string()`,
  `nonempty_string()`, `binary()`, `list()`, `tuple()`, `atom()`, `boolean()`
  for type tests, `true`/`ok` for side-effect builtins, the message for `!` and
  `send/2`; `trunc/1` and friends keep integer operands; unknown builtins are
  `term()`. An operation that always raises (`1 + a`, `1 div 0`, `error/1`,
  `exit/1`, `throw/1`, `halt/0,1`) is `none()`, and `erlang:raise/3` returns
  only `badarg`. Folding never changes the generated code: overflow and failures
  stay runtime outcomes.
- A body whose expression never completes (`none()`) is `none()`; so is a call
  with such an argument. Raising paths add nothing to a join, so a function that
  always raises infers `none()`.
- Identity/projection functions keep exact argument relations, propagated
  through nested local and remote calls with fresh variables per call.
- Containers (`semantic/types/inference_containers`): a list `[E1, ..., En | T]`
  joins its elements in front of the tail's fact (a proper list when the tail is
  one, `nonempty_improper_list(H, T)` when it is no list,
  `nonempty_maybe_improper_list()` when it is unknown or may be improper: a cell
  is never empty, also in a pattern such as `[H | _]`); `++`, `--`, `hd/1`,
  `tl/1`, `element/2`, `setelement/3`, `tuple_to_list/1` and `map_get/2` read
  and rebuild element facts member by member of a union, a member that would
  raise adding nothing. A map update keeps exact keys (`:=` of a missing key
  drops that member) and makes `map()` of an unknown map. Tuple records are
  tuples: construction fills defaults (`undefined` without one), access and
  update read and set the field of the matching tuples, and `#r.f` is the
  field's index. A list comprehension is a possibly empty list of its templates'
  facts, a binary comprehension any number of copies of its template's size, a
  map comprehension `map()`.
- Funs (`semantic/types/inference_funs`): `fun F/A` is a fun of its arity
  returning the function's inferred result (a builtin's, or with a variable
  arity, `fun()`), `fun M:F/A` one returning `term()`, and an anonymous fun one
  returning its clauses' joined results, captured values included. A call of a
  value joins the results of its funs of that arity that its arguments select
  (`term()` for an unknown fun, `none()` when no member can be called so). An
  anonymous fun bound whole to a variable and called through it is evaluated
  again for that call with its patterns matching the arguments' facts, at most 4
  such calls deep, and its first evaluation's facts are restored afterwards
  (`Double = fun(Y) -> Y * 2 end, Double(3)` is 6). `apply(F, Args)` (also
  `erlang:apply/2`) is a call of `F` with the arguments `Args` holds: the
  elements of a literal proper list, else those of its fact (`[]`, a list of
  known positions, or as many copies of a proper list's element as `F`'s single
  arity takes); a bound anonymous fun is evaluated again as above
  (`apply(fun kind/1, [1])` selects `kind(1)`). `apply(M, F, Args)` whose `M`
  and `F` are known atoms naming an exported batch function reads that
  function's function types like `fun F/A`. Arguments of unknown length, other
  targets and `M:F(Args)` with variable names stay `term()`. Since `fun F/A` and
  `apply/3` may name a function inferred later, the passes over the batch also
  repeat until every such read saw its function's final result; otherwise a last
  pass gives them `term()` results.
- Whole-value body assignments and aliases copy the RHS fact
  (`Y = 42, Z = Y, id(Z)` infers 42); tuple, list, map and tuple-record patterns
  give their variables the facts of the parts they match, in body matches,
  `case` clauses (from the scrutinee), a `try`'s `of` clauses (from its body's
  value) and generators (from their input's elements or map keys and values).
  Unproved values stay `term()` without relations.
- Narrowing (`semantic/types/inference_narrowing`, `inference_scopes`, `meet`):
  inside a function, `case`, `receive` or fun clause, each pattern meets the
  value it matches (a literal, tuple, list, tuple-record, map or bitstring
  shape; a bound variable's fact), a `case` scrutinee variable narrows with it,
  and the guard narrows the variables it tests. Type tests (`is_atom/1` to
  `atom()`, `is_boolean/1`, `is_integer/1`, `is_float/1`, `is_number/1`,
  `is_binary/1`, `is_bitstring/1`, `is_list/1` to `maybe_improper_list()`,
  `is_tuple/1`, `is_map/1`, `is_function/1,2`, `is_pid/1`, `is_port/1`,
  `is_reference/1`, `is_record/2,3` to the record's tuple, `is_map_key/2` its
  map to `map()`, and the old guard names) meet their argument's fact;
  comparisons with integer constants (`<`, `=<`, `>`, `>=`, `==`, `=:=`, either
  side, folded constants included) narrow a value already proven to be an
  integer to a range, the bounds of one test accumulating (`10 >= X, X >= 0` is
  `0..10`); two proven integers compared narrow each other by their bounds
  (`X > Y` with `Y` in `0..5` makes `X` at least 1), and `/=`, `=/=` with a
  constant move a range bound equal to it inward. A value that may be a float or
  another term is not narrowed. A conjunction applies each test in turn, a
  disjunction joins what each alternative proves, `not` and false tests prove
  nothing. A clause after one whose patterns are all plain variables and whose
  whole guard was a single type test sees that value without the tested
  category; after a single comparison with an integer constant, the same value
  (a plain variable of this clause) proven to be an integer sees the comparison
  false (`f(N) when N >= 0 -> ...; f(N) when is_integer(N) -> ...`: the second
  clause sees `neg_integer()`). The right operand of `andalso`, the `true`
  clause of `case Test of`, and what follows a comprehension filter see the test
  as true. An empty meet makes the clause impossible: it adds nothing to the
  result. Narrowed facts hold only inside their clause or operand; `catch`, the
  right operand of `orelse` and every clause restore the facts from before them.
  After a `case`, `if`, `receive`, `try` or `maybe`, a variable's fact is the
  join of its facts at the end of each clause that completes, so
  `case X of forever -> ...; N when is_integer(N), N >= 0 -> ... end` leaves `X`
  as `non_neg_integer() | forever`.
- `try` and `maybe`: a `try` is the join of its `of` clauses' values
  (its body's without `of`) and its catch clauses' values; the `after` body adds
  nothing. `of` clauses start from the facts at the end of the body and match
  its value like `case` clauses (an impossible clause adds nothing); catch
  clauses and the `after` body start from the facts before the `try`, a class
  pattern matching `error | exit | throw`. A `maybe` is the join of its body's
  value, its `else` clauses' values and, without `else`, the values its `?=`
  matches can fail on: the matched value's fact without the pattern's shape when
  the pattern matches all of its shape (new variables used once, literal atoms
  and integers, `[]`, tuples of them), else the whole fact. Each `?=` pattern
  meets its value and publishes its variables for the rest of the body; a `?=`
  that can never match stops the body. `else` clauses start from the facts
  before the `maybe` and match the joined failing values like `case` clauses.
  The facts after a `try` join those at the end of its body (without `of`) and
  of each completing clause; after a `maybe`, those at the end of its body, of
  each completing `else` clause and, without `else`, those before it.
- Uses (`semantic/types/inference_uses`): an operation that raises unless an
  operand has a type proves that type for the variable it read, after the
  operation returns: arithmetic and unary `-`/`+` a `number()`, `div`, `rem`,
  bit operators and `bnot` an `integer()`, `and`/`or`/`xor`, `not` and the left
  operand of `andalso`/`orelse` a `boolean()`, `++` and `--` a `list()`, a
  called value a fun of that arity, a variable module or function name an
  `atom()`, a map update `map()`, a tuple-record access or update the record's
  tuple, a binary segment its type (`<<X:8>>`: `integer()`) and its size a
  `non_neg_integer()`, and one row per bridge builtin argument check (`hd/1`,
  `tl/1` `nonempty_maybe_improper_list()`, `length/1` `list()`, `element/2`
  `pos_integer()` and `tuple()`, `map_get/2` and `is_map_key/2` `map()`,
  `atom_to_list/1` `atom()`, ...). The operation keeps its runtime check. Names
  bound to the same value (`Y = X`, a variable `case` pattern on a variable
  scrutinee) narrow together.
- Entry and success domains: each argument's entry domain is the join over the
  possible function clauses of its fact after the head and guard (a plain
  variable's narrowed fact, else the pattern's); its success domain the join
  over the clauses that complete of its fact at their normal return.
  `--print-types` shows the success domain as the inputs
  (`bounded(1..10) -> 1..10`, `inc(X) -> X + 1` as `inc(number()) -> number()`),
  or the entry domain of a function that never returns. A call that returns
  narrows its variable arguments to the callee's domain, except within a
  recursive component still being solved.
- Clause results join conservatively: a projection survives only if every clause
  returns the same argument. A `case` or `if` joins its clause results the same
  way.
- Function types (`semantic/types/function_types`): beside the union summary
  above, a function keeps one function type per possible clause, like the
  overloads of a `-spec`: the arguments' facts after the head and guard (a plain
  variable's narrowed fact, else the pattern's) and the clause's result
  (`none()` for a clause that always raises; impossible clauses add none). A
  clause whose result is a [dependent fact](#dependent-facts) on its arguments
  (a `case` or `if` ending it, or a variable bound to one, nested at any depth)
  splits into one function type per clause of that fact, its parameters' facts
  met with the arguments they name. Function types of equal inputs merge (their
  results join); past 32 the last ones merge into one, inputs and results
  joined. Recursive components iterate them with the results, each round joining
  (then widening) each type's result; a component that does not converge keeps
  only its union summaries. An anonymous fun's fact keeps one function type per
  possible clause (its patterns and guard over any argument), `fun F/A` the
  function types of `F/A`; funs that join keep their function types only when
  they are equal, otherwise they join as one fun of their joined results with
  any inputs (meeting a fun of any inputs, as the use `F(A)` does, keeps them).
  Specialization, domains and specification checks read the union summary.
- Calls select function types: a call of a function with function types reads,
  in order, each type whose inputs every argument fact meets, and stops after an
  exact one (patterns of new variables used once, literal atoms and integers,
  `[]` and tuples of them; no guard or only `true` and type tests of plain
  argument variables; for a branch, an argument as the `case` scrutinee) whose
  inputs hold the arguments: no later clause can be entered. Its result is the
  join of the selected types' results, a result equal to an argument being that
  argument's fact within the type's result; none selected makes the call
  `none()` (it can only raise `function_clause`). Unknown arguments select every
  type, the union; a function without function types (budget, unconverged
  component) uses its union summary. After a call returns, its variable
  arguments narrow to the success domain and to the selected types' joined
  inputs. Calls of fun values select the fun fact's function types the same way,
  and an anonymous fun evaluated again for a call (see Funs) enters, guards and
  leaves its clauses like a `case`, so a clause its arguments cannot match adds
  nothing (`F = fun(1) -> one; (_) -> other end, F(2)` is `other`). Recursive
  components converge on results and function types.
- Calls evaluate their callee again: when a call's argument facts are within the
  callee's inputs and narrower in one, the callee's body is evaluated again with
  them as its inputs, like a bound anonymous fun, and the call's result is the
  selected function types' result met with that evaluation's
  (`two_callers() -> {add_one(10), add_one(20)}` is `{11, 21}`); the call's
  arguments also narrow to that evaluation's success domain. The callee's
  summary, function types and recorded expression facts never change
  (specialization reads only those). Budgets: 4 nested evaluations, callees of
  at most 256 expressions, 4,096 work units per call from a pool of 262,144 per
  pass over the batch (separate from the batch's own budget); recursive
  components are never evaluated again. Past a budget the call keeps the
  selected function types' result.
- A `receive` is the join of its clauses and its `after` body, which a timeout
  of `infinity` never runs.
- A recursive component starts every member's result at `none()` and re-infers
  all members until no result changes; each round joins the new result with the
  previous one (widens it after the first 8 rounds,
  [inference domain](#inference-domain)), and a pending recursive call adds
  nothing to a join. A function that can never return stays `none()`. A
  component that has not converged after 8 rounds plus 4 per member, or whose
  rounds have spent more than an eighth of the pass budget, widens every member
  to `term()` (reported as widened, like budget exhaustion) and a final round
  recomputes the expression facts; earlier rounds' expression facts are
  discarded, so only facts from the final assumptions remain. Such a component
  is remembered: later passes give its members `term()` at once and run only
  that final round, instead of iterating to the cap again. The pass that widens
  every input to `term()` forgets the remembered components, so each gets one
  more solve with those inputs.
- Rounds never shrink a member's summary: its function types join with the
  previous round's type by type (results join, changed inputs join and the type
  is no longer exact), or, when the number of types changed, both rounds' types
  merge. As a safety net each member keeps its last 4 summaries, and one that
  returns to any of them is `term()` without function types for the rest of the
  solve, which lets the other members converge.
- A round walks only members that read a summary changed since their last walk:
  each walk notes the summaries it reads (calls, call results, `fun F/A`), a
  changed summary marks its readers, and a member not marked keeps its
  expression facts. The first round walks every member.
- A shared work budget bounds inference; exhaustion loses precision and falls
  back to generic code, never rejects a program.
- Specifications that contradict inference are diagnosed
  (`semantic/types/contracts`), but never become facts, so the program behaves
  as its code says either way. A declared type becomes the facts it holds (or
  more): built-in types by name (`byte()` is `0..255`, `timeout()` is
  `non_neg_integer() | infinity`, `iodata()` and `iolist()` lists of any shape),
  aliases by their definition, opaque and nominal types by their definition
  inside their module and as any term outside it, remote types through the
  batch, type variables through their `when` bounds (unconstrained: any term),
  maps and records by their category. Only a disjoint pair contradicts, since
  inferred facts may hold more values than the code produces:
  - an argument entry domain sharing no value with the spec is an error;
  - an inferred result sharing none (unless unknown or `none()`, which fits any
    result), or a call whose argument facts every overload rejects, is a warning
    (the call may sit on a path that never runs);
  - `none()`/`no_return()` admits only a function that never returns.

  The diagnostic names the function, the declared type as written and the
  inferred one. `-callback` specifications are not checked. OTP's compiler does
  not check specifications ([differences](differences.md#language-edge-cases)).

### Dependent facts

`semantic/types/dependent`. A `case`, `if` or `try ... of` behaves like a fun of
the variables its clauses narrow, applied to them: its value is a dependent
fact, one function type per possible clause.

- Parameters: the variables bound before the construct that its scrutinee (a
  `try`'s body's last expression) and guards read, in source order (variables
  its clause patterns bind are new in each clause, not parameters); at most 4, a
  later one's narrowing is forgotten. Each possible clause keeps the parameters'
  facts after its pattern and guard and its value (`none()` for a clause that
  never completes); it is exact, like a function type in call selection, when a
  `case` or `try` matches a parameter or a tuple of parameters with an exact
  pattern and guard, or an `if` has an exact guard.
- A scrutinee that is a tuple of variables narrows each variable to its element
  of the matched value, and a tuple pattern's variable is bound to the same
  value as the scrutinee's element at its position
  (`case {X, Y} of {A, b} when is_integer(A) -> ...` narrows `X` and `Y`).
- A `try ... of` depends on its `of` clauses; its catch clauses can follow any
  value of its body, so their joined values join into every clause's value
  (`try X of 1 -> one; _ -> other catch _:_ -> error end` is
  `$try_of_operator(X :: 1) -> error | one; (X :: _) -> error | other`).
- `receive` and `maybe ... else` stay the join of their clauses: their clauses
  match a message or a failed `?=` value that nothing outside them can name, so
  no read could select among them.
- A clause whose value is itself dependent (a nested `case` or `if`, or a read
  of a dependent variable) contributes one function type per clause of it over
  the union of both parameter lists, inputs met. Types that can never be entered
  (an input `none()`, or within an earlier exact type's inputs) are dropped, so
  are parameters whose facts are the same in every type; equal inputs merge and
  past 32 types the last ones merge, as for function types. A construct whose
  remaining types all give the same value, or that narrows no parameter, is its
  plain join.
- Every other consumer reads the erased fact, the join of the clauses' values.
  Facts that leave the walked function (results, function types, fun facts) are
  erased; joins keep a dependence only when both sides have the same one.
- A variable bound to a dependent value keeps it. Reading the variable selects
  the clauses its parameters' current facts enter (in call-selection order, an
  exact clause whose inputs hold them stops) and whose values meet the
  variable's own narrowed fact, and the read's fact is the join of their values
  (`Y = case X of 1 -> one; _ -> other end, case X of 1 -> Y end` reads `one`).
- A variable every clause of a `case` or `if` binds is the join of its facts at
  the end of the clauses that complete, dependent on the construct's parameters
  like its value: `case X of 1 -> Y = 5; _ -> Y = 6 end, Y` infers
  `(1) -> 5; (_) -> 6`. The same join applies after `receive`, `try` and
  `maybe`, without dependence.
- Operators, builtins, tuple, list, map and record constructions, and calls of
  batch functions (their function types selected as in call selection, the
  arguments not narrowed again) with dependent operands are evaluated once per
  combination of the operands' clauses, with those clauses' values in place of
  the operands' facts and their inputs met: reads of one variable take the same
  clause, other operands combine, at most 16 combinations (past that the use is
  the plain join). `R + 1` with `R = case X of a -> 1; b -> 2 end` infers
  `(a) -> 2; (b) -> 3`.
- Narrowing a dependent variable (a pattern, a guard, a use) also narrows its
  parameters to the join of the inputs of the clauses entered whose values meet
  the narrowed fact, and their parameters in turn, 4 levels deep: after
  `R = case X of a -> 1; b -> 2 end`, `case R of 1 -> X end` reads `X` as `a`.
- A function clause's dependent result splits it (see Function types):
  parameters that name an argument, or a name bound to its value, meet its
  input; others are dropped (such a type is exact only if their input is any
  term). `nested_case(X, L)` with `case X of 1 -> case L of spanish -> uno; _ ->
  one end; _ -> other end` infers `(1, spanish) -> uno; (1, _) -> one; (_, _) ->
  other`.
- `--print-types` notes a dependent value as a function type named by its
  construct, each input after its parameter's name:
  `case X of 1 -> one; _ -> other end` ends in
  `end. % $case_of_operator(X :: 1) -> one; (X :: _) -> other`; an `if` prints
  `$if_operator`, a `try ... of` `$try_of_operator`.

### Inference domain

`semantic/types/lattice`. A fact is a set of values a variable or result can
have. Facts join where control flow meets (clauses, branches) and widen between
the rounds of a recursive component; every budget below widens soundly to a
larger set, never rejects a program. Facts print as Erlang types, categories by
their built-in names.

| Fact | Printed | Join | Budget and widening |
| --- | --- | --- | --- |
| Nothing | `none()` | Identity | A function that never returns stays `none()` |
| Anything | `term()` | Absorbs every fact | `dynamic()` and `any()` are `term()` |
| Integers | `42`, `1 \| 3 \| 7` | Union of singletons | More than 8 singletons become their range |
| Integer range | `1..10`, `0..255` | Smallest range holding both | A bound that moved between rounds goes to the next threshold: a lower one to 1, then 0, then unbounded; an upper one to -1, then unbounded |
| Unbounded integers | `pos_integer()` (1 and up), `non_neg_integer()` (0 and up), `neg_integer()` (-1 and down), `integer()` | Smallest interval holding both, printed by its category | — |
| Floats | `float()` | — | — |
| Numbers | `number()` | A range or category of integers joined with `float()` | Singleton integers with `float()` stay `1 \| float()` |
| Atoms | `ok`, `error \| ok`, `boolean()` | Union of singletons; exactly `false` and `true` print `boolean()` | More than 8 singletons become `atom()` |
| Identifiers | `pid()`, `port()`, `reference()` | — | — |
| Tuples | `{ok, 1}`, `tuple()`, `#point{x :: 0, y :: _}` | Tuples of the same size whose first elements are not two different atoms (their tag) join element by element; others stay separate members | More than 16 elements become `tuple()` unless every element is known; more than 8 separate shapes become `tuple()`. A tuple of a visible tuple record's name and size prints as the record |
| Lists | `[]`, `[T]`, `[T, ...]`, `nonempty_improper_list(H, T)`, `[1, a]`, `[a, b \| T]` | Elements join; `[]` with a nonempty list gives a possibly empty one; improper lists join heads and tails. Lists of two or more known elements keep their positions (a Clause notation, the type language has none, elements printed with `\|` in parentheses): positional lists of one length join position by position, otherwise they join as plain lists | A list of `0..1114111` (`char()`) prints `string()` or `nonempty_string()`; a possibly empty list of `_` prints `list()` |
| Maps | `#{}`, `#{a := 1}`, `#{1..17 => a}`, `map()` | Maps with the same keys join value by value; maps of other keys join into one association of their joined keys and values (`=>`: any key may be missing) | More than 16 keys join into one association |
| Funs | `fun((term()) -> 1)`, `fun()` | Funs of one arity join their results; other arities give `fun()` | — |
| Bitstrings | `<<_:16>>`, `<<_:3, _:_*2>>`, `binary()` | The shorter size plus every difference of sizes as a unit | Base and unit 0/8, 8/8, 0/1, 1/1 print `binary()`, `nonempty_binary()`, `bitstring()`, `nonempty_bitstring()` |

- A union keeps one member per joined shape, in Erlang term order of their
  values: numbers, atoms, `reference()`, funs, `port()`, `pid()`, tuples, maps,
  `[]`, lists, bitstrings, then declared named types; more than 8 members become
  `term()`.
- Containers nest at most 4 levels; a fact deeper inside becomes `term()`.
- A union drops an exact tuple member that another exact tuple of its size holds
  (of two that hold each other, the earlier stays), so one set of values has one
  union and recursions over trees settle.
- Recursive components join for 8 rounds (cycles of up to 8 functions converge
  exactly), then widen ([inference](#inference)).
- Narrowing (`Lattice::meet`) is the meet of facts, the values both hold (or
  more, but `none()` only when they share none). An empty meet means the path
  cannot run.

At `-O2` with type specialization, lowering uses these facts to remove checks
([specialization](specialization.md#proofs)); specifications never become
facts.

### Inference cache

Modules in the directories of the
[inference cache](projects.md#inference-cache) may start a compile with the
results an earlier compile kept: summaries, local inputs and expression facts of
their functions. `infer` keeps them fixed in every pass, never walks a component
whose members all reuse them, and walks every other function as usual, reading
the kept summaries like any other.

- **What a module's facts depend on.** Exported functions and functions a
  `fun F/A` names start from `term()`; every other function's inputs come from
  its callers, which are in the same module; a fact names a function of another
  module only through an atom: a remote call, `fun M:F/A`, or `apply/2,3` on an
  atom fact (and reads another module's record layout only through a native
  record it qualifies or imports), and every atom fact comes from an atom the
  module spells or from a fact of a module it reaches. So a module's facts
  depend only on its own code and on the code of the batch modules its atoms
  name, transitively. The key of its kept results covers exactly that: `clau`'s
  identity, the module's fingerprint (its source, every file it includes,
  preprocessing options and a parse transform reply), predefined forms, escript
  and library status, and the same of every batch module its atoms reach. A
  module reached that way which has no fingerprint makes the module uncacheable.
- **Liveness.** Export liveness only removes functions, and a removed caller
  only narrows the inputs of the functions it called: facts computed with more
  live functions still hold. Kept results are reused when they hold every
  function live now (`results do not fit` otherwise); functions left out get
  nothing.
- **Mixing.** Kept and fresh results may meet in one batch: each module's facts
  hold for every run of the program, whatever another module's facts say, so a
  fresh module reading a kept summary (or the reverse) stays sound. A recursive
  component with both kinds of members is walked whole.
- **Precision.** Reused results are the ones of the compile that kept them. A
  fresh module's results may still differ from a compile without the cache: the
  pass budget no longer pays for reused modules, and graph identities, by which
  a few union members are ordered, are numbered differently.

## `--print-types`

Prints each module of the batch (input/target order, library modules after them)
as Erlang source ([source printing](compile.md#source-printing)) with what type
inference found. Output is on stdout and is human-readable, not Erlang and not
an interchange format. Warnings stay on stderr.

```erlang
%% module "branches" source="branches.erl" target="" declared=complete inferred=complete
-module(branches).
-export([mixed/1]).

%% inferred: mixed(1) -> 1; (_) -> 2
mixed(X) ->
    case X of
        1 ->
            1;
        _ ->
            2
    end. % $case_of_operator(X :: 1) -> 1; (X :: _) -> 2
```

- A `%% module` line names the module, its source, the project target and
  whether declared and inferred types completed or were widened by a limit.
- Declarations (`-type`, `-spec`, `-callback`, records) appear as written.
- Above each function, `%% declared: f(Inputs) -> Result` gives each overload of
  its `-spec` as resolved (with its `when` constraints), and
  `%% inferred: f(Inputs) -> Result` what inference found, so the two can be
  compared. The inputs are each argument's success domain (from `term()` for
  exported functions and those `fun F/A` names, from the callers' joined
  arguments for other functions). A function with several function types prints
  one signature per type, each with its own inputs:
  `f(integer()) -> integer(); (atom()) -> string()`
  (`semantic::types::function_source`). A `-spec` stays with the function right
  after it, set apart from other forms by a blank line.
- Each line's outermost expression (a body expression, or a `case`'s scrutinee)
  whose fact says more than `term()` is noted with a trailing `% Type` comment
  after the line's punctuation (a known type hides an argument relation, which
  shows only for a value known as nothing else): at most one note per line, none
  for the expressions nested in it, so the output stays valid Erlang. A body
  expression on several lines carries its note on its last line (`end, % ...`).
  Literal terms (literals, and tuples, lists, constructed maps and bitstrings of
  literals) are not noted; a match shows its value. A dependent value prints
  like a function type of its construct ([dependent facts](#dependent-facts)).
- A value inference proved equal to one of the function's arguments, and known
  as nothing more, prints as that argument's name, a type variable: the variable
  the first clause binding the whole argument gives it, else `_argumentN`
  (1-based, also when an earlier argument took the name). The argument's input
  shows the same name when it is any term: `second(_, Y) -> Y`,
  `keep(Acc, number()) -> Acc`. A variable shows only its type, its name already
  says which argument it is.

### Inference expectations

`tests/fixtures/inference/*.erl` record what inference should find for each
function, and what it finds today. Each module becomes the CTest
`inference_<module>` (`tests/compiler/inference/expectations.py`):

```erlang
%% expect: sum() -> 3
%% today: sum() -> _
sum() -> 1 + 2.
```

- `expect:` is the signature `--print-types` should print in its `%% inferred:`
  line; `today:`, present while inference falls short, is the one it prints now.
- The check compares the output with `today` when there is one, else with
  `expect`; every function of the module needs an `expect` line. A `today` line
  that inference has caught up with fails the check until it is removed.
- `expectations.py <clau> <fixture> --record` rewrites the `today` lines from
  the current output (a maintainer action: review the diff).
- `values.erl` covers literal and constructed values, operator and builtin
  results, containers and their parts, funs and their calls, local inputs from
  callers, integer joins and ranges, argument relations and `try`/`maybe`
  values; `narrowing.erl` type tests, guards, patterns, comparison ranges,
  disjunctions and narrowings that must not leak; `base_types.erl` one function
  per base and built-in type of the
  [type language](https://www.erlang.org/doc/system/typespec.html), whose
  `-spec` must resolve and whose body produces such a value; `clauses.erl`
  function types, call selection and evaluation per call within and past their
  budgets; `dependent.erl` [dependent facts](#dependent-facts); `trees.erl` and
  `parsers.erl` recursions over trees and generated-parser shapes.
- `tests/fixtures/inference/contracts/` holds the specification checks (CTest
  `inference_contracts`).

### Printing types

`semantic::types::type_source(graph, type)` (`semantic/types/printing`) renders
a type of the type graph in Erlang type syntax: `_` for any term (`term()`,
written by `TERM_SOURCE` for brevity; type syntax reads `_` as `any()`),
`none()`, atoms and integers, `1..5`, `{ok, T}`, `tuple()`, `[T]`, `[T, ...]`,
`#{K => V, K := V}`, `#r{f :: T}`, `<<_:B, _:_*U>>`, `fun((A) -> R)`, `A | B`. A
fun of several function types prints them in a Clause notation, `fun((1) -> one;
(_) -> other)`: Erlang type syntax has no overloaded fun type, and a union of
fun types means something else. A union's integers print in value order where
its first integer stands, consecutive ones as a range (`1 | 2 | 3 | 5` prints
`1..3 | 5`; the fact keeps the singletons). Predefined `erlang` types drop their
module; references to declared types stay named. A budget of nodes bounds the
text; past it, and below 32 levels of nesting, `...` stands in.

```sh
clau --print-types answer.erl client.erl
clau --print-types --project project.toml --target demo --verbose
```
