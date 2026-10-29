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
keep only a debug map naming the staged objects, so `dsymutil` (beside the
Clang driver, else on `PATH`) gathers their DWARF into `<name>.dSYM` before
staging is removed; the bundle is published beside the executable, replacing
the previous one. Darwin compile units record the working directory as their
compile directory: the linker leaves a unit without one out of the debug map.

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

At `-O2` small call-free local functions and known funs run inlined in their
caller ([leaf inlining](specialization.md#leaf-inlining)). Their code keeps its
lines in the function's own scope, inlined at the call, so debuggers stop in
them and show an inlined frame (``debug.exe`leaf at debug.erl:8 [inlined]``);
the Erlang stack has the caller on top. LLDB reading a PDB finds the lines of an
included file only in functions of their own, so it cannot break in an inlined
function from a `.hrl`.

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
  on Windows, a dSYM bundle on macOS), then drives LLDB (the LLVM installation's on Windows) or GDB:
  breakpoints in `debug.hrl` and `debug.erl` stop in `twice` and `leaf`, and the
  helper prints their Erlang stacks (at O2 both run inlined in `middle`, and
  LLDB on Windows skips the `debug.hrl` stop). A debugger that does not start (an LLVM
  release's LLDB without its Python library) is passed over; exit 77 (skipped)
  when none exists or the system does not let it start programs (macOS
  without Developer Mode). Validated with LLDB on Windows x64 and GDB 16 on Linux
  x86-64.
