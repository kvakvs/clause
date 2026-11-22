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
    # 32-bit lowering; the native runs cover 64 bits, codegen_cross_targets other targets (plan 11 step VAL14).
    run([tool,'--target-triple','i686-pc-windows-msvc','--emit','obj','--artifact-dir',str(work/'width32'),str(work/'answer.erl'),str(work/'client.erl')])
    (work/'evidence.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf8')
    print(f'{records["calls"]} float OTP/native calls passed in four policies; 32-bit objects emitted.')


if __name__ == '__main__':
    main()
