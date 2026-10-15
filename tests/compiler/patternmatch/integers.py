"""Compare arbitrary integer construction, promotion, guards and matching with OTP."""
import json
import pathlib
import subprocess
import sys
from stored import load
from evidence import run
from immediate import native


def main():
    tool,cmake,root,directory,settings,config,suffix = sys.argv[1:]
    source, work = pathlib.Path(root), pathlib.Path(directory)
    work.mkdir(parents=True,exist_ok=True)
    records = load(source, "integers", work)
    native(tool,cmake,source,work,settings,config,suffix)
    ir = run([tool,'--print-ir',str(work/'answer.erl'),str(work/'client.erl')])
    assert all(name in ir for name in ['add i128','sub i128','mul i128','integer.fallback'])
    for bits,triple in [(32,'i686-pc-windows-msvc'),(64,'x86_64-pc-windows-msvc')]:
        run([tool,'--target-triple',triple,'--emit','obj','--artifact-dir',str(work/f'width{bits}'),str(work/'answer.erl'),str(work/'client.erl')])
    # 20,000 digits passed the former 10,000-digit cap; 1,262,594 digits exceed 2^4194240 - 1, OTP's largest integer
    # (its scanner reports a longer literal as an illegal integer).
    for body in ['f() -> NUMBER.', 'f(NUMBER) -> ok.']:
        for digits, valid in [(20000, True), (1262594, False)]:
            path = work/'excessive.erl'
            path.write_text('-module(excessive).\n'+body.replace('NUMBER','9'*digits)+'\n',encoding='utf8')
            output = work/('valid' if valid else 'invalid')
            result = subprocess.run([tool,'--emit','obj','--artifact-dir',str(output),str(path)],
                                    capture_output=True,text=True,encoding='utf8',timeout=60)
            if valid:
                assert result.returncode == 0, result.stderr
                continue
            assert result.returncode == 1 and 'illegal integer (more than 4194240 bits)' in result.stderr, result.stderr
            assert not output.exists()
    (work/'evidence.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf8')
    print(f'{records["calls"]} exact integer OTP/native calls passed in four policies; both target payload widths emitted.')


if __name__ == '__main__':
    main()
