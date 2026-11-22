"""Execute immutable maps, exact keys and scoped key patterns against the pinned OTP oracle."""
import json
import pathlib
import sys
from stored import load
from evidence import run
from immediate import native


def mapping(*entries):
    """Represent exact Erlang keys without Python's integer/float or signed-zero key coalescing."""
    return {'map':list(entries)}


def main():
    tool,cmake,root,directory,settings,config,suffix = sys.argv[1:]
    source, work = pathlib.Path(root), pathlib.Path(directory)
    work.mkdir(parents=True,exist_ok=True)
    records = load(source, "maps", work)
    native(tool,cmake,source,work,settings,config,suffix)
    ir = run([tool,'--print-ir',str(work/'answer.erl'),str(work/'client.erl')])
    assert 'map.outcome' in ir and 'map.badmap' in ir and 'map.badkey' in ir
    # 32-bit lowering; the native runs cover 64 bits, codegen_cross_targets other targets (plan 11 step VAL14).
    run([tool,'--target-triple','i686-pc-windows-msvc','--emit','obj','--artifact-dir',str(work/'width32'),str(work/'answer.erl'),str(work/'client.erl')])
    (work/'evidence.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf8')
    print(f'{records["calls"]} map OTP/native calls passed in four policies; 32-bit objects emitted.')


if __name__ == '__main__':
    main()
