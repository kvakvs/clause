#!/usr/bin/env python3
"""Compile-time benchmarks of clau (plan 22, step B.1); manual, never part of the test gate.

Runs one workload several times with a given clau under --print-stage-times --no-cache, parses the [time] lines
(phases, inference passes, slowest components, recursive-component rounds and any later inference detail line) and
writes JSON plus a text summary. With --baseline, baseline and candidate runs alternate (A B A B ...) and the
summary gives the candidate/baseline ratio per phase and per pass. Runs refuse to start while a build, a test run or
another clau is running, because their timings would be skewed (--force-noisy times anyway and marks the results).

Examples:
  compile_times.py --workload W6
  compile_times.py --runs 3 -- --project <scratch>/b2s/b2s.toml --print-types
  compile_times.py --workload W1 --baseline build/bench/bin/Release/clau-base.exe
"""

import argparse
import datetime
import hashlib
import json
import platform
import re
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from run import RUN_DIR, benchmark_cpus, cpus_description, run_program

BENCH_DIR = Path(__file__).resolve().parent
REPO = BENCH_DIR.parent
OUTPUT_DIR = REPO / "build" / "bench" / "compile-times"
DEFAULT_CLAU = REPO / "build" / "bench" / "bin" / "Release" / "clau.exe"
# Workloads of plan 22 that live in the repository, as (clau arguments, timed runs). `{out}` becomes an empty
# directory per run. W5a and W5r are scratch projects: pass their arguments after `--`.
WORKLOADS = {
    "W1": (["--project", "test-luerl-euler.toml", "--entry", "euler:run", "--print-types"], 3),
    "W2": (["--project", "test-luerl-euler.toml", "--entry", "euler:run", "-O2", "-o", "{out}/euler.exe"], 3),
    "W3": (["--project", "test-luerl-benchmark.toml", "--entry", "benchmarks:run/0", "--print-types"], 5),
    "W4": (["--project", "test-emqx.toml", "--print-types"], 3),
    "W6": (["--project", "test-inference.toml", "--print-types"], 5),
}
# Processes whose CPU and disk use would skew a timing: compilers, linkers, build and test drivers, other clau runs
# and OTP. Names starting with "clang" (clang, clang++, clang-cl, clang-tidy) count too.
BUSY_PROCESSES = {"cl.exe", "ninja.exe", "link.exe", "lld-link.exe", "clau.exe", "ctest.exe", "msbuild.exe",
                  "erl.exe", "beam.smp.exe"}
TIME_LINE = re.compile(r"^\[time\] (.+?): ([0-9.]+) ms(?: \((.*)\))?$")
# An inference detail line: `inference <kind> <rest>`, unlike the phase line `inference target ...`/`inference N
# modules`.
DETAIL = re.compile(r"^inference ([a-z][a-z-]*) (.*)$")
PASS = re.compile(r"^(\d+) (.*)$")
# Work steps, budget, outcome, then what gathering inputs changed (added after the first semicolon).
PASS_NOTE = re.compile(r"^(\d+) of (\d+) work steps, ([^;]*)(?:; (.*))?$")
ROUNDS_NOTE = re.compile(r"^(\d+) solves, (\d+) rounds .*?; (\d+) member walks \((\d+) skipped\)")
POLL_SECONDS = 5
# How many of the slowest inference components the summary lists.
SLOWEST = 10


