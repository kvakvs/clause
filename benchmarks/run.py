#!/usr/bin/env python3
"""Manual benchmarks of Clause against Erlang/OTP (plan step 69A); never part of the test gate.

Builds every benchmarks/bench_*.erl program with OTP (JIT, and the emu flavor when installed),
Clause at HEAD (O2, with and without --lto) and a previous Clause commit, runs them on this host,
prints a text report and keeps a 6-month history per host under benchmarks/history/, one run per day.
With --listings it also writes each program's annotated assembly and IR (clau --print-asm, --print-ir) at O0 and
O2 next to the reports. Kernel times are measured in nanoseconds and reported in milliseconds with six decimals.
"""

import argparse
import datetime
import json
import os
import platform
import shlex
import shutil
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
REPO = BENCH_DIR.parent
HISTORY_DIR = BENCH_DIR / "history"
EXE = ".exe" if os.name == "nt" else ""
# Records run longer ago than this, relative to the newest record, are pruned.
HISTORY_DAYS = 183
# Paths whose changes make the Clause build differ from the HEAD commit.
SOURCE_PATHS = ["abi", "cmake", "compiler", "library", "runtime", "CMakeLists.txt"]
SECONDS_PER_DAY = 86400
# Listing configurations with their optimization level, and the inspection flag and file extension of each listing
# kind; every run replaces the previous run's listings.
LISTING_CONFIGS = (("debug", "-O0"), ("release", "-O2"))
LISTING_KINDS = (("--print-asm", "asm"), ("--print-ir", "ll"))


def parse_args():
    """Command-line options; make-benchmark.bat and `make benchmark` pass the build directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build/bench", help="Release build of clau at HEAD")
    parser.add_argument("--config", default="Release", help="build configuration of clau")
    parser.add_argument("--baseline", help="previous Clause revision (default: newest commit >= 1 day older than HEAD)")
    parser.add_argument("--no-baseline", action="store_true", help="skip the previous Clause version")
    parser.add_argument("--no-lto", action="store_true", help="skip the Clause --lto build")
    parser.add_argument("--repeat", type=int, default=5, help="runs of each program per system")
    parser.add_argument("--timeout", type=int, default=600, help="seconds allowed for one program run")
    parser.add_argument("--only", help="comma-separated program names, for example bench_gc,bench_msg")
    parser.add_argument("--erl", help="erl executable of the host Erlang/OTP")
    parser.add_argument("--host", default=platform.node().lower(), help="host label of the history records")
    parser.add_argument("--jobs", default="", help="parallel jobs for building the baseline")
    parser.add_argument("--no-history", action="store_true", help="do not write history or report files")
    parser.add_argument("--listings", action="store_true", help="also write --print-asm and --print-ir listings")
    return parser.parse_args()


def run_command(args, cwd=None, timeout=None):
    """Run a command and return (exit code, stdout, stderr, seconds); a timeout returns code None."""
    start = datetime.datetime.now()
    try:
        done = subprocess.run(args, cwd=cwd, capture_output=True, text=True, timeout=timeout)
        code, out, err = done.returncode, done.stdout, done.stderr
    except subprocess.TimeoutExpired as expired:
        code, out, err = None, _text(expired.stdout), _text(expired.stderr)
    return code, out, err, (datetime.datetime.now() - start).total_seconds()


def _text(data):
    """Partial output of a timed-out command, which may arrive as bytes."""
    if isinstance(data, bytes):
        return data.decode(errors="replace")
    return data or ""


def git(*args, cwd=REPO):
    """Output of one git command in the repository."""
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def commit_info(rev):
    """Full hash, short hash and ISO commit date of a revision."""
    full, short, date = git("log", "-1", "--format=%H %h %cI", rev).split()
    return {"hash": full, "short": short, "date": date}


def default_baseline():
    """The newest commit whose commit date is at least one day older than HEAD's."""
    head_time = int(git("log", "-1", "--format=%ct", "HEAD"))
    for line in git("log", "--format=%H %ct", "HEAD").splitlines():
        rev, when = line.split()
        if int(when) <= head_time - SECONDS_PER_DAY:
            return rev
    return None


