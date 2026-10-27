# syntax_tools

Clause's `syntax_tools` application (plan 21, LIB77E) is under `library/syntax_tools/`, with
[`syntax_tools.app`](../../library/syntax_tools/syntax_tools.app). `clau` itself does not need it.
It exists for third-party parse transforms and tools that call `erl_syntax`, `prettypr` and the
other syntax_tools modules; without them such code fails with `undef`. Parse transforms run in
Clause's [transform host](../transforms.md), which links the whole library, so interpreted
transform code finds these modules there too.

All the application's modules are provided: `erl_syntax` (LIB77E2), `prettypr` (LIB77E3a),
`erl_prettypr` (LIB77E3b), `erl_syntax_lib` (LIB77E4a), `erl_comment_scan` and `erl_recomment`
(LIB77E4b), `epp_dodger` (LIB77E4c), and `merl` with `merl_transform` and
[`merl.hrl`](../../library/syntax_tools/include/merl.hrl) (LIB77E5).

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

## erl_prettypr

[`erl_prettypr`](../../library/syntax_tools/erl_prettypr.erl) has OTP 29's 16 exports, all
implemented (see the [inventory](../../library/syntax_tools/erl_prettypr.terms)). It is Erlang:
hooks are funs that call back into the layout. It builds `prettypr` documents with OTP's text for
every node type, and `prettypr`'s `best/3` builtin chooses the layout. The layout context is OTP's
`#ctxt{}` record, which hooks read and change with the `get_ctxt_*` and `set_ctxt_*` functions.
The options are OTP's: `paper`, `ribbon`, `hook`, `user`, `encoding` and `empty_lines`.

