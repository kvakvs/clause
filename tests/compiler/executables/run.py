"""Link one executable golden case, or one program fixture (tests/fixtures/programs), under every test policy, run it
and compare with its OTP golden."""
import argparse
import concurrent.futures
import difflib
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import cases

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'patternmatch'))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'programs'))
import fixtures as programs  # noqa: E402  (program fixtures and their goldens)
import matrix  # noqa: E402  (shared fast/full policy selection)

# Policy combinations linked and run at once; each is independent, so a case takes about one combination's time.
COMBINATION_JOBS = 4
# Scheduler counts every program fixture runs with (plan step 58: one and several workers).
PROGRAM_WORKERS = [1, 4]
# The link-time optimization combinations (--lto): bitcode linked by LLD, through both drivers.
LTO_POLICIES = [('O2', '--lto', 'O2-lto', False), ('O2', '--lto', 'O2-lto', True)]
# The word width of this host's executables; runs of goldens that hold on one width only ('word_bits') follow it.
HOST_WORD_BITS = struct.calcsize('P') * 8
# This host's operating system family; runs of goldens that hold on one family only ('os') follow it (plan 21 step
# LIB55B: win32reg answers differently on Windows).
HOST_OS = 'windows' if sys.platform == 'win32' else 'unix'
# Seconds one golden invocation may run; --run-timeout raises it for emulated targets, and CLAUSE_TEST_TIMEOUT_SCALE
# multiplies the default for slower instrumented builds (sanitizers).
RUN_TIMEOUT = 60 * int(os.environ.get('CLAUSE_TEST_TIMEOUT_SCALE', '1'))
# What a program fixture's copy leaves out: its golden and its compile diagnostics.
PROGRAM_EXTRAS = ('expected', 'compile.txt')


def manifest(case, entry, output):
    """Schema-1 project manifest that links the staged sources as one executable target."""
    return (f'schema_version = 1\n\n[[targets]]\nname = "{case}"\nsource_dirs = ["src"]\n'
            f'entry = "{entry}"\noutput = "{output}"\n')


def command(tool, work, case, golden, policy, target_options=()):
    """Compiler invocation for one policy; returns its label and argument list. A program fixture's project build uses
    its own manifest, with the entry and output given on the command line. `target_options` select another target
    (--target-triple, --runtime-library, --linker)."""
    level, extra, name, project = policy
    label = f'{name}-{"project" if project else "positional"}'
    options = [f'-{level}', *([extra] if extra else []), *target_options]
    output = f'{label}/{case}'
    if project and 'manifest' not in golden:
        (work / f'{label}.toml').write_bytes(manifest(case, golden['entry'], output).encode())
        return label, [tool, '--project', f'{label}.toml', *options]
    (work / label).mkdir()
    if project:
        return label, [tool, '--project', golden['manifest'], *options, '--entry', golden['entry'], '-o', output]
    inputs = sorted(path.relative_to(work).as_posix() for path in (work / 'src').rglob('*.erl'))
    return label, [tool, *options, '--entry', golden['entry'], '-o', output, *inputs]


def text(data):
    """Decodes captured output with LF line ends."""
    return data.replace(b'\r\n', b'\n').decode('utf8', errors='replace')


def stdout_problem(expected, actual):
    """Describes a stdout mismatch as a unified diff, or None when the text is identical."""
    if expected == actual:
        return None
    lines = list(difflib.unified_diff(expected.splitlines(), actual.splitlines(),
                                      'expected stdout (golden)', 'actual stdout', lineterm=''))
    if not lines:
        return f'stdout differs only in line breaks: expected {expected!r}, got {actual!r}'
    return 'stdout differs:\n' + '\n'.join(lines)


def environment(run):
    """The host environment without inherited runtime flags, plus the Python that runs helper programs
    (CLAUSE_TEST_PYTHON), the logger's default handler on standard error as the OTP oracle has it
    (CLAUSE_LOGGER_TYPE) and the run's own 'env' entries."""
    inherited = {name: value for name, value in os.environ.items() if name.upper() != 'CLAUSE_FLAGS'}
    oracle = {'CLAUSE_TEST_PYTHON': sys.executable, 'CLAUSE_LOGGER_TYPE': 'standard_error'}
    return inherited | oracle | run.get('env', {})


def variants(golden, word_bits):
    """Every run for targets of `word_bits`, once per scheduler count the golden lists under 'workers', else once with
    the default count."""
    for run in golden['runs']:
        if run.get('word_bits', word_bits) != word_bits or run.get('os', HOST_OS) != HOST_OS:
            continue
        if 'workers' not in golden:
            yield run, ''
        for count in golden.get('workers', []):
            flags = ' '.join(filter(None, [run.get('env', {}).get('CLAUSE_FLAGS'), f'--schedulers {count}']))
            yield run | {'env': run.get('env', {}) | {'CLAUSE_FLAGS': flags}}, f' workers={count}'


