"""Step 58P: a recursive group that spends more than its share of the inference pass budget widens every member to
term() and is remembered, so later passes give up on it at once instead of iterating again.

The group is generated: two mutually recursive functions whose bodies are long list expressions, sized from the pass
budget clau reports (debug and release builds have different budgets). A small copy of the same group converges.
Steps 58Q/58S: a map over a tree of two node shapes (as luerl's ttdict has) flipped between two
union shapes of the same values; since unions drop the members another member holds, it converges below its round
cap without flipping.
"""
import re
import subprocess
import sys
from pathlib import Path

tool, directory = sys.argv[1:]
work = Path(directory)
work.mkdir(parents=True, exist_ok=True)


def source(name, items):
    """A module whose f/2 and g/2 call each other, each building a list of `items` copies of N."""
    elements = ", ".join("N" for _ in range(items))
    clauses = [f"{own}(0, Acc) -> Acc;\n{own}(N, Acc) -> {other}(N - 1, [Acc, {elements}])."
               for own, other in (("f", "g"), ("g", "f"))]
    text = (f"-module({name}).\n-export([main/1, f/2]).\n\n"
            f"main(_) -> io:format(\"~p~n\", [length(f(3, []))]), io:format(\"{name}: ok~n\").\n\n"
            + "\n\n".join(clauses) + "\n")
    path = work / f"{name}.erl"
    path.write_text(text, encoding="utf-8")
    return path


def clau(*arguments):
    """Run clau; returns (stdout, stderr) after checking it succeeded."""
    result = subprocess.run([tool, *map(str, arguments)], cwd=work, capture_output=True, text=True, timeout=300)
    assert result.returncode == 0, (arguments, result.stderr[-2000:])
    return result.stdout, result.stderr


def rounds_of(stderr, group):
    """The `inference rounds` line of a recursive group, named by its first member."""
    found = re.search(rf"\[time\] inference rounds {re.escape(group)}: [^\n]*", stderr)
    assert found, stderr[-3000:]
    return found.group(0)


def rounds(stderr, name):
    """The `inference rounds` line of the module's f/g group."""
    found = re.search(rf"\[time\] inference rounds {name}:[fg]/2 and 1 more: [^\n]*", stderr)
    assert found, stderr[-3000:]
    return found.group(0)


# The pass budget, from a trivial compile; one round of the heavy group costs about 12 steps per list element.
_, timed = clau("--print-stage-times", "--print-types", source("probe", 1))
budget = int(re.search(r"inference pass 1 [^\n]*\(\d+ of (\d+) work steps", timed).group(1))
heavy = source("heavy", budget // 48)
small = source("small", 50)

types, timed = clau("--print-stage-times", "--print-types", heavy)
line = rounds(timed, "heavy")
assert "1 over work share" in line and "0 capped" in line, line
assert int(re.search(r"(\d+) remembered", line).group(1)) >= 1, line
assert re.search(r"^%% inferred: f\(.*\) -> _$", types, re.MULTILINE), types[:2000]
assert 'inferred=widened' in types
assert clau("--print-types", heavy)[0] == types, "remembering a group must be deterministic"

types, timed = clau("--print-stage-times", "--print-types", small)
line = rounds(timed, "small")
assert "0 over work share" in line and "0 remembered" in line, line
assert re.search(r"^%% inferred: f\(0, Acc\) -> Acc; \(_, _\) -> \[_, \.\.\.\]$", types, re.MULTILINE), types

TREES = """-module(trees).
-export([main/1, map/2]).
main(_) -> io:format("~p~n", [map(fun(_, V) -> {V} end, {empty, 1, a, {empty, 2, b, empty, 3, c, empty}})]).
map(_, empty) -> empty;
map(F, {A, K, V, B}) -> {map(F, A), K, F(K, V), map(F, B)};
map(F, {A, K1, V1, B, K2, V2, C}) -> {map(F, A), K1, F(K1, V1), map(F, B), K2, F(K2, V2), map(F, C)}.
"""
(work / "trees.erl").write_text(TREES, encoding="utf-8")
types, timed = clau("--print-stage-times", "--print-types", work / "trees.erl")
line = rounds_of(timed, "trees:map/2")
assert "0 flipped" in line and "0 capped" in line and "0 over work share" in line, line
assert re.search(r"^%% inferred: map\(.*\) -> \{empty \|", types, re.MULTILINE), types[:2000]

# The given-up group still compiles to code that runs.
clau("-O2", "-o", "heavy", heavy)
run = subprocess.run([str(work / "heavy")], capture_output=True, text=True, timeout=60)
assert run.returncode == 0 and run.stdout.endswith("heavy: ok\n"), run
print("inference_budgets: ok")
