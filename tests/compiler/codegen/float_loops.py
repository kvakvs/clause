"""Check which functions of the float_loops executable case get a variant taking float parameters as raw doubles
(float loops, plan 20 OPT1.2 and OPT1.11); the executable case checks their results against OTP."""
from pathlib import Path
import re
import subprocess
import sys

tool, fixture = sys.argv[1:]
source = Path(fixture) / "float_loops.erl"
result = subprocess.run([tool, "-O2", "--print-ir", str(source)], capture_output=True, text=True, encoding="utf-8",
                        timeout=60)
assert result.returncode == 0, result.stderr
# A variant that re-enters its function shares one merged body with it (a tail-call cycle), so look for the symbol.
variants = {f"{bytes.fromhex(name).decode('utf-8')}/{arity}"
            for name, arity in re.findall(r"@clausev1_666c6f61745f6c6f6f7073_([0-9a-f]+)_(\d+)\.float\b", result.stdout)}
looping = {"decay/2", "halve/2", "sum/2", "mean/3", "pair/3", "reset/2", "literal/2", "until/1", "roots/2",
           "handoff/2", "garbage/3", "calling/2", "failing/2", "signs/3", "wide/8",
           # Wider float loops (OPT1.11): unary minus, float/1, is_float/1, comparisons with other words, float
           # variables and loops entered with float cells.
           "grow/3", "alternate/3", "clamp/3", "escape/5", "branches/2", "entered/2", "mixed/4",
           # Parameters inference proves floats (OPT1.11 remainder): swap/3's first one is passed the second one,
           # overflow/2's and horner/3's floats come from their callers.
           "swap/3", "overflow/2", "horner/3", "integrate/4"}
# A parameter kept in a list, captured by a fun, passed on as another parameter, or never given a float-only value
# keeps a new float per step.
# Calls of other functions enter the variants with doubles (OPT1.11 remainder).
for caller, callee in (("integrate", "horner"), ("handed", "decay"), ("collect", "garbage")):
    body = re.search(r"define [^@]*@clausev1_666c6f61745f6c6f6f7073_" + caller.encode().hex() + r"_\d+\.body\(.*?\n}",
                     result.stdout, re.S)
    assert body and re.search(callee.encode().hex() + r"_\d+\.float", body.group(0)), caller
assert variants == looping, sorted(variants ^ looping)
print("float_loops: ok")