def find_erl(explicit):
    """The host erl executable: --erl, PATH, then the default Windows installation."""
    candidates = [explicit, shutil.which("erl")]
    if os.name == "nt":
        candidates.append(str(Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Erlang OTP/bin/erl.exe"))
    for candidate in candidates:
        if candidate and Path(candidate).exists():
            return str(Path(candidate))
    sys.exit("benchmark: Erlang/OTP was not found; pass --erl PATH")


def erl_eval(erl, flags, expression):
    """Evaluate one expression in a fresh OTP node; returns stdout or None when the node fails."""
    code, out, _, _ = run_command([erl, *flags, "-noshell", "-eval", expression], timeout=120)
    return out.strip() if code == 0 else None


def otp_systems(erl):
    """The OTP systems to compare: the default flavor, plus emu when installed; and OTP version facts."""
    facts = erl_eval(erl, [], 'io:format("~s ~s ~s", [erlang:system_info(otp_release), '
                     'erlang:system_info(version), erlang:system_info(emu_flavor)]), halt().')
    if facts is None:
        sys.exit(f"benchmark: {erl} does not run")
    release, erts, flavor = facts.split()
    systems = {f"otp-{flavor}": []}
    emu = erl_eval(erl, ["-emu_flavor", "emu"], 'io:format("~s", [erlang:system_info(emu_flavor)]), halt().')
    if flavor != "emu" and emu == "emu":
        systems["otp-emu"] = ["-emu_flavor", "emu"]
    return systems, {"release": release, "erts": erts, "flavors": sorted(systems)}


def programs(only):
    """Benchmark program names (modules exporting main/1), optionally filtered by --only."""
    names = sorted(p.stem for p in BENCH_DIR.glob("bench_*.erl") if p.stem != "bench_util")
    if only:
        wanted = set(only.split(","))
        names = [name for name in names if name in wanted]
    return names


def build_otp(erl, out_dir):
    """Compile every benchmark module with erlc next to erl."""
    erlc = Path(erl).with_name("erlc" + EXE)
    out_dir.mkdir(parents=True, exist_ok=True)
    sources = [str(p) for p in BENCH_DIR.glob("bench_*.erl")]
    code, out, err, _ = run_command([str(erlc), "-o", str(out_dir), *sources])
    if code != 0:
        sys.exit(f"benchmark: erlc failed\n{out}{err}")


def build_clause(clau, out_dir, extra):
    """Link one executable per program with clau; returns {program: path or None when it fails}."""
    out_dir.mkdir(parents=True, exist_ok=True)
    built = {}
    for name in programs(None):
        target = out_dir / (name + EXE)
        sources = [str(BENCH_DIR / f"{name}.erl"), str(BENCH_DIR / "bench_util.erl")]
        code, out, err, _ = run_command([str(clau), "-O2", *extra, "-o", str(target), *sources])
        if code != 0:
            print(f"benchmark: {out_dir.name} cannot build {name}:\n{(out + err).strip()[:2000]}", file=sys.stderr)
        built[name] = target if code == 0 else None
    return built


def clau_path(build_dir, config):
    """clau of a build directory, for multi-config and single-config generators."""
    for candidate in (build_dir / "bin" / config / ("clau" + EXE), build_dir / "bin" / ("clau" + EXE)):
        if candidate.exists():
            return candidate
    return None


def link_thirdparty(worktree):
    """Share the main checkout's downloaded dependencies with a baseline worktree."""
    link = worktree / "thirdparty"
    if link.exists() or not (REPO / "thirdparty").exists():
        return
    if os.name == "nt":
        subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(REPO / "thirdparty")], check=True,
                       capture_output=True)
    else:
        link.symlink_to(REPO / "thirdparty", target_is_directory=True)