def parse_args():
    """Command-line options; clau arguments follow `--` unless --workload names them."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--clau", default=str(DEFAULT_CLAU), help="candidate clau (default: build/bench Release)")
    parser.add_argument("--baseline", help="baseline clau, run interleaved with the candidate; keep it next to a "
                                           "clau.exe in build/*/bin so it finds the library sources")
    parser.add_argument("--workload", choices=sorted(WORKLOADS), help="a plan 22 workload kept in the repository")
    parser.add_argument("--label", help="name of the results (default: the workload, else 'custom')")
    parser.add_argument("--runs", type=int, help="timed runs per clau (default: 5, or the workload's count)")
    parser.add_argument("--warmup", type=int, default=1, help="discarded runs per clau before timing")
    parser.add_argument("--timeout", type=int, default=3600, help="seconds allowed for one run")
    parser.add_argument("--cpus", default="performance", help="CPUs clau runs on, as for run.py (default: P-cores)")
    parser.add_argument("--force-noisy", action="store_true", help="time even while builds or other clau runs")
    parser.add_argument("--wait", type=int, default=0, help="seconds to wait for a quiet machine before refusing")
    parser.add_argument("--json", help="output file (default: build/bench/compile-times/<label>-<time>.json)")
    parser.add_argument("--save-output", help="directory for the stdout of each clau's first timed run")
    parser.add_argument("--no-stage-times", action="store_true",
                        help="time whole runs only, without --print-stage-times (instrumentation overhead checks)")
    parser.add_argument("clau_args", nargs=argparse.REMAINDER, help="-- then the clau arguments")
    args = parser.parse_args()
    args.clau_args = args.clau_args[1:] if args.clau_args[:1] == ["--"] else args.clau_args
    if bool(args.workload) == bool(args.clau_args):
        parser.error("give either --workload or clau arguments after --")
    return args


def workload_command(args):
    """The clau arguments, timed run count and label of the requested workload."""
    if args.workload:
        clau_args, runs = WORKLOADS[args.workload]
    else:
        clau_args, runs = args.clau_args, 5
    # Every run compiles from scratch and, unless --no-stage-times, reports its stage times.
    clau_args = [arg for arg in clau_args if not (args.no_stage_times and arg == "--print-stage-times")]
    flags = ("--no-cache",) if args.no_stage_times else ("--no-cache", "--print-stage-times")
    extra = [flag for flag in flags if flag not in clau_args]
    return clau_args + extra, args.runs or runs, args.label or args.workload or "custom"


def busy_processes():
    """Names of running processes that would skew a timing, with their counts."""
    listing = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
    counts = {}
    for line in listing.splitlines():
        name = line.split(",", 1)[0].strip('"').lower()
        if name in BUSY_PROCESSES or name.startswith("clang"):
            counts[name] = counts.get(name, 0) + 1
    return counts


def wait_until_quiet(args):
    """Wait up to --wait seconds for a quiet machine; returns the busy processes left (empty when quiet).

    Refuses (exits) while they run, unless --force-noisy is given.
    """
    deadline = time.monotonic() + args.wait
    busy = busy_processes()
    while busy and time.monotonic() < deadline:
        time.sleep(POLL_SECONDS)
        busy = busy_processes()
    if busy and not args.force_noisy:
        names = ", ".join(f"{name} x{count}" for name, count in sorted(busy.items()))
        sys.exit(f"compile_times: refusing to time while {names} run (--force-noisy times anyway)")
    return busy


def parse_note_numbers(pattern, note, names):
    """The integers a note pattern captures, by name; empty when the note has another shape."""
    match = pattern.match(note or "")
    return {name: int(value) for name, value in zip(names, match.groups())} if match else {}


def parse_detail(kind, rest, ms, note):
    """One inference detail line (pass, slowest, rounds or a later kind) as a record."""
    record = {"kind": kind, "ms": ms, "note": note}
    if kind == "pass":
        number, subject = PASS.match(rest).groups()
        record.update(number=int(number), subject=subject)
        match = PASS_NOTE.match(note or "")
        if match:
            record.update(work=int(match[1]), budget=int(match[2]), outcome=match[3], inputs=match[4])
    else:
        record["subject"] = rest
        if kind == "rounds":
            record.update(parse_note_numbers(ROUNDS_NOTE, note, ("solves", "rounds", "walks", "skipped")))
    return record


def parse_times(stderr):
    """Phase times (summed over subjects, such as the parse time of every file) and inference detail records."""
    phases, details = {}, []
    for line in stderr.splitlines():
        match = TIME_LINE.match(line)
        if not match:
            continue
        head, ms, note = match[1], float(match[2]), match[3]
        detail = DETAIL.match(head)
        if detail and detail[1] != "target":
            details.append(parse_detail(detail[1], detail[2], ms, note))
        else:
            stage = head.split(" ", 1)[0]
            phases[stage] = phases.get(stage, 0.0) + ms
    return phases, details


def counters(details):
    """The deterministic part of a run: work steps and outcome of every pass."""
    return [(d["subject"], d["number"], d.get("work"), d.get("outcome"), d.get("inputs"))
            for d in details if d["kind"] == "pass"]


def run_clau(clau, clau_args, args, cpus):
    """One clau run on `cpus`; returns its record, or exits when clau fails."""
    busy = wait_until_quiet(args)
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="compile-", dir=RUN_DIR) as out:
        command = [clau] + [arg.replace("{out}", out) for arg in clau_args]
        code, stdout, stderr, seconds, _ = run_program(command, None, args.timeout, cpus)
    if code != 0:
        tail = "\n".join(stderr.splitlines()[-20:])
        sys.exit(f"compile_times: {clau} exited with {code}:\n{tail}")
    phases, details = parse_times(stderr)
    phases["wall"] = seconds * 1000.0
    return {"noisy": sorted(busy), "phases": phases, "details": details,
            "stdout_sha256": hashlib.sha256(stdout.encode()).hexdigest()}, stdout


def measure(systems, clau_args, args, runs, cpus):
    """Warm-up, then `runs` timed runs per system, alternating systems; returns the timed records per system."""
    for _ in range(args.warmup):
        for name, clau in systems.items():
            print(f"compile_times: warm-up {name}", flush=True)
            run_clau(clau, clau_args, args, cpus)
    records = {name: [] for name in systems}
    for index in range(runs):
        for name, clau in systems.items():
            record, stdout = run_clau(clau, clau_args, args, cpus)
            print(f"compile_times: {name} run {index + 1}/{runs}: {record['phases']['wall'] / 1000:.1f} s"
                  f"{' (noisy)' if record['noisy'] else ''}", flush=True)
            if index == 0 and args.save_output:
                Path(args.save_output).mkdir(parents=True, exist_ok=True)
                (Path(args.save_output) / f"{name}.txt").write_bytes(stdout.encode())
            records[name].append(record)
    return records


def stats(values):
    """Median, min, max and spread ((max - min) / median, in percent) of some times."""
    median = statistics.median(values)
    spread = 100.0 * (max(values) - min(values)) / median if median else 0.0
    return {"median": median, "min": min(values), "max": max(values), "spread": spread}


def slowest_components(records):
    """The SLOWEST components by median time over the runs that list them, with their rounds when recursive."""
    times, rounds = {}, {}
    for record in records:
        for detail in record["details"]:
            if detail["kind"] == "slowest":
                times.setdefault(detail["subject"], []).append(detail["ms"])
            elif detail["kind"] == "rounds" and "rounds" in detail:
                rounds[detail["subject"]] = {key: detail[key] for key in ("solves", "rounds", "walks", "skipped")}
    medians = sorted(((statistics.median(values), subject) for subject, values in times.items()), reverse=True)
    return [{"component": subject, "median": median, **rounds.get(subject, {})}
            for median, subject in medians[:SLOWEST]]


def summarize(records):
    """Statistics per phase and per pass of one system's runs, and whether their counters and outputs agree."""
    phases = {}
    for stage in records[0]["phases"]:
        phases[stage] = stats([record["phases"].get(stage, 0.0) for record in records])
    passes = []
    for index, detail in enumerate(d for d in records[0]["details"] if d["kind"] == "pass"):
        times = [[d for d in record["details"] if d["kind"] == "pass"][index]["ms"] for record in records]
        passes.append({**{key: detail.get(key) for key in ("subject", "number", "work", "outcome")}, **stats(times)})
    return {"phases": phases, "passes": passes, "slowest": slowest_components(records),
            "counters_agree": all(counters(r["details"]) == counters(records[0]["details"]) for r in records),
            "outputs_agree": len({record["stdout_sha256"] for record in records}) == 1,
            "noisy_runs": sum(1 for record in records if record["noisy"])}


