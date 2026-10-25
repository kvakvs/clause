"""Explicitly regenerate, or check, executable case goldens with an installed OTP."""
import argparse
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import cases

# The program-fixture oracle already runs Entry:main(Argv) under the executable contract.
ORACLE = cases.ROOT / 'tests/compiler/programs/oracle.escript'
# The operating system family the oracle runs on; runs for another ('os') are kept as written.
HOST_OS = 'windows' if sys.platform == 'win32' else 'unix'


def reference():
    """Reads the pinned maint-29 revision recorded alongside the oracle version."""
    text = (cases.ROOT / 'references/otp-pin.cmake').read_text(encoding='utf8')
    branch = re.search(r'REFERENCE_BRANCH "([^"]+)"', text).group(1)
    return {'branch': branch, 'revision': re.search(r'REFERENCE_REVISION "([0-9a-f]{40})"', text).group(1)}


def observe(escript, staged, entry, run, work):
    """Runs the entry with one run's argv and standard input under OTP; returns the completed process and the oracle
    version."""
    args = run['args']
    version = work / 'version.txt'
    version.unlink(missing_ok=True)
    environment = os.environ | {'CLAUSE_TEST_PYTHON': sys.executable}
    result = subprocess.run([escript, str(ORACLE), str(version), str(staged), entry, *args],
                            cwd=staged, capture_output=True, timeout=300, check=False,
                            input=run.get('stdin', '').encode(), env=environment)
    if result.returncode == 125 or not version.exists():
        sys.exit(f'{staged.name}: oracle failed: {result.stderr.decode(errors="replace")}')
    return result, version.read_text().split()


def generated_run(case, run, result):
    """Golden run: authored args and stderr pattern plus OTP's exit status and stdout."""
    pattern = run.get('stderr')
    if pattern is None and result.stderr:
        sys.exit(f'{case}: OTP wrote stderr for args {run["args"]}; author a "stderr" pattern for '
                 f'the Clause report:\n{result.stderr.decode(errors="replace")}')
    stdout = result.stdout.replace(b'\r\n', b'\n').decode('utf8')
    authored = {key: run[key] for key in ('stdin', 'word_bits', 'os') if key in run}
    return {'args': run['args'], **authored, 'stderr': pattern or '^$', 'exit_status': result.returncode,
            'stdout': stdout}


def regenerate(escript, case_dir, work):
    """Observes every run of one case; returns the committed golden and the freshly generated one."""
    golden = cases.load(case_dir)
    staged = cases.stage(case_dir, golden, work / case_dir.name)
    runs, version = [], None
    for run in golden['runs']:
        if run.get('authored') or run.get('os', HOST_OS) != HOST_OS:
            # Clause-only behavior, or another operating system's, OTP here cannot show: kept as written.
            runs.append(run)
            continue
        result, version = observe(escript, staged, golden['entry'], run, work)
        runs.append(generated_run(case_dir.name, run, result))
    # Authored top-level fields: listed sources and the scheduler counts every run repeats with.
    listed = {key: golden[key] for key in ('sources', 'data', 'workers') if key in golden}
    return golden, {'schema': 1, 'entry': golden['entry'], **listed, 'runs': runs, 'oracle_version': version,
                    'reference': reference(), 'inputs': cases.inputs(case_dir, golden)}


def refresh(escript, case_dir, work, check):
    """Writes one golden, or in check mode reports whether OTP still reproduces it."""
    golden, fresh = regenerate(escript, case_dir, work)
    if check:
        same = golden == fresh
        print(f'{case_dir.name}: {"reproduced" if same else "DRIFT"} ({len(fresh["runs"])} runs)')
        return same
    (case_dir / cases.GOLDEN).write_bytes((json.dumps(fresh, indent=2, ensure_ascii=False) + '\n').encode())
    print(f'{case_dir.name}: wrote {len(fresh["runs"])} runs')
    return True


def main():
    """Parses options and refreshes or checks every selected case."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--escript', required=True, help='OTP 29 escript executable')
    parser.add_argument('--case', action='append', choices=cases.names(), help='limit to one case')
    parser.add_argument('--check', action='store_true', help='compare with committed goldens, write nothing')
    options = parser.parse_args()
    work_root = cases.ROOT / 'build'
    work_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='executables-oracle-', dir=work_root) as work:
        results = [refresh(options.escript, cases.CASES / name, pathlib.Path(work), options.check)
                   for name in options.case or cases.names()]
    sys.exit(0 if all(results) else 1)


if __name__ == '__main__':
    main()
