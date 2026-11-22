"""Exercise semantic patterns through both CLI modes against the pinned OTP source/oracle."""
import json
import pathlib
import re
import subprocess
import sys
from matrix import rejection_lists
from bindings import compile_case
from evidence import digest, run, verify_manifest
from stored import load

ERRORS = r"illegal pattern|illegal expression in pattern|unbound variable|unsafe variable|invalid binary|conflicting binary|UTF binary|literal string pattern|unsized binary|map pattern requires"


def policies(tool, work, rows):
    """Repeat semantic/capability rejection through both batch modes at one policy (they precede code generation)."""
    rejected = [row for row in rows if row["diagnostic"] or row["capability"]]
    for policy, project in rejection_lists():
        for row in rejected:
            compile_case(tool, work, row, policy, project)


def accepted(tool, work, rows):
    """Ensure every OTP-accepted form is free of erroneous pattern or binding diagnostics."""
    for row in rows:
        result = subprocess.run([tool, str(work / (row["name"] + ".erl"))], capture_output=True,
                                text=True, encoding="utf-8", timeout=30)
        if not row["diagnostic"]:
            assert not re.search(ERRORS, result.stderr), (row, result.stderr)


def cli(tool, work, rows):
    """Every rejected batch preserves prior output; a valid batch subsequently recovers."""
    out = work / "out"
    out.mkdir(exist_ok=True)
    for child in out.iterdir():
        if child.is_file():
            child.unlink()
    (out / "sentinel").write_bytes(b"preserve")
    policies(tool, work, rows)
    accepted(tool, work, rows)
    assert (out / "sentinel").read_bytes() == b"preserve"
    compile_case(tool, work, next(row for row in rows if row["name"] == "pat_groups"), ["-O2"], False)


def limits(tool, work):
    """Public source depth and arithmetic ceilings reject cleanly, including after an earlier valid form."""
    for name, body, diagnostic in [
        ("depth", "f(" + "{" * 800 + "X" + "}" * 800 + ") -> X.", "nesting budget exhausted"),
        ("constant", "f({1 bsl 4194240}) -> ok.", "illegal pattern"),
        ("digits", "f({(1 bsl 2000000) bsl 2194240}) -> ok.", "illegal pattern"),
        ("wide", "f({" + ",".join(["1 bsl 30000"] * 200) + "}) -> ok.", "binding analysis work limit exceeded")]:
        path = work / (name + ".erl")
        path.write_text(f"-module({name}).\ng(X) -> X.\n{body}\n", encoding="utf-8")
        result = subprocess.run([tool, "--emit", "obj", "--artifact-dir", str(work / "limits-out"), str(path)],
                                capture_output=True, text=True, encoding="utf-8", timeout=30)
        assert result.returncode == 1 and diagnostic in result.stderr, result.stderr
        assert not (work / "limits-out").exists()


def suites(tool, otp):
    """Original suite parsing is syntax evidence only; selected helpers get independent lint comparison."""
    options = ["-I", str(otp / "lib/compiler/src"), "--enable-feature", "maybe_expr", "--disable-feature", "compr_assign"]
    for app in ["stdlib", "kernel", "common_test", "syntax_tools"]:
        options += ["--app-dir", f"{app}={otp / 'lib' / app}"]
    for suite in ["map_SUITE", "bs_size_expr_SUITE"]:
        run([tool, "--parse-check", *options, str(otp / "lib/compiler/test" / (suite + ".erl"))])


def catalog(source, otp):
    """Keep embedded-expression legality synchronized with the exact OTP guard BIF signatures."""
    internal = (otp / "lib/stdlib/src/erl_internal.erl").read_text(encoding="utf-8")
    expected = set(re.findall(r"^(?:guard_bif|new_type_test)\(([^,]+), (\d+)\) -> true;", internal, re.M))
    implementation = (source / "compiler/src/semantic/pattern_calls.cpp").read_text(encoding="utf-8")
    actual = set(re.findall(r'\{U"([^"]+)", (\d+)\}', implementation))
    assert actual == expected, (expected - actual, actual - expected)


def main():
    tool, root, directory = sys.argv[1:]
    source, work = pathlib.Path(root), pathlib.Path(directory)
    work.mkdir(parents=True, exist_ok=True)
    fixtures = source / "tests/fixtures/patternmatch"
    verify_manifest(fixtures, (fixtures / "pattern-fixtures.tsv").read_text(encoding="utf-8"))
    retained = load(source, 'patterns', work)
    rows, records, oracle = retained['cases'], retained['helpers'], retained['oracle']
    cli(tool, work, rows)
    limits(tool, work)
    (work / "evidence.json").write_text(json.dumps({"reference": 'project-owned generated/patterns/manifest.json',
        "pattern_manifests": {name: digest(fixtures / name) for name in ["pattern-otp.tsv", "pattern-fixtures.tsv"]},
        "cases": rows, "helpers": records, "oracle": oracle,
        "policies": "O0; positional/project; nonpublication/recovery",
        "execution": "deferred; existing native identity/projection workflows remain required"}, indent=2) + "\n", encoding="utf-8")
    print(oracle + "Retained pattern legality, source limits and batch nonpublication passed.")


if __name__ == "__main__":
    main()