Error and warning markers `{Line, Module, Descriptor}` are formatted by `Module:format_error/1`.
A program has only the modules it links, so a marker of a module the program does not otherwise
call prints as the term ([differences](../differences.md#erl_prettypr)).

- CTest `executables_erl_prettypr_format`, compared with OTP:
  - a corpus of forms with every node type, at the default, narrow and wide widths, and as trees;
  - comments;
  - hooks reading and changing their context;
  - the `empty_lines` and `encoding` options;
  - long strings at several ribbons and floats;
  - nodes erl_parse has no form for, and markers;
  - `best/1,2` and `layout/1,2` documents.

## erl_syntax_lib

[`erl_syntax_lib`](../../library/syntax_tools/erl_syntax_lib.erl) has OTP 29's 40 exports, all
implemented (see the [inventory](../../library/syntax_tools/erl_syntax_lib.terms)). The folds and
maps (`map/2`, `fold/3`, `mapfold/3` and their `_subtrees` and list forms) take funs and are
Erlang; they visit children before their node, groups and subtrees left to right, as OTP does.
`new_variable_name/1,2` and `new_variable_names/2,3` are Erlang over `rand`: a process without a
`rand` state is seeded as OTP seeds it, so the names are OTP's. `to_comment/1,2,3` format with
`erl_prettypr` (or the given fun) and split the text into comment lines in Erlang.

The analyses (`analyze_forms/1` and the other `analyze_*` functions), `annotate_bindings/1,2`,
`is_fail_expr/1`, `function_name_expansions/1`, `variables/1`, `limit/2,3` and
`strip_comments/1` are [runtime builtins](../builtins.md#erl-syntax-lib). The analyses throw
`syntax_error` for nodes they cannot analyse, as OTP's do. `variables/1` returns a set of
`sets:new/0`'s kind (a map).

Differences from OTP are in [differences](../differences.md#erl_syntax_lib).

- CTest `executables_syntax_tools_lib`, compared with OTP:
  - `analyze_forms/1` of forms, form lists and trees, `analyze_form/1` and each attribute analysis
    of every form, preprocessor directives, parameterized modules and imports;
  - the expression analyses: applications, implicit funs, record expressions, type applications,
    function and type names of every shape, `is_fail_expr/1`, `function_name_expansions/1`;
  - the errors of nodes the analyses do not accept;
  - binding annotations of every function of a corpus with each expression and pattern kind, and
    of annotated nodes and macros;
  - `variables/1`, fresh variable names with the default and an own name function;
  - `limit/2,3` at several depths, `strip_comments/1`, `to_comment/1,2,3`;
  - the folds and maps with their visiting order.

## erl_comment_scan

[`erl_comment_scan`](../../library/syntax_tools/erl_comment_scan.erl) has OTP 29's 4 exports, all
implemented (see the [inventory](../../library/syntax_tools/erl_comment_scan.terms)).
`scan_lines/1`, `join_lines/1` and `string/1` are [runtime builtins](../builtins.md#comments):
comment lines `{Line, Column, Indentation, Text}` after OTP's scanner, which skips strings,
quoted atoms and character literals and counts tabs to the next multiple of 8, and comments of
consecutive lines alone in one column joined. `file/1` reads the file in Erlang: in its declared
encoding, else UTF-8 and then Latin-1, with OTP's error reports and exits.

## erl_recomment

[`erl_recomment`](../../library/syntax_tools/erl_recomment.erl) has OTP 29's 3 exports, all
implemented (see the [inventory](../../library/syntax_tools/erl_recomment.terms)), as
[runtime builtins](../builtins.md#comments). `recomment_forms/2` puts each comment before the
first form it does not follow: standalone when a blank line separates them or it starts in
column 1 over the form's first line, else into the form. `quick_recomment_forms/2` leaves
comments inside forms out, and `recomment_tree/2` returns the comments below the tree. Forms
after a `-file` attribute that leaves the file get no comments. Rebuilt nodes are `erl_syntax`
trees with their original attributes, as OTP's.

- CTest `executables_syntax_tools_comments`, compared with OTP:
  - comment lines and comments of a source text with strings, quoted atoms, character literals,
    escapes, tabs, CR, LF and CR LF line breaks, trailing white space and Unicode white space;
  - texts that end inside a string, a comment or a character, consecutive and indented lines,
    `join_lines/1` of given lines and the errors of malformed input;
  - `file/1` of UTF-8, declared Latin-1 and invalid UTF-8 files;
  - `recomment_forms/2` and `quick_recomment_forms/2` printed with `erl_prettypr`, nested form
    lists, the comments of each form, `recomment_tree/2` with its leftovers, a tree that is not a
    form list, `-file` attributes, comments alone and the errors of a bad position or comment.

## epp_dodger

[`epp_dodger`](../../library/syntax_tools/epp_dodger.erl) has OTP 29's 16 exports, all implemented
(see the [inventory](../../library/syntax_tools/epp_dodger.terms)). Reading forms with
`io:scan_erl_form/4` (with the reserved words of the `feature` options), parsing them with
`erl_parse`, the fix of a `-define` whose body does not parse (its body kept as text), the
`no_fail` and `clever` options, `-error` and `-warning` markers, the Latin-1 retry of files that
are not UTF-8, `tokens_to_string/1` and `format_error/1` are Erlang. The rewriting of directives
and macros in the tokens of a form and back in the parsed form are
[runtime builtins](../builtins.md#epp-dodger) of the internal module
[`clause_epp_dodger`](../../library/syntax_tools/clause_epp_dodger.erl).

- CTest `executables_epp_dodger_forms`, compared with OTP:
  - `parse_file/1,2` and `quick_parse_file/1,2` of a file with every directive, `-error` and
    `-warning`, macros with and without arguments at the start of forms and functions, after `:`,
    in `try` and `case` clause patterns and around strings, a `-define` whose body does not parse,
    unclosed macro arguments and syntax errors, with the `clever` and `no_fail` options, as text and
    as terms; missing files;
  - `parse/1,2,3`, `quick_parse/1,2,3`, `parse_form/2,3` and `quick_parse_form/2,3` on devices;
  - a Latin-1 file parsed again as Latin-1, and the error of one that declares UTF-8;
  - `tokens_to_string/1` and `format_error/1`.

## merl

[`merl`](../../library/syntax_tools/merl.erl) has OTP 29's 21 exports, all implemented (see the
[inventory](../../library/syntax_tools/merl.terms)). Quoting (`quote/1,2`, `qquote/2,3`: forms
when the text has dots, else expressions, `try` handlers, fun clauses or case clauses, the first
that parses, with the text's comments attached by line), `print/1`, `show/1`, `switch/2` (its
guards and actions are funs), `var/1`, `term/1`, `compile/1,2` and `compile_and_load/1,2` are
Erlang. `compile` compiles to Clause's interpreter images ([compile](../builtins.md#compile)),
which `compile_and_load` loads. The templates (`template/1`, `tree/1`, `subst/2`, `tsubst/2`,
`alpha/2`, `template_vars/1`, `meta_template/1`) and `match/2` are
[runtime builtins](../builtins.md#merl).

[`merl.hrl`](../../library/syntax_tools/include/merl.hrl) defines `?Q/1,2` and selects
`merl_transform` unless `MERL_NO_TRANSFORM` is defined, as OTP's header does.

## merl_transform

[`merl_transform`](../../library/syntax_tools/merl_transform.erl) is merl.hrl's parse transform, run
in the [transform host](../transforms.md#transform-host) like the other library transforms. It
rewrites `?Q(Text) = Expr` matches and `case` clauses of `?Q` patterns into `merl:match/2` and
`merl:switch/2` calls with their templates built at compile time, and evaluates merl calls whose
arguments are literals. It is Erlang over `merl`, as OTP's.

- CTest `executables_merl_templates`, compared with OTP:
  - quoting expressions, forms, clauses of every kind, comments, lines and binaries; parse errors;
  - templates of every metavariable kind with lifting and globs, their variables, trees and meta
    templates; substitution, renaming and the errors of bad metavariables and environments;
  - matching with variables, globs, anonymous variables and lists, arity and glob errors;
  - `switch/2` with guards, actions, defaults and its errors; `print/1` and `show/1`;
  - `compile_and_load/1` of quoted forms and a call of the loaded module, `compile/1`;
  - the program's own `?Q` matches, quasi-quotes, Erlang-variable metavariables and `case`
    clauses, rewritten by `merl_transform`.
