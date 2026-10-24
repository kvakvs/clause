"""Plan 22 B.0: inference cost of a compiler pass written in Erlang (tests/fixtures/inference/budgets/walkers.erl:
a state record of 18 fields threaded through mutually recursive walkers that return unions of many record shapes).

The budgets below are deterministic counters from `--print-stage-times`, never time: passes until inference settles,
work steps and lattice joins over all passes, and the solves, rounds and member walks of the walker group. They sit
about a quarter above the measured values; a change that needs more must explain why, one that needs far less should
lower them. The compiled program must still run.
"""
import re
import subprocess
import sys
from pathlib import Path

tool, fixture, directory = sys.argv[1:]
work = Path(directory)
work.mkdir(parents=True, exist_ok=True)

BUDGETS = {
    "passes": 16,
    "work steps": 480_000,
    "lattice joins": 6_200_000,
    "group solves": 14,
    "group rounds": 70,
    "group member walks": 300,
    "group work steps": 150_000,
}

result = subprocess.run([tool, "--no-cache", "--print-stage-times", "-O2", "-o", "walkers", fixture], cwd=work,
                        capture_output=True, text=True, timeout=300)
assert result.returncode == 0, result.stderr[-3000:]
timed = result.stderr

passes = re.findall(r"\[time\] inference pass (\d+) [^\n]*\((\d+) of \d+ work steps, (\w+)[^\n]*?(\d+) lattice joins",
                    timed)
assert passes and passes[-1][2] == "settled", timed[-3000:]
group = re.search(r"\[time\] inference rounds walkers:used_vars/2 and 6 more: [^(]*\((\d+) solves, (\d+) rounds "
                  r"[^;]*0 capped[^;]*; (\d+) member walks", timed)
assert group, timed[-3000:]
group_work = re.search(r"\[time\] inference slowest walkers:used_vars/2 and 6 more: [^(]*\((\d+) work steps", timed)
assert group_work, timed[-3000:]

measured = {
    "passes": len(passes),
    "work steps": sum(int(steps) for _, steps, _, _ in passes),
    "lattice joins": sum(int(joins) for _, _, _, joins in passes),
    "group solves": int(group.group(1)),
    "group rounds": int(group.group(2)),
    "group member walks": int(group.group(3)),
    "group work steps": int(group_work.group(1)),
}
print(", ".join(f"{name} {value}" for name, value in measured.items()))
over = [f"{name} {measured[name]} > {limit}" for name, limit in BUDGETS.items() if measured[name] > limit]
assert not over, over

run = subprocess.run([str(work / "walkers")], capture_output=True, text=True, timeout=60)
assert run.returncode == 0 and run.stdout == "[main,helper]\nwalkers: ok\n", run
print("inference_walkers: ok")
