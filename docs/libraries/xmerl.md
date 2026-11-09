# xmerl

Clause's `xmerl` (plan 21 MIM3) covers what MongooseIM's dependencies call: erlcloud scans AWS responses with
`xmerl_scan:string/1` and reads them with `xmerl_xpath:string/2`, builds requests with `xmerl:export_simple/2,3`, and
jiffy and rabbit_common convert text with `xmerl_ucs`. The library is Erlang, in `library/xmerl/`, with the
[`xmerl.app`](../../library/xmerl/xmerl.app) resource and OTP's header
[`xmerl.hrl`](../../library/xmerl/include/xmerl.hrl) (`-include_lib("xmerl/include/xmerl.hrl")`). Its records and
macros are OTP 29's, observed from a module including it: the record definitions from its debug information, the
macros through `epp`.

Steps: MIM3a (`xmerl.hrl`, `xmerl_ucs`, `xmerl_scan`), MIM3b (`xmerl_xpath`, `xmerl_xs:select/2`) and MIM3c
(`xmerl`'s export with the `xmerl_xml` callback) done.

## xmerl_scan

`string/1,2` scan an element (or, with `{document, true}`, an `#xmlDocument{}`) from a string, as OTP 29 scans it
without a DTD, and return it with the rest of the input after the trailing comments, processing instructions and
whitespace (the rest's bytes as given). A binary is `function_clause`, as in OTP.

- **Nodes:** `#xmlElement{}` with its attributes, `#xmlText{}` (type `text` or `cdata`), `#xmlComment{}` and
  `#xmlPI{}`. Each node's `pos` counts the nodes before it in its parent, `parents` lists `{Name, Pos}` from the
  nearest; the root's `pos` counts the comments and processing instructions before it.
- **Text pieces:** a run of text, a reference with the text after it, or a CDATA section. Until a comment, processing
  instruction or element comes in a parent, its pieces merge into one node, which keeps the first piece's type;
  after one, each piece is a node. After a comment (kept or not) whitespace is passed over. `\r\n` and `\r` become
  `\n`, `&#13;` too.
- **Attributes:** values with `&amp;`, `&lt;`, `&gt;`, `&quot;`, `&apos;` and character references; whitespace
  becomes a space. After a whitespace character reference, whitespace is dropped until other text, and the
  attribute's `normalized` is `true`, as OTP's.
- **Languages and bases:** an element's `language` is its `xml:lang` or its parent's, its attributes take the
  parent's; text and comments have none. `xmlbase` starts as the `xmlbase` option or the current directory. An
  element with attributes caches the base before applying its `xml:base` (`filename:join/2`), and closing any
  element restores the cache: as in OTP, an element after a closed sibling without attributes has the `xmlbase`
  `undefined`.
- **Namespaces:** `namespace` is the parent's `#xmlNamespace{}` with this element's `xmlns` (default, an atom) and
  `xmlns:Prefix` declarations (a redeclared prefix keeps its place); `nsinfo` is `{Prefix, Local}` for a prefixed
  name. With `{namespace_conformant, true}`, `expanded_name` is `{URI, Local}` (`xml` built in) and an undeclared
  prefix is fatal; attributes then carry the element's namespace.
- **Characters:** UTF-8 bytes are decoded unless the `encoding` option or the XML declaration names ISO-8859-1;
  illegal characters are fatal.
- **Options:** `space` (`normalize` collapses each whitespace run to a space and turns whitespace references into
  spaces), `comments`, `quiet`, `xmlbase`, `namespace_conformant`, `document`, `encoding` and `{validation, off}`.
  OTP's other options (hooks, events, accumulators, fetching, rules, entities, ...) raise `{not_implemented,
  {option, Option}}`; unknown ones `function_clause`, as in OTP.
- **Document types:** `<!DOCTYPE ...>` is read, its markup declarations skipped; an `<!ENTITY` is fatal
  (`{error, entities_not_allowed}`, OTP's default). A `SYSTEM` DTD that is missing fails as OTP's fetch does; one
  that exists is not read ([differences](../differences.md)).
- **Errors:** `exit({fatal, {Reason, {file, file_name_unknown}, {line, L}, {col, C}}})` with OTP's reasons, each
  reported unless `quiet` as OTP reports it (`"2626- fatal: ..."`, OTP's source line in front). The line and column
  are where Clause's scan was, which differs from OTP's ([differences](../differences.md)).

`file/1,2` and the state functions are declined.

## xmerl_xpath

`string/2,3` evaluate an XPath 1.0 expression on a scanned element, document, text, attribute, comment or processing
instruction, as OTP 29's xmerl evaluates it (which differs from XPath 1.0 in places). The expression is parsed into
OTP's parse trees (`clause_xmerl_xpath`); a syntax error is OTP's
`{badmatch, {error, {1, xmerl_xpath_parse, ["syntax error before: ", [Token]]}}}`. `string/3` takes the options
`namespace`, `bindings` and `functions` (unused, as by OTP for name tests); another option is `function_clause`.
`xmerl_xs:select/2` is `string/2`.

- **Results:** a location path gives its nodes as records (`/` the `#xmlDocument{}` around the element given, or the
  document given); a function call, literal or number gives an `#xmlObj{}`; other expressions (comparisons, `and`,
  `or`, arithmetic, negation, variables) exit with `{primary_expression, {not_implemented, Tree}}`, as OTP's do.
