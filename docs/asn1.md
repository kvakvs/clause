# ASN.1 specifications

Clau compiles ASN.1 specifications (`.asn`, `.asn1`, and `.set.asn` lists of modules) into Erlang modules as OTP's
`asn1ct` does: `Module.erl` with `encode/2`, `decode/2` and the module's values, and `Module.hrl` with its records and
macros. The generated module joins the batch like any other source; its encoding and decoding run in the C++ BER/DER
engine of `clause_runtime` (no OpenSSL needed). OTP's `public_key` specifications will be compiled the same way
(plan 21, LIB80).

## Inputs

Positional inputs ending in `.asn` or `.asn1` are specifications:

```sh
clau --asn1-option der --asn1-outdir gen Kitchen.asn main.erl -o app
clau --generate-asn1 --asn1-outdir gen Kitchen.asn   # only write gen/Kitchen.erl and gen/Kitchen.hrl
```

| Option | Meaning |
| --- | --- |
| `--asn1-option <option>` | An `asn1ct` option for all specifications of the invocation; repeatable. Default `ber`. |
| `--asn1-outdir <dir>` | Where `Module.erl` and `Module.hrl` are written (asn1ct's `outdir`); default the current directory. The directory joins the include path, so `-include("Module.hrl").` works. |
| `--generate-asn1` | Write the generated modules and headers only; compile nothing. |
| `-I <dir>` | Also searched for imported ASN.1 modules (asn1ct's `i`), after the specification's own directory. |

A project target lists specifications in `[[targets.asn1]]` groups, each compiled with its own options:

```toml
[[targets]]
name = "app"
sources = ["src/*.erl"]

[[targets.asn1]]
sources = ["asn1/*.asn"]
options = ["per", "noobj"]
outdir = "build/gen"   # default: asn1/ in the target's build directory
```

`sources` (required) takes files and wildcard patterns as target sources do, matching `.asn` and `.asn1`; an unmatched
pattern is an error. Imports are searched in the directories of the group's specifications. A generated module already
listed among the target's sources is not added twice. `outdir` joins the target's include path.

A `.set.asn` file lists one module file per line (asn1ct's merged compilation); each name is tried as written, then
with `.asn1`, `.asn` and `.py` appended. The merged module takes the set file's name.

## Options

| Option | Effect |
| --- | --- |
| `ber` | Basic Encoding Rules (the default). |
| `der` | Distinguished Encoding Rules: `ber` with sorted `SET`/`SET OF`, omitted `DEFAULT` values, and DER checks. |
| `maps` | `SEQUENCE`/`SET` values are maps; absent components are left out. |
| `undec_rest` | `decode/2` returns `{ok, Value, Rest}` with the bytes after the value. |
| `no_ok_wrapper` | `encode/2` and `decode/2` return the value itself and raise on errors. |
| `legacy_erlang_types` | `BIT STRING` and `OCTET STRING` values as lists, `{Unused, Binary}` and integers accepted. |
| `legacy_bit_string`, `compact_bit_string` | Old `BIT STRING` forms (lists of 0 and 1, `{Unused, Binary}`); imply `legacy_erlang_types`. |
| `noobj`, `asn1config`, `verbose`, `warnings`, `errors`, `deterministic` | Accepted, change nothing. |
| `per`, `uper` | Rejected: `PER/UPER encoding is not supported yet` (plan 21, LIB84). |
| `jer` | Rejected: `jer (JSON encoding rules) is not supported`. |

Any other option is `unknown ASN.1 option 'Name'` (exit 2).

## Coverage

The constructs of X.680/X.681/X.682/X.683 that OTP's PKIX and PKCS modules and emqx's specifications use:

- Types: `BOOLEAN`, `INTEGER` with named numbers, `ENUMERATED` with extensions, `NULL`, `BIT STRING` with named bits,
  `OCTET STRING`, `OBJECT IDENTIFIER`, `RELATIVE-OID`, `REAL`, the restricted character strings, `UTCTime`,
  `GeneralizedTime`, `ObjectDescriptor`, `SEQUENCE`, `SET`, `SEQUENCE OF`, `SET OF`, `CHOICE`, `ANY`,
  `ANY DEFINED BY`, `INSTANCE OF`, open types, selection types (`name < Type`).
- Tagging: explicit, implicit and automatic tags, module defaults, `COMPONENTS OF`, extension markers, extension
  addition groups and the components after the extensions.
- Constraints: single values, ranges, `SIZE`, `FROM`, unions and intersections (evaluated for the bounds BER checks);
  `WITH COMPONENT(S)`, `CONTAINING`, `PATTERN`, `CONSTRAINED BY` and `INCLUDES` are parsed and not checked, as by
  asn1ct for BER.
- Information objects: classes with `WITH SYNTAX`, `TYPE-IDENTIFIER`, `ABSTRACT-SYNTAX`, objects, object sets
  (unions, extensible sets, sets from other sets and object fields), simple and component relation table constraints
  (`{Set}{@a.b}`, decoded and encoded through the object's type).
- Parameterized types, objects and object sets; imports between modules; `DEFAULT` values of every type.

Not supported, each an error at its position: `EXTERNAL`, `EMBEDDED PDV` and `CHARACTER STRING` (`unsupported ASN.1
type`), parameterized values, objects extracted from object sets, `MACRO` definitions, and value notations OTP's
specifications do not use (`unsupported value notation`).

## Generated modules

`Module.erl` exports what asn1ct's does for BER: `encoding_rule/0`, `maps/0`, `bit_string_format/0`,
`legacy_erlang_types/0`, `encode/2`, `decode/2`, `info/0` and one function per value assignment. Records and their
names follow asn1ct (inner types named by their path, `SEQOF`/`SETOF` for collection elements, parameterized
instances), so `Module.hrl` is identical to asn1ct's apart from the first comment line; values are printed as asn1ct
prints them.

`encode/2` and `decode/2` return `{ok, Value}` or `{error, {asn1, Reason}}` with asn1ct's reasons (`{bad_range, ...}`,
`{unexpected, ...}`, `{invalid_choice_type, ...}`, `{'Type not compatible with table constraint', ...}`, the TLV
errors of `asn1rt_nif`); decoding keeps asn1ct's leniency and quirks (unknown extension components skipped,
`{asn1_OPENTYPE, Bytes}` and `{asn1_ExtAlt, Bytes}` for unknown open type and `CHOICE` values, `{asn1_enum, N}`).

The generated functions call `clause_asn1:encode/3` and `clause_asn1:decode/3` with a schema term: `{asn1_schema, 1,
Rule, Flags, #{Type => Node}, Nodes}`. Each node is `{Kind, Tags, Fields...}` (kinds in the order of the compiler's
`NodeKind`, tags as `Number bsl 2 bor Class`); the format is internal to Clause and may change.

## Diagnostics

Errors carry the specification's path, line and column, and stop the invocation (exit 1) before Erlang compilation:

```text
syntax.asn:4:1: error: expected '::=' near 'END'
undefined.asn:3:22: error: undefined reference Missing
import.asn:4:9: error: cannot find ASN.1 module Absent
empty.asn:1:1: error: no ASN.1 module found
```

Differences from asn1ct are listed in [differences.md](differences.md#asn1).
