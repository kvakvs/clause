"""Runner self-check: a wrong golden fails with a readable diff, also when the kept compile cache replays the compile;
an edited source recompiles; a stale golden fails before linking."""
import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import cases

RUNNER = pathlib.Path(__file__).resolve().parent / 'run.py'
# The two fast-mode policies whose compiles the cache checks follow (plan step VAL8).
POLICIES = ('O0-positional', 'O2-project')
# What a wrong golden's report must hold.
WRONG_GOLDEN = ['FAIL broken [O0-positional] args=[]', '--- expected stdout (golden)', '+++ actual stdout',
                '\n    -43\n    +42\n']


def broken_case(work):
    """Copies the demo sources beside a regenerated-looking golden whose first stdout line is wrong."""
    demo = cases.CASES / 'demo'
    golden = cases.load(demo)
    case_dir = work / 'broken'
    shutil.rmtree(case_dir, ignore_errors=True)
    case_dir.mkdir(parents=True)
    for path in cases.sources(demo, golden).values():
        shutil.copyfile(path, case_dir / path.name)
    del golden['sources']
    golden['inputs'] = cases.inputs(case_dir, golden)
    golden['runs'][0]['stdout'] = golden['runs'][0]['stdout'].replace('42\n', '43\n', 1)
    (case_dir / cases.GOLDEN).write_bytes(json.dumps(golden, indent=2).encode())
    return case_dir


def edit_source(case_dir, regenerate):
    """Appends a comment to a source; `regenerate` also refreshes the golden's input hashes, as regenerating would."""
    with (case_dir / 'answer.erl').open('ab') as source:
        source.write(b'%% edited after generation\n')
    if regenerate:
        golden = cases.load(case_dir)
        golden['inputs'] = cases.inputs(case_dir, golden)
        (case_dir / cases.GOLDEN).write_bytes(json.dumps(golden, indent=2).encode())


def run_runner(tool, work, case_dir, suffix):
    """Runs the real runner in fast mode (two policies, kept compile cache) with its compile time lines; returns its
    exit status and output."""
    environment = dict(os.environ, CLAUSE_TEST_MODE='fast', CLAUSE_TEST_COMPILE_TIMES='1')
    result = subprocess.run([sys.executable, str(RUNNER), tool, str(work), str(case_dir), f'--suffix={suffix}'],
                            capture_output=True, text=True, env=environment, timeout=600, check=False)
    return result.returncode, result.stdout + result.stderr


def expect(name, status, output, fragments):
    """Requires a runner failure whose output contains every fragment."""
    print(f'--- {name}: runner exit {status} ---\n{output}')
    missing = [fragment for fragment in fragments if fragment not in output]
    if status != 1 or missing:
        sys.exit(f'{name}: expected runner exit 1 with output containing {missing}')


def expect_cached(name, output, cached):
    """Requires each policy's compile line to show a replay from the kept cache (`cached`) or a fresh compile."""
    for policy in POLICIES:
        lines = [line for line in output.splitlines() if line.startswith(f'compile broken [{policy}] [time] compile ')]
        if len(lines) != 1 or lines[0].endswith('(cached)') != cached:
            sys.exit(f'{name}: expected one {"cached" if cached else "fresh"} compile line for {policy}, got {lines}')


def main():
    """Checks the failure reports and the kept compile cache against a disposable copy of the demo case."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tool', help='clau executable')
    parser.add_argument('work', type=pathlib.Path, help='scratch directory')
    parser.add_argument('--suffix', default='', help='host executable suffix appended by the linker')
    options = parser.parse_args()
    case_dir = broken_case(options.work)
    work = options.work / 'run'
    shutil.rmtree(work.with_name('run-cache'), ignore_errors=True)
    status, output = run_runner(options.tool, work, case_dir, options.suffix)
    expect('wrong golden', status, output, WRONG_GOLDEN)
    expect_cached('wrong golden', output, False)
    status, output = run_runner(options.tool, work, case_dir, options.suffix)
    expect('wrong golden, warm cache', status, output, WRONG_GOLDEN)
    expect_cached('wrong golden, warm cache', output, True)
    edit_source(case_dir, regenerate=True)
    status, output = run_runner(options.tool, work, case_dir, options.suffix)
    expect('edited source', status, output, WRONG_GOLDEN)
    expect_cached('edited source', output, False)
    edit_source(case_dir, regenerate=False)
    status, output = run_runner(options.tool, work, case_dir, options.suffix)
    expect('stale golden', status, output, ['broken: stale golden: sources changed since OTP generation'])
    print('executables_selfcheck: ok')


if __name__ == '__main__':
    main()