- **Steps:** each step applies its axis and node test to every context node and joins the results in that order,
  without removing duplicates (`a/following-sibling::*`); its predicates then apply to the whole joined result, so
  `//b[2]` is the second `b` of the document. A number predicate past the last node gives none; before the first, or
  not an integer, it is `function_clause`. Unions remove duplicates and sort the nodes in their records' term order.
- **Axes:** all but `namespace` (empty), in document order; the attribute axis leaves out namespace declarations;
  the ancestors stop below the root. An ancestor step with predicates fails with `{badarg, Name}`, as OTP's.
- **Values:** an element's string value is its own text children; equality compares the first node of a node-set
  (two empty node-sets are equal), numerically against a number; `!=` is the negation of `=`; OTP's relational
  operators all compare as greater-than on Erlang terms (an empty node-set as `[]`). `substring-before` and
  `substring-after` keep the separator, `floor` and `ceiling` give integers, `name/1` of a prefixed element is
  `{URI}local`, `last()` at the top is 0, and `processing-instruction()` without a target matches nothing, as in OTP.
  `id`, `string-length`, `normalize-space`, `sum` and `round`, which fail in OTP's xmerl, fail with
  `{not_implemented, {function, Name}}`; `substring` and unknown functions exit `{not_a_core_function, Name}`.

`string/5`, `axis/3,4`, `eval_path/3`, `write_node/1` and `xmerl_xs`'s transformation functions are declined.

## xmerl export

`xmerl:export/2,3`, `export_content/2`, `export_element/2` (records) and `export_simple/2,3`,
`export_simple_content/2`, `export_simple_element/2` (simple forms) export XML through a callback module and the
modules it inherits from (`'#xml-inheritance#'/0`, `callbacks/1`), as OTP's:

- **Simple forms** become records first: `{Tag, Attributes, Content}`, `{Tag, Content}`, `Tag` and text (a possibly
  deep list), records keeping their content but taking their place's parents and position. Attribute values may be
  strings, binaries, atoms or integers (another value is `badarg`, as is a name that is not an atom); text that is
  not a list is `function_clause`, as is an element name that is not an atom or content that is not a list.
- **Calls:** bottom-up, children first: `'#text#'/1` for text, `'#cdata#'/1` for CDATA, `'#comment#'/1` for
  comments, `Tag/4` where a module of the chain exports it, else `'#element#'/5`, each with the outputs of the
  children; processing instructions are left out. Last `'#root#'/4`, with the export's options as `#xmlAttribute{}`
  of a `'#root#'` element. An unknown callback module is `undef`.
- **`xmerl_xml`:** OTP's nested lists (`["<", Name, Attributes, ">"]`, content, `["</", Name, ">"]`, or
  `["<", Name, Attributes, "/>"]` for no content); text escapes `&`, `<` and `>`, attribute values `&`, `<` and `"`;
  CDATA and comments as written. `'#root#'` puts the `prolog` option first when it is the only option, else
  `<?xml version="1.0"?>`.

`export_element/3` and the `xmerl_html`, `xmerl_sgml`, `xmerl_text` and `xmerl_otpsgml` callbacks are not provided.

## xmerl_ucs

`from_utf8/1` (lists and binaries; surrogates and four-byte code points above `16#10FFFF` accepted, other bad
sequences `exit({ucs, {bad_utf8_character_code}})`), `to_utf8/1` (`if_clause` for surrogates, `16#FFFE` and
`16#FFFF`), `is_unicode/1` and `from_utf16be/1` (`{error, not_utf16be}` reported with OTP's text for a lone surrogate
or odd byte) as OTP's. Its other charset functions are declined.
