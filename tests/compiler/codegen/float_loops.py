"""Check which functions of the float_loops executable case get a variant taking float parameters as raw doubles
(float loops, plan 20 OPT1.2); the executable case checks their results against OTP."""
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
           "handoff/2", "garbage/3", "calling/2", "failing/2", "signs/3", "wide/8"}
# A parameter kept in a list, captured by a fun, passed on as another parameter, or never given a float-only value
# keeps a new float per step.
assert variants == looping, sorted(variants ^ looping)
print("float_loops: ok")
