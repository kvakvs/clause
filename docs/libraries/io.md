# Console output (`io`)

`io:format/1,2,3`, `io:fwrite/1,2,3`, `io:put_chars/1,2` and `io:nl/0,1` are runtime builtins
registered under module `io` in the [bridge catalog](../builtins.md). Formatting uses the
[`io_lib` engine](../builtins.md#io_lib) to produce and flatten `io_lib:format/2` output. The `~p`
control uses OTP's `io_lib_pretty` layout rules, implemented in
[io_lib/pretty.cpp](../../runtime/src/builtins/io_lib/pretty.cpp). The `io` input functions are
implemented in Erlang in the [library](library.md#contents).

## Calls

- `io:format(Format)` is equivalent to `io:format(Format, [])`. Both return `ok`.
  `io:fwrite/1,2,3` are equivalent to `io:format/1,2,3`; `io:nl/0,1` write a newline.
- `io:format/3`, `io:fwrite/3`, `io:put_chars/2` and `io:nl/1` accept an output device.
  `standard_io` and `user` select standard output. `standard_error` selects standard error;
  standard output is flushed before a standard error write to preserve write order.
- A pid, or an atom registered to a pid, identifies an I/O server. Output is sent through
  `clause_io:put_chars/2` as an I/O protocol `{put_chars, unicode, Utf8}` request. The server
  writes the text using its configured encoding, and the call returns the server's reply.
  An unregistered atom raises `badarg`; a dead pid raises `error:terminated`; the caller's own
  pid raises `error:calling_self`; any other device term raises `function_clause`, matching OTP.
- `io:put_chars(Chardata)` accepts Unicode characters, UTF-8 binaries and nested lists terminated
  by `[]` or a binary. Invalid chardata, including atoms, surrogate code points, invalid UTF-8
  and non-byte-aligned bitstrings, raises `badarg`.
- Output is constructed in full before it is written as UTF-8, matching OTP's standard output
  device with encoding `unicode` in `-noshell` mode. Formatting or validation errors produce no
  output. A rejected write causes the runtime failure `output_failure`, with exit status 70.
- Qualified calls, `fun io:format/2`, `apply(io, format, Args)` and `M:format(...)` resolve to the
  builtins. A compilation batch containing a module named `io` does not replace these builtins;
  OTP's `io` module is sticky. See [registration](../builtins.md#registration).

## Formats

- `Format` accepts an atom interpreted as its characters, a binary interpreted as Latin-1 bytes,
  or a list. A non-character list element is emitted as literal chardata without control-sequence
  interpretation. Such an element advances the tracked column by one, matching OTP.
- Control sequences have the form `~F.P.PadModC`: field width `F`, precision `P`, padding character
  `Pad`, modifiers `Mod` and control character `C`. A negative width or `-` selects left
  adjustment. `*` obtains an integer parameter or padding character from the argument list.
  Supported modifiers are `t` (Unicode), `l` (disable string detection), `k` (ordered maps) and
  `K` (map order supplied as an argument: `ordered` or `reversed`). Comparison functions for `K`
  raise `badarg`. Maps are printed in key order.
- All OTP 29 controls and their field rules are supported:
  `~w ~W ~p ~P ~s ~e ~f ~g ~b ~B ~x ~X ~+ ~# ~c ~i ~n ~~`.
  `~s` truncates or pads to precision `P` within field width `F`. `~w` and integer and float
  controls emit `*` characters when the result exceeds `F`. `~c` repeats a character; integer
  controls use `P` as the base, in the range 2..36; `~n` with width `F` emits `F` newlines.
  Widths are measured according to `io_lib:chars_length/1`.
- Without `t`, `~s` accepts only Latin-1 characters, `~c` emits the low byte of its integer
  argument, and `~w`/`~p` escape atom characters outside Latin-1 as `\x{...}`. With `t`, `~s`
  interprets binaries as UTF-8 with Latin-1 fallback, and `~p` renders printable UTF-8 binaries
  as `<<"..."/utf8>>`.
- Inputs rejected by `io_lib:format/2` raise `badarg`. These include unknown control characters,
  missing or excess arguments, arguments of an incorrect type, `F` smaller than `P`, left
  adjustment for `~p` or `~n`, and integer bases outside 2..36. Output that is not valid Unicode
  also raises `badarg`.

## Pretty printing (`~p`)

- `F` specifies the line length, with a default of 80; 0 disables line breaks. `P` specifies the
  initial column. When `P` is absent, formatting uses the current output column, with tab stops
  at multiples of 8.
- `~P` limits traversal according to its depth argument. The `chars_limit` option of `io:format/3`
  and `io_lib:format/3` limits each control to its allocated share of the character budget.
  Omitted content is represented by `...`, matching OTP.
- Terms are converted to OTP's intermediate representation, which records each value's length
  when rendered on one line. Strings, printable binaries, atoms and numbers are represented as
  text. String detection uses flat lists of printable Latin-1 characters, corresponding to the
  default `io:printable_range()`.
- Content that fits within the remaining line width is emitted without line breaks. Lists and
  tuples break between elements and align after the opening bracket. Tagged tuples (`{atom, ...}`)
  align after the tag, or use indentation of 4 or 1 when tag alignment would exceed half the line
  width. Map pairs and native record fields place the value on the next line when necessary.
  Long binaries wrap between bytes.
- Container nesting deeper than 256 raises `system_limit` because layout traversal uses native
  stack recursion.
- Formatting completes without yielding, regardless of term size. See
  [portions](../builtins.md#portions).
