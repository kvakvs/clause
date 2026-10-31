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
import nodes

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'patternmatch'))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'programs'))
import fixtures as programs  # noqa: E402  (program fixtures and their goldens)
import matrix  # noqa: E402  (shared fast/full policy selection)

# Policy combinations linked and run at once; each is independent, so a case takes about one combination's time.
COMBINATION_JOBS = 4
# Code generation threads of each clau run (--jobs): without it every clau takes all hardware threads, and the fast
# gate's parallel cases (two combinations each) would ask for many times the CPU budget of AGENTS.md.
CLAU_JOBS = 2
# Scheduler counts every program fixture runs with (plan step 58: one and several workers).
PROGRAM_WORKERS = [1, 4]
# The link-time optimization combinations (--lto): bitcode linked by LLD, through both drivers.
LTO_POLICIES = [('O2', '--lto', 'O2-lto', False), ('O2', '--lto', 'O2-lto', True)]
# The word width of this host's executables; runs of goldens that hold on one width only ('word_bits') follow it.
HOST_WORD_BITS = struct.calcsize('P') * 8
# Seconds one golden invocation may run; --run-timeout raises it for emulated targets, and CLAUSE_TEST_TIMEOUT_SCALE
# multiplies the default for slower instrumented builds (sanitizers).
RUN_TIMEOUT = 60 * int(os.environ.get('CLAUSE_TEST_TIMEOUT_SCALE', '1'))
# What a program fixture's copy leaves out: its golden and its compile diagnostics.
PROGRAM_EXTRAS = ('expected', 'compile.txt')
# The OTP oracle that runs nodes of kind otp, as regenerate.py runs every node.
ORACLE = cases.ROOT / 'tests/compiler/programs/oracle.escript'
# clause-epmd for multi-node runs (--epmd-executable), and the host OTP's escript for runs that need OTP (--escript):
# without it they are skipped.
EPMD_EXECUTABLE = None
ESCRIPT = None
# The exit status of a case whose runs were all skipped or passed, at least one skipped (CTest SKIP_RETURN_CODE).
SKIPPED = 77


def manifest(case, entry, output, golden, asn1_dir):
    """Schema-1 project manifest that links the staged sources as one executable target; ASN.1 specifications form
    one asn1 group with the golden's 'asn1_options', generated into `asn1_dir` (plan 21 step LIB84)."""
    text = (f'schema_version = 1\n\n[[targets]]\nname = "{case}"\nsource_dirs = ["src"]\n'
            f'entry = "{entry}"\noutput = "{output}"\n')
    specifications = sorted(f'src/{path.name}' for path in asn1_sources(golden))
    if specifications:
        options = ', '.join(f'"{option}"' for option in golden.get('asn1_options', []))
        text += (f'\n[[targets.asn1]]\nsources = [{", ".join(chr(34) + s + chr(34) for s in specifications)}]\n'
                 f'options = [{options}]\noutdir = "{asn1_dir}"\n')
    return text


def asn1_sources(golden):
    """The staged ASN.1 specifications of a case (the work directory's src)."""
    return [path for path in golden.get('_staged', []) if path.suffix in cases.ASN1_SUFFIXES]


def asn1_arguments(golden, asn1_dir):
    """The compiler options for a case's ASN.1 specifications in positional mode."""
    if not asn1_sources(golden):
        return []
    arguments = ['--asn1-outdir', asn1_dir]
    for option in golden.get('asn1_options', []):
        arguments += ['--asn1-option', option]
    return arguments


def command(tool, work, case, golden, policy, target_options=()):
    """Compiler invocation for one policy; returns its label and argument list. A program fixture's project build uses
    its own manifest, with the entry and output given on the command line. `target_options` select another target
    (--target-triple, --runtime-library, --linker)."""
    level, extra, name, project = policy
    label = f'{name}-{"project" if project else "positional"}'
    # A golden's 'options' are compiler options every policy adds (plan 21 step LIB74: --keep-exports for the shell).
    options = [f'-{level}', *([extra] if extra else []), '--jobs', str(CLAU_JOBS), *target_options,
               *golden.get('options', [])]
    output = f'{label}/{case}'
    # Each policy generates its ASN.1 modules into its own directory: policies run at once.
    asn1_dir = f'{label}/asn1'
    if project and 'manifest' not in golden:
        (work / f'{label}.toml').write_bytes(manifest(case, golden['entry'], output, golden, asn1_dir).encode())
        return label, [tool, '--project', f'{label}.toml', *options]
    (work / label).mkdir()
    if project:
        return label, [tool, '--project', golden['manifest'], *options, '--entry', golden['entry'], '-o', output]
    sources = [path for path in (work / 'src').rglob('*') if path.suffix in ('.erl', '.abstr', *cases.ASN1_SUFFIXES)]
    inputs = sorted(path.relative_to(work).as_posix() for path in sources)
    return label, [tool, *options, *asn1_arguments(golden, asn1_dir), '--entry', golden['entry'], '-o', output,
                   *inputs]


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


