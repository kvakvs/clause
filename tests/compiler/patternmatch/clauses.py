"""Execute locally authored ordered clauses, guards and candidate isolation."""
import json
import pathlib
import re
import sys
from matrix import rejection_lists
from stored import load
from evidence import run
from immediate import native
from bindings import compile_case


def rejection(tool, work):
    """Later or unused bodies still resolve calls, check scopes and reject unsupported syntax."""
    directory = work / "negative"
    (directory / "out").mkdir(parents=True, exist_ok=True)
    for file in (directory / "out").iterdir():
        if file.is_file():
            file.unlink()
    (directory / "out/sentinel").write_bytes(b"preserve")
    (directory / "client.erl").write_bytes(b"-module(client). -export([id/1]). id(X) -> X.\n")
    cases = [("later_dynamic", "f(X) -> X; f(_) -> fun erlang:apply/2.", "dynamic calls"),
             ("unused", "f(X) -> X. unused(X) -> X; unused(_) -> fun erlang:apply/2.", "dynamic calls"),
             ("later_call", "f(X) -> X; f(_) -> missing().", "undefined function"),
             ("later_remote", "f(X) -> X; f(_) -> absent:f().", "unknown module"),
             ("later_leak", "f(X,_) -> X; f(_,_) -> X.", "unbound variable X")]
    for name, body, diagnostic in cases:
        (directory / (name + ".erl")).write_text(f"-module({name}).\n\n{body}\n", encoding="utf-8")
        row = {"name": name, "diagnostic": diagnostic, "capability": ""}
        for options, project in rejection_lists():
            compile_case(tool, directory, row, options, project)
    run([tool, "--emit", "obj", "--artifact-dir", str(directory / "recovery"), str(directory / "client.erl")])
    return len(cases)


def main():
    tool, cmake, root, directory, settings, config, suffix = sys.argv[1:]
    source, work = pathlib.Path(root), pathlib.Path(directory)
    work.mkdir(parents=True, exist_ok=True)
    records = load(source, "clauses", work)
    native(tool, cmake, source, work, settings, config, suffix)
    records["negative_cases"] = rejection(tool, work)
    for mode in ["--print-ir", "--print-optimized-ir", "--print-types"]:
        options = [] if mode == "--print-types" else ["-O2"]
        result = run([tool, *options, mode, str(work / "answer.erl"), str(work / "client.erl")])
        if mode == "--print-types":
            # One signature per function type (step 58K): each clause keeps its own result and relation.
            summaries = {match.group(1): match.group(2) for match in
                         re.finditer(r'%% inferred: (\w+)(\([^\n]+)', result)}
            assert summaries["common"] == "(0) -> 0; (X) -> X", summaries["common"]
            assert "argument" not in summaries["rollback"], summaries["rollback"]
    assert summaries["join"] == "(0) -> 1; (_) -> 2", summaries["join"]
    (work / "evidence.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    print(f'{records["calls"]} OTP/native ordered-clause calls; later-body diagnostics and retry passed.')


if __name__ == "__main__":
    main()
