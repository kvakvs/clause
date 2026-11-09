# xmerl

Clause's `xmerl` (plan 21 MIM3) covers what MongooseIM's dependencies call: erlcloud scans AWS responses with
`xmerl_scan:string/1` and reads them with `xmerl_xpath:string/2`, builds requests with `xmerl:export_simple/2,3`, and
jiffy and rabbit_common convert text with `xmerl_ucs`. The library is Erlang, in `library/xmerl/`, with the
[`xmerl.app`](../../library/xmerl/xmerl.app) resource and OTP's header
[`xmerl.hrl`](../../library/xmerl/include/xmerl.hrl) (`-include_lib("xmerl/include/xmerl.hrl")`). Its records and
macros are OTP 29's, observed from a module including it: the record definitions from its debug information, the
macros through `epp`.

Steps: MIM3a (`xmerl.hrl`, `xmerl_ucs`, `xmerl_scan`) done; MIM3b `xmerl_xpath` and `xmerl_xs:select/2`, MIM3c
`xmerl:export_simple/2,3` with the `xmerl_xml` callback to come.

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

## xmerl_ucs

`from_utf8/1` (lists and binaries; surrogates and four-byte code points above `16#10FFFF` accepted, other bad
sequences `exit({ucs, {bad_utf8_character_code}})`), `to_utf8/1` (`if_clause` for surrogates, `16#FFFE` and
`16#FFFF`), `is_unicode/1` and `from_utf16be/1` (`{error, not_utf16be}` reported with OTP's text for a lone surrogate
or odd byte) as OTP's. Its other charset functions are declined.
