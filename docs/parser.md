# Parser

Parses the full OTP 29 grammar from preprocessed tokens into an owned AST.

## CLI

`clau --parse-check -I include -DDEBUG module.erl` preprocesses and parses,
writing diagnostics to stderr and nothing to stdout on success. All
preprocessing options apply. `--print-pp` and `--print-ast` print expanded
source or the tree from the same pass. Output-path options are usage errors in
check/print modes. Inputs are isolated; any failure keeps a failing exit. `--`
allows paths starting with `-`. Exit codes: 0 success (warnings allowed), 1
source/filesystem failure, 2 usage error.

AST text uses parenthesized objects, two-space indent and `name=value` fields:

```lisp
(Function name=i arity=0 clauses=1
  clause[0]=(FunctionClause arguments=0 guard=none body=1
    body[0]=(IntegerLiteral value=42)
  )
)
```

The printer is iterative; past 64 levels it caps indentation and adds
`[depth=N]` labels. AST text is for inspection only; there is no reader.

## API

Header `clause/compiler/parser.hpp`, CMake target `clause_frontend`:

```cpp
clause::ParseResult load(const std::filesystem::path &path) {
    clause::SourceManager sources;
    clause::PreprocessorSession pp(sources.read(path));
    return clause::parse_module(pp);
}
auto result = load("module.erl"); // Sources and session are gone; AST owns its data.
for (const auto &id : result.module.forms()) {
    const auto &origin = result.module.anchor(result.module.form(id).source);
}
```

- `ast::Module` is move-only and owns forms, flat node arenas, source buffers
  and feature snapshots. Moves preserve handles.
- Distinct `ExprId`, `PatternSyntaxId`, `TermId`, `TypeId`, `FormId` handles;
  owner/generation checks reject foreign or stale handles.
- `module.visit(id, visitor)` dispatches a closed variant. Traverse deep trees
  with explicit stacks.
- `NodeSource` is an expanded-token range plus anchor; use `extent()`/`anchor()`
  rather than assuming contiguous physical spelling.
- Streaming: `ParserSession::consume(event)`,
  `parse_form(tokens, eof, features)`, then
  `std::move(session).finish(features)`. Always check `ParseResult::failed`;
  recovered forms do not imply success.
- `tests/compiler/parser/consumer.cpp` is a compiled ownership example.

Grammar coverage: attributes and literal terms, tuple/native records,
type/opaque/nominal declarations, specs/callbacks, all expressions, patterns,
guards, clauses, binaries, fun/try/maybe/receive, and list/map/binary
comprehensions with OTP 29 strict generators and zip groups. SSA test
annotations are excluded. Syntax success implies nothing about lint, binding or
code generation.

## Recovery and limits

Failed forms roll back arenas and origins; later forms remain available but
failure is sticky. Diagnostics carry category, logical coordinates,
macro/include trace, nearest unmatched opener and `Diagnostic::expected`
terminals.

Defaults: 1,000,000 tokens/form, 4,000,000 tokens/module, 1,000,000 nodes, 1,000
diagnostics (+1 exhaustion message), nesting 256 (API hard ceiling 512),
16,000,000 work units. The tree printer defaults to 4,000,000 visited objects
and throws `std::length_error` on exhaustion. These are accounting limits, not
wall-clock guarantees. Resource exhaustion stops the session.

## Compatibility evidence

- All 344 ordinary `erl_parse.yrl` productions have measured fixture witnesses;
  79 SSA annotation productions are excluded (`tests/fixtures/parser/phase6/`).
- 43 positive fixtures compare structurally with OTP; 183 rejection fixtures,
  three OTP builder-exception fixtures (`record_helper`, `record_extra`,
  `any_first`) and `bad.erl` cover errors.
- Real-source corpus (opt-in audit): stdlib `lists`, `maps`, `sets`, `erl_scan`;
  compiler `beam_ssa`, `beam_asm` and four headers, using `-I`/`--app-dir`
  mappings, `maybe_expr` on, `compr_assign` off and
  `-DCOMPILER_VSN='"parser-compatibility"'`.
- `parser_stress` (12,000-operator chain), `parser_mutations_0`..`3` (shards of
  900 seeded mutations, seed `0x29a016`, run twice) and `parser_hardening` bound
  behavior.