def compare(executable, run, directory):
    """Runs one golden invocation in `directory`; returns readable mismatch descriptions (empty when it matches)."""
    try:
        result = subprocess.run([executable, *run['args']], capture_output=True, timeout=RUN_TIMEOUT, check=False,
                                env=environment(run), input=run.get('stdin', '').encode(), cwd=directory)
    except subprocess.TimeoutExpired:
        return [f'timed out after {RUN_TIMEOUT} s']
    stdout, stderr = text(result.stdout), text(result.stderr)
    problems = []
    if result.returncode != run['exit_status']:
        problems.append(f'exit status: expected {run["exit_status"]}, got {result.returncode}')
    if problem := stdout_problem(run['stdout'], stdout):
        problems.append(problem)
    if not re.search(run['stderr'], stderr):
        problems.append(f'stderr does not match /{run["stderr"]}/')
    if problems and stderr:
        problems.append('stderr:\n' + stderr.rstrip('\n'))
    return problems


def check_policy(tool, work, case, golden, policy, suffix, target_options=(), word_bits=HOST_WORD_BITS):
    """Links the case under one policy and runs every golden invocation; returns the failure count and the report
    lines, printed by the caller so concurrent combinations do not interleave."""
    label, arguments = command(tool, work, case, golden, policy, target_options)
    result = subprocess.run(arguments, cwd=work, capture_output=True, timeout=300, check=False)
    if result.returncode != 0:
        return 1, [f'FAIL {case} [{label}]: compiler exited {result.returncode}\n{text(result.stderr)}']
    executable = work / f'{label}/{case}{suffix}'
    # Each combination runs in its own directory beside its executable, with the case's data files, so programs that
    # write files do not meet each other.
    for path in (work / 'src').iterdir():
        if path.is_file() and path.suffix not in ('.erl', '.hrl'):
            shutil.copyfile(path, work / label / path.name)
    failures, lines = 0, []
    for run, variant in variants(golden, word_bits):
        problems = compare(executable, run, work / label)
        status = 'FAIL' if problems else 'ok'
        lines.append(f'{status} {case} [{label}] args={json.dumps(run["args"])}{variant}')
        lines.extend('    ' + problem.replace('\n', '\n    ') for problem in problems)
        failures += bool(problems)
    return failures, lines


def program_golden(case_dir):
    """A program fixture as a golden: its entry and argv and OTP's exit status and stdout, built through its own
    project.toml; stderr is not compared (tests/fixtures/programs/README.md)."""
    manifest_data, stdout = programs.verify(case_dir.name)
    spec = programs.spec(case_dir.name)
    run = {'args': spec['args'], 'stderr': '', 'exit_status': manifest_data['exit_status'],
           'stdout': stdout.decode('utf8')}
    return {'schema': 1, 'entry': spec['entry'], 'workers': PROGRAM_WORKERS, 'manifest': 'project.toml',
            'runs': [run]}


def prepare(case_dir, work):
    """Recreates `work` holding the case's inputs; returns the case's golden."""
    shutil.rmtree(work, ignore_errors=True)
    if (case_dir / 'fixture.json').exists():
        golden = program_golden(case_dir)
        shutil.copytree(case_dir, work, ignore=shutil.ignore_patterns(*PROGRAM_EXTRAS))
        return golden
    golden = cases.verify(case_dir)
    cases.stage(case_dir, golden, work / 'src')
    return golden


def main():
    """Checks one case or program directory against its golden under the fast or full policy matrix."""
    global RUN_TIMEOUT  # pylint: disable=global-statement
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tool', help='clau executable')
    parser.add_argument('work', type=pathlib.Path, help='scratch directory, recreated')
    parser.add_argument('case', type=pathlib.Path,
                        help='case directory holding golden.json, or program fixture directory holding fixture.json')
    parser.add_argument('--suffix', default='', help='host executable suffix appended by the linker')
    parser.add_argument('--lto', action='store_true',
                        help='link only with -O2 --lto, positional and project (plan step 62), instead of the matrix')
    parser.add_argument('--target-option', action='append', default=[], dest='target_options', metavar='OPTION',
                        help='one compiler argument for another target, repeatable in order, e.g. '
                             '--target-option=--target-triple --target-option=T (docs/validation.md#other-targets)')
    parser.add_argument('--run-timeout', type=int, default=RUN_TIMEOUT,
                        help='seconds each run may take (an emulated target runs many times slower)')
    parser.add_argument('--word-bits', type=int, choices=(32, 64), default=HOST_WORD_BITS,
                        help="the target's word width: runs of the other width only are skipped")
    options = parser.parse_args()
    RUN_TIMEOUT = options.run_timeout
    case_dir, work, tool = options.case.resolve(), options.work.resolve(), str(pathlib.Path(options.tool).resolve())
    golden = prepare(case_dir, work)
    other = 64 if options.word_bits == 32 else 32
    if skipped := [run['args'] for run in golden['runs'] if run.get('word_bits', options.word_bits) == other]:
        print(f'skip {case_dir.name}: {len(skipped)} run(s) hold on {other}-bit targets only: {skipped}')
    policies = LTO_POLICIES if options.lto else matrix.combinations()
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(COMBINATION_JOBS, len(policies))) as pool:
        outcomes = list(pool.map(lambda policy: check_policy(tool, work, case_dir.name, golden, policy, options.suffix,
                                                             options.target_options, options.word_bits), policies))
    failures = 0
    for count, lines in outcomes:
        print('\n'.join(lines))
        failures += count
    print(f'{case_dir.name}: {failures} failure(s)')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
