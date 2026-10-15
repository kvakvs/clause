# Deferred-feature reporting

Legal Erlang that needs an unimplemented feature fails with one explicit
message, distinct from ordinary invalid-input errors. Supported generic fallback
(for example skipped specialization) is silent and not a failure.

```text
[pattern matching] notimpl: src/example.erl:12:5 [module="example"] [target="native"] [operation="match argument"]
```

## Catalog

[features.hpp](../abi/include/clause/abi/features.hpp) assigns stable,
never-reused IDs and diagnostic names, with owner and status per entry. Retire
or refine an entry when its feature lands; keep its ID reserved.

| Owner | Where it reports |
| --- | --- |
| Compiler semantic analysis | Capability checks over every function, including unused code |
| Compiler lowering | Defensive rejection at reached operations; clears staged artifacts |
| Driver | Nothing deferred: `executable linking` is implemented ([linking](executables.md#linking)) |
| Runtime services | See [runtime deferred services](runtime.md#deferred-services) |

Deferred compiler features today, each covered by `codegen_placeholders`:

| Feature | Example that reports it |
| --- | --- |
| `heap expressions` | `compr_assign` comprehension assignments (`[Y \|\| X <- L, Y = X]`) |
| `dynamic calls` | A builtin fun of `apply` (`fun erlang:apply/2`) |
| `behavior-changing attributes` | `-on_load`, `-nifs`, non-inert `-compile` options, parameterized modules ([accepted subset](compile.md#accepted-source-subset)) |

## Message rules

- The shared [formatter](../abi/include/clause/abi/feature_diagnostic.hpp) omits
  unknown context, escapes control bytes as `\xHH` and escapes quotes and
  backslashes so context cannot inject lines. UTF-8 is kept.
- Exactly one message per failed operation, independent of `--verbose`.
- A deferred feature the compiler could skip without changing what it accepts
  would report as a warning and compilation would continue; none does today.
  `codegen_placeholders` checks one message per deferred compiler feature.
- Compiler: [reject_feature](../compiler/src/codegen/features.hpp) reports the
  first failure of a batch, latches failure, discards staged output and marks
  the diagnostic `reported` so drivers do not print it again.
- Runtime: [FeatureFailure](../runtime/include/clause/runtime/features.hpp)
  reports once per operation through a borrowed sink (null → stderr). Later
  calls return the latched status. Sink or formatting failures become
  `diagnostic_failure`; exceptions are contained.
- Callers propagate the failure without reporting again; a host should exit
  nonzero and tear down normally.
