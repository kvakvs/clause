"""Compare finite binary64 arithmetic, exact matching and mixed numeric order with OTP."""
import json
import pathlib
import re
import sys
from stored import load
from evidence import run
from immediate import native


def main():
    tool,cmake,root,directory,settings,config,suffix = sys.argv[1:]
    source, work = pathlib.Path(root), pathlib.Path(directory)
    work.mkdir(parents=True,exist_ok=True)
    records = load(source, "floats", work)
    native(tool,cmake,source,work,settings,config,suffix)
    ir = run([tool,'--print-ir',str(work/'answer.erl'),str(work/'client.erl')])
    # Float literals are cells of the module literal area (OPT0.4).
    assert 'float.outcome' not in ir and '.literals' in ir
    assert not re.search(r'\b(fast|nnan|ninf|nsz|arcp|reassoc|afn)\b',ir)
    for bits,triple in [(32,'i686-pc-windows-msvc'),(64,'x86_64-pc-windows-msvc')]:
        run([tool,'--target-triple',triple,'--emit','obj','--artifact-dir',str(work/f'width{bits}'),str(work/'answer.erl'),str(work/'client.erl')])
    (work/'evidence.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf8')
    print(f'{records["calls"]} float OTP/native calls passed in four policies; both word widths emitted.')


if __name__ == '__main__':
    main()
