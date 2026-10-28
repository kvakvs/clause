"""Compile cache (plan step 62D4): a project build reuses its whole batch's outcome from <build_dir>/cache.

Each program fixture builds cold and warm: the warm build says "(cached)", its executable prints the golden stdout
and the diagnostics are the same. The reuse unit is the whole batch: editing any module, the triple's options or the
optimization level misses; IR and type inspection always recompute; --no-cache never reuses.
"""
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

tool, root, directory, suffix = sys.argv[1:]
source, work = Path(root), Path(directory)
shutil.rmtree(work, ignore_errors=True)
work.mkdir(parents=True)


def build(project, *options, status=0):
    """Build a project with stage times; returns (stderr without traces, compile line was cached, cache trace)."""
    result = subprocess.run([tool, "--print-stage-times", "--verbose", *options, "--project", "project.toml"],
                            cwd=project, capture_output=True, text=True, timeout=600)
    assert result.returncode == status, (options, result)
    lines = result.stderr.splitlines()
    plain = [line for line in lines if not re.match(r"\[(cache|time|index|pp|parse|comp)\]", line)]
    cached = any(re.match(r"\[time\] compile .* \(cached\)$", line) for line in lines)
    trace = [line for line in lines if line.startswith("[cache] ") and " compile " in line]
    return plain, cached, trace


def run(executable, args):
    """A linked program's stdout."""
    result = subprocess.run([str(executable), *args], capture_output=True, text=True, timeout=120)
    return result.stdout.replace("\r\n", "\n")


# Program fixtures: cold and warm builds link programs that print their goldens, with the same diagnostics.
for fixture in sorted((source / "tests/fixtures/programs").glob("*/fixture.json")):
    project = work / fixture.parent.name
    shutil.copytree(fixture.parent, project, ignore=shutil.ignore_patterns("expected", "compile.txt"))
    settings = json.loads(fixture.read_text(encoding="utf-8"))
    options = ["-O2", "--entry", settings["entry"], "-o", "app"]
    expected = (fixture.parent / "expected/stdout.txt").read_text(encoding="utf-8")
    cold, cold_cached, _ = build(project, *options)
    first = run(project / ("app" + suffix), settings.get("args", []))
    warm, warm_cached, _ = build(project, *options)
    second = run(project / ("app" + suffix), settings.get("args", []))
    assert not cold_cached and warm_cached and cold == warm, (fixture.parent.name, cold, warm)
    assert first == second == expected, (fixture.parent.name, first, second)
print("compile_cache: warm program builds reuse the batch and match their goldens")

# A batch with a warning: the warm build reports it again.
project = work / "edits"
(project / "src").mkdir(parents=True)
for name in ("main.erl", "worker.erl", "service.erl", "callbacks.erl", "hidden.erl", "strict.erl"):
    shutil.copy(source / "tests/fixtures/linking/exports" / name, project / "src" / name)
(project / "src/lazy.erl").write_bytes(b"-module(lazy).\n-behaviour(service).\n-export([other/0]).\nother() -> ok.\n")
(project / "project.toml").write_bytes(b'schema_version = 1\n\n[[targets]]\nname = "app"\nsources = ["src/*.erl"]\n'
                                       b'output = "app"\nentry = "main"\n')
cold, _, _ = build(project)
warm, cached, _ = build(project)
assert cached and cold == warm and any("callback" in line for line in warm), (cold, warm)


def misses(*options):
    """Whether a build with `options` recompiled the batch."""
    _, cached, trace = build(project, *options)
    return not cached and trace and "miss" in trace[0]


# The whole batch is the unit: any module edit, another optimization level or triple option misses.
(project / "src/hidden.erl").write_bytes((project / "src/hidden.erl").read_bytes() + b"%% edited\n")
assert misses() and not misses()
assert misses("-O2") and not misses("-O2") and not misses()
assert misses("-g") and not misses()
# --emit publishes kept objects exactly like fresh ones.
def emitted():
    """Build with --emit obj and return the published artifacts' bytes by name."""
    shutil.rmtree(project / "objects", ignore_errors=True)
    _, cached, _ = build(project, "--emit", "obj", "--artifact-dir", "objects")
    return cached, {path.name: path.read_bytes() for path in sorted((project / "objects").rglob("*")) if path.is_file()}


fresh_cached, fresh = emitted()
warm_cached, warm_objects = emitted()
assert not fresh_cached and warm_cached and fresh and fresh == warm_objects, sorted(fresh)
# Inspection always recomputes; --no-cache neither reads nor writes.
for inspection in (["--print-types"], ["--print-ir"]):
    result = subprocess.run([tool, "--verbose", *inspection, "--project", "project.toml"], cwd=project,
                            capture_output=True, text=True, timeout=600)
    assert result.returncode == 0 and " compile " not in "".join(
        line for line in result.stderr.splitlines(keepends=True) if line.startswith("[cache]")), result
_, cached, trace = build(project, "--no-cache")
assert not cached and not trace
print("compile_cache: edits and options miss, inspection and --no-cache recompute")
