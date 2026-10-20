# Debugging executables

`-g` emits Erlang source line tables. They work at every optimization level and
with `--emit`, positional `-o` linking and project targets.

## Line tables

- Each generated function and anonymous fun gets a debug scope named by its
  Erlang name (`leaf`, `-main/1-fun-0-`), in the file that declares it, so a
  function from an included `.hrl` belongs to that file. It has no linkage name:
  GDB would show the native symbol (`clausev1_...`) instead.
- Every expression is located at its physical source line. Code a macro expands
  to is located at the macro's invocation; included code at its line in the
  included file.
- No variables or types are described, language `DW_LANG_lo_user`. The compile
  unit is `FullDebug` all the same: LLVM leaves the function scopes of a
  `LineTablesOnly` unit out of DWARF unless something was inlined, and debuggers
  then name frames by symbol.
- Format: CodeView for MSVC targets, DWARF 5 elsewhere. Without `-g` objects
  carry no line tables (IR inspection and `--emit llvm-ir` still keep the
  locations their source comments need).

## Linking

`-g` passes `-g` to the Clang driver. MSVC targets also write a PDB that the
executable names by file name only (`/PDBALTPATH:%_PDB%`); it is published
beside the executable as `<name>.pdb`, after the executable, in the same
deferred publication. ELF executables keep DWARF inside. Mach-O executables
would need `dsymutil` over the staged objects before staging is removed; it is
not run, so macOS executables lose line tables at link
([step 66](../.agents/11-plan.md#step-66)).

## Erlang call stack

Generated code moves between functions by tail transfers through the process
stack ([execution model](execution-model.md)), so a native backtrace shows the
current Erlang function and then the scheduler (`ProcessStack::run`,
`Executor::work`). The Erlang frames of the stopped process are printed by the
runtime helper `clause::runtime::debug_erlang_stack()`, innermost first, as
`module:function/arity`; it reads the thread's running process and the frames'
compiled names, never runtime atom tables:

```text
(lldb) breakpoint set --file debug.erl --line 8
(lldb) run
* thread #1, stop reason = breakpoint 1.1
    frame #0: debug.exe`leaf at debug.erl:8
(lldb) expr -- clause::runtime::debug_erlang_stack()
  debug:leaf/1
  debug:middle/1
  debug:main/1
```

GDB spells the call `call clause::runtime::debug_erlang_stack()` after
`set language c++` (frames of Erlang code have no language GDB parses C++ names
in). It names the frames `twice () at /path/debug.hrl:5`. `-Os` executables may
drop the helper with other unreferenced code.

## Tests

- `linking_debug_info`: IR metadata (subprogram files and lines, a macro located
  in the included file) and object line-table sections for x86-64 and x86
  Windows, x86-64, AArch64 and ARMv7 Linux and arm64 macOS at O0/O2, and none
  without `-g`.
- `linking_debugger`: links the fixture with `-g` at O0/O2 (and a PDB beside it
  on Windows), then drives LLDB (the LLVM installation's on Windows) or GDB:
  breakpoints in `debug.hrl` and `debug.erl` stop in `twice` and `leaf`, and the
  helper prints their Erlang stacks. A debugger that does not start (an LLVM
  release's LLDB without its Python library) is passed over; exit 77 (skipped)
  when none exists. Validated with LLDB on Windows x64 and GDB 16 on Linux
  x86-64.
