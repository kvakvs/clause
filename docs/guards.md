# Guards

Guard legality follows the pinned OTP 29 `erl_internal`/`erl_lint` rules; it is
never derived from runtime registration. Every operand is checked, including
unreachable ones. A guard succeeds only when the final value is atom `true`.

## Control flow

- `,` conjoins tests in order. `;` starts a new alternative after `false`, a
  non-boolean or a reached semantic error; when all fail, the clause mismatches.
- Head bindings are readable in every alternative; guards create no bindings.
- Infrastructure errors (OOM, limits, ownership) stop execution and never try
  another alternative.

| Operator | Semantics |
| --- | --- |
| `A andalso B` | `A` must be boolean; `false` → `false`; otherwise returns `B` as any term |
| `A orelse B` | `A` must be boolean; `true` → `true`; otherwise returns `B` |
| `and`, `or`, `xor` | Both operands evaluated in order, both boolean |
| `not A` | Boolean inverse |

`true andalso 7` returns 7 in a body and fails as a final guard test;
`(true andalso 7) =:= 7` succeeds. A reached error inside a nested expression
rejects the whole alternative: `hd([]) orelse true` fails, `hd([]); true`
succeeds. In bodies, bad strict operands raise `badarg` and a bad lazy left
operand raises `{badarg, Value}`, as in OTP.

## Catalog

[guards.tsv](../tests/fixtures/patternmatch/guards.tsv) lists all 81 guard
signatures from the pinned `guard_bif`, `new_type_test`, `old_type_test`,
`arith_op`, `bool_op` and `comp_op` tables, and a test enforces exact set
equality with upstream.
[The audit manifest](../tests/fixtures/patternmatch/generated/guard_catalog/manifest.json)
maps each to resolver, lowering and runtime owner. All 81 are implemented:
`self()` is the calling process's pid, `node()` and `node/1` of a pid,
reference or port are `nonode@nohost` (there is one node; any other `node/1`
argument fails the guard, `badarg` in a body). Native records in guards:
`is_record/1,2,3` and field access as in
[native records](native-records.md#operations).

- Type tests: `is_atom`, `is_integer`, `is_float`, `is_number`, `is_boolean`,
  `is_tuple`, `is_list`, `is_map`, `is_binary`, `is_bitstring`,
  `is_function/1,2` ([funs](funs.md)), `is_pid`, `is_reference`
  ([pids and references](terms.md#pids-and-references)), `is_port`
  ([ports](ports.md#identity)).
- `is_integer(V, Lo, Hi)` validates both bounds as integers first (else
  `badarg`), then tests the inclusive range; reversed bounds give false.
- Comparisons `==`, `/=`, `=:=`, `=/=`, `<`, `=<`, `>`, `>=`; arithmetic and
  bitwise operators; queries and conversions per [terms](terms.md).
- Constructors and map updates in guards use the same rooted services as bodies.
- Record tests: see [terms](terms.md#records).

## Resolution

- Unqualified guard BIFs honor local definitions, imports and global/selective
  `no_auto_import`. `erlang:F(...)` and `erlang:'op'(...)` bypass shadowing but
  only for guard BIFs and operators.
- Legacy tests (`integer/1`, `float/1`, `atom/1`, `record/2`, ...) are valid
  only at the top level of a guard test (parentheses allowed). There `float(X)`
  is `is_float`; nested or qualified `float/1` is conversion. Legacy `record/2`
  ignores suppression of its old name, but a local `is_record/2` blocks it.
- Not allowed in guards: assignment, arbitrary local/remote/dynamic calls, `++`,
  `--`, `!`, funs, comprehensions, control flow, record updates,
  `record_info/2`.

## Oracle limitations

Installed OTP 29.1.1 crashes in SSA conversion for some unrelated modern imports
behind legacy scalar aliases; Clause rejects that unauthorized owner explicitly.
OTP's loader also rejects a guard with a huge literal record arity despite lint
accepting it; that case has semantic evidence only.
