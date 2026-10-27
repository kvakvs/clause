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
    target_reports = []
    for triple in ['i686-pc-windows-msvc','x86_64-pc-windows-msvc','aarch64-unknown-linux-gnu']:
        run([tool,'--target-triple',triple,'--emit','obj','--artifact-dir',str(work/triple),str(work/'answer.erl'),str(work/'client.erl')])
        objects = sorted(p for p in (work/triple).iterdir() if p.suffix in ['.obj','.o'])
        assert len(objects) == 2
        arch, width = {'i686-pc-windows-msvc':('i386','32bit'),
                       'x86_64-pc-windows-msvc':('x86_64','64bit'),
                       'aarch64-unknown-linux-gnu':('aarch64','64bit')}[triple]
        reports = [run([str(readobj),'--file-headers','--symbols',str(obj)]) for obj in objects]
        assert all('Arch: ' + arch in report and 'AddressSize: ' + width in report for report in reports)
        assert any('CLAUSE_bits_v1' in report for report in reports)
        target_reports.append({'triple':triple,'execution':'not attempted','header_and_symbols':'checked'})
    records['targets'] = target_reports
    run([tool,'--print-types',str(work/'answer.erl'),str(work/'client.erl')])
    (work/'evidence.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf8')
    print(f'{records["calls"]} bitstring golden calls passed in four policies; foreign objects separately inspected.')


if __name__ == '__main__':
    main()
