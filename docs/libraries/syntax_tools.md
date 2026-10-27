# syntax_tools

Clause's `syntax_tools` application (plan 21, LIB77E) is under `library/syntax_tools/`, with
[`syntax_tools.app`](../../library/syntax_tools/syntax_tools.app). `clau` itself does not need it.
It exists for third-party parse transforms and tools that call `erl_syntax`, `prettypr` and the
other syntax_tools modules; without them such code fails with `undef`. Parse transforms run in
Clause's [transform host](../transforms.md), which links the whole library, so interpreted
transform code finds these modules there too.

The application is partial: `erl_syntax` (LIB77E2) and `prettypr` (LIB77E3a) are provided. `erl_prettypr` (LIB77E3b),
`erl_syntax_lib`, `epp_dodger`, `erl_comment_scan` and `erl_recomment` (LIB77E4), and `merl` with
`merl_transform` (LIB77E5) follow, as [plan 21](../../.agents/21-lib.md) orders them.

## erl_syntax

[`erl_syntax`](../../library/syntax_tools/erl_syntax.erl) has OTP 29's 303 exports, all
implemented (see the [inventory](../../library/syntax_tools/erl_syntax.terms)). The functions are
runtime builtins in [`runtime/src/builtins/syntax/`](../builtins.md#erl-syntax). The `*_literal`
functions and `atom_name/1` are Erlang over `io_lib`'s writers.

Nodes keep OTP's representation, so `=:=`, printing and pattern matching on them agree with OTP:

- erl_parse forms are nodes as they are.
- Positions, comments and annotations on an erl_parse form wrap it in
  `#wrapper{type, attr, tree}`.
- Constructors build `#tree{type, attr, data}`, with OTP's data records (`#case_expr{argument,
  clauses}`, `#func{name, clauses}`, `#list{prefix, suffix}`, ...).
- Attributes are `#attr{pos, ann, com}`, with `com` `none` or `#com{pre, post}`.

[`erl_syntax.types.hrl`](../../library/syntax_tools/erl_syntax.types.hrl) has the exported types
and these records, as OTP defines them.

Every accessor reads both representations. Errors on malformed nodes are OTP's:
`{badarg, Node}` from `type/1` and `data/1`, `{badrecord, Data}` for a tree of another type,
`{case_clause, Type}` from `subtrees/1` and the list functions, and `function_clause` from
`make_tree/2`. `revert/1` turns trees back into erl_parse forms, leaving the nodes it cannot revert
out of context as trees. `revert_forms/1` raises `{error, Tree}` for such a form.

Differences from OTP are in [differences](../differences.md#erl_syntax).

- CTest `executables_erl_syntax_trees`, compared with OTP:
  - each node of a corpus of forms covering every node type, with its type, leaf, form and literal
    tests and every accessor;
  - the same forms rebuilt with `make_tree/2`, printed and reverted;
  - every constructor;
  - list skeletons, `abstract/1` and `concrete/1`, positions, comments, annotations and
    attributes, form lists, `meta/1` and the errors.
- CTest `transforms_pipeline`: the `pt_tag` parse transform builds its function body with
  `erl_syntax` and runs in the transform host.

## prettypr

[`prettypr`](../../library/syntax_tools/prettypr.erl) has OTP 29's 20 exports, all implemented
(see the [inventory](../../library/syntax_tools/prettypr.terms)). Documents are OTP's records
(`#text{}`, `#nest{}`, `#beside{}`, `#above{}`, `#sep{}`, `#float{}`, `null`), built by the
Erlang constructors; texts keep their width in front of their characters (`[Width | Chars]`,
tabs to the next multiple of 8).

`best/3`, the layout chooser, is a [runtime builtin](../builtins.md#prettypr) that applies OTP's
rewriting rules one for one, so it returns OTP's very layout documents, joined strings included.
`format/1,2,3` lay the chosen layout out as text through `clause_prettypr:layout/1`, and
`text_par/1,2` split their words with `clause_prettypr:words/1`.

Differences from OTP are in [differences](../differences.md#prettypr).

- CTest `executables_prettypr_layout`, compared with OTP: texts, nests, besides, aboves, seps and
  pars at several widths, follows, breaks and empty documents, paragraphs with indentation and
  ribbon widths, a long paragraph, code-shaped documents, every float rule, `best/3`'s layout
  documents and the errors of malformed documents.
