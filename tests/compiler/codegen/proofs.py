"""Check that inferred proofs remove runtime checks at -O2 and that every policy keeps them otherwise (step 59; inline
shape tests since OPT0.2A)."""
from pathlib import Path
import re
import subprocess
import sys

tool, fixture = sys.argv[1:]
source = Path(fixture) / "proofs.erl"


def bodies(*options):
    """The pre-optimization IR body of each function, by its name, under the given options."""
    result = subprocess.run([tool, *options, "--print-ir", str(source)], capture_output=True, text=True,
                            encoding="utf-8", timeout=60)
    assert result.returncode == 0, (options, result.stderr)
    found = {}
    for match in re.finditer(r"^define internal void @clausev1_70726f6f6673_([0-9a-f]+)_(\d+)\.body.*?^}$",
                             result.stdout, re.MULTILINE | re.DOTALL):
        name = bytes.fromhex(match.group(1)).decode("utf-8")
        found[f"{name}/{match.group(2)}"] = match.group(0)
    return found


def services(body):
    """The runtime services a body still calls to check or inspect values."""
    return set(re.findall(r"CLAUSE_(inspect|immediate|exact)_v\d", body))


def checks(body):
    """Runtime checks a body runs: service calls and inline tag, header and word comparisons (OPT0.2A)."""
    return len(re.findall(r"CLAUSE_(?:inspect|immediate|exact)_v\d|= icmp ", body))


proven = bodies("-O2")
generic = bodies("-O2", "--no-type-specialization")
baseline = bodies("-O0")
# Fully proven: no inspection, arithmetic or exact-equality service remains, and no more checks than the generic
# and O0 bodies, which test shapes inline and keep their services (first/1 still tells a cons from []).
for function in ("norm/1", "squares/1", "swap/1", "first/1"):
    assert not services(proven[function]), (function, services(proven[function]))
    assert checks(proven[function]) <= min(checks(generic[function]), checks(baseline[function])), function
    assert function == "first/1" or checks(proven[function]) < checks(generic[function]), function
# Partly proven: the cells are read inline, but arithmetic keeps its generic fallback for unbounded operands.
assert services(proven["sum/2"]) == {"immediate"}, services(proven["sum/2"])
assert "match.tuple.header" in generic["norm/1"] and checks(proven["sum/2"]) < checks(generic["sum/2"])
# The proven operand of edge/1 skips its tag test; the overflowing result keeps the fallback.
assert "immediate" in services(proven["edge/1"]) and proven["edge/1"].count("and i64") < generic["edge/1"].count(
    "and i64")
# Exported: callers are unknown, so nothing is proven and the body is the generic one.
assert proven["scale/2"] == generic["scale/2"]
# Only proven words are read without a shape test.
assert "proven.word" in proven["norm/1"] and "match.tuple.header" not in proven["norm/1"]
print("proofs: removed checks in", sum(1 for name in proven if proven[name] != generic[name]), "functions")
