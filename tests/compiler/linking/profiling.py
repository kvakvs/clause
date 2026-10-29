"""Profile a program with a known hot function at run time and check the report ranks it first (step 61).

Profiling is a runtime option, so one executable serves both modes: without --profile its output, exit status and
files are those of an unprofiled run.
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
shutil.copyfile(Path(fixtures) / "profile.erl", work / "profile.erl")
EXPECTED = "{16,15}\n"


def run(executable, *arguments, flags=None):
    """Run the program in the work directory; returns exit status, stdout and stderr."""
    environment = {key: value for key, value in os.environ.items() if key != "CLAUSE_FLAGS"}
    if flags:
        environment["CLAUSE_FLAGS"] = flags
    result = subprocess.run([str(executable), *arguments], cwd=work, capture_output=True, text=True, timeout=120,
                            env=environment)
    return result.returncode, result.stdout.replace("\r\n", "\n"), result.stderr


def rows(report, section):
    """The rows of one report section: (microseconds, entries, name)."""
    body = report.split(section + ":\n", 1)[1].split("\n", 1)[1]
    found = []
    for line in body.splitlines():
        match = re.match(r"\s*(\d+)\s+[\d.]+%\s+(\d+)\s+(.*)$", line)
        if not match:
            break
        found.append((int(match[1]), int(match[2]), match[3]))
    return found


def check(report, level):
    """spin/2 ranks first by self time with exactly its entries; the busy process ranks first too. At O2 the leaf
    cold/1 runs inlined in main/1 (leaf inlining), so it has no entries of its own."""
    functions = rows(report, "Functions by self time")
    assert functions[0][1:] == (2000001, "profile:spin/2"), functions
    names = [row[1:] for row in functions]
    assert ((1, "profile:cold/1") in names) == (level == "O0"), (level, functions)
    assert functions[0][0] > sum(row[0] for row in functions[1:]), functions
    processes = rows(report, "Processes by time")
    assert len(processes) == 2 and processes[0][2].endswith(": profile:spin/2"), processes
    header = re.match(r"Clause profile: 2 processes, (\d+) time slices, (\d+) timer clock readings\n", report)
    assert header and int(header[1]) > 0, report
    print("scheduling:", header[0].strip())


for level in ("O0", "O2"):
    name = "profile-" + level
    result = subprocess.run([tool, "-" + level, "-o", name, "profile.erl"], cwd=work, capture_output=True,
                            timeout=120)
    assert result.returncode == 0 and not result.stdout and not result.stderr, result
    executable = work / (name + suffix)
    plain = run(executable)
    assert plain == (0, EXPECTED, ""), plain
    assert not list(work.glob("*.txt")), "a report without --profile"
    assert run(executable, "--profile", "report.txt") == plain
    check((work / "report.txt").read_text(encoding="utf-8"), level)
    assert run(executable, flags="--profile=flags.txt") == plain
    check((work / "flags.txt").read_text(encoding="utf-8"), level)
    for path in work.glob("*.txt"):
        path.unlink()
status, _, error = run(work / ("profile-O0" + suffix), "--profile")
assert status == 70 and "--profile needs the file" in error, (status, error)
print("profiling: spin/2 ranks first at O0/O2; unprofiled runs unchanged")
