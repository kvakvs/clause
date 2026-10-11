# Library modules

Plan 11 step 39 (2026-10-07): a project-owned subset of OTP's `lists` and
`maps` modules, written in Erlang from OTP's documented behavior (not copied
from OTP sources) and compiled with the programs that use it.

## Contents

| Module | Functions |
| --- | --- |
| [`lists`](../library/stdlib/lists.erl) | `append/1,2`, `filter/2`, `foldl/3`, `foldr/3`, `keyfind/3`, `map/2`, `member/2`, `nth/2`, `reverse/1,2`, `seq/2,3`, `sort/1` |
| [`maps`](../library/stdlib/maps.erl) | `find/2`, `fold/3`, `from_list/1`, `get/2`, `keys/1`, `put/3`, `to_list/1`, `values/1` |
| [`file`](../library/stdlib/file.erl) | `open/2`, `read/2`, `write/2`, `read_line/1`, `position/2`, `close/1`, `read_file/1`, `write_file/2`, `delete/1`, `rename/2`, `list_dir/1`, `make_dir/1`, `del_dir/1` over the file driver ([files](ports.md#standard-io-and-files)) |
| [`io`](../library/stdlib/io.erl) | `get_line/1,2`, `get_chars/2,3` (standard input server or an open file); `format/1,2` and `put_chars/1` are runtime builtins |
| [`gen_tcp`](../library/stdlib/gen_tcp.erl) | `listen/2`, `accept/1,2`, `connect/3,4`, `send/2`, `recv/2,3`, `shutdown/2`, `controlling_process/2`, `close/1` over socket ports ([sockets](ports.md#sockets-57f)) |
| [`gen_udp`](../library/stdlib/gen_udp.erl) | `open/1,2`, `send/4`, `recv/2,3`, `controlling_process/2`, `close/1` |
| [`inet`](../library/stdlib/inet.erl) | `port/1`, `sockname/1`, `peername/1`, `setopts/2`, `close/1` |
| [`clause_socket`](../library/stdlib/clause_socket.erl) | Clause's own: the socket driver protocol the three modules above share |
| [`erlang`](../library/stdlib/erlang.erl) | Generated stubs of every OTP 29 `erlang` export; builtins replace theirs, `nif_error/1,2` have bodies, others raise `undef` when called ([erlang module](builtins.md#erlang-module)) |
| [`os`](../library/stdlib/os.erl) | `cmd/1` over a port ([subprocesses](ports.md#subprocesses)); `type/0` and `getenv/1` are runtime builtins |

Results and error reasons match OTP 29, including the error shapes of OTP's
Erlang implementations (`lists:map(F, x)` is `{case_clause, x}`,
`lists:nth(0, L)` `function_clause`, `lists:seq(1, 10, 0)` `badarg`) and of
its BIFs (`lists:member/2`, `lists:keyfind/3`, `lists:reverse/2`, the `maps`
functions: `badarg`, `{badmap, M}`, `{badkey, K}`). `lists:member/2` matches
with `=:=`, `lists:keyfind/3` with `==`. `lists:sort/1` is a stable merge
sort. Stack traces name the library functions, not OTP's.

## How programs get them

- When a batch (positional inputs or one project target) names a module with
  a literal atom — `M:F(...)` with literal `M`, `fun M:F/A`,
  `apply(M, F, Args)` with literal `M`, `-behaviour(M)` or `-import(M, ...)` — that
  no input defines, and no project `source_search_paths` directory holds `M.erl`
  ([projects](projects.md#paths-and-discovery)) but `<library>/M.erl` exists, the compiler parses and compiles that file into
  the batch, then repeats for the modules the added files name. A call
  `M:F(...)` of a catalog builtin (`io:format/2`, `os:type/0`) names no
  module.
- `<library>` is `library/stdlib` of the build tree, found relative to
  `clau` like the runtime archive.
- A module of the batch with the same name replaces the library module.
- Library modules are ordinary batch modules: they link into executables and,
  for object or IR output, produce their own artifacts.
- A module reached only through runtime names (`M:F(...)` with `M` a
  variable) is not added: such a call raises `undef` unless something else
  names the module.
- Library functions are Erlang code, so they will yield like other code once
  the scheduler exists.
