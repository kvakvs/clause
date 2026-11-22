"""Exercise semantic guard resolution and immediate services through the public CLI and OTP."""
import json
import pathlib
import sys
from matrix import rejection_lists
from stored import load
from evidence import digest, run
from immediate import native, token, erl
from bindings import compile_case

PREDICATES = ["is_atom", "is_integer", "is_number", "is_boolean", "is_tuple", "is_list", "is_binary",
              "is_bitstring", "is_float", "is_map", "is_pid", "is_port", "is_reference", "is_function"]
OPERATORS = [("exact", "=:="), ("exact_ne", "=/="), ("equal", "=="), ("unequal", "/="),
             ("less", "<"), ("leq", "=<"), ("greater", ">"), ("geq", ">=")]
VALUES = [-576460752303423488, -1, 0, 576460752303423487, "z", "a", "true", "false", "Ω", "nil", "tuple"]


def write_calls(work, calls):
    """Write stable native arguments and independent mathematical OTP terms."""
    (work / "calls.txt").write_bytes(("\n".join(f"{module} {name} {len(args)} " + " ".join(map(token, args))
        for module, name, args in calls) + "\n").encode())
    (work / "calls.term").write_bytes(("\n".join(f"{{{module},{name},[" + ",".join(map(erl, args)) + "]}."
        for module, name, args in calls) + "\n").encode())
    (work / "project.toml").write_bytes(b'schema_version=1\n[[targets]]\nname="services"\nsources=["answer.erl","client.erl"]\n')


def resolution(tool, source, work, recorded):
    """Compare located legal/illegal/deferred calls with OTP, including skipped illegal operands."""
    cases = work / "resolution"
    (cases / "out").mkdir(parents=True, exist_ok=True)
    for file in (cases / "out").iterdir():
        file.unlink()
    (cases / "out/sentinel").write_bytes(b"preserve")
    (cases / "client.erl").write_bytes(b"-module(client). id(X) -> X.\n")
    rows = json.loads((source / "tests/fixtures/patternmatch/guard-resolution.json").read_text(encoding="utf-8"))
    terms = []
    for row in rows:
        text = f'-module({row["name"]}).\n{row.get("metadata", "")}\n{row["body"]}\n'
        (cases / (row["name"] + ".erl")).write_bytes(text.encode())
        terms.append(f'{{{row["name"]},{"rejected" if row["diagnostic"] else "accepted"},none}}.')
        if row["diagnostic"] or row["capability"]:
            for options, project in rejection_lists():
                compile_case(tool, cases, row, options, project)
        else:
            run([tool, str(cases / (row["name"] + ".erl"))])
        row["sha256"] = digest(cases / (row["name"] + ".erl"))
    (cases / "patterns.term").write_bytes(("\n".join(terms) + "\n").encode())
    assert digest(source / "tests/fixtures/patternmatch/guard-resolution.json") == recorded['fixture_sha256']
    oracle = recorded['oracle']
    assert (cases / "out/sentinel").read_bytes() == b"preserve"
    return {"cases": rows, "oracle": oracle}


def main():
    tool, cmake, root, directory, settings, config, suffix = sys.argv[1:]
    source, work = pathlib.Path(root), pathlib.Path(directory)
    work.mkdir(parents=True, exist_ok=True)
    records = load(source, "services", work)
    native(tool, cmake, source, work, settings, config, suffix)
    records["resolution"] = resolution(tool, source, work, records['resolution_oracle'])
    for mode in ["--print-ir", "--print-optimized-ir", "--print-types"]:
        options = [] if mode == "--print-types" else ["-O2"]
        run([tool, *options, mode, str(work / "answer.erl"), str(work / "client.erl")])
    (work / "evidence.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    print(f'{records["calls"]} native/OTP service calls; {len(records["resolution"]["cases"])} resolution cases passed.')


if __name__ == "__main__":
    main()
