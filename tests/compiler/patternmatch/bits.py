"""Exercise packed bit construction, checked cursors and retained tails using project-owned OTP goldens."""
import json
import pathlib
import sys
from evidence import run
from immediate import native
from stored import load


def bits(data, length=None):
    """Supply logical packed bits independently of runtime representation and padding."""
    return {'bits': bytes(data).hex(), 'length': len(data) * 8 if length is None else length}


def main():
    tool,cmake,root,directory,settings,config,suffix,readobj = sys.argv[1:]
    source,work = pathlib.Path(root),pathlib.Path(directory)
    records = load(source,'bits',work)
    native(tool,cmake,source,work,settings,config,suffix)
    ir = run([tool,'--print-ir',str(work/'answer.erl'),str(work/'client.erl')])
    assert 'binary.cursor' in ir and 'binary.outcome' in ir
    # 32-bit bit cursors; the native runs cover 64 bits, codegen_cross_targets other targets (plan 11 step VAL14).
    triple = 'i686-pc-windows-msvc'
    run([tool,'--target-triple',triple,'--emit','obj','--artifact-dir',str(work/triple),str(work/'answer.erl'),str(work/'client.erl')])
    objects = sorted(p for p in (work/triple).iterdir() if p.suffix in ['.obj','.o'])
    assert len(objects) == 2
    reports = [run([str(readobj),'--file-headers','--symbols',str(obj)]) for obj in objects]
    assert all('Arch: i386' in report and 'AddressSize: 32bit' in report for report in reports)
    assert any('CLAUSE_bits_v1' in report for report in reports)
    records['targets'] = [{'triple':triple,'execution':'not attempted','header_and_symbols':'checked'}]
    run([tool,'--print-types',str(work/'answer.erl'),str(work/'client.erl')])
    (work/'evidence.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf8')
    print(f'{records["calls"]} bitstring golden calls passed in four policies; 32-bit objects separately inspected.')


if __name__ == '__main__':
    main()
