"""Export liveness (plan step 62C): executables leave out exports they cannot reach. Checks the fixture's output with
and without --keep-exports (CLI and project option), the --verbose reasons, refused uses, every program fixture and
selected executable goldens with --keep-exports, and records -Os/--lto sizes with and without it (not a gate)."""
import concurrent.futures
import json
from pathlib import Path
import shutil
import subprocess
import sys

tool, root, directory, suffix = sys.argv[1:]
source, work = Path(root), Path(directory)
shutil.rmtree(work, ignore_errors=True)
work.mkdir(parents=True)
fixture = source / "tests/fixtures/linking/exports"
modules = [str(fixture / name) for name in ("main.erl", "worker.erl", "service.erl", "callbacks.erl", "hidden.erl")]
runner = source / "tests/compiler/executables/run.py"
# Program fixtures plus executables covering dynamic calls, behaviours, spawning, funs, module_info and traces.
CASES = [path.parent for path in sorted((source / "tests/fixtures/programs").glob("*/fixture.json"))]
CASES += [source / "tests/fixtures/executables" / name for name in (
    "dynamic_calls", "behaviours", "spawning", "fun_values", "module_info", "stack_traces", "imports", "library")]


def clau(arguments, status=0):
    """Run clau in the work directory; returns its stderr after checking the exit status."""
    result = subprocess.run([tool, *arguments], cwd=work, capture_output=True, text=True, timeout=300)
    assert result.returncode == status, (arguments, result)
    return result.stderr


def output(name):
    """Run a linked executable; returns its stdout."""
    result = subprocess.run([str(work / (name + suffix))], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, (name, result)
    return result.stdout.replace("\r\n", "\n")


def expected(dead):
    """The fixture's output: dead/0 answers only when every export is kept."""
    return (f"spawn 42\napply 42\nfun 9\ncallback started\nexported true\ndead {dead}\nexports: ok\n")


# Without --keep-exports hidden:dead/0 is left out: function_exported/3 still answers true, the call raises undef.
for level in ("-O0", "-O2"):
    trace = clau([level, "--verbose", "-o", "live" + level, *modules])
    assert output("live" + level) == expected("undef"), level
    clau([level, "--keep-exports", "-o", "kept" + level, *modules])
    assert output("kept" + level) == expected("dead"), level
for line in ('module="worker" detail="dropped-exports=3"', 'module="hidden" detail="dropped-exports=3"',
             'module="callbacks" detail="kept-all-exports reason=\'callbacks\' is used as a value in main:main/1"'):
    assert line in trace, (line, trace)
print("exports: unreachable exports are left out, --keep-exports keeps them")

# The project option keep_exports keeps every export of its target; a non-boolean value is refused.
manifest = work / "project.toml"
sources = ", ".join(json.dumps(path.replace("\\", "/")) for path in modules)
manifest.write_text(f'schema_version = 1\n\n[[targets]]\nname = "app"\nsources = [{sources}]\nsource_dirs = []\n'
                    f'output = "project"\n\n[targets.options]\nkeep_exports = true\n', encoding="utf-8")
clau(["--project", str(manifest)])
assert output("project") == expected("dead")
manifest.write_text(manifest.read_text(encoding="utf-8").replace("= true", '= "yes"'), encoding="utf-8")
assert "expected a boolean" in clau(["--project", str(manifest)], 1)
assert "--keep-exports requires --output" in clau(["--keep-exports", *modules], 2)
print("exports: project option keep_exports and refused uses")


def golden_run(case):
    """Link and run one case with --keep-exports against its golden; returns (case, ok, output)."""
    result = subprocess.run([sys.executable, str(runner), tool, str(work / "runs" / case.name), str(case),
                             f"--suffix={suffix}", "--target-option=--keep-exports"],
                            capture_output=True, text=True, timeout=900)
    return case.name, result.returncode == 0, result.stdout + result.stderr


with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    outcomes = list(pool.map(golden_run, CASES))
for name, ok, report in outcomes:
    if not ok:
        print(report)
assert all(ok for _, ok, _ in outcomes), [name for name, ok, _ in outcomes if not ok]
print(f"exports: {len(CASES)} cases pass their goldens with --keep-exports")


def size(options):
    """Executable bytes of the sizes program linked with `options`."""
    name = "sizes" + "".join(options).replace("-", "_")
    clau([*options, "-o", name, str(fixture / "sizes/sizes.erl")])
    assert output(name).endswith("sizes: ok\n")
    return (work / (name + suffix)).stat().st_size


sizes = {" ".join(options): {"bytes": size(options), "keep_exports_bytes": size([*options, "--keep-exports"])}
         for options in (["-Os"], ["-O2", "--lto"])}
report = {"sizes_are_not_thresholds": True, "sizes": sizes}
(work / "exports.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(json.dumps(report, indent=2))