def environment(run, epmd_port=None):
    """The host environment without inherited runtime flags and NO_COLOR (io_ansi drops colors under it, so a
    golden would depend on the caller's shell), plus the Python that runs helper programs (CLAUSE_TEST_PYTHON), the
    logger's default handler on standard error as the OTP oracle has it (CLAUSE_LOGGER_TYPE), the epmd port of a
    multi-node run, else one where no epmd listens, so that a user's epmd is never asked (ERL_EPMD_PORT), and the
    run's own 'env' entries."""
    inherited = {name: value for name, value in os.environ.items()
                 if name.upper() not in ('CLAUSE_FLAGS', 'NO_COLOR', 'ERL_EPMD_PORT')}
    oracle = {'CLAUSE_TEST_PYTHON': sys.executable, 'CLAUSE_LOGGER_TYPE': 'standard_error',
              'ERL_EPMD_PORT': str(epmd_port or nodes.free_port())}
    return inherited | oracle | run.get('env', {})


def epmd_tool(executable):
    """The epmd program a program linked with --epmd-executable finds beside it, in CLAUSE_TEST_EPMD (plan 30 step
    DIST3c); nothing for other programs."""
    path = pathlib.Path(executable)
    tool = path.with_name('clause-epmd' + path.suffix)
    return {'CLAUSE_TEST_EPMD': str(tool)} if tool.exists() else {}


def variants(golden, word_bits):
    """Every run for targets of `word_bits`, once per scheduler count the golden lists under 'workers', else once with
    the default count."""
    for run in golden['runs']:
        if run.get('word_bits', word_bits) != word_bits or not cases.runs_here(run):
            continue
        if 'workers' not in golden:
            yield run, ''
        for count in golden.get('workers', []):
            flags = ' '.join(filter(None, [run.get('env', {}).get('CLAUSE_FLAGS'), f'--schedulers {count}']))
            yield run | {'env': run.get('env', {}) | {'CLAUSE_FLAGS': flags}}, f' workers={count}'


def outcome_problems(expected, returncode, stdout, stderr, golden):
    """Compares one invocation's exit status, stdout and stderr with its golden entry."""
    stdout, stderr = cases.masked(golden, text(stdout)), text(stderr)
    problems = []
    if returncode != expected['exit_status']:
        problems.append(f'exit status: expected {expected["exit_status"]}, got {returncode}')
    if problem := stdout_problem(expected['stdout'], stdout):
        problems.append(problem)
    if not re.search(expected['stderr'], stderr):
        problems.append(f'stderr does not match /{expected["stderr"]}/')
    if problems and stderr:
        problems.append('stderr:\n' + stderr.rstrip('\n'))
    return problems


def compare(executable, run, directory, golden):
    """Runs one golden invocation in `directory`; returns readable mismatch descriptions (empty when it matches)."""
    if nodes.multi(run):
        return compare_nodes(executable, run, directory, golden)
    try:
        result = subprocess.run([executable, *run['args']], capture_output=True, timeout=RUN_TIMEOUT, check=False,
                                env=environment(run) | epmd_tool(executable), input=run.get('stdin', '').encode(), cwd=directory)
    except subprocess.TimeoutExpired:
        return [f'timed out after {RUN_TIMEOUT} s']
    return outcome_problems(run, result.returncode, result.stdout, result.stderr, golden)


def node_command(executable, node, directory, golden):
    """The command of one node of a multi-node run: the linked program, or the OTP oracle for a node of kind otp."""
    if node.get('kind') == 'otp':
        version = directory / f'oracle-{node["name"]}.txt'
        return [ESCRIPT, str(ORACLE), str(version), str(directory.parent / 'src'), golden['entry'], *node['args']]
    return [executable, *node['args']]


def epmd_command(run):
    """The epmd of a multi-node run: clause-epmd, or OTP's epmd for 'epmd' otp, with the run's 'epmd_args'."""
    program = nodes.otp_program(ESCRIPT, 'epmd') if run.get('epmd') == 'otp' else EPMD_EXECUTABLE
    return [program, *run.get('epmd_args', [])]


def node_environment(run, node, port):
    """A node's environment: a Clause node of a run with the runner's epmd never starts its own (--epmd none, plan 30
    step DIST3b)."""
    result = environment(run, port)
    if not nodes.bundled(run) and node.get('kind') != 'otp':
        result['CLAUSE_FLAGS'] = ' '.join(filter(None, [result.get('CLAUSE_FLAGS'), '--epmd none']))
    return result


