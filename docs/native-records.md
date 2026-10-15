# Native records

The OTP 29 record forms beyond ordinary tuple records: their representation,
operations and errors. Tuple records are in [terms](terms.md#records).

OTP 29 marks native records experimental. Facts below come from the pinned
`maint-29` sources (`erl_lint`, `erl_expand_records`, `v3_core`,
`beam_core_to_ssa`, `erts/emulator/beam/erl_record.c`, `utils.c`,
`erl_printf_term.c`) and live probes on OTP 29.1.1.

## Scope

All three forms beyond tuple records are implemented and share one
representation:

| Form | Syntax |
| --- | --- |
| Native record, local | `-record #r{...}.`, `#r{...}`, `X#r.f`, `X#r{...}`, patterns, `is_record/1,2,3` |
| Qualified and imported | `#m:r{...}`, `X#m:r.f`, `X#m:r{...}`, patterns; `-export_record`, `-import_record` |
| Inferred (anonymous) | `X#_.f`, `X#_{...}`, pattern `#_{...}` |

Not provided: the `records` reflection module (calls to `records:*` are
`unknown module` diagnostics), `term_to_binary` of native records
(`error:notsup`, [external term format](builtins.md#external-term-format)) and
hot code upgrade.

## Representation

- **Descriptor.** Each native record definition compiles to one immutable
  `abi::v1::RecordDescriptor` beside its module descriptor: the defining
  `ModuleDescriptor`, atom slots for the module and record names, the export
  flag (`-export_record` at compile time) and the field-name atom slots in
  definition order. Atom slots resolve through the module's registered atoms, as
  `FrameDescriptor` names do. Programs are static (no unloading), so a
  descriptor lives as long as the program; another module of the batch refers to
  it by symbol. Defaults are compiled into each construction site, never stored.
- **Instance.** Heap cell of kind `native_record`: header (count `1 + n`), an
  untraced descriptor word, then `n` traced values in definition order. A record
  with no fields is a two-word cell.
- The descriptor captures module, name, export flag and field list at
  construction, as OTP's captured definition does. Every operation after
  construction reads the instance's descriptor, never the code that runs it.
- Copying, collection, walking and verification treat the cell as a tuple whose
  first payload word is untraced. Admission requires a descriptor registered by
  a module of the same runtime.

## Operations

`M` is the current module. All errors have class `error`; badrecord payloads are
the offending value except failed external construction.

| Operation | Accepts | Failure |
| --- | --- | --- |
| Local construction `#r{...}` | Always (definition is local) | Unknown field or missing value are compile errors |
| External construction `#m:r{...}` (also imported `#r{...}`, also `#M:r`) | `m:r` defined in the batch and exported | `{badrecord, {m, r}}`; then `{badfield, {{m, r}, F}}`, `{novalue, {{m, r}, F}}` (badfield wins) |
| Local access `X#r.f` | Native record named `r` (module and export flag **not** checked, as OTP's runtime) | `{badrecord, X}`; field missing `{badfield, {{Mod, r}, f}}` |
| External access `X#m:r.f` | Module `m`, name `r`, exported | `{badrecord, X}`; `{badfield, ...}` |
| Anonymous access `X#_.f` | Any native record (export **not** checked, as OTP's runtime) | `{badrecord, X}`; `{badfield, ...}` |
| Local update `X#r{...}` | Module `M`, name `r` | `{badrecord, X}`; unknown field `{badfield, ...}` |
| External update `X#m:r{...}` | Exported, module `m`, name `r` | same |
| Anonymous update `X#_{...}` | Exported or module `M` | same |
| Local pattern `#r{...}` | Module `M`, name `r`, every listed field present | no match |
| External pattern `#m:r{...}` | Module `m`, name `r`; exported when a field is listed | no match |
| Anonymous pattern `#_{...}` | Any native record; exported or module `M` when a field is listed | no match |

- Construction evaluates the given fields in source order (defaults are
  literals); an update evaluates the record first, then the new values in source
  order, unlike a tuple-record update. Empty updates still check.
- Guards: only field access (a failure fails the guard) and `is_record`.
  Construction in a guard is `creating a record in a guard is only supported for
  tuple records`; update is `illegal guard expression`.
- `is_record(X)` is true only for native records. `is_record(X, r)` with a local
  native `r` tests module `M` and name `r`; with `r` imported from `m` it tests
  module `m`. `is_record(X, m, r)` with atoms tests module and name; with an
  integer it is the tuple test.
- Native records are not tuples: `is_tuple` is false; element and size services
  reject them.

## Compile-time rules

Messages follow `erl_lint`:

- Defaults must be literal after constant folding (numbers, atoms, strings,
  `[]`, conses/tuples/maps of those, string-only binaries):
  `illegal default value for field a in native record r`.
- Local construction without a value for a field without default:
  `field a is not initialized in native record r`; unknown field:
  `field zz undefined in record r`. Unknown fields in access, update and
  patterns are accepted (OTP only warns); they fail at run time.
- `#r.a` index, `record_info/2`, `_ = V` initializers and typed field
  refinements are tuple-record only, with OTP's messages.
- A name is either a tuple record, a native record or an imported record;
  conflicts and bad `-export_record`/`-import_record` forms are errors.
  `-export_record` must precede function definitions.
- External forms naming a module outside the batch, an undefined or a
  non-exported record compile and fail at run time like OTP. External
  construction is resolved against the batch at compile time: the importing
  module lowers the defining module's literal defaults itself and references its
  exported descriptor; unknown fields raise `badfield` before a missing value
  raises `novalue` (`ErrorReason` 21).

## Printing and order

- `erlang:display/1` prints `#m:r{a=1,b=2}`: no spaces, fields in definition
  order (OTP: atom-table index order, [differences](differences.md#maps)).
- Term order: tuple < native record < map. Records compare by module, name,
  export flag (false first), field count, field names in definition order, then
  values in definition order. `=:=` additionally requires the same field order;
  `==` compares values numerically (OTP's compiler folds some `==` on records to
  `=:=`, [differences](differences.md#maps); the runtime rule is followed).

## Differences kept

Display field order, `==` folding, external construction of unloaded modules
and missing record warnings are recorded in
[differences](differences.md#maps).
