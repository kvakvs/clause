"""Exercise public IR inspection and validate each snapshot using the installed LLVM tools."""
from pathlib import Path
import itertools
import re
import shutil
import subprocess
import sys

tool, assembler, disassembler, directory, fixtures = sys.argv[1:]
work = Path(directory)
if work.exists():
    shutil.rmtree(work)
work.mkdir(parents=True)
(work / 'oracle').mkdir()
for name in ('answer.erl', 'client.erl', 'source_comments.erl', 'source_comments.hrl'):
    shutil.copyfile(Path(fixtures) / name, work / name)
sequence = itertools.count()


def run(*args, expected=0):
    result = subprocess.run([tool, *args], cwd=work, capture_output=True, text=True, encoding='utf-8', timeout=30)
    assert result.returncode == expected, (args, result.returncode, result.stdout, result.stderr)
    return result


def snapshots(text):
    headers = list(re.finditer(r'^; clau .* stage=(before|after)\n', text, re.MULTILINE))
    if not headers:
        return [('', text)]
    return [(header.group(0), text[header.end():headers[i + 1].start() if i + 1 < len(headers) else len(text)])
            for i, header in enumerate(headers)]


def round_trip(text):
    parts = snapshots(text)
    for _, assembly in parts:
        source = work / 'oracle' / f'{next(sequence)}.ll'
        source.write_text(assembly, encoding='utf-8')
        bitcode = source.with_suffix('.bc')
        subprocess.run([assembler, str(source), '-o', str(bitcode)], check=True, capture_output=True)
        subprocess.run([disassembler, str(bitcode), '-o', str(source.with_suffix('.roundtrip.ll'))], check=True, capture_output=True)
    return parts


for level in ('-O0', '-O2'):
    for flag in ('--print-ir', '--print-optimized-ir'):
        result = run(level, flag, '--verbose', 'answer.erl')
        assert not result.stdout.startswith('; clau '), result.stdout
        assert len(round_trip(result.stdout)) == 1
        assert 'phase=lowering' in result.stderr and 'phase=emission' not in result.stderr
        assert ('phase=optimization' in result.stderr) == (flag == '--print-optimized-ir')
        assert re.search(r'define[^\n]*@clausev1_616e73776572_6964656e74697479_1\(', result.stdout)
        assert '.register' in result.stdout
        assert re.search(r'  [^\n]+ ; value\(\) -> 42\.', result.stdout)
        assert re.search(r'(?:getelementptr|load|store) [^\n]*; identity\(X\) -> X\.', result.stdout)
        assert result.stdout.startswith('; Erlang source files:\n; "answer.erl"\n')
        assert result.stdout.count('; "answer.erl"') == 1
        assert all(block.count('; identity(X) -> X.') <= 1 for block in result.stdout.split('\n\n'))

both = run('-O2', '--print-optimized-ir', '--print-ir', 'client.erl', 'answer.erl')
parts = round_trip(both.stdout)
assert len(parts) == 4
assert [re.search(r'module="([^"]+)"', head)[1] for head, _ in parts] == ['client', 'client', 'answer', 'answer']
assert [re.search(r'stage=(\w+)', head)[1] for head, _ in parts] == ['before', 'after', 'before', 'after']
assert 'clausev1_616e73776572_70726976617465_0' in parts[2][1]
assert 'clausev1_616e73776572_70726976617465_0' not in parts[3][1]
assert not both.stderr


def own_functions(ir):
    """The fixture's own function bodies with value and label numbering dropped: the compiler-generated
    module_info/1 has atom checks specialization removes, which also renumbers later functions."""
    bodies = re.findall(r'^define .*?^}', ir, re.M | re.S)
    generated = '_6d6f64756c655f696e666f_'  # hex of module_info in the symbol
    return [re.sub(r'([A-Za-z_.])\d+\b', r'\1', body) for body in bodies if generated not in body.split('\n', 1)[0]]


normal = run('-O2', '--print-ir', '--print-optimized-ir', 'answer.erl').stdout
disabled = run('--no-type-specialization', '-O2', '--print-ir', '--print-optimized-ir', 'answer.erl').stdout
# No removable representation checks exist in the fixture's own source.
assert own_functions(normal) == own_functions(disabled)
assert run('--print-ir', '--print-ir', 'answer.erl').stdout == run('--print-ir', 'answer.erl').stdout

(work / 'quoted.erl').write_text("-module('line\\nbreak'). value() -> 1.\n", encoding='utf-8')
quoted = run('--print-ir', '--print-optimized-ir', 'quoted.erl')
assert 'module="line\\x0abreak"' in quoted.stdout
round_trip(quoted.stdout)
shutil.copyfile(work / 'answer.erl', work / 'space å.erl')
round_trip(run('--print-ir', 'space å.erl').stdout)

