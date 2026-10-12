# Preprocessor

Native C++ reimplementation of OTP `epp` for the pinned `maint-29` source.
Boost.Parser handles character rules; shared compiler metadata supplies
operator precedence. Expanded tokens are never turned back into source text.

`PreprocessorSession` returns expanded forms (including implicit `-file`
attributes), structured diagnostics and immutable per-form feature snapshots;
`features()` exposes final session state for the parser. This is an internal
interface, not a public stage format. `DirectiveReader` is the older syntax-only API.

## Supported behavior

- Object and arity-overloaded macros, raw substitution, nested block/fun
  arguments, stringification with canonical OTP token spelling, redefinition,
  undefinition and dependency-cycle diagnostics.
- Conditional nesting with skipped effects and errors, EOF diagnostics.
- `-include`/`-include_lib`, environment-variable path heads, guarded recursion.
- `?MODULE`, `?BASE_MODULE` (and `_STRING` forms), `?FILE`, `?LINE`,
  `?FUNCTION_NAME`/`?FUNCTION_ARITY`, `?FEATURE_AVAILABLE`/`?FEATURE_ENABLED`,
  `?OTP_RELEASE` (29) and `?MACHINE` (`'BEAM'`, for source compatibility only).
- `-if`/`-elif` guard evaluation with arbitrary integers and the OTP guard BIF
  catalog; no arbitrary calls run.
- OTP 29.1 features: `maybe_expr` (enabled by default, controls `maybe`/`else`)
  and `compr_assign` (experimental, disabled). Initial `all` selects both;
  source `-feature(all, ...)` is invalid, as in epp. Other names are errors.

## Compatibility policies

- Include lookup: current file's directory, working directory, then include
  paths in API order. The CLI reverses repeated `-I` to match `erlc`. Absolute
  paths bypass search. `include_lib` tries ordinary paths, then explicitly
  supplied application directories, then releases' `<app>-<version>`
  directories below each searched directory (versions are dot-separated
  digits, as OTP's code server reads them; the highest wins). Several versions
  of one application warn once per run, naming the directory used; no code
  server.
- `$VAR` path heads use an injected resolver; unset variables stay literal.
  Include operands are not macro-expanded. Files decode as UTF-8 or declared
  Latin-1; host path rules apply regardless of target CPU.
- Includes share macro/feature/module state; each file owns its conditional
  stack and logical file mapping.
- Conditions validate all branches before short-circuit evaluation. Invalid
  syntax is an error; unbound variables, non-boolean results and evaluation
  exceptions select false. Record field/index access in conditions fails; an
  unselected short-circuit record access is valid.
- Record construction in a condition gets a controlled diagnostic
  (`record construction requires record expansion`); OTP 29.1 epp crashes there.
- `-D` initial values are literal terms normalized to tokens, including binaries
  and external funs that `erl_parse:tokens/1` cannot represent.
- `self()` is a stable synthetic pid and `node()` is `nonode@nohost`.
- An LF directly after an include's terminating dot advances the returned
  `-file` line; space, comment and CRLF follow OTP's one-character `scan_dot`.

## Limits and diagnostics

Defaults (configurable through `PreprocessorLimits`): 256 expansion levels and
1,000,000 produced tokens per form, include depth 64, expression nesting 256,
plus fixed bounds of 4,194,240 bits for integers (OTP's limit on 64-bit hosts) and
1,000,000 bits for bitstring values. Exhaustion is a
resource diagnostic, never a silent false condition.

Diagnostics carry a stable category, severity, physical spans, optional logical
coordinates and related macro/include spans. Warnings do not fail; errors latch
failure while recovery continues at the next form boundary.

## Tests

Token goldens (`semantic/*.erl.tokens`) record epp token kinds, decoded values
(floats as binary64 bits) and diagnostic order; wording need not equal OTP.
Local `semantic/headers/*.hrl` fragments replace formerly copied OTP headers.
`preprocessor_workflow` covers real include trees, option order, environment,
Latin-1 and condition cases through the CLI.