def build_baseline(info, config, jobs):
    """Build clau and the runtime of a previous commit in a cached worktree; returns clau or None."""
    root = REPO / "build" / "bench-base" / info["short"]
    worktree, build_dir = root / "src", root / "build"
    cached = clau_path(build_dir, config)
    if cached:
        return cached
    if not worktree.exists():
        git("worktree", "add", "--detach", str(worktree), info["hash"])
    link_thirdparty(worktree)
    configure = ["cmake", "-S", str(worktree), "-B", str(build_dir),
                 *shlex.split(os.environ.get("CLAUSE_CONFIGURE_ARGS", "")),
                 f"-DCMAKE_BUILD_TYPE={config}", "-DBUILD_TESTING=OFF",
                 "-DCLAUSE_BUILD_COMPILER=ON", "-DCLAUSE_BUILD_RUNTIME=ON"]
    build = ["cmake", "--build", str(build_dir), "--config", config, "--target", "clau", "clause_runtime",
             "--parallel", *([jobs] if jobs else [])]
    print(f"benchmark: building baseline {info['short']} in {root}", file=sys.stderr)
    for command in (configure, build):
        code, out, err, _ = run_command(command)
        if code != 0:
            print(f"benchmark: baseline build failed:\n{(out + err)[-4000:]}", file=sys.stderr)
            return None
    return clau_path(build_dir, config)


def parse_results(output):
    """Kernel times in nanoseconds from the BENCH lines of one program run."""
    results = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "BENCH":
            results[parts[1]] = int(parts[2])
    return results


def run_once(command, timeout):
    """Run one program in an empty temporary directory; returns (kernel times, wall seconds, status)."""
    with tempfile.TemporaryDirectory(prefix="clause-bench-") as cwd:
        code, out, err, seconds = run_command(command, cwd=cwd, timeout=timeout)
    if code is None:
        status = "timeout"
    elif code != 0:
        status = f"exit {code}: {err.strip()[:300]}"
    else:
        status = "ok"
    return parse_results(out), seconds, status


def commands_for(systems, otp_dir, erl, clause_builds, name):
    """The command running program `name` on each system, or None where the system cannot run it."""
    commands = {}
    for system, flags in systems.items():
        commands[system] = [erl, *flags, "-noshell", "-pa", str(otp_dir), "-eval", f"{name}:main([]), halt()."]
    for system, built in clause_builds.items():
        path = built.get(name)
        commands[system] = [str(path)] if path else None
    return commands


def measure(names, commands_of, repeat, timeout):
    """Run every program `repeat` times on every system, interleaving systems to spread host noise."""
    samples = {}  # (system, kernel) -> [nanoseconds]
    walls = {}  # (system, program) -> [seconds]
    failures = {}  # (system, program) -> last failure status
    for round_index in range(repeat):
        for name in names:
            for system, command in commands_of(name).items():
                if command is None:
                    failures[(system, name)] = "n/a (does not build)"
                    continue
                print(f"benchmark: round {round_index + 1}/{repeat} {system} {name}", file=sys.stderr)
                results, seconds, status = run_once(command, timeout)
                for kernel, nanos in results.items():
                    samples.setdefault((system, kernel), []).append(nanos)
                walls.setdefault((system, name), []).append(seconds)
                if status != "ok":
                    failures[(system, name)] = status
    return samples, walls, failures


def summarize(samples):
    """Median and relative spread ((max - min) / median) of every system and kernel."""
    summary = {}
    for key, values in samples.items():
        median = statistics.median(values)
        spread = (max(values) - min(values)) / median if median else 0.0
        summary[key] = (median, spread)
    return summary


