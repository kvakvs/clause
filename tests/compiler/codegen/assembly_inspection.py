"""Exercise --print-asm: each module's assembly commented with the IR and Erlang lines it came from.

Created for --print-asm. Reassembling a listing must give the machine code --emit obj writes, and every IR comment
must name an instruction --print-optimized-ir prints.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys

tool, mc, objdump, directory, fixtures, backends = sys.argv[1:]
backends = backends.split(',')
work = Path(directory)
if work.exists():
    shutil.rmtree(work)
work.mkdir(parents=True)
for name in ('answer.erl', 'client.erl', 'source_comments.erl', 'source_comments.hrl'):
    shutil.copyfile(Path(fixtures) / name, work / name)

# Targets whose reassembled listing matches the integrated object byte for byte. COFF x86-64 is left out: llvm-mc
# encodes the printed `rex64 jmpq *32(%r10)` with a separate REX prefix.
ELF_TARGETS = (('X86', 'x86_64-unknown-linux-gnu', '#'), ('AArch64', 'aarch64-unknown-linux-gnu', '//'),
               ('ARM', 'armv7-unknown-linux-gnueabihf', '@'))
HEADER = re.compile(r'^(?:;|#|//|@) clau .* stage=(\w+)\n', re.MULTILINE)


def run(*args, expected=0):
    result = subprocess.run([tool, *args], cwd=work, capture_output=True, text=True, encoding='utf-8', timeout=60)
    assert result.returncode == expected, (args, result.returncode, result.stdout[-2000:], result.stderr)
    return result


def sections(text):
    """Split a multi-snapshot output at its headers into (module, stage, body)."""
    headers = list(HEADER.finditer(text))
    return [(re.search(r'module="([^"]+)"', header.group(0))[1], header.group(1),
             text[header.end():headers[i + 1].start() if i + 1 < len(headers) else len(text)])
            for i, header in enumerate(headers)]


def instructions(path):
    """Disassembled instructions of an object file, addresses and encodings included."""
    listing = subprocess.run([objdump, '-d', str(path)], check=True, capture_output=True, text=True).stdout
    return [line for line in listing.splitlines() if re.match(r'\s+[0-9a-f]+:', line)]


def ir_instructions(ir):
    """Instruction lines of printed IR without their source comments and debug locations."""
    lines = set()
    for line in ir.splitlines():
        if line.startswith('  ') and not line.startswith('   '):
            lines.add(re.sub(r', !dbg !\d+', '', line.split(' ; ', 1)[0].strip()))
    return lines


# One module and one stage: plain assembly without a header, every run of code naming its IR and Erlang origin.
single = run('-O2', '--print-asm', '--verbose', 'answer.erl')
assert not HEADER.search(single.stdout), single.stdout[:200]
assert 'phase=optimization' in single.stderr and 'phase=assembly' in single.stderr
assert 'phase=emission' not in single.stderr
listing = single.stdout
comment = re.match(r'(\S+) ', re.search(r'^\S+ +ir ', listing, re.MULTILINE)[0])[1]
assert re.search(rf'^{re.escape(comment)} erlang answer\.erl:3: value\(\) -> 42\.$', listing, re.MULTILINE)
assert re.search(rf'^{re.escape(comment)} erlang answer\.erl:4: identity\(X\) -> X\.$', listing, re.MULTILINE)
# Line tables exist only to carry the mapping: their directives, sections and labels are gone.
assert not re.search(r'^\s*\.(loc|cv_loc|cv_file|file)\b', listing, re.MULTILINE)
assert not re.search(r'debug|Ltmp|Lfunc_begin', listing)

# Every IR comment names an instruction of the optimized IR the machine code was selected from.
optimized = ir_instructions(run('-O2', '--print-optimized-ir', 'answer.erl').stdout)
named = re.findall(rf'^{re.escape(comment)} +ir (.*)$', listing, re.MULTILINE)
assert named
missing = [text for text in named if not text.startswith('switch ') and text not in optimized]
assert not missing, missing[:5]

# Erlang lines name the physical file through includes and macros; logical -file remapping never appears.
included = run('-O2', '--print-asm', 'source_comments.erl').stdout
assert re.search(r'erlang [^\n]*source_comments\.hrl:\d+: +passthrough\($', included, re.MULTILINE)
assert re.search(r'erlang source_comments\.erl:11: +7\. % original literal line$', included, re.MULTILINE)
assert 'logical-only.erl' not in included

# Several modules and stages print one module at a time, each stage after its own header.
combined = run('-O2', '--print-asm', '--print-ir', '--print-optimized-ir', 'client.erl', 'answer.erl').stdout
assert [(module, stage) for module, stage, _ in sections(combined)] == [
    ('client', 'before'), ('client', 'after'), ('client', 'assembly'),
    ('answer', 'before'), ('answer', 'after'), ('answer', 'assembly')]
assert all(re.search(r' +ir ', body) for _, stage, body in sections(combined) if stage == 'assembly')
assert [stage for _, stage, _ in sections(run('--print-asm', 'client.erl', 'answer.erl').stdout)] == ['assembly'] * 2

# Reassembled listings give the integrated object's machine code at every optimization level.
for backend, triple, marker in ELF_TARGETS:
    if backend not in backends:
        continue
    for level in ('-O0', '-O2'):
        root = work / triple / level
        text = run(level, '--target-triple', triple, '--print-asm', 'answer.erl').stdout
        assert re.search(rf'^{re.escape(marker)} erlang answer\.erl:3: ', text, re.MULTILINE), (triple, text[:400])
        root.mkdir(parents=True)
        (root / 'listing.s').write_text(text, encoding='utf-8')
        subprocess.run([mc, '-triple', triple, '-filetype=obj', str(root / 'listing.s'), '-o', str(root / 'listing.o')],
                       check=True, capture_output=True)
        run(level, '--target-triple', triple, '--emit', 'obj', '--artifact-dir', str(root / 'emitted'), 'answer.erl')
        emitted = next((root / 'emitted').glob('*.o'))
        assert instructions(root / 'listing.o') == instructions(emitted), (triple, level)

# Assembly inspection is an inspection: it writes no artifacts and rejects other output actions.
for other in (('--emit', 'obj'), ('--artifact-dir', 'out'), ('--output', 'exe'), ('--print-types',),
              ('--parse-check',)):
    assert not run('--print-asm', *other, 'answer.erl', expected=2).stdout
assert not run('--print-asm', '--target-triple', 'invalid', 'answer.erl', expected=1).stdout
assert not (work / 'out').exists() and not (work / 'build').exists()
print('assembly_inspection: ok')