def ratio(value, reference):
    """value / reference as text, or '-' without a reference."""
    return f"{value / reference:.3f}" if reference else "-"


def print_phases(summaries, names):
    """One row per phase: median, min and spread of each system, then the candidate/baseline ratio."""
    header = "phase".ljust(16) + "".join(f"{name + ' median':>18}{'min':>12}{'spread':>8}" for name in names)
    print(header + ("   ratio" if len(names) == 2 else ""))
    for stage in summaries[names[0]]["phases"]:
        row = stage.ljust(16)
        medians = []
        for name in names:
            entry = summaries[name]["phases"].get(stage)
            medians.append(entry["median"] if entry else 0.0)
            row += f"{entry['median']:>15.1f} ms{entry['min']:>9.1f} ms{entry['spread']:>7.1f}%" if entry else ""
        print(row + (f"   {ratio(medians[0], medians[1])}" if len(names) == 2 else ""))


def print_passes(summaries, names):
    """One row per inference pass: median time and work steps of each system, then the time ratio."""
    passes = [summaries[name]["passes"] for name in names]
    for index in range(max(len(entries) for entries in passes)):
        row = "pass".ljust(6)
        for entries in passes:
            entry = entries[index] if index < len(entries) else None
            row += f"{entry['number']:>3}{entry['median']:>12.1f} ms{entry['work'] or 0:>11} steps" if entry else ""
        both = all(index < len(entries) for entries in passes)
        print(row + (f"   {ratio(passes[0][index]['median'], passes[1][index]['median'])}"
                     if len(names) == 2 and both else ""))
    for name, entries in zip(names, passes):
        if entries:
            print(f"{name}: {len(entries)} passes, last ended with: {entries[-1]['outcome']}")