def change(reference, value):
    """Percent change of `value` against `reference`: +N% faster (the reference takes N% longer), -N% slower."""
    if reference is None or value is None or reference <= 0 or value <= 0:
        return "n/a"
    if value <= reference:
        return f"+{(reference / value - 1) * 100:.1f}% ({reference / value:.2f}x)"
    return f"-{(value / reference - 1) * 100:.1f}% ({value / reference:.2f}x)"


def millis(entry):
    """Median milliseconds with spread, or n/a."""
    if entry is None:
        return "n/a"
    return f"{entry[0] / 1e6:.6f} +/-{entry[1] * 100:.0f}%"


def table(header, rows):
    """Left-aligned first column, right-aligned other columns."""
    widths = [max(len(str(row[i])) for row in [header, *rows]) for i in range(len(header))]
    lines = []
    for row in [header, *rows]:
        cells = [str(row[0]).ljust(widths[0])] + [str(c).rjust(w) for c, w in zip(row[1:], widths[1:])]
        lines.append("  ".join(cells).rstrip())
    lines.insert(1, "-" * len(lines[0]))
    return "\n".join(lines)


def geometric_mean_change(summary, kernels, reference, system):
    """Geometric mean of time ratios over the kernels both systems measured, as a percent change."""
    ratios = []
    for kernel in kernels:
        ref, val = summary.get((reference, kernel)), summary.get((system, kernel))
        if ref and val and ref[0] > 0 and val[0] > 0:
            ratios.append(val[0] / ref[0])
    if not ratios:
        return "n/a"
    return change(1.0, statistics.geometric_mean(ratios))


def report_header(context):
    """Report lines naming date, host, OTP and both Clause commits."""
    head, base, otp = context["head"], context.get("base"), context["otp"]
    lines = [
        f"Clause benchmarks, {context['run_date']}",
        f"Host: {context['host']} | {platform.processor() or platform.machine()} | "
        f"{os.cpu_count()} logical CPUs | {platform.system()} {platform.release()} ({platform.version()})",
        f"OTP: {otp['release']}, ERTS {otp['erts']}; flavors found: {', '.join(otp['flavors'])}"
        + ("" if "otp-emu" in otp["flavors"] else " (emu flavor not installed: OTP emu columns are n/a)"),
        f"Clause HEAD: {head['short']} {head['date']}" + (" (with uncommitted source changes)" if head["dirty"] else ""),
        f"Clause baseline: {base['short']} {base['date']}" if base else "Clause baseline: none",
        f"Runs per program and system: {context['repeat']}; times are medians of the kernel time in ms, "
        "+/- (max - min) / median.",
        "Change: +N% = Clause is faster (the other system takes N% longer); "
        "-N% = Clause is slower (Clause takes N% longer); (Fx) is the time ratio.",
    ]
    return "\n".join(lines)


def compare_table(summary, kernels, system, references):
    """Change of `system` against each reference per kernel, with the geometric mean row."""
    header = ["benchmark", *[f"{system} vs {r}" for r in references]]
    rows = [[k, *[change(_median(summary, r, k), _median(summary, system, k)) for r in references]]
            for k in kernels]
    rows.append(["geometric mean", *[geometric_mean_change(summary, kernels, r, system) for r in references]])
    return table(header, rows)


def build_report(context, summary, walls, failures, systems, kernels):
    """The full text report: times, changes, wall times and failures."""
    time_rows = [[k, *[millis(summary.get((s, k))) for s in systems]] for k in kernels]
    references = [s for s in systems if s not in ("clause-o2", "clause-lto")]
    sections = [report_header(context), "Kernel times (ms)", table(["benchmark", *systems], time_rows),
                "Clause HEAD (O2) compared to each system", compare_table(summary, kernels, "clause-o2", references)]
    if "clause-lto" in systems:
        sections += ["Clause --lto compared to OTP and to plain O2",
                     compare_table(summary, kernels, "clause-lto", [references[0], "clause-o2"])]
    sections += ["Program wall times (s, startup included, median)", wall_table(walls, systems)]
    if failures:
        sections += ["Failures", "\n".join(f"{s} {p}: {why}" for (s, p), why in sorted(failures.items()))]
    return "\n\n".join(sections) + "\n"