# Physical source lines survive includes, nested macros, logical file remapping and LLVM inlining.
annotated = run('-O2', '--print-ir', '--print-optimized-ir', 'source_comments.erl', 'answer.erl')
source_parts = round_trip(annotated.stdout)[:2]
before, after = [assembly for _, assembly in source_parts]
# Arguments arrive as transfer parameters (plan 20 step OPT0.1A): the read is the argument's rooting store, and the
# call's line names the inline entry that passes it on.
assert re.search(r'store [^\n]*%argument[^\n]*;         Value\)\.', before)
assert re.search(r'callee\.arity = load [^\n]*;     \?OUTER\(', before)
assert re.search(r'callee\.arity = load [^\n]*;     answer:identity\(', before)
assert re.search(r'  [^\n]+ ;     7\. % original literal line', after)
assert ';     answer:identity(' in after
# Frame transfers keep the helper out of line; the transfer into it retains provenance.
assert 'inlinedAt:' in after or re.search(r'call[^\n]*@clausev1_736f757263655f636f6d6d656e7473_68656c706572_1[^\n]*!dbg', after)
for assembly in (before, after):
    assert assembly.count('; "source_comments.erl"') == 1
    assert len(re.findall(r'^; "[^"\n]*source_comments.hrl"$', assembly, re.MULTILINE)) == 1
    assert not re.search(r'^  [^\n]*; [^\n]*source_comments\.(erl|hrl)', assembly, re.MULTILINE)
assert 'logical-only.erl' not in before + after

# Source comments use UTF-8 even for Latin-1 input, and controls cannot create new IR lines.
for flag, ending in (('--print-ir', b'\r\n'), ('--print-optimized-ir', b'')):
    (work / 'latin.erl').write_bytes(b'% coding: latin-1\r\n-module(latin).\r\n-export([value/0]).\r\n'
                                   b'value() -> 7. % caf\xe9\t\x1b\x00' + ending)
    latin = run('-O2', flag, 'latin.erl').stdout
    assert '; value() -> 7. % café\t\\x1b\\x00' in latin, latin
    round_trip(latin)

# Saved textual IR carries the same comments and remains valid LLVM assembly.
run('-O2', '--emit', 'llvm-ir', '--artifact-dir', 'source-artifacts', 'source_comments.erl', 'answer.erl')
artifacts = list((work / 'source-artifacts').glob('*.ll'))
assert len(artifacts) == 2
for artifact in artifacts:
    text = artifact.read_text(encoding='utf-8')
    assert text.startswith('; Erlang source files:\n'), artifact
    assert re.search(r'^  [^\n]+ ; (?:value\(|    )', text, re.MULTILINE), artifact
    round_trip(text)
(work / 'project.toml').write_text('''schema_version=1
[[targets]]
name="one"
sources=["answer.erl","client.erl"]
[[targets]]
name="two"
sources=["client.erl","answer.erl"]
''', encoding='utf-8')
project = run('--project', 'project.toml', '--target', 'two', '--target', 'one', '--print-ir', '--verbose')
project_parts = round_trip(project.stdout)
assert len(project_parts) == 4
assert all(assembly.startswith('; Erlang source files:\n') for _, assembly in project_parts)
assert ['target="two"' in h for h, _ in project_parts] == [True, True, False, False]
assert 'target="one"' in project_parts[-1][0]
assert 'phase=emission' not in project.stderr and '[comp]' in project.stderr

for flag in ('--print-ir', '--print-optimized-ir'):
    for other in (('--emit', 'obj'), ('--artifact-dir', 'out'), ('--output', 'exe'), ('--parse-check',),
                  ('--preprocess-check',), ('--print-pp',), ('--print-ast',)):
        rejected = run(flag, *other, 'missing.erl', expected=2)
        assert not rejected.stdout
    assert not run(flag, '--new-project', 'new', expected=2).stdout
    invalid = run(flag, '--target-triple', 'invalid', 'answer.erl', expected=1)
    assert not invalid.stdout
    (work / 'bad.erl').write_text('-module(bad). value() -> fun erlang:apply/2.\n', encoding='utf-8')
    assert not run(flag, 'answer.erl', 'bad.erl', expected=1).stdout
assert 'Usage:' in run('--help', '--print-ir', '--target-triple', 'invalid', 'missing.erl').stdout
assert not (work / 'build').exists() and not (work / 'out').exists() and not (work / 'new.toml').exists()
