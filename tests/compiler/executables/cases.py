"""Shared lookup, staging and hashing for executable golden cases."""
import hashlib
import json
import pathlib
import re
import shutil

ROOT = pathlib.Path(__file__).resolve().parents[3]
CASES = ROOT / 'tests/fixtures/executables'
GOLDEN = 'golden.json'
# Fields written by regenerate.py from OTP; a golden lacking any of them was never generated.
GENERATED_RUN_FIELDS = ('exit_status', 'stdout')
# The inputs of a case: sources, headers and transformed forms (sources()).
SOURCE_SUFFIXES = ('.erl', '.hrl', '.abstr')


def digest(path):
    """Hashes file bytes with CRLF normalized to LF so checkouts agree across hosts."""
    return hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest()


def names():
    """Lists case directories, each identified by its golden file."""
    return sorted(path.parent.name for path in CASES.glob(f'*/{GOLDEN}'))


def load(case_dir):
    """Reads a case golden and checks the authored fields every tool relies on."""
    golden = json.loads((case_dir / GOLDEN).read_text(encoding='utf8'))
    assert golden.get('schema') == 1, f'{case_dir.name}: golden schema must be 1'
    assert isinstance(golden.get('entry'), str), f'{case_dir.name}: golden needs an entry module'
    assert golden.get('runs'), f'{case_dir.name}: golden needs at least one run'
    assert all(isinstance(count, int) and count > 0 for count in golden.get('workers', [])), case_dir.name
    assert all(isinstance(arg, str) for run in golden['runs'] for arg in run['args']), case_dir.name
    # A run whose output holds on hosts of one word width only names it (the OTP oracle is a 64-bit host).
    assert all(run.get('word_bits', 64) in (32, 64) for run in golden['runs']), f'{case_dir.name}: word_bits'
    # OTP never sees a run's environment, so only authored runs may set one.
    assert all(run.get('authored') for run in golden['runs'] if 'env' in run), f'{case_dir.name}: env needs authored'
    return golden


def sources(case_dir, golden):
    """Erlang inputs as {case-relative spelling: path}: listed 'sources', else every .erl/.hrl/.abstr beside the
    golden. An .abstr holds its .erl's forms after the host's parse transforms: Clause compiles it instead of the
    .erl, which only the OTP oracle compiles (plan 21 step LIB73)."""
    if 'sources' in golden:
        return {source: case_dir / source for source in golden['sources']}
    return {path.name: path for path in sorted(case_dir.iterdir()) if path.suffix in SOURCE_SUFFIXES}


def transformed(directory):
    """The .erl files of a directory that have an .abstr beside them."""
    return [path.with_suffix('.erl') for path in sorted(directory.glob('*.abstr')) if path.with_suffix('.erl').exists()]


def masked(golden, text):
    """Output with the golden's 'stdout_masks' applied: [pattern, replacement] pairs of multiline regexes for lines the
    oracle and Clause print differently by design, such as the shell's system version (plan 21 step LIB74)."""
    for pattern, replacement in golden.get('stdout_masks', []):
        text = re.sub(pattern, replacement, text, flags=re.MULTILINE)
    return text


def data(case_dir, golden):
    """Files the program reads at run time (golden 'data'), staged beside the sources, keyed by case-relative path."""
    return {name: case_dir / name for name in golden.get('data', [])}


def inputs(case_dir, golden):
    """Hashes every input that determines the golden, keyed by its case-relative spelling."""
    files = sources(case_dir, golden) | data(case_dir, golden)
    return {spelling: digest(path) for spelling, path in files.items()}


def verify(case_dir):
    """Loads a golden and rejects one that was never generated or whose sources changed since."""
    golden = load(case_dir)
    generated = 'inputs' in golden and all(field in run for run in golden['runs'] for field in GENERATED_RUN_FIELDS)
    if not generated:
        raise SystemExit(f'{case_dir.name}: golden was not generated; run tests/compiler/executables/regenerate.py')
    if golden['inputs'] != inputs(case_dir, golden):
        raise SystemExit(f'{case_dir.name}: stale golden: sources changed since OTP generation; regenerate it')
    return golden


def stage(case_dir, golden, destination):
    """Copies the case sources into one flat directory, as compiled by the runner and the oracle."""
    shutil.rmtree(destination, ignore_errors=True)
    destination.mkdir(parents=True)
    for path in (sources(case_dir, golden) | data(case_dir, golden)).values():
        shutil.copyfile(path, destination / path.name)
    return destination