def _median(summary, system, kernel):
    """Median nanoseconds of one system and kernel, or None."""
    entry = summary.get((system, kernel))
    return entry[0] if entry else None


def wall_table(walls, systems):
    """Median wall seconds per program and system."""
    names = sorted({name for _, name in walls})
    rows = []
    for name in names:
        rows.append([name, *[f"{statistics.median(walls[(s, name)]):.2f}" if (s, name) in walls else "n/a"
                             for s in systems]])
    return table(["program", *systems], rows)


def load_history(path):
    """History records of one host, oldest first."""
    if not path.exists():
        return []
    return json.loads(path.read_text(encoding="utf-8"))


def save_history(path, records):
    """Drop records run more than HISTORY_DAYS before the newest one and write the rest, oldest first."""
    records.sort(key=lambda r: r["run_date"])
    newest = datetime.date.fromisoformat(records[-1]["run_date"][:10])
    cutoff = newest - datetime.timedelta(days=HISTORY_DAYS)
    kept = [r for r in records if datetime.date.fromisoformat(r["run_date"][:10]) >= cutoff]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(kept, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    return kept


def medians_of(summary, system):
    """Kernel medians in microseconds of one system, with nanosecond fractions (the history's unit)."""
    return {k: round(v[0] / 1000, 3) for (s, k), v in summary.items() if s == system}


def history_record(context, summary):
    """One history record: Clause HEAD's kernel medians with the OTP and baseline medians of the same run."""
    head, base = context["head"], context["base"]
    record = {"commit": head["hash"], "commit_date": head["date"], "run_date": context["run_date"],
              "otp_release": context["otp"]["release"], "clause_us": medians_of(summary, "clause-o2"),
              "otp_us": medians_of(summary, context["otp_reference"])}
    if base:
        record["baseline"] = {"commit": base["hash"], "commit_date": base["date"],
                              "clause_us": medians_of(summary, "clause-base")}
    return record


def trend_table(records, kernels):
    """Kernel medians (ms) of Clause O2 across the kept history of this host, newest runs last."""
    shown = records[-8:]
    header = ["benchmark", *[f"{r['run_date'][:10]} {r['commit'][:7]}" for r in shown]]
    rows = [[k, *[f"{r['clause_us'][k] / 1000:.6f}" if k in r["clause_us"] else "n/a" for r in shown]]
            for k in kernels]
    return table(header, rows)


def update_history(context, summary, kernels):
    """Record this run in the host history, replacing a run of the same day; returns the trend section."""
    path = HISTORY_DIR / f"{context['host']}.json"
    day = context["run_date"][:10]
    records = [r for r in load_history(path) if r["run_date"][:10] != day]
    records.append(history_record(context, summary))
    kept = save_history(path, records)
    return "History of Clause O2 on this host (ms, last 8 run days)\n\n" + trend_table(kept, kernels) + "\n"


def report_directory(host):
    """Where the reports and assembly listings of one host are kept."""
    return HISTORY_DIR / "reports" / host


def write_listing(clau, options, sources, path):
    """Write one inspection's stdout to `path`; on failure delete `path` and report clau's error."""
    # Bytes: listings quote UTF-8 Erlang source, which the locale's text decoding may reject.
    done = subprocess.run([str(clau), *options, *sources], capture_output=True, timeout=600)
    if done.returncode == 0:
        path.write_bytes(done.stdout)
        return
    # A stale listing would pass for the current code; drop it.
    path.unlink(missing_ok=True)
    error = done.stderr.decode(errors="replace").strip()[:2000]
    print(f"benchmark: no listing {path.name}:\n{error}", file=sys.stderr)


def write_listings(clau, directory):
    """Write every program's --print-asm and --print-ir listings as <program>.<debug|release>.<asm|ll>.

    Added for benchmark assembly listings: the newest run's code generation, for analysis beside the reports.
    """
    directory.mkdir(parents=True, exist_ok=True)
    for name in programs(None):
        sources = [str(BENCH_DIR / f"{name}.erl"), str(BENCH_DIR / "bench_util.erl")]
        for config, level in LISTING_CONFIGS:
            for flag, extension in LISTING_KINDS:
                # The entry leaves out library code the program cannot reach, as its executable does.
                path = directory / f"{name}.{config}.{extension}"
                write_listing(clau, [level, flag, "--entry", name], sources, path)


def write_report(context, report):
    """Save the report as <run day>-<commit>.txt, replacing reports of the same day, and prune old ones."""
    directory = report_directory(context["host"])
    directory.mkdir(parents=True, exist_ok=True)
    day = context["run_date"][:10]
    for same_day in directory.glob(f"{day}-*.txt"):
        same_day.unlink()
    (directory / f"{day}-{context['head']['short']}.txt").write_text(report, encoding="utf-8")
    prune_reports(directory)


def prepare_clause(args, head_dir):
    """Clause builds of HEAD (O2, optionally --lto) and the baseline; returns builds and baseline info."""
    clau = clau_path(head_dir, args.config)
    if clau is None:
        sys.exit(f"benchmark: clau was not found in {head_dir}; build it first (make-benchmark.bat)")
    out = REPO / "build" / "bench" / "programs"
    builds = {"clause-o2": build_clause(clau, out / "clause-o2", [])}
    if not args.no_lto:
        builds["clause-lto"] = build_clause(clau, out / "clause-lto", ["--lto"])
    base = None
    rev = None if args.no_baseline else (args.baseline or default_baseline())
    if rev:
        base = commit_info(rev)
        base_clau = build_baseline(base, args.config, args.jobs)
        empty = {name: None for name in programs(None)}
        builds["clause-base"] = build_clause(base_clau, out / "clause-base", []) if base_clau else empty
    return builds, base


def main():
    """Build, measure, report and record."""
    args = parse_args()
    erl = find_erl(args.erl)
    otp, otp_facts = otp_systems(erl)
    otp_dir = REPO / "build" / "bench" / "programs" / "otp"
    build_otp(erl, otp_dir)
    head_dir = (REPO / args.build_dir).resolve()
    builds, base = prepare_clause(args, head_dir)
    if args.listings:
        write_listings(clau_path(head_dir, args.config), report_directory(args.host))
    head = commit_info("HEAD")
    head["dirty"] = bool(git("status", "--porcelain", "--", *SOURCE_PATHS))
    names = programs(args.only)
    samples, walls, failures = measure(names, lambda n: commands_for(otp, otp_dir, erl, builds, n),
                                       args.repeat, args.timeout)
    summary = summarize(samples)
    kernels = sorted({k for _, k in summary})
    systems = [*otp, *builds]
    context = {"run_date": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
               "host": args.host, "head": head, "base": base, "otp": otp_facts, "repeat": args.repeat,
               "otp_reference": next(iter(otp))}
    report = build_report(context, summary, walls, failures, systems, kernels)
    if args.no_history or head["dirty"]:
        report += "\nNot recorded in the history (--no-history or uncommitted source changes).\n"
    else:
        report += "\n" + update_history(context, summary, kernels)
        write_report(context, report)
    print(report)


def prune_reports(directory):
    """Delete report files whose run day (file name prefix) is more than HISTORY_DAYS older than the newest."""
    reports = sorted(directory.glob("*.txt"))
    if not reports:
        return
    newest = datetime.date.fromisoformat(reports[-1].name[:10])
    for report in reports:
        if datetime.date.fromisoformat(report.name[:10]) < newest - datetime.timedelta(days=HISTORY_DAYS):
            report.unlink()


if __name__ == "__main__":
    main()
