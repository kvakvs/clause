"""Link a -g program and drive a debugger through it: break on Erlang lines, see the Erlang stack (step 60).

Uses LLDB (on Windows the LLVM installation's), else GDB; exits 77 (CTest skip) when no debugger exists or the
system does not let it start programs.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

tool, fixtures, directory, suffix = sys.argv[1:]
work = Path(directory)
shutil.rmtree(work, ignore_errors=True)
work.mkdir(parents=True)
for name in ("debug.erl", "debug.hrl"):
    shutil.copyfile(Path(fixtures) / name, work / name)


def link(name, *options):
    """Link the fixture into `name` and check it runs; returns the executable path."""
    result = subprocess.run([tool, *options, "-o", name, "debug.erl"], cwd=work, capture_output=True, timeout=120)
    assert result.returncode == 0 and not result.stdout and not result.stderr, result
    assert not list(work.glob(".clause-link*")), "link staging left behind"
    executable = work / (name + suffix)
    run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
    assert run.returncode == 0 and run.stdout.replace("\r\n", "\n") == "{leaf,40}\n{middle,41}\n", run
    return executable


def runs(candidate):
    """Whether the debugger starts at all (an LLVM release's LLDB may lack the Python library it links)."""
    try:
        return subprocess.run([candidate, "--version"], capture_output=True, timeout=60).returncode == 0
    except OSError:
        return False


def debugger():
    """The debugger command line prefix and its dialect, or None."""
    candidates = [shutil.which("lldb")]
    if os.name == "nt":
        candidates.append(str(Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "LLVM/bin/lldb.exe"))
    for candidate in candidates:
        if candidate and Path(candidate).is_file() and runs(candidate):
            return [candidate, "-b"], "lldb"
    gdb = shutil.which("gdb")
    return ([gdb, "-batch", "-nx"], "gdb") if gdb and os.name != "nt" and runs(gdb) else (None, None)


def session(command, dialect, executable):
    """Break in the included helper and in leaf/1, print the Erlang stack at each stop, and return the output."""
    steps = {
        "lldb": ["breakpoint set --file debug.hrl --line 5", "breakpoint set --file debug.erl --line 8", "run",
                 "bt 1", "expr -- clause::runtime::debug_erlang_stack()", "continue", "bt 1",
                 "expr -- clause::runtime::debug_erlang_stack()", "kill"],
        "gdb": ["set language c++", "break debug.hrl:5", "break debug.erl:8", "run", "bt 1",
                "call clause::runtime::debug_erlang_stack()", "continue", "bt 1",
                "call clause::runtime::debug_erlang_stack()", "kill"],
    }[dialect]
    flag = "-o" if dialect == "lldb" else "-ex"
    arguments = [part for step in steps for part in (flag, step)]
    for _ in range(3):
        result = subprocess.run([*command, *arguments, str(executable)], cwd=work, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=180)
        # Windows consoles end the helper's stderr lines with CR CR LF: drop the empty lines that leaves.
        output = "\n".join(line for line in (result.stdout + result.stderr).splitlines() if line.strip()) + "\n"
        # A crash of the debugger's own server (seen once under full parallel CTest load) is retried.
        if "PLEASE submit a bug report" not in output:
            break
    return output


plain = link("plain")
assert not plain.with_suffix(".pdb").exists(), "a PDB without -g"
command, dialect = debugger()
for level in ("O0", "O2"):
    executable = link("debug-" + level, "-g", "-" + level)
    if suffix == ".exe":
        assert executable.with_suffix(".pdb").is_file(), "no PDB beside the executable"
    if not command:
        continue
    output = session(command, dialect, executable)
    # macOS lets a debugger start programs only with Developer Mode (DevToolsSecurity -enable): skip without it.
    if "Not allowed to attach to process" in output:
        print(f"SKIP: {command[0]} may not debug programs here (enable Developer Mode)")
        sys.exit(77)
    # Both stops show the Erlang function and line (GDB may name the file by its full path), then the Erlang frames of
    # the stopped process.
    for function, place in (("twice", "debug.hrl:5"), ("leaf", "debug.erl:8")):
        assert re.search(rf"\b{function}( \(\))? at (\S*[/\\])?{re.escape(place)}", output), (level, output)
    stacks = output.count("debug:middle/1\n  debug:main/1")
    assert "debug:twice/1\n  debug:middle/1" in output and "debug:leaf/1\n  debug:middle/1" in output, output
    assert stacks == 2, (level, output)
if not command:
    print("debugger: no LLDB or GDB found; linked line tables only")
    sys.exit(77)
print(f"debugger: {dialect} stopped on Erlang lines at O0/O2 and printed the Erlang stack")
