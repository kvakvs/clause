"""Check that LLVM merges the atom cache claim checks of one function at -O2: the claim, the runtime identity and the
cached words are invariant loads (cleanup across inlined boundaries, plan 20 OPT1.10)."""
from pathlib import Path
import re
import subprocess
import sys

tool, fixtures = sys.argv[1:]
source = Path(fixtures) / "cleanup.erl"
result = subprocess.run([tool, "-O2", "--print-optimized-ir", str(source)], capture_output=True, text=True,
                        encoding="utf-8", timeout=60)
assert result.returncode == 0, result.stderr
bodies = {}
for match in re.finditer(r"^define internal (?:preserve_nonecc )?void @clausev1_636c65616e7570_([0-9a-f]+)_(\d+)\.body"
                         r".*?^}$", result.stdout, re.MULTILINE | re.DOTALL):
    bodies[f"{bytes.fromhex(match.group(1)).decode('utf-8')}/{match.group(2)}"] = match.group(0)
for name, atoms in (("kind/1", 8), ("count/2", 2)):
    body = bodies[name]
    claims = len(re.findall(r"= load atomic [^\n]*\.atom_cache unordered", body))
    words = len(re.findall(r"%atom\.word\d* = load", body))
    assert claims == 1 and words == atoms, (name, claims, words)
print("cleanup: ok")
