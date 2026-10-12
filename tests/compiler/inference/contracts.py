"""Specification contradictions (docs/semantic.md#inference): each fixture of tests/fixtures/inference/contracts lists
the diagnostics `--print-types` must report (`%% error: Text`, `%% warning: Text`) and the other modules it is
compiled with (`%% with: file`); a fixture without error lines must compile without one.

    contracts.py <clau> <fixture directory>
"""
import pathlib
import re
import subprocess
import sys

EXPECTED = re.compile(r'^%% (error|warning): (.+)$')
WITH = re.compile(r'^%% with: (.+)$')


def check(tool, path):
    """Problems with one fixture's diagnostics."""
    lines = path.read_text(encoding='utf-8').splitlines()
    expected = [(f'{match.group(1)}: ', match.group(2)) for match in map(EXPECTED.match, lines) if match]
    others = [path.parent / match.group(1) for match in map(WITH.match, lines) if match]
    result = subprocess.run([tool, '--print-types', path.name, *(other.name for other in others)], cwd=path.parent,
                            capture_output=True, text=True, encoding='utf-8', timeout=120, check=False)
    wanted = 1 if any(severity == 'error: ' for severity, _ in expected) else 0
    problems = [] if result.returncode == wanted else [f'{path.name}: exit {result.returncode}, wanted {wanted}']
    reported = [line for line in result.stderr.splitlines() if 'specification' in line or line.startswith('error:')]
    problems += [f'{path.name}: missing `{severity}{text}`' for severity, text in expected
                 if not any(line.startswith(severity) and line.endswith(text) for line in reported)]
    problems += [f'{path.name}: unexpected {line}' for line in reported
                 if not any(line.startswith(severity) and line.endswith(text) for severity, text in expected)]
    return problems


def main():
    tool, directory = sys.argv[1], pathlib.Path(sys.argv[2])
    owners = {match.group(1) for path in directory.glob('*.erl')
              for match in map(WITH.match, path.read_text(encoding='utf-8').splitlines()) if match}
    fixtures = sorted(path for path in directory.glob('*.erl') if path.name not in owners)
    problems = [problem for path in fixtures for problem in check(tool, path)]
    for problem in problems:
        print(problem)
    print(f'contracts: {len(fixtures)} fixtures, {len(problems)} problem(s)')
    sys.exit(1 if problems else 0)


if __name__ == '__main__':
    main()