def compare_nodes(executable, run, directory, golden):
    """Runs every node of a multi-node run at once on a fresh epmd, or on none for a run of 'epmd' bundled, whose nodes
    start the bundled epmd; returns each node's mismatches."""
    epmd, port = (None, nodes.free_port()) if nodes.bundled(run) else nodes.start_epmd(epmd_command(run))
    try:
        commands = [node_command(executable, node, directory, golden) for node in run['nodes']]
        environments = [node_environment(run, node, port) | epmd_tool(executable) for node in run['nodes']]
        results = nodes.run_nodes(commands, environments, directory, RUN_TIMEOUT)
    finally:
        if epmd:
            nodes.stop(epmd)
    problems = []
    for node, result in zip(run['nodes'], results):
        found = [f'timed out after {RUN_TIMEOUT} s'] if result is None else outcome_problems(node, *result, golden)
        problems.extend(f'node {node["name"]}: {problem}' for problem in found)
    if nodes.bundled(run) and nodes.listening(port):
        problems.append('the bundled epmd still listens after its program ended')
    return problems


def label(run):
    """How a report line names a run: its arguments, or its nodes' names and arguments."""
    if nodes.multi(run):
        return 'nodes=' + json.dumps({node['name']: node['args'] for node in run['nodes']})
    return f'args={json.dumps(run["args"])}'


def skipped(run):
    """Whether a run cannot run here: it needs a host OTP and none was given."""
    return nodes.multi(run) and nodes.needs_otp(run) and ESCRIPT is None


def check_policy(tool, work, case, golden, policy, suffix, target_options=(), word_bits=HOST_WORD_BITS):
    """Links the case under one policy and runs every golden invocation; returns the failure count and the report
    lines, printed by the caller so concurrent combinations do not interleave."""
    name, arguments = command(tool, work, case, golden, policy, target_options)
    result = subprocess.run(arguments, cwd=work, capture_output=True, timeout=300, check=False)
    if result.returncode != 0:
        return 1, [f'FAIL {case} [{name}]: compiler exited {result.returncode}\n{text(result.stderr)}']
    executable = work / f'{name}/{case}{suffix}'
    # Each combination runs in its own directory beside its executable, with the case's data files, so programs that
    # write files do not meet each other.
    for path in (work / 'src').iterdir():
        if path.is_file() and path.suffix not in cases.SOURCE_SUFFIXES:
            shutil.copyfile(path, work / name / path.name)
    failures, lines = 0, []
    for run, variant in variants(golden, word_bits):
        if skipped(run):
            lines.append(f'skip {case} [{name}] {label(run)}{variant}: needs a host OTP (--escript)')
            continue
        problems = compare(executable, run, work / name, golden)
        status = 'FAIL' if problems else 'ok'
        lines.append(f'{status} {case} [{name}] {label(run)}{variant}')
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
    golden['_staged'] = sorted((work / 'src').iterdir())
    # A module with transformed forms compiles from its .abstr, so the tests need no host OTP (plan 21 step LIB73).
    for source in cases.transformed(work / 'src'):
        source.unlink()
    return golden


def main():
    """Checks one case or program directory against its golden under the fast or full policy matrix."""
    global RUN_TIMEOUT, EPMD_EXECUTABLE, ESCRIPT  # pylint: disable=global-statement
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
    parser.add_argument('--epmd-executable', help='clause-epmd, the epmd of multi-node runs')
    parser.add_argument('--escript', help="the host OTP's escript, for runs with OTP's epmd or otp nodes")
    options = parser.parse_args()
    RUN_TIMEOUT, EPMD_EXECUTABLE, ESCRIPT = options.run_timeout, options.epmd_executable, options.escript
    case_dir, work, tool = options.case.resolve(), options.work.resolve(), str(pathlib.Path(options.tool).resolve())
    golden = prepare(case_dir, work)
    other = 64 if options.word_bits == 32 else 32
    if skipped := [run['args'] for run in golden['runs'] if run.get('word_bits', options.word_bits) == other]:
        print(f'skip {case_dir.name}: {len(skipped)} run(s) hold on {other}-bit targets only: {skipped}')
    policies = LTO_POLICIES if options.lto else matrix.combinations()
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(COMBINATION_JOBS, len(policies))) as pool:
        outcomes = list(pool.map(lambda policy: check_policy(tool, work, case_dir.name, golden, policy, options.suffix,
                                                             options.target_options, options.word_bits), policies))
    failures, skips = 0, 0
    for count, lines in outcomes:
        print('\n'.join(lines))
        failures += count
        skips += sum(line.startswith('skip ') for line in lines)
    print(f'{case_dir.name}: {failures} failure(s)')
    sys.exit(1 if failures else SKIPPED if skips else 0)


if __name__ == '__main__':
    main()
