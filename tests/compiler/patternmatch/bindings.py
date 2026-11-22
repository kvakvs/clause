"""Compare local binding fragments and stored acceptance through the CLI."""
import json
import pathlib
import re
import subprocess
import sys
from matrix import rejection_lists
from evidence import native, run
from stored import load

BINDING_ERRORS = r"unbound variable|unsafe variable|wildcard '_' cannot be read|guards cannot bind variables"


def compile_case(tool, work, row, policy, project):
    """Reject semantic errors or deferred capabilities before either batch mode publishes anything."""
    name = row["name"]
    args = [tool, *policy, "--emit", "obj"]
    if project:
        manifest = f'schema_version=1\n[[targets]]\nname="bindings"\nsources=["client.erl","{name}.erl"]\noutput="out"\n'
        (work / "project.toml").write_text(manifest, encoding="utf-8")
        args += ["--project", "project.toml"]
    else:
        args += ["--artifact-dir", "out", "client.erl", name + ".erl"]
    result = subprocess.run(args, cwd=work, capture_output=True, text=True, encoding="utf-8", timeout=30)
    rejected = bool(row["diagnostic"] or row["capability"])
    assert result.returncode == int(rejected), (args, result.stdout, result.stderr)
    assert not result.stdout, result.stdout
    if row["diagnostic"]:
        assert row["diagnostic"] in result.stderr, result.stderr
        assert re.search(rf'{name}\.erl:\d+:\d+:.*{re.escape(row["diagnostic"])}', result.stderr), result.stderr
    else:
        assert not re.search(BINDING_ERRORS, result.stderr), result.stderr
    if row["capability"] and not row["diagnostic"]:
        assert row["capability"] in result.stderr, result.stderr
    if rejected:
        assert sorted(p.name for p in (work / "out").iterdir()) == ["sentinel"], list((work / "out").iterdir())


def cli(tool, work, rows):
    """Run negative batches through both drivers at one policy (semantic errors precede code generation), then prove
    recovery."""
    out = work / "out"
    out.mkdir(exist_ok=True)
    for child in out.iterdir():
        if child.is_file():
            child.unlink()
    (out / "sentinel").write_bytes(b"preserve")
    rejected = [row for row in rows if row["diagnostic"] or row["capability"]]
    for policy, project in rejection_lists():
        for row in rejected:
            compile_case(tool, work, row, policy, project)
    assert (out / "sentinel").read_bytes() == b"preserve"
    compile_case(tool, work, rows[0], ["-O2"], False)
    report = run([tool, "--print-types", str(work / "identity.erl")])
    # _Name is an ordinary variable: the result is the first argument.
    assert "%% inferred: f(_Name, _) -> _Name\n" in report, report


def locations(tool, work):
    """Use a real include and macro expansion to preserve physical diagnostic provenance."""
    (work / "origin.hrl").write_bytes(b"f() -> Missing.\n")
    (work / "origin.erl").write_bytes(b'-module(origin).\n-include("origin.hrl").\n')
    result = subprocess.run([tool, str(work / "origin.erl")], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 1 and "origin.hrl:1:8:" in result.stderr, result.stderr
    (work / "macro.erl").write_bytes(b'-module(macro).\n-define(READ, Missing).\nf() -> ?READ.\n')
    result = subprocess.run([tool, str(work / "macro.erl")], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 1 and "unbound variable Missing" in result.stderr, result.stderr
    assert "macro.erl:3:" in result.stderr and "macro" in result.stderr, result.stderr


def main():
    """Separate legality, private binding invariants and executable projection evidence."""
    tool, cmake, root, directory, settings, config, suffix = sys.argv[1:]
    source, work = pathlib.Path(root), pathlib.Path(directory)
    work.mkdir(parents=True, exist_ok=True)
    records = load(source, 'bindings', work)
    rows, helper_record, oracle = records['cases'], records['helpers'], records['oracle']
    cli(tool, work, rows)
    locations(tool, work)
    native(tool, cmake, source, work, settings, config, suffix)
    (work / "evidence.json").write_text(json.dumps({"cases": rows, "local_helpers": helper_record,
        "oracle": oracle, "native": "local projection and identity fragments; four policies",
        "deferred": "step 9 clause isolation and failed-candidate rollback execution"}, indent=2) + "\n", encoding="utf-8")
    print(oracle + "Binding legality and nonpublication passed in both CLI modes; native projections passed four policies.")


if __name__ == "__main__":
    main()
