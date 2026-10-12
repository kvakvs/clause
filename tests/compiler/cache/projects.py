"""Build caches shared by processes (plan step 62D5): several clau runs building projects into one build directory
at the same time keep every entry; a garbage cache.db is rebuilt silently and the old v1 tree is removed.

projects.py CLAU WORK: four projects with three targets each build together twice with one --build-dir; the second
round reuses every target's compile outcome. The same happens after cache.db is overwritten with garbage.
"""
import pathlib
import re
import shutil
import subprocess
import sys

PROJECTS = 4
TARGETS = 3


def write(path, text):
    """Create one file with its directories."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode())


def make_project(root, number):
    """A project with TARGETS targets, each compiling two modules of its own."""
    manifest = "schema_version = 1\n"
    for target in range(TARGETS):
        name = f"p{number}t{target}"
        manifest += f'\n[[targets]]\nname = "{name}"\nsources = ["{name}/*.erl"]\n'
        write(root / name / "a.erl", f"-module(a).\n-export([f/1]).\nf(X) -> b:g(X) + {number * 10 + target}.\n")
        write(root / name / "b.erl", "-module(b).\n-export([g/1]).\ng(X) -> X * 2.\n")
    write(root / "project.toml", manifest)


def build_all(tool, work, build_dir, together=True):
    """Build every project, all at once or one after another; returns each run's (status, cached targets,
    diagnostics)."""
    def start(number):
        return subprocess.Popen([str(tool), "--verbose", "--print-stage-times", "--build-dir", str(build_dir),
                                 "--project", "project.toml"], cwd=work / f"p{number}", stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)

    runs = [start(number) for number in range(PROJECTS)] if together else range(PROJECTS)
    results = []
    for run in runs:
        run = run if together else start(run)
        _, err = run.communicate(timeout=600)
        lines = err.splitlines()
        cached = {m[1] for m in (re.match(r"\[cache\] hit compile (\w+)@", line) for line in lines) if m}
        plain = [line for line in lines if not re.match(r"\[(cache|time|index|pp|parse|comp)\]", line)]
        results.append((run.returncode, cached, plain))
    return results


def every_target(number):
    return {f"p{number}t{target}" for target in range(TARGETS)}


def main():
    tool, work = pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve()
    shutil.rmtree(work, ignore_errors=True)
    for number in range(PROJECTS):
        make_project(work / f"p{number}", number)
    build_dir = work / "shared-build"
    write(build_dir / "cache/v1/src-00000000/index", "the file backend of step 62D2\n")

    cold = build_all(tool, work, build_dir)
    assert all(status == 0 and not cached for status, cached, _ in cold), cold
    warm = build_all(tool, work, build_dir)
    for number, (status, cached, plain) in enumerate(warm):
        assert status == 0 and cached == every_target(number) and plain == cold[number][2], (number, warm)
    assert not (build_dir / "cache/v1").exists() and (build_dir / "cache/v2/cache.db").is_file()
    print(f"projects: {PROJECTS} concurrent runs kept all {PROJECTS * TARGETS} compile outcomes; v1 removed")

    (build_dir / "cache/v2/cache.db").write_bytes(b"garbage, not a database\n" * 200)
    rebuilt = build_all(tool, work, build_dir, together=False)
    assert all(status == 0 and not cached for status, cached, _ in rebuilt), rebuilt
    assert [plain for _, _, plain in rebuilt] == [plain for _, _, plain in cold], rebuilt
    again = build_all(tool, work, build_dir)
    assert all(cached == every_target(number) for number, (_, cached, _) in enumerate(again)), again
    print("projects: a garbage cache.db is rebuilt silently")
    print("projects: ok")


if __name__ == "__main__":
    main()