def print_slowest(summaries, names):
    """The candidate's slowest inference components, with the baseline's median for each."""
    reference = {entry["component"]: entry["median"] for entry in summaries[names[-1]]["slowest"]}
    for entry in summaries[names[0]]["slowest"]:
        rounds = f", {entry['solves']} solves, {entry['rounds']} rounds, {entry['walks']} walks" \
            if "rounds" in entry else ""
        base = f" (baseline {reference[entry['component']]:.1f} ms)" \
            if len(names) == 2 and entry["component"] in reference else ""
        print(f"slowest {entry['median']:>10.1f} ms {entry['component']}{rounds}{base}")


def print_checks(summaries, records, names):
    """Warnings for counters or outputs that differ between runs, and the output comparison of the two systems."""
    for name in names:
        summary = summaries[name]
        if not summary["counters_agree"]:
            print(f"WARNING: {name} pass work steps or outcomes differ between runs")
        if not summary["outputs_agree"]:
            print(f"WARNING: {name} stdout differs between runs")
        if summary["noisy_runs"]:
            print(f"WARNING: {name}: {summary['noisy_runs']} runs timed on a busy machine (--force-noisy)")
    if len(names) == 2:
        same = records[names[0]][0]["stdout_sha256"] == records[names[1]][0]["stdout_sha256"]
        print(f"stdout {'identical' if same else 'DIFFERS'} between {names[0]} and {names[1]}")


def git_head():
    """The HEAD commit and whether the worktree has changes."""
    def git(*command):
        return subprocess.run(["git", *command], cwd=REPO, capture_output=True, text=True).stdout.strip()
    return {"commit": git("rev-parse", "HEAD"), "dirty": bool(git("status", "--porcelain", "--untracked-files=no"))}


def main():
    """Measure, summarize and write the JSON results."""
    args = parse_args()
    clau_args, runs, label = workload_command(args)
    systems = {"candidate": str(Path(args.clau).resolve())}
    if args.baseline:
        systems["baseline"] = str(Path(args.baseline).resolve())
    cpus = benchmark_cpus(args.cpus)
    print(f"compile_times: {label}: {' '.join(clau_args)}; {runs} runs on {cpus_description(args.cpus, cpus)}",
          flush=True)
    records = measure(systems, clau_args, args, runs, cpus)
    names = list(systems)
    summaries = {name: summarize(records[name]) for name in names}
    print_phases(summaries, names)
    print_passes(summaries, names)
    print_slowest(summaries, names)
    print_checks(summaries, records, names)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    path = Path(args.json) if args.json else OUTPUT_DIR / f"{label}-{stamp}.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    result = {"label": label, "date": stamp, "host": platform.node().lower(), "git": git_head(),
              "clau_args": clau_args, "runs": runs, "cpus": cpus_description(args.cpus, cpus),
              "systems": systems, "summaries": summaries, "records": records}
    path.write_bytes(json.dumps(result, indent=1).encode())
    print(f"compile_times: wrote {path}")


if __name__ == "__main__":
    main()
