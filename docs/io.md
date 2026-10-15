# Console output (`io`)

`io:format/1,2,3`, `io:fwrite/1,2,3`, `io:put_chars/1,2` and `io:nl/0,1` are
runtime builtins of module `io` in the bridge catalog
([builtins](builtins.md)); the runtime implements OTP's `io_lib_format` and
`io_lib_pretty` rules in C++
([io_format.cpp](../runtime/src/builtins/io_format.cpp),
[io_pretty.cpp](../runtime/src/builtins/io_pretty.cpp)). The input functions
of `io` are Erlang in the [library](library.md#contents).

## Calls

- `io:format(Format)` is `io:format(Format, [])`. Both return `ok`.
  `io:fwrite/1,2,3` are the same as `io:format/1,2,3`; `io:nl/0,1` write a
  newline.
- Devices of `io:format/3`, `io:fwrite/3`, `io:put_chars/2` and `io:nl/1`:
  `standard_io` and `user` are standard output, `standard_error` is standard
  error (standard output is flushed first, so the two keep their order). A
  pid, or an atom registered to one, is an I/O server: the text goes to it as
  UTF-8 through `file:write/2` (an open file of Clause's `file` module), and
  the call returns what that returns. An unregistered atom is `badarg`, a dead
  pid `error:terminated`, the caller itself `error:calling_self`, any other
  term `function_clause`, as OTP.
- `io:put_chars(Chardata)` writes chardata: Unicode characters, UTF-8 binaries
  and nested lists ending in `[]` or a binary. Anything else (an atom, a
  surrogate, invalid UTF-8, a bitstring) raises `badarg`.
- Text is built whole, then written as UTF-8, as OTP's standard output device
  does in `-noshell` mode (encoding `unicode`). An error writes nothing. A
  rejected write is the runtime failure `output_failure` (exit 70).
- Qualified calls, `fun io:format/2`, `apply(io, format, Args)` and
  `M:format(...)` all reach the builtins. A batch module named `io` does not
  replace the builtins (OTP's `io` is sticky,
  [registration](builtins.md#registration)).

## Formats

- `Format` is an atom (its characters), a binary (its bytes as Latin-1
  characters) or a list. A list element that is not a character is chardata
  written as it is, without control sequences, and moves the column by one
  (OTP's quirk).
- Control sequences `~F.P.PadModC`: field width `F` (negative or `-`: left
  adjusted), precision `P`, pad character, `*` taking an integer (or the pad)
  from the arguments, modifiers `t` (Unicode), `l` (no string detection) and `k`
  (ordered maps; Clause always prints maps in key order).
- Supported `C`: `~w`, `~p`, `~s`, `~c`, `~b`, `~B`, `~i`, `~n`, `~~`, with
  OTP's field rules: `~s` truncates or pads to `P` within `F`; `~w` and `~b`
  print `*` characters when the text does not fit `F`; `~c` repeats the
  character; `~b`/`~B` take the base from `P` (2..36); `~n` with `F` writes `F`
  newlines.
- Without `t`, `~s` takes Latin-1 characters only and `~c` writes the low byte
  of its integer; `~w`/`~p` escape atom characters beyond Latin-1 as `\x{...}`.
  With `t`, `~s` reads binaries as UTF-8 (falling back to Latin-1) and `~p`
  writes `<<"..."/utf8>>` for printable UTF-8 binaries.
- `badarg`: an unknown control character, missing or extra arguments, an
  argument of the wrong type, `F` smaller than `P` for `~s`/`~c`, a left
  adjusted `~p` or `~n`, a base outside 2..36, output that is not Unicode.
- Not implemented (`badarg`, see [differences](differences.md#io)): `~e`, `~f`,
  `~g`, `~x`, `~X`, `~+`, `~#`, `~W`, `~P` and the `K` modifier.

## Pretty printing (`~p`)

- Line length is `F` (default 80, 0 writes one line); the first column is `P`,
  else the column the output has reached on its line (tabs to multiples of 8).
- The term becomes OTP's intermediate form: each value with its one-line length;
  strings (flat lists of printable Latin-1 characters, the default
  `io:printable_range()`), printable binaries, atoms and numbers are text.
- What fits on the rest of the line is written whole. Lists and tuples break
  between elements, aligned after the bracket; tagged tuples (`{atom, ...}`)
  align after the tag, or indent by 4 (or 1) when that would pass half the line;
  map pairs and native record fields that do not fit put the value on the next
  line; long binaries wrap between bytes.
- Containers nested more than 256 deep raise `system_limit` (the layout recurses
  on the native stack).
- Formatting runs to completion, however large the term
  ([portions](builtins.md#portions)).
